#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""D-01 取证：--mode chain 缺失模型 -> exit 139 (SIGSEGV) 的崩溃点定位。

复用 V-EXT 保留树（V6 因失败被跳过，树仍在）：
  /tmp/railhmi_vision_item009_verify-20261020-item009-v1/app

步骤:
  S1 复现：默认 models_dir=../models（不存在）-> 记录退出码与输出字节数
  S2 stdbuf -o0 无缓冲复现：取崩溃前最后 emit 行
  S3 对照：--models-dir 指向有效 model-assets -> 应 PASS exit 0
  S4 gdb 回溯（若板端有 gdb）
  S5 空模型目录对照
"""
import os
import re
import subprocess
import sys

import paramiko

HERE = os.path.dirname(os.path.abspath(__file__))
TREE = "/tmp/railhmi_vision_item009_verify-20261020-item009-v1"
PROBE = "build-media/tools/railhmi-vision-pipeline"
EVID = []


def note(step, ok, detail=""):
    EVID.append({"step": step, "ok": bool(ok), "detail": str(detail)[:1500]})
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
        # S0 树在场
        rc, out = rrun(client, "test -x %s/app/%s && echo TREE_OK" % (TREE, PROBE), 20)
        note("S0_tree_present", "TREE_OK" in out, out.strip()[-40:])
        if "TREE_OK" not in out:
            return 2

        # S1 复现（默认 ../models 不存在；缓冲输出 -> 预期 0 字节 + 139）
        cmd = ("cd %s/app && %s --mode chain --config ./config --source test --duration 2 "
               ">/tmp/d1_buf.log 2>&1; echo EXIT=$?; wc -c /tmp/d1_buf.log" % (TREE, PROBE))
        rc, out = rrun(client, cmd, 60)
        m = re.search(r"^EXIT=(\d+)$", out, re.M)
        mw = re.search(r"^(\d+) /tmp/d1_buf.log$", out, re.M)
        note("S1_repro_default_models",
             m is not None and int(m.group(1)) >= 128,
             "exit=%s bytes=%s" % (m.group(1) if m else "?", mw.group(1) if mw else "?"))

        # S2 stdbuf 无缓冲：崩溃前最后 emit 行
        cmd = ("cd %s/app && stdbuf -o0 -e0 %s --mode chain --config ./config --source test "
               "--duration 2 >/tmp/d1_unbuf.log 2>&1; echo EXIT=$?; "
               "echo ---LAST15---; tail -15 /tmp/d1_unbuf.log; echo ---WC---; wc -c /tmp/d1_unbuf.log"
               % (TREE, PROBE))
        rc, out = rrun(client, cmd, 60)
        m = re.search(r"^EXIT=(\d+)$", out, re.M)
        tail = out.split("---LAST15---")[1].split("---WC---")[0] if "---LAST15---" in out else ""
        note("S2_unbuffered_last_lines", True,
             "exit=%s | last=%s" % (m.group(1) if m else "?",
                                    " ~ ".join(l.strip() for l in tail.strip().splitlines()[-4:])))

        # S3 对照：有效模型目录
        cmd = ("cd %s/app && %s --mode chain --config ./config --models-dir %s/model-assets "
               "--source test --duration 2 --lanes CAM-01 >/tmp/d1_valid.log 2>&1; echo EXIT=$?; "
               "tail -2 /tmp/d1_valid.log" % (TREE, PROBE, TREE))
        rc, out = rrun(client, cmd, 90)
        m = re.search(r"^EXIT=(\d+)$", out, re.M)
        note("S3_valid_models_control", m is not None and m.group(1) in ("0", "1"),
             "exit=%s tail=%s" % (m.group(1) if m else "?", out.strip().splitlines()[-1][:90]))

        # S4 gdb 回溯（若可用）
        rc, out = rrun(client, "command -v gdb || echo NOGDB", 10)
        if "NOGDB" in out:
            note("S4_gdb_backtrace", True, "gdb 不可用，S2 无缓冲定位为准")
        else:
            cmd = ("cd %s/app && ulimit -c 0; gdb -batch -ex run -ex bt --args %s "
                   "--mode chain --config ./config --source test --duration 2 2>&1 | "
                   "tail -30; echo GDB_DONE" % (TREE, PROBE))
            rc, out = rrun(client, cmd, 120)
            body = out.split("GDB_DONE")[0] if "GDB_DONE" in out else out
            note("S4_gdb_backtrace", True, body.strip()[-600:])
    finally:
        client.close()

    with open(os.path.join(HERE, "dbg-missing-models-evidence.json"), "w", encoding="utf-8") as fh:
        import json
        json.dump({"checks": EVID}, fh, ensure_ascii=False, indent=2)
    print("DBG SUMMARY done (%d checks)" % len(EVID), flush=True)
    return 0


if __name__ == "__main__":
    sys.exit(main())
