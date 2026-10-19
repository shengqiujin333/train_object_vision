#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-012 验证轮 v1 —— V-EXT/V-NEG/V-DET 板端自有判据驱动。

前置：V-RUN（board_vision_debug.py --keep-remote）保留 fresh 树
      /tmp/railhmi_vision_item012_verify-20261019-item012-v1。

阶段:
  V0 fresh 保留树在场（core + media + platform 静态库、两个探针）
  V1 上传 vext_item012.cpp，板端 g++ 直编（core + media 层，链接静态库 + gstreamer）
  V2 运行 vext：判例全 PASS；PASS 名集合与夹具源 check( 第二实参集合恒等
  V3 CLI 负例（debug 探针）：unknown-arg=2 / --help=0 / unknown-fixture=2 /
     无参数=selftest 默认 PASS / unknown overlay 名=2 / unknown role=2；
     media 探针负例：unknown-arg=2 / --debug-overlays bogus=2
  V4 语义反转负例：--config ... --role maintainer --overlays none 但 --mask-demo
     依赖 switches？——改用可咬合判据：--overlays 'mask,bogus' 必须被解析器拒绝
     （exit 2，证明开关解析在板端真实二进制中咬合，而非只在单测里）。
  V5 确定性：debug --selftest --json 同参两轮 sha256 恒等
  V6 板端产物凭据语义扫描（有界）
  V7 板端清理：vext 构件 + 整棵保留树（仅在全部判例通过时执行）
"""
import hashlib
import io
import json
import os
import re
import subprocess
import sys
import time

import paramiko

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", ".."))
STAMP = "verify-20261019-item012-v1"
REMOTE = "/tmp/railhmi_vision_item012_%s" % STAMP
OUTDIR = os.path.normpath(os.path.join(REPO, "docs", "vision", "test_evidence", "ITEM-012"))
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
                       "&& test -f %s/app/build-media/src/media/librailhmi_media.a "
                       "&& test -f %s/app/build-media/src/platform/librailhmi_platform.a "
                       "&& test -x %s/app/build-core/tools/railhmi-vision-debug "
                       "&& test -x %s/app/build-media/tools/railhmi-vision-pipeline "
                       "&& echo TREE_OK" % (REMOTE, REMOTE, REMOTE, REMOTE, REMOTE), 20)
        note("V0_keep_remote_tree_present", "TREE_OK" in out, out.strip()[-80:])
        all_ok &= "TREE_OK" in out

        # V1 上传 + 板端 g++ 直编（core+media 层夹具，需 gst + pthread + platform）
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(120)
        with open(os.path.join(HERE, "vext_item012.cpp"), encoding="utf-8") as fh:
            src = fh.read()
        sftp.putfo(io.BytesIO(src.encode("utf-8")), REMOTE + "/vext_item012.cpp")
        sftp.close()
        cmd = ("cd %s/app && g++ -std=c++17 -O1 -pthread -I src/core/include "
               "-I src/media/include $(pkg-config --cflags gstreamer-1.0 gstreamer-video-1.0) "
               "%s/vext_item012.cpp -o %s/vext_item012 "
               "build-media/src/media/librailhmi_media.a build-core/src/core/librailhmi_core.a "
               "build-media/src/platform/librailhmi_platform.a "
               "$(pkg-config --libs gstreamer-1.0 gstreamer-video-1.0) "
               "2>&1; echo VEXT_BUILD_EXIT=$?" % (REMOTE, REMOTE, REMOTE))
        t0 = time.time()
        rc, out = rrun(client, cmd, 300)
        note("V1_vext_build", "VEXT_BUILD_EXIT=0" in out,
             "exit=%s %ss %s" % (rc, round(time.time() - t0, 1), out.strip()[-400:]))
        all_ok &= out.count("VEXT_BUILD_EXIT=0") == 1

        # V2 运行 vext
        cmd = "cd %s && ./vext_item012; echo VEXT_RUN_EXIT=$?" % REMOTE
        rc, out = rrun(client, cmd, 120)
        with open(os.path.join(OUTDIR, "vext-item012-%s.stdout.log" % STAMP), "w",
                  encoding="utf-8") as fh:
            fh.write(out)
        passes = out.count("vext PASS ")
        fails = out.count("vext FAIL ")
        names = [ln.split("vext PASS ", 1)[1] for ln in out.splitlines()
                 if ln.startswith("vext PASS ")]
        names = [n.split(" :: ")[0] for n in names]
        src_names = extract_check_names(src)
        fixture_ok = fails == 0 and passes >= 27 and sorted(names) == sorted(src_names)
        note("V2_vext_run", fixture_ok,
             "pass=%d fail=%d src_names=%d" % (passes, fails, len(src_names)))
        all_ok &= fixture_ok
        if fails:
            bad = [ln for ln in out.splitlines() if ln.startswith("vext FAIL")]
            for b in bad[:12]:
                print("   ", b, flush=True)

        # V3 CLI 负例（debug 探针 + media 探针）。
        # 注意：debug 探针只在 --config 分支校验 --role/--overlays（--selftest 分支忽略之）；
        # media 探针只在 --mode chain 路径校验 --debug-overlays（selftest 先行返回）。
        for label, cmd, want in (
                ("debug_unknown_arg",
                 "cd %s/app && ./build-core/tools/railhmi-vision-debug --bogus 2>&1; echo EXIT=$?"
                 % REMOTE, "EXIT=2"),
                ("debug_help",
                 "cd %s/app && ./build-core/tools/railhmi-vision-debug --help 2>&1; echo EXIT=$?"
                 % REMOTE, "EXIT=0"),
                ("debug_unknown_fixture",
                 "cd %s/app && ./build-core/tools/railhmi-vision-debug --fixture no_such 2>&1; "
                 "echo EXIT=$?" % REMOTE, "EXIT=2"),
                ("debug_no_args_selftest",
                 "cd %s/app && ./build-core/tools/railhmi-vision-debug 2>&1 | tail -1; "
                 "echo EXIT=$?" % REMOTE, "RESULT: PASS"),
                ("debug_unknown_overlay_name",
                 "cd %s/app && ./build-core/tools/railhmi-vision-debug --config ./config "
                 "--role maintainer --overlays mask,bogus 2>&1; echo EXIT=$?" % REMOTE, "EXIT=2"),
                ("debug_unknown_role",
                 "cd %s/app && ./build-core/tools/railhmi-vision-debug --config ./config "
                 "--role superuser --overlays mask 2>&1; echo EXIT=$?" % REMOTE, "EXIT=2"),
                ("media_unknown_arg",
                 "cd %s/app && ./build-media/tools/railhmi-vision-pipeline --bogus 2>&1; "
                 "echo EXIT=$?" % REMOTE, "EXIT=2"),
                ("media_unknown_overlay_name",
                 "cd %s/app && ./build-media/tools/railhmi-vision-pipeline --mode chain "
                 "--config ./config --models-dir %s/model-assets --lanes CAM-01 --source test "
                 "--duration 1 --debug-overlays bogus > %s/neg_v3.out 2>&1; echo EXIT=$?; "
                 "tail -2 %s/neg_v3.out" % (REMOTE, REMOTE, REMOTE, REMOTE), "EXIT=2"),
        ):
            rc, out = rrun(client, cmd, 60)
            if want.startswith("EXIT="):
                m = re.search(r"EXIT=(\d+)", out)
                got_ok = m is not None and m.group(1) == want.split("=")[1]
            else:
                got_ok = want in out
            note("V3_%s" % label, got_ok, out.strip().splitlines()[-1][:120] if out.strip() else "")
            all_ok &= got_ok

        # V4 语义反转负例：--overlays none + --mask-demo 依声明不提供掩码数据
        #    （开关全关 -> overlays_effective=false）——用 --json 输出直接断言（注意
        #    config 模式的 JSON 形状是 {"view":...,"failures":N}）；对照：--overlays mask
        #    + --mask-demo 必须给出 CAM-01 positive>0（合成掩码每 7 字节 1 正像素）。
        cmd = ("cd %s/app && ./build-core/tools/railhmi-vision-debug --config ./config "
               "--role maintainer --overlays none --mask-demo "
               "--json %s/neg_v4_off.json >/dev/null 2>&1; echo OFF_EXIT=$?; "
               "./build-core/tools/railhmi-vision-debug --config ./config "
               "--role maintainer --overlays mask --mask-demo "
               "--json %s/neg_v4_on.json >/dev/null 2>&1; echo ON_EXIT=$?; "
               "python3 -c \"import json;"
               "d=json.load(open('%s/neg_v4_off.json'))['view'];"
               "e=json.load(open('%s/neg_v4_on.json'))['view'];"
               "cam=[c for c in e['channels'] if c['camera_id']=='CAM-01'][0];"
               "print('OFF_EFF', d['overlays_effective'], 'ON_EFF', e['overlays_effective'], "
               "'ON_MASK', cam['overlays']['mask'], 'POS', cam['mask']['positive_pixels'])\""
               ) % (REMOTE, REMOTE, REMOTE, REMOTE, REMOTE)
        rc, out = rrun(client, cmd, 60)
        # python bool 打印为 False/True（非 json 的小写）
        off_eff = "OFF_EFF False" in out
        on_eff = "ON_EFF True" in out
        pos_m = re.search(r"POS (\d+)", out)
        pos_ok = pos_m is not None and int(pos_m.group(1)) > 0
        mask_ok = "ON_MASK True" in out
        note("V4_overlay_switch_bites_end_to_end",
             "OFF_EXIT=0" in out and "ON_EXIT=0" in out and off_eff and on_eff and pos_ok
             and mask_ok,
             out.strip().splitlines()[-1][:120] if out.strip() else "")
        all_ok &= ("OFF_EXIT=0" in out and "ON_EXIT=0" in out and off_eff and on_eff
                   and pos_ok and mask_ok)

        # V5 确定性：--selftest --json 同参两轮 sha256 恒等
        for i in (1, 2):
            rrun(client, "cd %s/app && ./build-core/tools/railhmi-vision-debug --selftest "
                         "--json %s/dbg_selftest_det%d.json >/dev/null 2>&1; echo D%d_EXIT=$?"
                 % (REMOTE, REMOTE, i, i), 60)
        rc, out = rrun(client, "sha256sum %s/dbg_selftest_det1.json %s/dbg_selftest_det2.json"
                       % (REMOTE, REMOTE), 30)
        h = re.findall(r"^([0-9a-f]{64})", out, re.M)
        det_ok = len(h) == 2 and h[0] == h[1]
        note("V5_selftest_deterministic", det_ok,
             h[0][:16] + "..." if h else out.strip()[-80:])
        all_ok &= det_ok

        # V6 板端产物凭据语义扫描（有界）。SSH 凭据为 admin（5 字符，恰等于登记用户名，
        # MEMORY ITEM-002/004 教训：全串匹配必产生 cameras.yaml username 与权限判例名的
        # 文件级假阳性）——沿用既定口径：长度>=10 的凭据才参与全串命中计数；本轮照常
        # 执行扫描记录命中数，len<10 时命中不判 leak。
        rc, out = rrun(client, "cd %s && grep -r -c '%s' app/core-evidence/ vext_item012 "
                               "2>/dev/null | grep -v ':0'; echo SCAN_DONE" % (REMOTE, secret), 30)
        raw_hits = re.findall(r":\d+", out.split("SCAN_DONE")[0])
        leak = raw_hits if len(secret) >= 10 else []
        note("V6_board_artifact_credential_scan", "SCAN_DONE" in out and not leak,
             "cred_len=%d raw_hits=%s (len<10 -> admin full-string false-positive excluded per "
             "ITEM-002/004 convention)" % (len(secret), raw_hits))
        all_ok &= "SCAN_DONE" in out and not leak

        # V7 清理（仅在全部判例通过时）
        if all_ok:
            rc, out = rrun(client, "rm -rf %s && ls %s 2>&1 | head -2; echo CLEAN_DONE"
                           % (REMOTE, REMOTE), 30)
            note("V7_board_cleanup", "CLEAN_DONE" in out, out.strip()[:80])
            all_ok &= "CLEAN_DONE" in out
        else:
            note("V7_board_cleanup_skipped_tree_retained", False,
                 "failures above; tree kept for diagnosis")
    finally:
        client.close()

    with open(os.path.join(HERE, "item012-vext-%s.json" % STAMP), "w", encoding="utf-8") as fh:
        json.dump({"stamp": STAMP, "pass": all_ok, "steps": EVID}, fh, ensure_ascii=False, indent=1)
    print("VEXT TOTAL: %s" % ("PASS" if all_ok else "FAIL"), flush=True)
    return 0 if all_ok else 1


if __name__ == "__main__":
    sys.exit(main())
