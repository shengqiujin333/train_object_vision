#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-004 验证自有扩展夹具驱动（software_verification, VERIFY-20261019-item004-v1）。

复用 --keep-remote 保留的板端构建树（交付 runner 自身产物）：
  /tmp/railhmi_vision_item004_verify-20261019-item004-v1/app
    build-core/src/core/librailhmi_core.a
    build-core/tools/railhmi-vision-seg
    src/core/include

阶段:
  V0  保留树在场（build-core 工具与静态库）
  V1  上传 vext_item004.cpp，g++ 直编（-I src/core/include + 静态库）
  V2  运行 vext：全部 PASS；PASS 行数与夹具源 check( 调用数一致（24，不手抄）
  V3  CLI 负例（探针）：unknown-arg=2、无动作=2、--help=0
  V4  确定性：--selftest 同参两轮 sha256 恒等
  V5  板端产物凭据语义扫描（core-evidence 下日志/JSON）
  V6  板端清理：vext 构件 + 整棵保留树（仅在全部判例通过时执行）
"""
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
STAMP = "verify-20261019-item004-v1"
REMOTE = "/tmp/railhmi_vision_item004_%s" % STAMP
OUTDIR = os.path.join(REPO, "docs", "vision", "test_evidence", "ITEM-004")
PROBE = "build-core/tools/railhmi-vision-seg"
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
        # V0 保留树在场
        rc, out = rrun(client,
                       "test -x %s/app/%s && test -f %s/app/build-core/src/core/librailhmi_core.a "
                       "&& echo TREE_OK" % (REMOTE, PROBE, REMOTE), 20)
        note("V0_keep_remote_tree_present", "TREE_OK" in out, out.strip()[-60:])

        # V1 上传 + g++ 直编
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(120)
        with open(os.path.join(HERE, "vext_item004.cpp"), encoding="utf-8") as fh:
            sftp.putfo(io.BytesIO(fh.read().encode("utf-8")), REMOTE + "/vext_item004.cpp")
        sftp.close()
        cmd = ("cd %s/app && g++ -std=c++17 -O1 -pthread -I src/core/include "
               "%s/vext_item004.cpp -o %s/vext_item004 build-core/src/core/librailhmi_core.a "
               "2>&1; echo VEXT_BUILD_EXIT=$?" % (REMOTE, REMOTE, REMOTE))
        t0 = time.time()
        rc, out = rrun(client, cmd, 180)
        note("V1_vext_build", "VEXT_BUILD_EXIT=0" in out,
             "exit=%s %ss %s" % (rc, round(time.time() - t0, 1), out.strip()[-300:]))

        # V2 运行 vext
        cmd = "cd %s && ./vext_item004; echo VEXT_RUN_EXIT=$?" % REMOTE
        rc, out = rrun(client, cmd, 300)
        with open(os.path.join(OUTDIR, "vext-item004-%s.stdout.log" % STAMP), "w",
                  encoding="utf-8") as fh:
            fh.write(out)
        passes = out.count("vext PASS ")
        fails = out.count("vext FAIL ")
        names = [ln.split("vext PASS ", 1)[1] for ln in out.splitlines()
                 if ln.startswith("vext PASS ")]
        with open(os.path.join(HERE, "vext_item004.cpp"), encoding="utf-8") as fh:
            vsrc = fh.read()
        # 计数口径(集合级): 夹具源全部 check/check_near 判例名（顶层逗号扫描提取）
        # 与板上 vext PASS 名单逐名相等——g05/g06 失败分支仅守卫语义，运行时恒不触发
        BS = chr(92)
        src_names = []
        for mm in re.finditer(r"\bcheck(?:_near)?\(", vsrc):
            if vsrc[max(0, mm.start() - 7):mm.start()].endswith("static "):
                continue
            i = mm.end()
            d = 0
            while i < len(vsrc):
                ch = vsrc[i]
                if ch in "([{":
                    d += 1
                elif ch in ")]}":
                    d -= 1
                elif ch == '"':
                    i += 1
                    while i < len(vsrc) and vsrc[i] != '"':
                        if vsrc[i] == BS:
                            i += 1
                        i += 1
                elif ch == ',' and d == 0:
                    break
                i += 1
            j = i + 1
            while j < len(vsrc) and vsrc[j] in ' \t\r\n':
                j += 1
            if vsrc[j] == '"':
                j += 1
                k = j
                while k < len(vsrc) and vsrc[k] != '"':
                    k += 1
                src_names.append(vsrc[j:k])
        # check_near(a, b, tol, name): 名为第 4 个顶层实参——解析整体调用取所有顶层字符串取最后一个
        near_names = []
        for mm in re.finditer(r"\bcheck_near\(", vsrc):
            i = mm.end()
            d = 0
            strings = []
            while i < len(vsrc):
                ch = vsrc[i]
                if ch in "([{":
                    d += 1
                    i += 1
                elif ch in ")]}":
                    if d == 0:
                        break
                    d -= 1
                    i += 1
                elif ch == '"':
                    i += 1
                    buf = []
                    while i < len(vsrc) and vsrc[i] != '"':
                        if vsrc[i] == BS:
                            i += 1
                        buf.append(vsrc[i])
                        i += 1
                    i += 1
                    strings.append(''.join(buf))
                elif ch == ',' and d == 0:
                    i += 1
                elif ch == ')' and d == 0:
                    break
                else:
                    i += 1
            if strings and not strings[-1].startswith('vext OK') and not strings[-1].startswith('a='):
                near_names.append(strings[-1])
        src_set = set(src_names) | set(near_names)
        board_set = set(names)
        msum = re.search(r"vext (?:OK|FAILED) \((\d+) failure", out)
        note("V2_vext_all_pass", "VEXT_RUN_EXIT=0" in out and msum is not None
             and int(msum.group(1)) == 0 and fails == 0,
             "pass=%d fail=%d exit=%s" % (passes, fails, rc))
        note("V2_vext_count_matches_source", src_set == board_set and fails == 0,
             "src=%d board=%d only_src=%s only_board=%s" %
             (len(src_set), len(board_set), sorted(src_set - board_set)[:3],
              sorted(board_set - src_set)[:3]))
        doms = ["g01", "g02", "g04", "g05", "g07", "g08", "g09", "g10", "g12", "g14",
                "g16", "g17", "g20", "g21", "g22", "g23", "g24"]
        dom_ok = {d: any(n.startswith(d + "_") for n in names) for d in doms}
        note("V2_judgement_domains", all(dom_ok.values()), str(dom_ok))

        # V3 CLI 负例（退出码 token，外层 shell 恒 0）
        neg = []
        for label, tmpl in [
            ("unknown-arg", "cd %s/app && %s --bogus-flag 2>&1; echo X=$?" % (REMOTE, PROBE)),
            ("no-action", "cd %s/app && %s 2>&1; echo X=$?" % (REMOTE, PROBE)),
            ("help", "cd %s/app && %s --help 2>&1; echo X=$?" % (REMOTE, PROBE)),
        ]:
            rc, out = rrun(client, tmpl, 30)
            m = re.search(r"^X=(\d+)$", out, re.M)
            neg.append((label, int(m.group(1)) if m else None,
                        (out.strip().splitlines() or [""])[-1][-60:]))
        exp_neg = {"unknown-arg": 2, "no-action": 2, "help": 0}
        note("V3_cli_negative_exits", all(v == exp_neg[k] for k, v, _ in neg), str(neg))

        # V4 确定性：--selftest 同参两轮 sha256 恒等
        cmd = ("cd %s/app && %s --selftest --out /tmp/d1.log 2>&1; echo E1=$?; "
               "%s --selftest --out /tmp/d2.log 2>&1; echo E2=$?; "
               "sha256sum /tmp/d1.log /tmp/d2.log; echo D_DONE" % (REMOTE, PROBE, PROBE))
        rc, out = rrun(client, cmd, 120)
        lines = [ln for ln in out.splitlines()
                 if re.match(r"^[0-9a-f]{64}  /tmp/d", ln)]
        e1 = re.search(r"E1=(\d+)", out)
        e2 = re.search(r"E2=(\d+)", out)
        same = len(lines) == 2 and lines[0].split()[0] == lines[1].split()[0]
        note("V4_selftest_deterministic", same and e1 and e2 and e1.group(1) == "0" == e2.group(1),
             lines[0][:16] + "..." if lines else out[-120:])

        # V5 板端产物凭据语义扫描
        cmd = ("grep -rIl -E '(password=|[a-z]+://[^/ :]+:[^@ ]+@)' %s/app/core-evidence "
               "%s/vext_item004 2>/dev/null; echo LEAK_SCAN_DONE" % (REMOTE, REMOTE))
        rc, out = rrun(client, cmd, 30)
        hits = [ln for ln in out.splitlines() if ln and "LEAK_SCAN_DONE" not in ln]
        note("V5_board_no_credential_leak", "LEAK_SCAN_DONE" in out and not hits, str(hits[:3]))

        # V6 清理：仅在全部判例通过时
        if all(e["ok"] for e in EVID):
            cmd = ("rm -f %s/vext_item004.cpp %s/vext_item004 /tmp/d1.log /tmp/d2.log; "
                   "rm -rf %s; ls -d %s 2>&1; echo CLEAN_DONE" % (REMOTE, REMOTE, REMOTE, REMOTE))
            rc, out = rrun(client, cmd, 60)
            cleaned = "CLEAN_DONE" in out and ("No such file" in out or "cannot access" in out)
            note("V6_board_cleanup", cleaned, out.strip()[-90:])
        else:
            note("V6_board_cleanup", False, "skipped: prior failures; keep-remote tree preserved")
    finally:
        client.close()

    ok_all = all(e["ok"] for e in EVID)
    with open(os.path.join(OUTDIR, "vext-item004-%s.json" % STAMP), "w", encoding="utf-8") as fh:
        json.dump({"stamp": STAMP, "checks": EVID, "all_pass": ok_all}, fh,
                  ensure_ascii=False, indent=2)
    print("VEXT SUMMARY all_pass=%s (%d checks)" % (ok_all, len(EVID)), flush=True)
    return 0 if ok_all else 1


if __name__ == "__main__":
    raise SystemExit(main())
