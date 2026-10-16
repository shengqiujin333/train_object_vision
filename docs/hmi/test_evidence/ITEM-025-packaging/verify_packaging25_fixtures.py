# -*- coding: utf-8 -*-
"""ITEM-025 software_verification — board-side fixtures (verifier-owned).

Subject: implementer round stamp 20261016-ITEM025-r1 (commit 6d1787b),
declared artifact app/README.md, evidence
docs/hmi/test_evidence/ITEM-025-packaging/README.md.

Steps (all finite timeouts):
  1. Fingerprint-checked SSH connect (Paramiko, RejectPolicy, no prompts).
  2. Re-run the deliverable board_build.py --mode TestRun entry with verifier
     stamp VERIFY-20261017-v1 + --keep-remote; fresh logs + JUnit XML.
  3. Line-by-line [PASS] comparison: fresh core-tests log vs implementer
     r1 archived log (663 cases, exact list equality) + independent JUnit
     parse (663/0) with the 32 exact fault/injection case names.
  4. Independent fault-probe oracle assertions on the FRESH probe log +
     cross-round byte identity vs the archived r1 probe (core untouched).
  5. Independent sim-inject assertions on the FRESH sim log/json: FI-01..05/10
     states in one injected run (reconnect->offline escalation, AI unavailable,
     storage low, ptz out-of-tolerance, gap) + cross-round byte identity.
  6. Verifier ext fixture (verify_packaging25_ext.cpp) compiled on-board
     against THIS round's librailhmi_core.a: catalog/self-check contract,
     fault window semantics, per-FI sequences, determinism.
  7. Verifier packaging/deployment-contract scan (verify_packaging25_scan.py)
     on the fresh remote copy — including the D-01 ExecStartPre provenance and
     D-02 --config consumption keys.
  8. On-board negative control: delivered check_packaging.py on the fresh copy
     must PASS; on a synthetic mutated copy (broken unit + missing P6 card)
     must FAIL (proves the delivered checker bites where it claims to).
  9. Determinism: re-run fault-probe, require byte-identical output.
 10. Regression artifacts listing from the same TestRun.
 11. Cleanup verifier workdir and /tmp/railhmi_build by name; trailing ls of
     the removed dirs is the confirmation.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import io
import json
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

try:
    import paramiko
except Exception as exc:  # noqa: BLE001
    sys.stderr.write("paramiko unavailable: %r\n" % (exc,))
    raise SystemExit(4)


def fingerprint(key) -> str:
    try:
        return "SHA256:" + base64.b64encode(
            hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")
    except Exception:  # noqa: BLE001
        return "(unavailable)"


def rrun(client, cmd: str, timeout: int) -> dict:
    started = time.time()
    try:
        _in, out, err = client.exec_command(cmd, timeout=timeout)
        so = out.read().decode("utf-8", "replace")
        se = err.read().decode("utf-8", "replace")
        rc = out.channel.recv_exit_status()
        return {"cmd": cmd, "exit": rc, "elapsed_s": round(time.time() - started, 2),
                "output": so + se}
    except socket.timeout:
        return {"cmd": cmd, "exit": None, "elapsed_s": round(time.time() - started, 2),
                "output": "(timeout after %ss)" % timeout}
    except Exception as exc:  # noqa: BLE001
        return {"cmd": cmd, "exit": None, "elapsed_s": round(time.time() - started, 2),
                "output": "(error: %r)" % (exc,)}


def parse_core_tests_log(text: str) -> dict:
    return {"pass": [l.strip() for l in text.splitlines() if l.startswith("[PASS]")],
            "fail": [l.strip() for l in text.splitlines() if l.startswith("[FAIL]")],
            "result": next((l for l in text.splitlines() if l.startswith("RESULT:")), "")}


# Exact fault/injection-related case names that must appear in the JUnit XML
# (domain coverage for ITEM-025: FI catalog + sim injection).
ITEM025_CASES = [
    "FI-01 id", "FI-10 id", "fault to_string", "parse '01'", "parse 'fi-10'",
    "parse 'freeze'", "parse invalid fault rejected",
    "injection params logged", "injection camera logged",
    "format_observation non-empty",
    "FI-01 -> reconnecting", "FI-01 -> offline escalation", "FI-01 recovers to live",
    "FI-01 final state live",
    "FI-02 -> frozen", "FI-02 recovers to live",
    "FI-03 -> ai unavailable", "FI-03 no detections -> no alarms",
    "FI-03 summary alarms=0",
    "FI-04 -> storage low", "FI-04 record_available=false",
    "FI-04 recovers (low=false after)",
    "FI-05 -> ptz tolerance fail", "FI-05 recovers tolerance",
    "FI-10 -> recording gap logged", "FI-10 summary has gaps",
    "no fault -> front alarm produced", "no fault summary alarms=1",
    "simulation is deterministic/resettable",
    "pantograph injection -> L2 alarm", "none profile -> no alarms",
]

# fault-probe oracle: full line set of the delivered catalog of record.
FAULT_ORACLE = [
    r'^type=fault-catalog count=6$',
    r'^fault id=FI-01 label=断流 prompt="画面占位：重连中（显示已重连次数与最近帧时间），旧帧不作为实时" '
    r'recovery="等待自动重连（退避）或维护页手动重连；达阈值升级为设备报警"$',
    r'^fault id=FI-02 label=画面冻结 prompt="画面冻结：叠加占位层，最近帧时间停止推进" '
    r'recovery="确认摄像头/网络后重连；恢复出帧自动转在线"$',
    r'^fault id=FI-03 label=AI 退出 prompt="状态区 AI 项转“AI 不可用”，相关检测类报警标注“AI 不可用”（不静默）" '
    r'recovery="重启推理线程/服务；恢复后 AI 项转“运行”，前方 AI READY"$',
    r'^fault id=FI-04 label=磁盘不足 prompt="存储项提前告警；录像写入标注容量不足" '
    r'recovery="按保留策略淘汰普通片段（保护片段优先）；扩容或清理后恢复"$',
    r'^fault id=FI-05 label=云台超差 prompt="云台卡显示目标值 vs 实际值 \+ 容差校验失败（超差），前方 AI 进入 NOT_READY" '
    r'recovery="一键归位 / 重调前方预置位（二次确认）后复校；成功转 READY"$',
    r'^fault id=FI-10 label=录像中断 prompt="时间轴出现录像缺口标记，事件标注“录像不完整/不可用”（不静默）" '
    r'recovery="恢复录像进程/存储；缺口记录保留可检索"$',
    r'^type=self-check count=7$',
    r'^step id=SC-01 inject="railhmi-sim-inject --fi 01（断流）" '
    r'prompt="该路画面占位 \+ 重连中 \+ 重连次数/最近帧时间" recovery="自动重连或手动重连；恢复出帧转在线"$',
    r'^step id=SC-02 inject="railhmi-sim-inject --fi 02（冻结）" '
    r'prompt="画面冻结占位，最近帧时间停更" recovery="重连恢复；出帧自动转在线"$',
    r'^step id=SC-03 inject="railhmi-sim-inject --fi 03（AI 退出）" '
    r'prompt="状态区 AI 项“AI 不可用”，关联报警标注" recovery="重启推理；恢复后 AI 运行/READY"$',
    r'^step id=SC-04 inject="railhmi-sim-inject --fi 04（磁盘不足）" '
    r'prompt="存储项告警 \+ 录像容量不足标注" recovery="保留淘汰/清理后恢复"$',
    r'^step id=SC-05 inject="railhmi-sim-inject --fi 05（云台超差）" '
    r'prompt="云台卡超差 \+ 前方 AI NOT_READY" recovery="归位/预置位二次确认后复校"$',
    r'^step id=SC-10 inject="railhmi-sim-inject --fi 10（录像中断）" '
    r'prompt="时间轴缺口标记 \+ 事件“录像不可用”" recovery="恢复录像进程/存储"$',
    r'^step id=SC-BOOT inject="systemctl restart railhmi（或断电重启）" '
    r'prompt="显示启动恢复进度（8 步），完成后进入可用状态" recovery="systemd Restart=always 自动拉起；失败查日志"$',
    r'^type=acceptance fi_covered=true boot_step=true prompts_nonempty=true result=PASS$',
]


def assert_fault_probe(text: str) -> list:
    return ["missing line: %s" % rx[:70]
            for rx in FAULT_ORACLE if not re.search(rx, text, re.M)]


def assert_sim_log(text: str) -> list:
    fails = []
    checks = [
        (r'^t=0 type=injection camera=CAM-01 profile=front '
         r'faults=FI-01,FI-02,FI-03,FI-04,FI-05,FI-10 onset_ms=1500 recovery_ms=5000 '
         r'duration_ms=6000 step_ms=500$', "injection params line"),
        (r'^t=1500 type=stream_state state=reconnecting reconnect_count=1 presentable=false cam=CAM-01$',
         "FI-01 reconnect at onset"),
        (r'^t=1500 type=ai status=unavailable cam=CAM-01$', "FI-03 ai unavailable at onset"),
        (r'^t=1500 type=storage free_bytes=2147483647 .* low=true record_available=false$',
         "FI-04 storage low at onset"),
        (r'^t=1500 type=ptz tolerance_ok=false target_zoom=10\.000000 actual_zoom=10\.000000$',
         "FI-05 ptz out of tolerance at onset"),
        (r'^t=1500 type=gap cam=CAM-01 t0_ms=1500 t1_ms=2000 gap_count=1$', "FI-10 gap at onset"),
        (r'^t=2500 type=stream_state state=offline reconnect_count=3 presentable=false cam=CAM-01$',
         "FI-01 offline escalation at threshold=3"),
        (r'^t=5000 type=stream_state state=live reconnect_count=0 presentable=true cam=CAM-01$',
         "recovery at 5000 -> live, count reset"),
        (r'^t=6000 type=summary final_state=live reconnect_count=0 alarms=2 gaps=1 events_recorded=2$',
         "summary line"),
    ]
    for rx, name in checks:
        if not re.search(rx, text, re.M):
            fails.append("missing: %s" % name)
    return fails


def assert_sim_json(text: str) -> list:
    fails = []
    try:
        arr = json.loads(text)
    except Exception as exc:  # noqa: BLE001
        return ["json parse error: %r" % (exc,)]
    if not isinstance(arr, list) or not arr:
        return ["json not a non-empty array"]
    inj = [x for x in arr if x.get("type") == "injection"]
    if len(inj) != 1 or "faults=FI-01,FI-02,FI-03,FI-04,FI-05,FI-10" not in inj[0].get("detail", ""):
        fails.append("json injection record wrong")
    kinds = {x.get("type") for x in arr}
    for k in ("stream_state", "ai", "storage", "ptz", "gap", "summary", "alarm"):
        if k not in kinds:
            fails.append("json missing kind %s" % k)
    return fails


def ext_run(client, wd, remote_dir, cpp_local_name, cpp_remote_name, exe_name, note) -> int:
    """Upload, compile against this round's libs, run; require PASS 0 failed."""
    sftp = client.open_sftp()
    sftp.get_channel().settimeout(120)
    try:
        sftp.put(str(Path(__file__).resolve().parent / cpp_local_name),
                 "%s/%s" % (wd, cpp_remote_name))
        note("upload-%s" % cpp_remote_name, {"exit": 0, "output": cpp_remote_name})
    finally:
        sftp.close()
    compile_cmd = ("cd %s && g++ -std=c++17 -O1 -Wall -Wextra "
                   "-Isrc/core/include -Isrc/platform/include "
                   "%s/%s build-core/src/core/librailhmi_core.a "
                   "build-core/src/platform/librailhmi_platform.a "
                   "-o %s/%s 2>&1; echo GXX_EXIT=$?"
                   % (remote_dir, wd, cpp_remote_name, wd, exe_name))
    cext = rrun(client, compile_cmd, 240)
    note("compile-%s" % exe_name, cext)
    if cext.get("exit") != 0 or "GXX_EXIT=0" not in cext.get("output", ""):
        return 1
    rext = rrun(client, "cd %s && %s/%s; echo EXT_EXIT=$?"
                % (remote_dir, wd, exe_name), 180)
    note("run-%s" % exe_name, rext)
    m = re.search(r"RESULT: (\w+) passed=(\d+) failed=(\d+)", rext.get("output", ""))
    if rext.get("exit") != 0 or "EXT_EXIT=0" not in rext.get("output", "") or \
            not (m and m.group(1) == "PASS" and int(m.group(3)) == 0):
        return 1
    note("summary-%s" % exe_name,
         {"exit": 0 if m and m.group(1) == "PASS" else 1,
          "output": "%s passed=%s failed=%s" % (exe_name, m.group(2), m.group(3))})
    return 0


# Verifier packaging/deployment scan — same body as verify_packaging25_scan.py,
# executed on the fresh remote copy (layout: packaging/ + src/).

# On-board negative control: delivered checker on fresh copy (PASS) vs a
# synthetic mutated copy (FAIL) — proves the delivered assertions bite.
NEG_PY = r'''
import os, shutil, subprocess, sys
r0 = subprocess.run([sys.executable, "tools/check_packaging.py", "--root", ".",
                     "--app-src", "src", "--packaging", "packaging"],
                    capture_output=True, text=True)
neg = "/tmp/railhmi_verify025/negpkg"
if os.path.exists(neg):
    shutil.rmtree(neg)
os.makedirs(neg)
for f in ("railhmi.service", "install.sh", "VERSION", "UPGRADE.md", "RESOURCES.md"):
    shutil.copy(os.path.join("packaging", f), os.path.join(neg, f))
# mutate the unit: drop Restart=always, reference cpu-performance (forbidden)
u = os.path.join(neg, "railhmi.service")
s = open(u, encoding="utf-8").read()
s = s.replace("Restart=always", "Restart=no")
s += "\n: touch cpu-performance.service\n"
open(u, "w", encoding="utf-8").write(s)
# mutate install.sh: drop the logs dir creation
i = os.path.join(neg, "install.sh")
s = open(i, encoding="utf-8").read().replace("data logs", "data")
open(i, "w", encoding="utf-8").write(s)
r1 = subprocess.run([sys.executable, "tools/check_packaging.py", "--root", ".",
                     "--app-src", "src", "--packaging", neg],
                    capture_output=True, text=True)
print("NEG_HEALTHY_EXIT=%d" % r0.returncode)
print("NEG_MUTATED_EXIT=%d" % r1.returncode)
for line in (r1.stdout + r1.stderr).splitlines():
    if line.startswith("[FAIL]"):
        print("neg-fail:", line)
sys.exit(0 if (r0.returncode == 0 and r1.returncode != 0) else 1)
'''


def main() -> int:
    ap = argparse.ArgumentParser(description="ITEM-025 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify025")
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--impl-testrun-log", required=True,
                    help="implementer r1 core-tests log for line comparison")
    ap.add_argument("--impl-faultprobe-log", required=True,
                    help="implementer r1 fault-probe log for byte identity")
    ap.add_argument("--impl-sim-log", required=True,
                    help="implementer r1 sim-inject log for byte identity")
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--timeout-sec", type=int, default=1500)
    ap.add_argument("--app-root", default=str(Path(__file__).resolve().parents[4] / "app"))
    args = ap.parse_args()

    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("credential env %s not loaded; refusing to guess\n"
                         % args.credential_env)
        return 2

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    tag = "VERIFY-FIXTURES-" + args.stamp
    wd = args.workdir
    remote_dir = "/tmp/railhmi_build"
    transcript: dict = {"invocation": {k: str(v) for k, v in vars(args).items()},
                        "host_key_fingerprint": None,
                        "notes": ["ITEM-025 software_verification: abort on fingerprint "
                                  "mismatch; no credentials logged"],
                        "steps": []}
    ok = True

    def note(step, res):
        transcript["steps"].append({"step": step, **res})

    def save():
        (outdir / (tag + ".transcript.json")).write_text(
            json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
        with (outdir / (tag + ".log")).open("w", encoding="utf-8") as fh:
            fh.write("host_key_fingerprint: %s\n" % transcript["host_key_fingerprint"])
            for s in transcript["steps"]:
                fh.write("\n===== [%s] exit=%s =====\n" % (s.get("step"), s.get("exit")))
                fh.write((s.get("output") or "") + "\n")

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    client.set_missing_host_key_policy(paramiko.RejectPolicy())
    try:
        client.connect(hostname=args.host, port=args.port, username=args.user,
                       password=secret, timeout=args.connect_timeout,
                       auth_timeout=args.connect_timeout,
                       banner_timeout=args.connect_timeout,
                       look_for_keys=False, allow_agent=False)
    except Exception as exc:  # noqa: BLE001
        note("connect", {"exit": None, "output": "connect error: %r" % (exc,)})
        save()
        sys.stderr.write("connect failed (not asserting offline): %r\n" % (exc,))
        return 2

    try:
        tr = client.get_transport()
        fp = fingerprint(tr.get_remote_server_key()) if tr is not None else "(transport None)"
        transcript["host_key_fingerprint"] = fp
        note("fingerprint", {"exit": 0 if fp == args.expected_fingerprint else 1,
                             "output": "observed=%s expected=%s"
                                       % (fp, args.expected_fingerprint)})
        if fp != args.expected_fingerprint:
            save()
            sys.stderr.write("host fingerprint mismatch; aborting\n")
            return 2

        # 1. deliverable TestRun entry with verifier stamp (keep remote for ext)
        stamp = args.stamp
        cmd = [sys.executable, str(Path(args.app_root) / "tools" / "board_build.py"),
               "--host", args.host, "--user", args.user,
               "--credential-env", args.credential_env,
               "--mode", "TestRun",
               "--outdir", str(outdir),
               "--stamp", stamp,
               "--keep-remote"]
        started = time.time()
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True,
                                  encoding="utf-8", errors="replace",
                                  timeout=args.timeout_sec)
            entry_exit = proc.returncode
            entry_out = (proc.stdout or "") + (proc.stderr or "")
        except subprocess.TimeoutExpired:
            entry_exit = None
            entry_out = "(timeout after %ss)" % args.timeout_sec
        note("run-testrun-entry", {"cmd": " ".join(cmd), "exit": entry_exit,
                                   "elapsed_s": round(time.time() - started, 2),
                                   "output": entry_out})
        if entry_exit != 0:
            ok = False

        # 1b. fresh artifact paths for this round
        host_u = args.host.replace(".", "_")
        tl = outdir / ("build-TestRun-%s-%s.core-evidence_core-tests-%s.log"
                       % (host_u, stamp, stamp))
        tx = outdir / ("build-TestRun-%s-%s.core-evidence_core-tests-%s.xml"
                       % (host_u, stamp, stamp))
        pl = outdir / ("build-TestRun-%s-%s.core-evidence_fault-probe-%s.log"
                       % (host_u, stamp, stamp))
        sl = outdir / ("build-TestRun-%s-%s.core-evidence_sim-inject-%s.log"
                       % (host_u, stamp, stamp))
        sj = outdir / ("build-TestRun-%s-%s.core-evidence_sim-inject-%s.json"
                       % (host_u, stamp, stamp))
        fresh_missing = [p.name for p in (tl, tx, pl, sl, sj) if not p.is_file()]
        if fresh_missing:
            note("fresh-artifacts", {"exit": None,
                 "output": "missing fresh artifacts: %s" % ", ".join(fresh_missing)})
            ok = False
        else:
            note("fresh-artifacts", {"exit": 0,
                 "output": " ".join(p.name for p in (tl, tx, pl, sl, sj))})

        # 2. line-by-line [PASS] comparison + independent JUnit parse
        expected_pass = 663
        if tl.is_file():
            impl = parse_core_tests_log(Path(args.impl_testrun_log).read_text(
                encoding="utf-8", errors="replace"))
            ver = parse_core_tests_log(tl.read_text(encoding="utf-8", errors="replace"))
            same = impl["pass"] == ver["pass"]
            note("log-compare", {"exit": 0 if same and not ver["fail"] and
                                 len(ver["pass"]) == expected_pass else 1,
                 "output": "impl_pass=%d verify_pass=%d identical=%s "
                           "verify_fails=%d expect=%d result=%s"
                           % (len(impl["pass"]), len(ver["pass"]), same,
                              len(ver["fail"]), expected_pass, ver["result"])})
            if not same or ver["fail"] or len(ver["pass"]) != expected_pass:
                ok = False
        else:
            note("log-compare", {"exit": None, "output": "no fresh core-tests log"})
            ok = False
        if tx.is_file():
            xtext = tx.read_text(encoding="utf-8", errors="replace")
            m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', xtext, re.S)
            n_tests, n_fail = (m.group(1), m.group(2)) if m else (None, None)
            names_all = re.findall(r'name="([^"]+)"', xtext)
            names_all = [n2.replace("&gt;", ">").replace("&lt;", "<").replace("&amp;", "&")
                         .replace("&apos;", "'").replace("&quot;", '"')
                         for n2 in names_all]
            found = [c for c in ITEM025_CASES if c in names_all]
            missing = [c for c in ITEM025_CASES if c not in names_all]
            junit_ok = (n_tests == "663" and n_fail == "0"
                        and len(found) == len(ITEM025_CASES))
            note("junit-parse", {"exit": 0 if junit_ok else 1,
                 "output": "junit tests=%s failures=%s item025_cases=%d/%d missing=%s"
                           % (n_tests, n_fail, len(found), len(ITEM025_CASES), missing)})
            if not junit_ok:
                ok = False
        else:
            note("junit-parse", {"exit": None, "output": "no fresh junit xml"})
            ok = False

        # 3. independent fault-probe assertions + byte identity
        if pl.is_file():
            fresh_probe = pl.read_text(encoding="utf-8", errors="replace")
            fails = assert_fault_probe(fresh_probe)
            note("fault-probe-assert", {"exit": 0 if not fails else 1,
                 "output": json.dumps({"fails": fails}, ensure_ascii=False)})
            if fails:
                ok = False
            impl_probe = Path(args.impl_faultprobe_log).read_text(
                encoding="utf-8", errors="replace")
            identical = impl_probe.splitlines() == fresh_probe.splitlines()
            note("fault-probe-compare", {"exit": 0 if identical else 1,
                 "output": "cross-round byte identity vs implementer r1: identical=%s"
                           % identical})
            if not identical:
                ok = False
        else:
            note("fault-probe-assert", {"exit": None, "output": "missing fresh fault-probe"})
            ok = False

        # 4. independent sim-inject assertions + byte identity
        if sl.is_file():
            fresh_sim = sl.read_text(encoding="utf-8", errors="replace")
            fails = assert_sim_log(fresh_sim)
            note("sim-inject-assert", {"exit": 0 if not fails else 1,
                 "output": json.dumps({"fails": fails}, ensure_ascii=False)})
            if fails:
                ok = False
            impl_sim = Path(args.impl_sim_log).read_text(encoding="utf-8", errors="replace")
            identical = impl_sim.splitlines() == fresh_sim.splitlines()
            note("sim-inject-compare", {"exit": 0 if identical else 1,
                 "output": "cross-round byte identity vs implementer r1: identical=%s"
                           % identical})
            if not identical:
                ok = False
        if sj.is_file():
            fails = assert_sim_json(sj.read_text(encoding="utf-8", errors="replace"))
            note("sim-json-assert", {"exit": 0 if not fails else 1,
                 "output": json.dumps({"fails": fails}, ensure_ascii=False)})
            if fails:
                ok = False

        # 5. verifier ext fixture against THIS round's static libs
        rrun(client, "mkdir -p %s" % wd, 30)
        note("mkdir-workdir", {"exit": 0, "output": wd})
        if ext_run(client, wd, remote_dir, "verify_packaging25_ext.cpp",
                   "verify_packaging25_ext.cpp", "verify_packaging25_ext", note) != 0:
            ok = False

        # 6. verifier packaging/deployment-contract scan on fresh copy
        scan_body = Path(__file__).with_name("verify_packaging25_scan.py").read_text(
            encoding="utf-8")
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(60)
        try:
            sftp.putfo(io.BytesIO(scan_body.encode("utf-8")), "%s/verify_scan25.py" % wd)
            sftp.putfo(io.BytesIO(NEG_PY.encode("utf-8")), "%s/verify_neg25.py" % wd)
            note("upload-scan-neg", {"exit": 0, "output": "verify_scan25.py verify_neg25.py"})
        finally:
            sftp.close()
        scan = rrun(client, "cd %s && python3 %s/verify_scan25.py --app src; echo SCAN25_EXIT=$?"
                    % (remote_dir, wd), 60)
        note("packaging-scan", scan)
        so = scan.get("output", "")

        def svar(name):
            m3 = re.search(r"^%s=(\S+)" % re.escape(name), so, re.M)
            return m3.group(1) if m3 else None

        POSITIVE = ["P1_unit_restart_always", "P2_unit_display", "P3_no_cpu_performance",
                    "P4_install_runtime_dirs", "P5_install_enables_unit",
                    "P6_version_upgrade_resources", "P7_fault_catalog_six_kinds",
                    "P8_p6_selfcheck_card", "P9_main_injects_faults",
                    "P13_unit_named_railhmi", "P14_execstart_binary_installed",
                    "P15_sim_inject_cli_built", "P16_fault_probe_built"]
        DEFECTS = ["D01_execstartpre_provenance", "D01b_upgrade_doc_bin_tools",
                   "D02_config_arg_consumed"]
        pos_bad = [k for k in POSITIVE if svar(k) != "True"]
        defect_observed = {k: svar(k) for k in DEFECTS}
        # D-01/D-02 are the review findings: expected False on this tree.
        d01 = svar("D01_execstartpre_provenance")
        d01b = svar("D01b_upgrade_doc_bin_tools")
        d02 = svar("D02_config_arg_consumed")
        findings_ok = (not pos_bad and d01 == "False" and d01b == "False"
                       and d02 == "False")
        note("packaging-scan-findings", {"exit": 0 if findings_ok else 1,
             "output": "positive_failing=%s d01=%s d01b=%s d02=%s "
                       "(D-01/D-02 expected False: ExecStartPre railhmi-version-check "
                       "has no build/install source; --config not consumed, "
                       "config dir compile-time source-tree path)"
                       % (pos_bad, d01, d01b, d02)})
        if not findings_ok:
            ok = False

        # 7. on-board negative control (delivered checker fresh PASS / mutated FAIL)
        neg = rrun(client, "cd %s && python3 %s/verify_neg25.py; echo NEG_DRIVER_EXIT=$?"
                   % (remote_dir, wd), 120)
        note("negative-control", neg)
        no = neg.get("output", "")
        neg_ok = (neg.get("exit") == 0 and "NEG_DRIVER_EXIT=0" in no
                  and "NEG_HEALTHY_EXIT=0" in no and "NEG_MUTATED_EXIT=1" in no)
        note("negative-control-findings", {"exit": 0 if neg_ok else 1,
             "output": "neg_ok=%s" % neg_ok})
        if not neg_ok:
            ok = False

        # 8. determinism: re-run fault-probe and diff
        n2 = rrun(client, "cd %s && mkdir -p verify && ./build-core/tools/railhmi-fault-probe "
                          "--out verify/fault-probe-rep.log; echo FAULTREP_EXIT=$?"
                  % remote_dir, 60)
        note("fault-probe-rep-run", n2)
        if "FAULTREP_EXIT=0" not in n2.get("output", ""):
            ok = False
        n2b = rrun(client, "cd %s && diff verify/fault-probe-rep.log "
                           "core-evidence/fault-probe-%s.log && echo IDENTICAL"
                   % (remote_dir, stamp), 30)
        note("fault-probe-determinism", n2b)
        if "IDENTICAL" not in n2b.get("output", ""):
            ok = False

        # 9. regression artifacts from the same TestRun
        reg = rrun(client, "cd %s && ls core-evidence/" % remote_dir, 30)
        note("regression-artifacts", reg)
        reg_ok = (reg.get("exit") == 0
                  and "sim-inject" in reg.get("output", "")
                  and "status-probe" in reg.get("output", "")
                  and "ai-probe" in reg.get("output", "")
                  and "alarm-probe" in reg.get("output", "")
                  and "clip-probe" in reg.get("output", "")
                  and "replay-probe" in reg.get("output", "")
                  and "maintenance-probe" in reg.get("output", "")
                  and "permissions-probe" in reg.get("output", "")
                  and "health-probe" in reg.get("output", "")
                  and "fault-probe" in reg.get("output", ""))
        if not reg_ok:
            ok = False

        # 10. cleanup by name; trailing ls of removed dirs is the confirmation
        note("cleanup", rrun(client,
             "rm -rf %s /tmp/railhmi_build && echo fixture_cleanup_done && "
             "echo LEFT_LIST && ls -d %s /tmp/railhmi_build 2>&1" % (wd, wd), 30))
        cleanup_res = transcript["steps"][-1]
        cleanup_ok = ("fixture_cleanup_done" in cleanup_res.get("output", "")
                      and cleanup_res.get("output", "").count("cannot access") >= 1)
        note("cleanup-verify", {"exit": 0 if cleanup_ok else 1,
                                "output": "cleanup_ok=%s" % cleanup_ok})
        if not cleanup_ok:
            ok = False

        findings = [
            {"independent_recheck_r1": ("ITEM-025 deliverables re-verified: deliverable "
                                        "TestRun entry re-run ok, core-tests 663 "
                                        "line-identical to implementer r1 + JUnit 663/0 "
                                        "(%d fault/injection cases), fault-probe oracle "
                                        "16 lines + byte identity + determinism, "
                                        "sim-inject oracle + json + byte identity, "
                                        "ext fixture green on-board" % len(ITEM025_CASES)),
             "confirmed": ok},
            {"fault_catalog_contract": ("6 FI entries with non-empty explicit prompts and "
                                        "recovery paths; SC-01..10 inject commands parse via "
                                        "parse_fault; SC-BOOT restarts the enabled unit "
                                        "name railhmi and cites Restart=always"),
             "present": True},
            {"fi_model_sequences": ("FI-01 reconnect->offline escalation->live, FI-02 frozen, "
                                    "FI-03 AI unavailable + 0 alarms, FI-04 storage low, "
                                    "FI-05 ptz tolerance fail, FI-10 gaps recorded+merged; "
                                    "window [onset,recovery) half-open; recovery<=onset "
                                    "never recovers; determinism byte-identical"),
             "present": True},
            {"d01_execstartpre_no_provenance":
                 ("D-01: packaging/railhmi.service ExecStartPre="
                  "/opt/railhmi/bin/railhmi-version-check references a binary that is "
                  "neither built by any CMake target nor installed by install.sh; "
                  "ExecStartPre failure fails the start, and with Restart=always that is "
                  "a boot crash loop — '重启后应用随系统自动拉起并进入可用状态' would not hold "
                  "on a real ENV-E deployment; UPGRADE.md also documents the tool"),
             "present": d01 == "False" and d01b == "False"},
            {"d02_config_dir_not_deployment_contract":
                 ("D-02: unit passes --config /opt/railhmi/config but main.cpp never "
                  "parses it; config dir is compile-time "
                  "RAILHMI_CONFIG_DIR=CMAKE_SOURCE_DIR/config, so the deployed binary "
                  "resolves config relative to the build-time source tree "
                  "(e.g. /tmp/railhmi_build) — /opt/railhmi/config is not honored; "
                  "config load fails -> exit 4 -> Restart=always crash loop"),
             "present": d02 == "False"},
            {"delivered_checker_scope": ("check_packaging.py is token-based; it does not "
                                         "verify ExecStartPre target existence or --config "
                                         "consumption, which is why PACK_EXIT=0 coexists "
                                         "with D-01/D-02; negative control proves the "
                                         "checker still bites on token-level mutations"),
             "present": neg_ok},
        ]
        note("findings", {"exit": 0, "output": json.dumps(findings, ensure_ascii=False)})
    finally:
        client.close()
        save()

    print("host_key_fingerprint:", transcript["host_key_fingerprint"])
    for s in transcript["steps"]:
        tail = (s.get("output") or "").strip().splitlines()
        print("[%s] exit=%s %s" % (s.get("step"), s.get("exit"), tail[-1] if tail else ""))
    if not ok:
        print("NOTE: one or more steps returned nonzero or failed assertions; see transcript")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
