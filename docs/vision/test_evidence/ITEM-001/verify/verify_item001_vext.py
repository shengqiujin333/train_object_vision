#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-001 验证自有扩展夹具驱动：vext 上板 g++ 直编 + CLI 负例 + 确定性 + 清理。

复用已保留的板端构建树（--keep-remote 产物）：
  /tmp/railhmi_vision_item001_verify-20261018-item001-v1
    app/build-core/src/librailhmi_core.a
    app/build-core/tools/railhmi-model-probe
    app/src/core/include/...

阶段:
  V1  上传 vext_item001.cpp，g++ 直编（-I src/core/include + 静态库）
  V2  运行 vext，断言 26 个 check 全 PASS
  V3  CLI 负例：unknown-arg=2、无动作=2、--help=0、--descriptor 缺文件=2
  V4  确定性：--selftest --json 同参两轮输出逐字节恒等
  V5  板端产物凭据语义扫描
  V6  板端树清理（vext 构件 + 构建树），确认输出
"""
import json
import os
import re
import sys
import time

import paramiko

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", ".."))
VEXT_CPP = os.path.join(HERE, "vext_item001.cpp")
OUTDIR = os.path.join(REPO, "docs", "vision", "test_evidence", "ITEM-001")
REMOTE = "/tmp/railhmi_vision_item001_verify-20261018-item001-v1"
STAMP = "verify-20261018-item001-v1"
HOST, USER = "192.168.0.103", "admin"
EVID = []
REBUILD = "--rebuild" in sys.argv


def note(step, ok, detail=""):
    EVID.append({"step": step, "ok": bool(ok), "detail": str(detail)[:400]})
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
        import subprocess
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
    client.connect(hostname=HOST, port=22, username=USER, password=secret, timeout=10,
                   auth_timeout=10, banner_timeout=10, look_for_keys=False, allow_agent=False)
    try:
        # V0 构建树仍在（keep-remote）或按需重建
        rc, out = rrun(client, "test -d %s/app/build-core && echo TREE_OK" % REMOTE, 20)
        if rc != 0 or "TREE_OK" not in out:
            if not REBUILD:
                note("V0_tree_present", False, "tree missing; rerun with --rebuild")
                return 1
            note("V0_tree_present", True, "tree missing -> rebuild")
            sys.path.insert(0, os.path.join(REPO, "app", "tools"))
            import board_vision_model_probe as probe
            argv = ["--host", HOST, "--user", USER,
                    "--credential-env", "TRAIN_RK3588_SSH_CREDENTIAL",
                    "--outdir", OUTDIR, "--stamp", STAMP, "--keep-remote"]
            old = sys.argv
            sys.argv = ["board_vision_model_probe.py"] + argv
            try:
                prc = probe.main()
            finally:
                sys.argv = old
            note("V0_rebuild", prc == 0, "probe_exit=%s" % prc)
        else:
            note("V0_tree_present", True, out.strip()[-60:])

        # V1 上传 + g++ 直编
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(120)
        sftp.put(VEXT_CPP, REMOTE + "/vext_item001.cpp")
        sftp.close()
        cmd = ("cd %s/app && g++ -std=c++17 -O1 -I src/core/include "
               "%s/vext_item001.cpp -o %s/vext_item001 build-core/src/core/librailhmi_core.a "
               "2>&1; echo VEXT_BUILD_EXIT=$?" % (REMOTE, REMOTE, REMOTE))
        t0 = time.time()
        rc, out = rrun(client, cmd, 180)
        note("V1_vext_build", "VEXT_BUILD_EXIT=0" in out,
             "exit=%s %ss %s" % (rc, round(time.time() - t0, 1), out.strip()[-160:]))

        # V2 运行 vext：全部 check PASS + 计数与夹具源 check( 调用数一致（不手抄）
        cmd = "cd %s && ./vext_item001; echo VEXT_RUN_EXIT=$?" % REMOTE
        rc, out = rrun(client, cmd, 60)
        passes = out.count("vext PASS ")
        fails = out.count("vext FAIL ")
        names = [ln.split("vext PASS ", 1)[1] for ln in out.splitlines()
                 if ln.startswith("vext PASS ")]
        with open(VEXT_CPP, encoding="utf-8") as fh:
            src_calls = len(re.findall(r"^\s*check\(", fh.read(), re.M))
        # 源 29 处 check( 调用含 g10 if/else 二选一分支（运行时恰执行其一）→ 板上恒 28 行
        expect_checks = src_calls - 1
        msum = re.search(r"vext SUMMARY passed=(\d+) failed=(\d+)", out)
        note("V2_vext_all_pass", "VEXT_RUN_EXIT=0" in out and fails == 0
             and msum is not None and int(msum.group(2)) == 0,
             "pass=%d fail=%d exit=%s" % (passes, fails, rc))
        note("V2_vext_count_matches_source", passes == expect_checks
             and expect_checks == 28 and msum is not None
             and int(msum.group(1)) == expect_checks,
             "board=%d source_calls=%d (g10 if/else picks 1)" % (passes, src_calls))
        # 关键判例域在场（结构识别负控/实板 NHWC/evidence 门）
        dom = {"g01": any(n.startswith("g01_") for n in names),
               "g04": any(n.startswith("g04_") for n in names),
               "g07": any(n.startswith("g07_") for n in names),
               "g14": any(n.startswith("g14_") for n in names)}
        note("V2_judgement_domains", all(dom.values()), str(dom))

        # V3 CLI 负例（退出码 token 解析，外层 shell 恒 0）
        neg = []
        for label, c in [
            ("unknown-arg", "cd %s/app && ./build-core/tools/railhmi-model-probe --nope 2>&1; echo X=$?"),
            ("no-action", "cd %s/app && ./build-core/tools/railhmi-model-probe 2>&1; echo X=$?"),
            ("help", "cd %s/app && ./build-core/tools/railhmi-model-probe --help 2>&1; echo X=$?"),
            ("missing-descriptor",
             "cd %s/app && ./build-core/tools/railhmi-model-probe --descriptor /tmp/nope.desc "
             "--declared yolov5s 2>&1; echo X=$?"),
        ]:
            fmt_args = [REMOTE] * c.count("%s")
            rc, out = rrun(client, c % tuple(fmt_args), 30)
            mtok = None
            for ln in out.splitlines():
                if ln.strip().startswith("X="):
                    mtok = ln.strip()
            val = int(mtok.split("=")[1]) if mtok and mtok[2:].isdigit() else None
            neg.append((label, val))
        exp_neg = {"unknown-arg": 2, "no-action": 2, "help": 0, "missing-descriptor": 2}
        ok3 = all(v == exp_neg[k] for k, v in neg)
        note("V3_cli_negative_exits", ok3, str(neg))

        # V4 确定性：--selftest --json 同参两轮逐字节恒等
        cmd = ("cd %s/app && ./build-core/tools/railhmi-model-probe --selftest --json --out /tmp/d1.json "
               ">/dev/null; ./build-core/tools/railhmi-model-probe --selftest --json --out /tmp/d2.json "
               ">/dev/null; sha256sum /tmp/d1.json /tmp/d2.json; echo D4_DONE" % REMOTE)
        rc, out = rrun(client, cmd, 60)
        lines = [ln for ln in out.splitlines() if "/tmp/d" in ln]
        same = len(lines) == 2 and lines[0].split()[0] == lines[1].split()[0]
        note("V4_selftest_deterministic", same and "D4_DONE" in out,
             lines[0][:16] + "..." if lines else out[-80:])

        # V5 板端产物凭据语义扫描（保留树内日志/JSON）
        cmd = ("grep -rIl -E '(password=|[a-z]+://[^/ :]+:[^@ ]+@)' %s/app/core-evidence %s/descriptors "
               "2>/dev/null; echo LEAK_SCAN_DONE" % (REMOTE, REMOTE))
        rc, out = rrun(client, cmd, 30)
        hits = [ln for ln in out.splitlines() if ln and "LEAK_SCAN_DONE" not in ln]
        note("V5_board_no_credential_leak", "LEAK_SCAN_DONE" in out and not hits, str(hits[:3]))

        # V6 清理：vext 构件 + /tmp/d*.json + 构建树
        cmd = ("rm -f %s/vext_item001.cpp %s/vext_item001 /tmp/d1.json /tmp/d2.json && "
               "rm -rf %s && ls -d %s 2>&1; echo CLEAN_DONE" % (REMOTE, REMOTE, REMOTE, REMOTE))
        rc, out = rrun(client, cmd, 60)
        cleaned = "CLEAN_DONE" in out and ("No such file" in out or "cannot access" in out)
        note("V6_board_cleanup", cleaned, out.strip()[-80:])
    finally:
        client.close()

    ok_all = all(e["ok"] for e in EVID)
    with open(os.path.join(OUTDIR, "vext-item001-%s.json" % STAMP), "w", encoding="utf-8") as fh:
        json.dump({"stamp": STAMP, "checks": EVID, "all_pass": ok_all}, fh, ensure_ascii=False, indent=2)
    print("VEXT SUMMARY all_pass=%s (%d checks)" % (ok_all, len(EVID)), flush=True)
    return 0 if ok_all else 1


if __name__ == "__main__":
    raise SystemExit(main())
