#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-009 修复复核轮 v2 —— V-NEG：D-01 修复反转取证。

v1 取证（verify/dbg_missing_models.py -> dbg-missing-models-evidence.json）在 r6 树上：
  S1 默认 models_dir 缺失 -> exit=139 bytes=0；S2 stdbuf 无缓冲最后一行 type=config；
  S3 有效模型 exit 0；S4 gdb 回溯 load_kind。
修复后同一命令面必须反转：exit=1（探针 chain_models_ready 判 FAIL）、type=models JSON
逐模型 MODEL_MISSING、unavailable envelope 照常发布、无崩溃标记。
运行前提：V-EXT 驱动以 --keep-remote 保留了 fresh 树；本驱动只读取，不再建树。

步骤（S 编号与 v1 取证一一对应，便于逐条对照反转）：
  S1 复现同命令：exit=1 且缓冲输出非 0 字节（emit flush 后崩溃丢失不复存在）
  S2 stdbuf 无缓冲：末行 RESULT: FAIL failures=1（完整走完，非中断）
  S3 对照：有效模型 -> exit 0
  S4 源码级守恒：load_kind 内不再存在 move 后解引用（rg 计数断言）
"""
import json
import os
import re
import subprocess
import sys

import paramiko

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", ".."))
STAMP = "verify-20261021-item009-v2"
REMOTE = "/tmp/railhmi_vision_item009_%s" % STAMP
OUTDIR = os.path.join(REPO, "docs", "vision", "test_evidence", "ITEM-009")
PROBE = "build-media/tools/railhmi-vision-pipeline"
SRC_REL = "app/src/media/src/vision_rknn_detector.cpp"
EVID = []


def note(step, ok, detail=""):
    EVID.append({"step": step, "ok": bool(ok), "detail": str(detail)[:900]})
    print("[%s] %s %s" % ("PASS" if ok else "FAIL", step, detail), flush=True)


def rrun(client, cmd, timeout):
    _in, out, err = client.exec_command(cmd, timeout=timeout)
    so = out.read().decode("utf-8", "replace")
    se = err.read().decode("utf-8", "replace")
    rc = out.channel.recv_exit_status()
    return rc, so + se


def main():
    secret = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL")
    if not secret:
        ps = subprocess.run(
            ["powershell.exe", "-NoProfile", "-Command",
             "[Environment]::GetEnvironmentVariable('TRAIN_RK3588_SSH_CREDENTIAL','User')"],
            capture_output=True, text=True, timeout=30)
        secret = (ps.stdout or "").strip()
    if not secret:
        print("credential unavailable", flush=True)
        return 2

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    client.connect(hostname="192.168.0.103", port=22, username="admin", password=secret,
                   timeout=10, auth_timeout=10, banner_timeout=10,
                   look_for_keys=False, allow_agent=False)
    try:
        # S0 fresh 保留树在场
        rc, out = rrun(client, "test -x %s/app/%s && echo TREE_OK" % (REMOTE, PROBE), 20)
        note("S0_tree_present", "TREE_OK" in out, out.strip()[-40:])
        if "TREE_OK" not in out:
            return 2

        # S1 复现 v1 取证的同一命令面（默认 models_dir 缺失）
        cmd = ("cd %s/app && %s --mode chain --config ./config --source test --duration 2 "
               ">/tmp/d2_buf.log 2>&1; echo EXIT=$?; wc -c /tmp/d2_buf.log" % (REMOTE, PROBE))
        rc, out = rrun(client, cmd, 90)
        m = re.search(r"^EXIT=(\d+)$", out, re.M)
        mw = re.search(r"^(\d+) /tmp/d2_buf\.log$", out, re.M)
        exit_v = int(m.group(1)) if m else -1
        nbytes = int(mw.group(1)) if mw else -1
        note("S1_repro_exit_graceful", exit_v == 1 and nbytes > 200,
             "exit=%d bytes=%d (v1: exit=139 bytes=0)" % (exit_v, nbytes))

        # S2 无缓冲完整走完：退出码 1、末行 RESULT: FAIL failures=1（未被崩溃中断）；
        # MODEL_MISSING reason 可见性按全文断言（[FAIL] 行距尾部 4 行，tail-3 窗口截不到）。
        cmd = ("cd %s/app && stdbuf -o0 -e0 %s --mode chain --config ./config --source test "
               "--duration 2 >/tmp/d2_unbuf.log 2>&1; echo EXIT=$?; "
               "echo ---TAIL3---; tail -3 /tmp/d2_unbuf.log; echo ---MODELS---; "
               "grep -c MODEL_MISSING /tmp/d2_unbuf.log; echo ---WC---" % (REMOTE, PROBE))
        rc, out = rrun(client, cmd, 90)
        m = re.search(r"^EXIT=(\d+)$", out, re.M)
        tail = out.split("---TAIL3---")[1].split("---MODELS---")[0] if "---TAIL3---" in out else ""
        mm = re.search(r"---MODELS---\n(\d+)", out)
        note("S2_unbuffered_complete_run",
             m is not None and m.group(1) == "1" and "RESULT: FAIL failures=1" in tail
             and mm is not None and int(mm.group(1)) >= 2,
             "exit=%s models_missing_hits=%s | tail=%s"
             % (m.group(1) if m else "?", mm.group(1) if mm else "?",
                " ~ ".join(l.strip() for l in tail.strip().splitlines()[-2:])))

        # S3 对照：有效模型目录 exit 0
        cmd = ("cd %s/app && %s --mode chain --config ./config --models-dir %s/model-assets "
               "--source test --duration 2 --lanes CAM-01 >/tmp/d2_valid.log 2>&1; echo EXIT=$?; "
               "tail -1 /tmp/d2_valid.log" % (REMOTE, PROBE, REMOTE))
        rc, out = rrun(client, cmd, 120)
        m = re.search(r"^EXIT=(\d+)$", out, re.M)
        note("S3_valid_models_control", m is not None and m.group(1) == "0",
             "exit=%s tail=%s" % (m.group(1) if m else "?", out.strip().splitlines()[-1][:80]))

        # S4 源码级守恒：load_kind 失败路径全部经 fail_with 收口，无 move 后解引用
        with open(os.path.join(REPO, SRC_REL), encoding="utf-8") as fh:
            src = fh.read()
        n_fail_with = len(re.findall(r"return fail_with\(", src))
        bare = re.findall(r"models_\[kind\] = std::move\(m\);\s*\n\s*if \(err\) \*err = m->",
                          src)
        note("S4_source_fail_with_coverage",
             n_fail_with == 5 and not bare and "reason_copy" in src,
             "fail_with=%d bare_deref=%d" % (n_fail_with, len(bare)))
    finally:
        client.close()

    ok_all = all(e["ok"] for e in EVID)
    with open(os.path.join(HERE, "item009-neg-verify-20261021-item009-v2.json"), "w",
              encoding="utf-8") as fh:
        json.dump({"stamp": STAMP, "checks": EVID, "all_pass": ok_all}, fh,
                  ensure_ascii=False, indent=2)
    print("NEG SUMMARY all_pass=%s (%d checks)" % (ok_all, len(EVID)), flush=True)
    return 0 if ok_all else 1


if __name__ == "__main__":
    sys.exit(main())
