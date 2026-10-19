#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-009 视觉推理验证轮 v1 —— 阶段 3：V-EXT 板端自有扩展夹具驱动。

复用 V-RUN（board_vision_pipeline.py --keep-remote）保留的板端构建树：
  /tmp/railhmi_vision_item009_verify-20261020-item009-v1/app
    build-core/src/core/librailhmi_core.a

阶段:
  V0 保留树在场（core 静态库）
  V1 上传 vext_item009.cpp，板端 g++ 直编（-I src/core/include + 静态库）
  V2 运行 vext：全部 PASS；PASS 名集合与夹具源 check( 第二实参集合恒等（不手抄）
  V3 CLI 负例（探针 railhmi-vision-pipeline）：unknown-arg=2 / --help=0
  V4 确定性：--selftest 同参两轮输出 sha256 恒等（selftest 确定域行相同；
     不比对 --json 时间戳差异——json 行含失败计数为 0，确定）
  V5 板端产物凭据语义扫描（vext 输出 + core-evidence）
  V6 板端清理：vext 构件 + 整棵保留树（仅在全部判例通过时执行）
"""
import io
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
STAMP = "verify-20261020-item009-v1"
REMOTE = "/tmp/railhmi_vision_item009_%s" % STAMP
OUTDIR = os.path.join(REPO, "docs", "vision", "test_evidence", "ITEM-009")
PROBE = "build-media/tools/railhmi-vision-pipeline"
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
        # V0 保留树在场（media 构建树含探针；core 静态库用于 vext 直编）
        rc, out = rrun(client,
                       "test -x %s/app/%s && test -f %s/app/build-core/src/core/librailhmi_core.a "
                       "&& echo TREE_OK" % (REMOTE, PROBE, REMOTE), 20)
        note("V0_keep_remote_tree_present", "TREE_OK" in out, out.strip()[-60:])

        # V1 上传 + 板端 g++ 直编（core 层夹具：仅链 core 静态库，无 media/GStreamer 依赖）
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(120)
        with open(os.path.join(HERE, "vext_item009.cpp"), encoding="utf-8") as fh:
            sftp.putfo(io.BytesIO(fh.read().encode("utf-8")), REMOTE + "/vext_item009.cpp")
        sftp.close()
        cmd = ("cd %s/app && g++ -std=c++17 -O1 -pthread -I src/core/include "
               "%s/vext_item009.cpp -o %s/vext_item009 build-core/src/core/librailhmi_core.a "
               "2>&1; echo VEXT_BUILD_EXIT=$?" % (REMOTE, REMOTE, REMOTE))
        t0 = time.time()
        rc, out = rrun(client, cmd, 180)
        note("V1_vext_build", "VEXT_BUILD_EXIT=0" in out,
             "exit=%s %ss %s" % (rc, round(time.time() - t0, 1), out.strip()[-300:]))

        # V2 运行 vext
        cmd = "cd %s && ./vext_item009; echo VEXT_RUN_EXIT=$?" % REMOTE
        rc, out = rrun(client, cmd, 120)
        with open(os.path.join(OUTDIR, "vext-item009-%s.stdout.log" % STAMP), "w",
                  encoding="utf-8") as fh:
            fh.write(out)
        passes = out.count("vext PASS ")
        fails = out.count("vext FAIL ")
        names = [ln.split("vext PASS ", 1)[1] for ln in out.splitlines()
                 if ln.startswith("vext PASS ")]
        names = [n.split(" :: ")[0] for n in names]
        with open(os.path.join(HERE, "vext_item009.cpp"), encoding="utf-8") as fh:
            vsrc = fh.read()
        # 判例名集合级比对：夹具源全部 check( 第二实参（顶层逗号扫描，跳过 static 定义）
        src_names = []
        for mm in re.finditer(r"\bcheck\(", vsrc):
            if vsrc[max(0, mm.start() - 7):mm.start()].endswith("static "):
                continue
            i = mm.end()
            d = 0
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
                    while i < len(vsrc) and vsrc[i] != '"':
                        if vsrc[i] == "\\":
                            i += 1
                        i += 1
                    i += 1
                elif ch == "," and d == 0:
                    break
                else:
                    i += 1
            j = i + 1
            while j < len(vsrc) and vsrc[j] in " \t\r\n":
                j += 1
            if vsrc[j] == '"':
                j += 1
                k = j
                while k < len(vsrc) and vsrc[k] != '"':
                    k += 1
                src_names.append(vsrc[j:k])
        src_set = set(src_names)
        board_set = set(names)
        note("V2_vext_all_pass", "VEXT_RUN_EXIT=0" in out and fails == 0,
             "pass=%d fail=%d exit=%s" % (passes, fails, rc))
        note("V2_vext_count_matches_source", src_set == board_set and fails == 0,
             "src=%d board=%d only_src=%s only_board=%s" %
             (len(src_set), len(board_set), sorted(src_set - board_set)[:3],
              sorted(board_set - src_set)[:3]))
        doms = ["A_", "B_", "C_", "D_", "E_", "F_", "G_", "H_", "I_", "J_", "K_", "L_", "M_"]
        dom_ok = {d: any(d in n for n in names) for d in doms}
        note("V2_judgement_domains", all(dom_ok.values()),
             str([d for d, v in dom_ok.items() if not v]))

        # V3 CLI 负例（退出码 token，外层 shell 恒 0）。
        # 口径：mode 默认 selftest -> 无动作 = 自检通过 exit 0（非负例）；
        # unknown-arg=2；--help=0。bad-source（缺失模型 chain）另案 = D-01 缺陷取证。
        neg = []
        for label, tmpl in [
            ("unknown-arg", "cd %s/app && %s --bogus-flag 2>&1; echo X=$?" % (REMOTE, PROBE)),
            ("no-action", "cd %s/app && %s --out /tmp/e9na.log 2>&1; echo X=$?" % (REMOTE, PROBE)),
            ("help", "cd %s/app && %s --help 2>&1; echo X=$?" % (REMOTE, PROBE)),
        ]:
            rc, out = rrun(client, tmpl, 120)
            m = re.search(r"^X=(\d+)$", out, re.M)
            neg.append((label, int(m.group(1)) if m else None,
                        (out.strip().splitlines() or [""])[-1][-80:]))
        exp_neg = {"unknown-arg": 2, "no-action": 0, "help": 0}
        note("V3_cli_negative_exits",
             all(v == exp_neg[k] for k, v, _ in neg if k in exp_neg), str(neg))

        # V4 确定性：--selftest 同参两轮 --json 摘要 sha256 恒等。
        # （stdout 含实测温度/时序行属环境量，不做字节恒等；--json 仅 mode/failures/result。）
        cmd = ("cd %s/app && %s --selftest --config ./config --out /tmp/e9d1.log "
               "--json /tmp/e9d1.json >/dev/null 2>&1; echo E1=$?; "
               "%s --selftest --config ./config --out /tmp/e9d2.log "
               "--json /tmp/e9d2.json >/dev/null 2>&1; echo E2=$?; "
               "sha256sum /tmp/e9d1.json /tmp/e9d2.json; cat /tmp/e9d1.json; echo D_DONE"
               % (REMOTE, PROBE, PROBE))
        rc, out = rrun(client, cmd, 180)
        hashes = re.findall(r"^([0-9a-f]{64})  (/tmp/e9d\d\.json)$", out, re.M)
        exits = dict(re.findall(r"^([EF]\d)=(\d+)$", out, re.M))
        body = out.split("D_DONE")[0]
        jbody = out.split("D_DONE")[1] if "D_DONE" in out else ""
        by_file = {p: h for h, p in hashes}
        det_ok = (len(hashes) == 2 and exits.get("E1") == "0" and exits.get("E2") == "0"
                  and by_file.get("/tmp/e9d1.json") == by_file.get("/tmp/e9d2.json"))
        note("V4_deterministic_selftest", det_ok,
             "sha=%s json=%s" % (by_file.get("/tmp/e9d1.json", "?")[:16],
                                 jbody.strip().splitlines()[0][:80] if jbody.strip() else "?"))

        # V5 板端产物凭据语义扫描
        cmd = ("grep -rIl -E '(password=|[a-z]+://[^/ :]+:[^@ ]+@)' %s/app/core-evidence "
               "%s/vext_item009 2>/dev/null; echo LEAK_SCAN_DONE" % (REMOTE, REMOTE))
        rc, out = rrun(client, cmd, 30)
        hits = [ln for ln in out.splitlines() if ln and "LEAK_SCAN_DONE" not in ln]
        note("V5_board_no_credential_leak", "LEAK_SCAN_DONE" in out and not hits, str(hits[:3]))

        # V6 清理：仅在全部判例通过时
        if all(e["ok"] for e in EVID):
            cmd = ("rm -f %s/vext_item009.cpp %s/vext_item009 /tmp/e9d1.log /tmp/e9d2.log; "
                   "rm -rf %s; ls -d %s 2>&1; echo CLEAN_DONE" % (REMOTE, REMOTE, REMOTE, REMOTE))
            rc, out = rrun(client, cmd, 60)
            cleaned = "CLEAN_DONE" in out and ("No such file" in out or "cannot access" in out)
            note("V6_board_cleanup", cleaned, out.strip()[-90:])
        else:
            note("V6_board_cleanup", False, "skipped: prior failures; keep-remote tree preserved")
    finally:
        client.close()

    ok_all = all(e["ok"] for e in EVID)
    with open(os.path.join(OUTDIR, "vext-item009-%s.json" % STAMP), "w", encoding="utf-8") as fh:
        json.dump({"stamp": STAMP, "checks": EVID, "all_pass": ok_all},
                  fh, ensure_ascii=False, indent=2)
    print("VEXT SUMMARY all_pass=%s (%d checks)" % (ok_all, len(EVID)), flush=True)
    return 0 if ok_all else 1


if __name__ == "__main__":
    raise SystemExit(main())
