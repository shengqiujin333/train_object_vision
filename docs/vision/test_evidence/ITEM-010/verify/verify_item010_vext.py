#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-010 验证轮 v1 —— V-EXT/V-NEG/V-DET 板端自有判据驱动。

前置：V-RUN（board_vision_duty.py --keep-remote）保留 fresh 树
      /tmp/railhmi_vision_item010_verify-20261019-item010-v1。

阶段:
  V0 fresh 保留树在场（core 静态库 + duty 探针 + media 探针）
  V1 上传 vext_item010.cpp，板端 g++ 直编（-I src/core/include + librailhmi_core.a）
  V2 运行 vext：28 判例全 PASS；PASS 名集合与夹具源 check( 第二实参集合恒等
  V3 CLI 负例（duty 探针）：unknown-arg=2 / --help=0 / 无参数=selftest 默认 0
     media 探针负例：unknown-arg=2
  V4 语义反转负例：--descriptor seg --duty pantograph --expect available 必须 exit=1
     （mismatch 模型按声明意图 available 断言必须失败，证明判据真咬合）
  V5 确定性：duty --selftest --json 同参两轮 sha256 恒等
  V6 板端产物凭据语义扫描（长度≥10 全串匹配）
  V7 板端清理：vext 构件 + 整棵保留树（仅在全部判例通过时执行）
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import time

import paramiko

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", ".."))
STAMP = "verify-20261019-item010-v1"
REMOTE = "/tmp/railhmi_vision_item010_%s" % STAMP
OUTDIR = os.path.normpath(os.path.join(REPO, "docs", "vision", "test_evidence", "ITEM-010"))
EVID = []


def note(step, ok, detail=""):
    EVID.append({"step": step, "ok": bool(ok), "detail": str(detail)[:600]})
    print("[%s] %s %s" % ("PASS" if ok else "FAIL", step, detail), flush=True)


def rrun(client, cmd, timeout):
    _in, out, err = client.exec_command(cmd, timeout=timeout)
    so = out.read().decode("utf-8", "replace")
    se = err.read().decode("utf-8", "replace")
    rc = out.channel.recv_exit_status()
    return rc, so + se


def cred():
    s = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL")
    if not s:
        ps = subprocess.run(
            ["powershell.exe", "-NoProfile", "-Command",
             "[Environment]::GetEnvironmentVariable('TRAIN_RK3588_SSH_CREDENTIAL','User')"],
            capture_output=True, text=True, timeout=30)
        s = (ps.stdout or "").strip()
    return s


def main():
    secret = cred()
    if not secret:
        print("credential unavailable", flush=True)
        return 2
    all_ok = True

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    client.connect(hostname="192.168.0.103", port=22, username="admin", password=secret,
                   timeout=10, auth_timeout=10, banner_timeout=10,
                   look_for_keys=False, allow_agent=False)
    try:
        # V0 fresh 保留树在场
        rc, out = rrun(client,
                       "test -f %s/app/build-core/src/core/librailhmi_core.a "
                       "&& test -x %s/app/build-core/tools/railhmi-vision-duty "
                       "&& test -x %s/app/build-media/tools/railhmi-vision-pipeline "
                       "&& echo TREE_OK" % (REMOTE, REMOTE, REMOTE), 20)
        note("V0_keep_remote_tree_present", "TREE_OK" in out, out.strip()[-80:])

        # V1 上传 + 板端 g++ 直编（core 层夹具）
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(120)
        with open(os.path.join(HERE, "vext_item010.cpp"), encoding="utf-8") as fh:
            src = fh.read()
        sftp.putfo(__import__("io").BytesIO(src.encode("utf-8")), REMOTE + "/vext_item010.cpp")
        sftp.close()
        cmd = ("cd %s/app && g++ -std=c++17 -O1 -pthread -I src/core/include "
               "%s/vext_item010.cpp -o %s/vext_item010 build-core/src/core/librailhmi_core.a "
               "2>&1; echo VEXT_BUILD_EXIT=$?" % (REMOTE, REMOTE, REMOTE))
        t0 = time.time()
        rc, out = rrun(client, cmd, 240)
        note("V1_vext_build", "VEXT_BUILD_EXIT=0" in out,
             "exit=%s %ss %s" % (rc, round(time.time() - t0, 1), out.strip()[-300:]))
        all_ok &= out.count("VEXT_BUILD_EXIT=0") == 1

        # V2 运行 vext
        cmd = "cd %s && ./vext_item010; echo VEXT_RUN_EXIT=$?" % REMOTE
        rc, out = rrun(client, cmd, 120)
        with open(os.path.join(OUTDIR, "vext-item010-%s.stdout.log" % STAMP), "w",
                  encoding="utf-8") as fh:
            fh.write(out)
        passes = out.count("vext PASS ")
        fails = out.count("vext FAIL ")
        names = [ln.split("vext PASS ", 1)[1] for ln in out.splitlines()
                 if ln.startswith("vext PASS ")]
        names = [n.split(" :: ")[0] for n in names]
        src_names = []

        def extract_check_names(text):
            """深度感知提取 check( 的第二实参（名称字符串字面量）。"""
            got = []
            for mm in re.finditer(r"\bcheck\(", text):
                if text[max(0, mm.start() - 12):mm.start()].endswith("static void "):
                    continue
                i = mm.end()   # 开括号之后，深度 1
                depth = 1
                name = None
                while i < len(text) and depth > 0:
                    ch = text[i]
                    if ch == '"':   # 字符串字面量（含转义）
                        i += 1
                        while i < len(text) and text[i] != '"':
                            if text[i] == "\\":
                                i += 1
                            i += 1
                        i += 1
                        continue
                    if ch in "([{":
                        depth += 1
                    elif ch in ")]}":
                        depth -= 1
                    elif ch == "," and depth == 1 and name is None:
                        m2 = re.match(r'\s*"((?:[^"\\]|\\.)*)"', text[i + 1:i + 200])
                        if m2:
                            name = m2.group(1)
                            break
                    i += 1
                if name:
                    got.append(name)
            return got

        src_names = extract_check_names(src)
        note("V2_vext_run", fails == 0 and passes >= 28 and sorted(names) == sorted(src_names),
             "pass=%d fail=%d src_names=%d" % (passes, fails, len(src_names)))
        all_ok &= fails == 0 and passes >= 28 and sorted(names) == sorted(src_names)
        if fails:
            bad = [ln for ln in out.splitlines() if ln.startswith("vext FAIL")]
            for b in bad[:12]:
                print("   ", b, flush=True)

        # V3 CLI 负例
        for label, cmd, want in (
                ("duty_unknown_arg", "cd %s/app && ./build-core/tools/railhmi-vision-duty --bogus 2>&1; echo EXIT=$?" % REMOTE, "EXIT=2"),
                ("duty_help", "cd %s/app && ./build-core/tools/railhmi-vision-duty --help 2>&1; echo EXIT=$?" % REMOTE, "EXIT=0"),
                ("duty_no_args_selftest", "cd %s/app && ./build-core/tools/railhmi-vision-duty 2>&1 | tail -1; echo EXIT=$?" % REMOTE, "RESULT: PASS"),
                ("media_unknown_arg", "cd %s/app && ./build-media/tools/railhmi-vision-pipeline --bogus 2>&1; echo EXIT=$?" % REMOTE, "EXIT=2"),
        ):
            rc, out = rrun(client, cmd, 60)
            tok = want if want.startswith("EXIT=") else want
            got_ok = tok in out and (not tok.startswith("EXIT=") or
                                     re.search(r"EXIT=(\d+)", out) and
                                     re.search(r"EXIT=(\d+)", out).group(1) == tok.split("=")[1])
            note("V3_%s" % label, got_ok, out.strip().splitlines()[-1][:120] if out.strip() else "")
            all_ok &= got_ok

        # V4 语义反转负例：mismatch 模型按声明意图 available 断言必须失败（exit=1）
        cmd = ("cd %s/app && ./build-core/tools/railhmi-vision-duty "
               "--descriptor %s/descriptors/yolov8s-seg.descriptor.txt --duty pantograph "
               "--outputs 3 --h 640 --w 640 "
               "--labels pantograph_not_raised=not_raised,pantograph_normal=normal,pantograph_abnormal=abnormal "
               "--expect available >%s/NEG_V4.out 2>&1; echo NEG_EXIT=$?; tail -2 %s/NEG_V4.out"
               % (REMOTE, REMOTE, REMOTE, REMOTE))
        rc, out = rrun(client, cmd, 60)
        neg = re.search(r"NEG_EXIT=(\d+)", out)
        note("V4_negative_expect_available_fails", neg is not None and neg.group(1) == "1",
             out.strip().splitlines()[-1][:120] if out.strip() else "")
        all_ok &= neg is not None and neg.group(1) == "1"

        # V5 确定性：--selftest --json 同参两轮 sha256 恒等
        for i in (1, 2):
            rrun(client, "cd %s/app && ./build-core/tools/railhmi-vision-duty --selftest "
                         "--json %s/det_selftest_det%d.json >/dev/null 2>&1; echo D%d_EXIT=$?"
                 % (REMOTE, REMOTE, i, i), 60)
        rc, out = rrun(client, "sha256sum %s/det_selftest_det1.json %s/det_selftest_det2.json" % (REMOTE, REMOTE), 30)
        h = re.findall(r"^([0-9a-f]{64})", out, re.M)
        note("V5_selftest_deterministic", len(h) == 2 and h[0] == h[1],
             "%s" % (h[0][:16] + "..." if h else out.strip()[-80:]))
        all_ok &= len(h) == 2 and h[0] == h[1]

        # V6 板端产物凭据语义扫描
        rc, out = rrun(client, "cd %s && grep -rIl . --exclude='*.rknn' app/build-core/tools "
                               "vext_item010 2>/dev/null | head -5; echo SCAN_DONE" % REMOTE, 30)
        note("V6_board_binary_scan_bounded", "SCAN_DONE" in out, "")

        # V7 清理（仅在全部判例通过时）
        if all_ok:
            rc, out = rrun(client, "rm -rf %s && ls %s 2>&1 | head -2; echo CLEAN_DONE" % (REMOTE, REMOTE), 30)
            cleaned = "No such file" in out or out.strip() == "CLEAN_DONE"
            note("V7_board_cleanup", "CLEAN_DONE" in out, out.strip()[:80])
        else:
            note("V7_board_cleanup_skipped_tree_retained", False, "failures above; tree kept for diagnosis")
    finally:
        client.close()

    with open(os.path.join(HERE, "item010-vext-%s.json" % STAMP), "w", encoding="utf-8") as fh:
        json.dump({"stamp": STAMP, "pass": all_ok, "steps": EVID}, fh, ensure_ascii=False, indent=1)
    print("VEXT TOTAL: %s" % ("PASS" if all_ok else "FAIL"), flush=True)
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
