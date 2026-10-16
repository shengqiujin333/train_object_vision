# -*- coding: utf-8 -*-
"""ITEM-024 re-verification round (uifix-r1) — board-side fixtures (verifier-owned).

Subject: implementer round stamp 20261016-ITEM024-uifix-r1 (commit fd9dde4),
the UI-review D-01..D-04 repair verified by web_frontend.ui_implementation_review
TEST_FAIL -> fix -> this independent software_verification re-check.

Steps (all finite timeouts):
  1. Fingerprint-checked SSH connect (Paramiko, no interactive prompts).
  2. Re-run the deliverable board_build.py --mode TestRun entry with verifier
     stamp VERIFY-20261017-v1 + --keep-remote; archive fresh logs + JUnit XML.
  3. Line-by-line [PASS] comparison: fresh core-tests log vs implementer
     uifix-r1 archived log (663 cases, exact list equality) + independent JUnit
     parse (663/0) with the 22 exact ITEM-024 case names.
  4. Independent health-probe oracle assertions on the FRESH probe log +
     cross-round byte identity vs the archived uifix-r1 probe (core untouched).
  5. Verifier ext fixtures linked against THIS round's build-core static libs:
     - ext v2 (verify_health24_ext.cpp, 103 checks, unchanged regression base)
     - ext v3 (verify_health24_ext_v3.cpp, demo recovery path oracle for the
       D-02/D-04 bridge state machine: reset -> 9 ticks -> completion ->
       idempotence, critical-notice state, status tokens)
  6. Verifier wiring scan v3 (20 keys incl. D-01 Flickable/reverse-anchored-grid,
     D-03 source/keyword inputs, D-02 reactive bindings + reverse one-shot,
     D-04 demo path semantics) on the fresh remote copy.
  7. On-board negative control: delivered check_health_wiring.py on the fresh
     copy must PASS; on a synthetic pre-fix mutated copy must FAIL (proves the
     checker catches the review findings).
  8. Determinism: re-run the health probe, require byte-identical output.
  9. Regression artifacts listing from the same TestRun.
 10. Cleanup verifier workdir and /tmp/railhmi_build by name.
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


ORACLE = [
    r'^type=health level=ok text="NPU 42\.0% / CPU 33\.0% / 温度 58\.0°C / 网络错误 3 / 正常" alerts=0$',
    r'^type=health level=warn alerts=1 first="温度 80\.0°C > 75\.0°C"$',
    r'^type=log-json \{"ts_ms":1792108801000,"level":"error","source":"stream",'
    r'"message":"CAM-04 断流","fields":\{"cam":"CAM-04"\}\}$',
    r'^type=log-query by_source=1 by_keyword=1 by_severity=2 day_rolled=2$',
    r'^type=recovery step=load_config label=加载配置 status=完成 progress=12$',
    r'^type=recovery step=open_database label=打开数据库 status=完成 progress=25$',
    r'^type=recovery step=start_recording label=启动录像/存储 status=完成 progress=37$',
    r'^type=recovery step=start_streaming label=启动相机取流 status=降级 progress=50$',
    r'^type=recovery step=start_ai label=启动 AI status=完成 progress=62$',
    r'^type=recovery step=ptz_home label=云台归位 status=完成 progress=75$',
    r'^type=recovery step=front_preset label=调前方预置位 status=完成 progress=87$',
    r'^type=recovery step=front_ai_ready label=前方 AI READY status=完成 progress=100$',
    r'^type=recovery-summary completed=true alive=true degraded_cameras=1 '
    r'progress=100 text="恢复完成（部分降级）"$',
    r'^type=recovery-critical config_failed_alive=false progress=25$',
    r'^type=acceptance health_ok_warn=true log_query_and_rolling=true '
    r'recovery_completed=true noncritical_isolated=true '
    r'critical_stops_alive=true result=PASS$',
]

ITEM024_CASES = [
    "healthy -> ok, no alerts", "health text", "temp+net warn",
    "very high temp -> error", "low disk -> warn", "log json with fields",
    "query by source", "query by min severity", "query by keyword",
    "query by time window", "log day key", "day-rolling keeps latest day",
    "recovery has 8 steps", "all ok -> completed", "step label",
    "single non-critical camera failure does not exit app",
    "degraded camera counted", "streaming step degraded, rest continue",
    "recovery still completes", "critical step failure -> app not alive",
    "config step failed recorded", "progress reflects terminal steps",
]


def assert_health_probe(text: str) -> list:
    fails: list = []
    for rx in ORACLE:
        if not re.search(rx, text, re.M):
            fails.append("missing line: %s" % rx[:70])
    if re.search(r'^type=acceptance\b.*result=FAIL', text, re.M):
        fails.append("acceptance verdict FAIL")
    got = [int(x) for x in re.findall(r'^type=recovery step=\S+ .*?progress=(\d+)$', text, re.M)]
    want = [i * 100 // 8 for i in range(1, 9)]
    if got != want:
        fails.append("progress math got=%s want=%s" % (got, want))
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


# On-board negative control: delivered checker on fresh copy (PASS) vs a
# synthetic pre-fix mutated copy (FAIL) — proves the D-01..D-04 assertions bite.
NEG_PY = r'''
import shutil, subprocess, sys, os
root_fresh = "src/ui"
root_neg = "/tmp/railhmi_verify024/negtree"
if os.path.exists(root_neg):
    shutil.rmtree(root_neg)
os.makedirs(root_neg + "/qml/components", exist_ok=True)
os.makedirs(root_neg + "/qml/pages", exist_ok=True)
os.makedirs(root_neg + "/src", exist_ok=True)
for rel in ("qml/main.qml", "qml/components/RecoveryOverlay.qml",
            "qml/pages/P6Maintenance.qml", "src/health_model.cpp", "src/main.cpp"):
    shutil.copy(os.path.join(root_fresh, rel), os.path.join(root_neg, rel))
p = os.path.join(root_neg, "qml/pages/P6Maintenance.qml")
s = open(p, encoding="utf-8").read()
s = s.replace("Flickable {\n        id: flick\n        anchors.fill: parent\n        clip: true",
              "Item {\n        id: flick\n        anchors.fill: parent", 1)
for old, new in (
    ("logSourceInput", "goneSrcInput"),
    ("logKeywordInput", "goneKwInput"),
    ("recoveryDemo", "goneRecoveryDemo"),
    ("property var healthSt", "property var goneSt"),
    ("property var logRows", "property var goneRows"),
    ("function reloadHealth", "function goneReloadHealth"),
    ("function reloadLogs", "function goneReloadLogs"),
    ("page.reloadHealth(); page.reloadLogs()", ""),
):
    s = s.replace(old, new)
open(p, "w", encoding="utf-8").write(s)
r0 = subprocess.run([sys.executable, "tools/check_health_wiring.py", "--root", root_fresh],
                    capture_output=True, text=True)
r1 = subprocess.run([sys.executable, "tools/check_health_wiring.py", "--root", root_neg],
                    capture_output=True, text=True)
print("NEG_HEALTHY_EXIT=%d" % r0.returncode)
print("NEG_MUTATED_EXIT=%d" % r1.returncode)
for line in r1.stdout.splitlines():
    if line.startswith("[FAIL]"):
        print("neg-fail:", line)
sys.exit(0 if (r0.returncode == 0 and r1.returncode != 0) else 1)
'''

# Verifier wiring scan v3 — same body offline_selfcheck1_v3 validated locally.
SCAN_PY = r'''
import re
overlay = open('src/ui/qml/components/RecoveryOverlay.qml', encoding='utf-8').read()
main_qml = open('src/ui/qml/main.qml', encoding='utf-8').read()
p6 = open('src/ui/qml/pages/P6Maintenance.qml', encoding='utf-8').read()
bridge = open('src/ui/src/health_model.cpp', encoding='utf-8').read()
bridge_h = open('src/ui/src/health_model.h', encoding='utf-8').read()
mc = open('src/ui/src/main.cpp', encoding='utf-8').read()
bb = open('tools/board_build.py', encoding='utf-8').read()

print('T1_overlay_structure=',
      'recoveryOverlay' in overlay and 'recoveryStep-' in overlay and
      'recoveryText' in overlay and 'recoveryAlive' in overlay and
      'modelData.statusLabel' in overlay and 'modelData.label' in overlay and
      'progress' in overlay)
print('T2_overlay_wired=',
      'RecoveryOverlay' in main_qml and 'railhmiHealth' in overlay and
      'R.recovery()' in overlay)
print('T3_p6_log_card=',
      'maintLogQueryCard' in p6 and 'structuredLogRow' in p6 and
      'logFilterAll' in p6 and 'logFilterWarn' in p6 and 'logFilterError' in p6 and
      'logSourceInput' in p6 and 'logKeywordInput' in p6 and
      re.search(r'logSourceInput[^>]*\n[^>]*onEditingFinished', p6) is not None and
      re.search(r'logKeywordInput[^>]*\n[^>]*onEditingFinished', p6) is not None and
      re.search(r'railhmiHealth\.logs\(', p6) is not None and
      'modelData.levelLabel' in p6 and 'modelData.source' in p6 and
      'modelData.message' in p6)
print('T4_p6_recovery_card=',
      'maintRecoveryCard' in p6 and 'recoveryProgressText' in p6 and
      'recoveryStepRow-' in p6 and 'recoveryDemo' in p6 and
      re.search(r'resetRecovery\(\)', p6) is not None and
      re.search(r'railhmiHealth\.recovery\(\)', p6) is not None and
      'modelData.statusLabel' in p6 and 'modelData.detail' in p6)
print('T5_bridge_core=',
      'evaluate_health' in bridge and 'query_logs' in bridge and
      'log_record_json' in bridge and 'recovery_' in bridge and
      'railhmi::core::' in bridge)
print('T6_main_inject=',
      'railhmiHealth' in mc and 'RailhmiHealthModel' in mc and
      'healthModel.seed(' in mc and
      'RecoveryStep::FrontAiReady' in mc and
      'RecoveryStep::StartStreaming' in mc)
print('T7_board=',
      'check_health_wiring.py' in bb and 'railhmi-health-probe' in bb and
      'HEALTH_WIRE_EXIT' in bb and 'HEALTH_EXIT' in bb)
print('T8_bridge_filter=',
      re.search(r'level == "warn"', bridge) is not None and
      re.search(r'level == "error"', bridge) is not None and
      'min_severity' in bridge and 'f.source' in bridge and 'f.keyword' in bridge)
print('T9_overlay_gating=',
      re.search(r'visible:\s*st\.completed === false', overlay) is not None and
      re.search(r'visible:\s*overlay\.st\.alive === false', overlay) is not None)
print('T11_overlay_reactive=',
      'function reload()' in overlay and
      re.search(r'overlay\.st\s*=\s*overlay\.R \? overlay\.R\.recovery\(\)', overlay) is not None and
      'Connections' in overlay and 'function onChanged()' in overlay and
      re.search(r'property var st:\s*\(\{', overlay) is not None and
      re.search(r'property var st:\s*R \?', overlay) is None)
print('T12_p6_reactive=',
      'property var healthSt' in p6 and 'property var logRows' in p6 and
      'function reloadHealth()' in p6 and 'function reloadLogs()' in p6 and
      re.search(r'page\.reloadHealth\(\);\s*page\.reloadLogs\(\)', p6) is not None and
      'page.reloadHealth' in p6 and 'page.reloadLogs' in p6)
print('T13_demo_path=',
      'resetRecovery' in bridge_h and 'advanceRecovery' in bridge_h and
      'Q_INVOKABLE void resetRecovery' in bridge_h and
      'Q_INVOKABLE void advanceRecovery' in bridge_h and
      re.search(r'Timer\s*\{[^}]*onTriggered:\s*overlay\.R\.advanceRecovery\(\)', overlay, re.S) is not None and
      re.search(r'running:\s*overlay\.visible && overlay\.R', overlay) is not None and
      re.search(r'resetRecovery\(\)', p6) is not None)
print('T14_demo_semantics=',
      re.search(r'if \(s == railhmi::core::RecoveryStep::StartStreaming\)\s*\n\s*demo_tracker_\.camera_result\(false, false,', bridge, re.M) is not None and
      re.search(r'complete_step\(s, true\)', bridge) is not None and
      re.search(r'if \(all_terminal && !now\.completed\) demo_tracker_\.finish\(demo_clock_\);', bridge) is not None)
print('N2_degraded_not_failed=',
      'degraded' in overlay and 'failed' in overlay and
      'degraded' in p6 and 'failed' in p6 and
      'Theme.color.degraded' in overlay and 'Theme.color.degraded' in p6)
overlay_l = [line.split('//', 1)[0] for line in overlay.splitlines()]
p6l = [line.split('//', 1)[0] for line in p6.splitlines()]
print('T10_no_hex=',
      not any(re.search(r'#[0-9a-fA-F]{3,8}\b', l) for l in overlay_l + p6l) and
      'Theme.color.' in overlay and 'Theme.color.' in p6)
print('R1_p6_scrollable=',
      re.search(r'Flickable\s*\{[^{}]*anchors\.fill:\s*parent', p6, re.S) is not None and
      'clip: true' in p6 and
      re.search(r'contentHeight:\s*grid\.height', p6) is not None and
      re.search(r'contentWidth:\s*width', p6) is not None)
print('R2_p6_no_anchored_grid=',
      re.search(r'Grid\s*\{[^{}]*anchors\.fill:\s*parent', p6, re.S) is None)
print('R3_no_one_shot_bindings=',
      re.search(r'railhmiHealth\.recovery\(\)\.text', p6) is None and
      re.search(r'railhmiHealth\.recovery\(\)\.steps', p6) is None and
      re.search(r'property var st:\s*R \?', overlay) is None)
print('R4_demo_reachable=',
      'rec.finish(' in mc and 'resetRecovery' in bridge_h)
print('R5_theme_tokens_present=',
      'touch_target_min' in p6 and 'bg_sunken' in p6 and 'radius.chip' in p6)
'''

SCAN_KEYS = [
    "T1_overlay_structure", "T2_overlay_wired", "T3_p6_log_card",
    "T4_p6_recovery_card", "T5_bridge_core", "T6_main_inject", "T7_board",
    "T8_bridge_filter", "T9_overlay_gating", "T11_overlay_reactive",
    "T12_p6_reactive", "T13_demo_path", "T14_demo_semantics",
    "N2_degraded_not_failed", "T10_no_hex", "R1_p6_scrollable",
    "R2_p6_no_anchored_grid", "R3_no_one_shot_bindings", "R4_demo_reachable",
    "R5_theme_tokens_present",
]


def main() -> int:
    ap = argparse.ArgumentParser(description="ITEM-024 re-verification fixtures (uifix-r1)")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify024")
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--impl-testrun-log", required=True,
                    help="implementer uifix-r1 core-tests log for line comparison")
    ap.add_argument("--impl-healthprobe-log", required=True,
                    help="implementer uifix-r1 health-probe log for byte identity")
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
                        "notes": ["ITEM-024 re-verification (uifix-r1 D-01..D-04): "
                                  "abort on fingerprint mismatch; no credentials logged"],
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
        pl = outdir / ("build-TestRun-%s-%s.core-evidence_health-probe-%s.log"
                       % (host_u, stamp, stamp))
        if not (tl.is_file() and tx.is_file() and pl.is_file()):
            missing = [p.name for p in (tl, tx, pl) if not p.is_file()]
            note("fresh-artifacts", {"exit": None,
                 "output": "missing fresh artifacts: %s" % ", ".join(missing)})
            ok = False
        else:
            note("fresh-artifacts", {"exit": 0,
                 "output": tl.name + " " + tx.name + " " + pl.name})

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
        if tx.is_file():
            xtext = tx.read_text(encoding="utf-8", errors="replace")
            m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', xtext, re.S)
            n_tests, n_fail = (m.group(1), m.group(2)) if m else (None, None)
            names_all = re.findall(r'name="([^"]+)"', xtext)
            names_all = [n2.replace("&gt;", ">").replace("&lt;", "<").replace("&amp;", "&")
                         for n2 in names_all]
            found = [c for c in ITEM024_CASES if c in names_all]
            missing = [c for c in ITEM024_CASES if c not in names_all]
            junit_ok = (n_tests == "663" and n_fail == "0"
                        and len(found) == len(ITEM024_CASES))
            note("junit-parse", {"exit": 0 if junit_ok else 1,
                 "output": "junit tests=%s failures=%s item024_cases=%d/%d missing=%s"
                           % (n_tests, n_fail, len(found), len(ITEM024_CASES), missing)})
            if not junit_ok:
                ok = False
        else:
            note("junit-parse", {"exit": None, "output": "no fresh junit xml"})
            ok = False

        # 3. independent health-probe assertions (oracle) + byte identity
        if pl.is_file():
            fresh_text = pl.read_text(encoding="utf-8", errors="replace")
            fails = assert_health_probe(fresh_text)
            note("health-probe-assert", {"exit": 0 if not fails else 1,
                 "output": json.dumps({"fails": fails}, ensure_ascii=False)})
            if fails:
                ok = False
            impl_probe_text = Path(args.impl_healthprobe_log).read_text(
                encoding="utf-8", errors="replace")
            identical = impl_probe_text.splitlines() == fresh_text.splitlines()
            note("health-probe-compare", {"exit": 0 if identical else 1,
                 "output": "cross-round deterministic reproduction identical=%s" % identical})
            if not identical:
                ok = False
        else:
            note("health-probe-assert", {"exit": None, "output": "missing fresh health-probe"})
            ok = False

        # 4. verifier ext fixtures against THIS round's static libs
        rrun(client, "mkdir -p %s" % wd, 30)
        note("mkdir-workdir", {"exit": 0, "output": wd})
        if ext_run(client, wd, remote_dir, "verify_health24_ext.cpp",
                   "verify_health24_ext.cpp", "verify_health24_ext", note) != 0:
            ok = False
        if ext_run(client, wd, remote_dir, "verify_health24_ext_v3.cpp",
                   "verify_health24_ext_v3.cpp", "verify_health24_ext_v3", note) != 0:
            ok = False

        # 5. verifier wiring scan v3 on the fresh remote copy
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(60)
        try:
            sftp.putfo(io.BytesIO(SCAN_PY.encode("utf-8")), "%s/verify_scan24_v3.py" % wd)
            sftp.putfo(io.BytesIO(NEG_PY.encode("utf-8")), "%s/verify_neg24.py" % wd)
            note("upload-scan-neg", {"exit": 0, "output": "verify_scan24_v3.py verify_neg24.py"})
        finally:
            sftp.close()
        scan = rrun(client, "cd %s && python3 %s/verify_scan24_v3.py; echo SCAN24_EXIT=$?"
                    % (remote_dir, wd), 60)
        note("wiring-scan", scan)
        so = scan.get("output", "")

        def svar(name):
            m3 = re.search(r"%s\s*=\s*(\S+)" % re.escape(name), so)
            return m3.group(1) if m3 else None

        wants = {k: "True" for k in SCAN_KEYS}
        observed = {k: svar(k) for k in wants}
        mismatch = sorted(k for k, v in wants.items() if observed[k] != v)
        scan_exit_ok = scan.get("exit") == 0 and "SCAN24_EXIT=0" in so
        wiring_ok = not mismatch and scan_exit_ok
        note("wiring-scan-findings", {"exit": 0 if wiring_ok else 1,
             "output": "mismatch=%s scan_exit_ok=%s raw=%s"
                       % (mismatch, scan_exit_ok, json.dumps(observed, ensure_ascii=False))})
        if not wiring_ok:
            ok = False

        # 6. on-board negative control (checker fresh PASS / mutated FAIL)
        neg = rrun(client, "cd %s && python3 %s/verify_neg24.py; echo NEG_DRIVER_EXIT=$?"
                   % (remote_dir, wd), 120)
        note("negative-control", neg)
        no = neg.get("output", "")
        neg_ok = (neg.get("exit") == 0 and "NEG_DRIVER_EXIT=0" in no
                  and "NEG_HEALTHY_EXIT=0" in no and "NEG_MUTATED_EXIT=1" in no)
        note("negative-control-findings", {"exit": 0 if neg_ok else 1,
             "output": "neg_ok=%s" % neg_ok})
        if not neg_ok:
            ok = False

        # 7. determinism: re-run the probe and diff
        n2 = rrun(client, "cd %s && mkdir -p verify && ./build-core/tools/railhmi-health-probe "
                          "--out verify/health-probe-rep.log; echo HEALTHREP_EXIT=$?"
                  % remote_dir, 60)
        note("health-probe-rep-run", n2)
        if "HEALTHREP_EXIT=0" not in n2.get("output", ""):
            ok = False
        n2b = rrun(client, "cd %s && diff verify/health-probe-rep.log "
                           "core-evidence/health-probe-%s.log && echo IDENTICAL"
                   % (remote_dir, stamp), 30)
        note("health-probe-determinism", n2b)
        if "IDENTICAL" not in n2b.get("output", ""):
            ok = False

        # 8. regression artifacts from the same TestRun
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
                  and "health-probe" in reg.get("output", ""))
        if not reg_ok:
            ok = False

        # 9. cleanup by name; trailing ls of removed dirs is the confirmation
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
            {"independent_recheck_uifix_r1": ("ITEM-024 D-01..D-04 repair re-verified: "
                                              "deliverable TestRun entry re-run ok, "
                                              "core-tests 663 line-identical to uifix-r1 "
                                              "+ JUnit 663/0 (22 cases), health-probe "
                                              "oracle + byte identity + determinism, "
                                              "ext v2 (103) and ext v3 (demo path) "
                                              "green on-board"),
             "confirmed": ok},
            {"ui_reactive_wiring": ("D-02: overlay reload()/Connections/onChanged + "
                                    "cached st property; P6 healthSt/logRows + "
                                    "reloadHealth/reloadLogs + health Connections; "
                                    "one-shot bindings absent (R3)"),
             "present": svar("T11_overlay_reactive") == "True"
                        and svar("T12_p6_reactive") == "True"
                        and svar("R3_no_one_shot_bindings") == "True"},
            {"ui_scrollable_cards": ("D-01: Flickable + clip + contentHeight "
                                     "present; anchored-Grid pre-fix form absent; "
                                     "delivered checker fails on synthetic pre-fix copy"),
             "present": svar("R1_p6_scrollable") == "True"
                        and svar("R2_p6_no_anchored_grid") == "True"
                        and neg_ok},
            {"ui_structured_query_inputs": ("D-03: source/keyword inputs with "
                                            "onEditingFinished -> reloadLogs"),
             "present": svar("T3_p6_log_card") == "True"},
            {"ui_demo_recovery_path": ("D-04: resetRecovery/advanceRecovery bridge "
                                       "invokables + overlay Timer + P6 chip; "
                                       "ext v3 asserts the full demo state machine"),
             "present": svar("T13_demo_path") == "True"
                        and svar("T14_demo_semantics") == "True"},
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
