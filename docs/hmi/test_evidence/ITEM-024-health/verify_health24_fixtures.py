# -*- coding: utf-8 -*-
"""ITEM-024 verification round — independent board-side re-run (verifier-owned).

Work item: 健康监控、结构化日志与断电自恢复 (ITEM-024).
Subject: implementer round commit 93e625a, stamp 20261016-ITEM024-r2
(663/0 core-tests incl. health/log/recovery cases + health-probe PASS +
check_health_wiring PASS).

Steps (all finite timeouts):
  1. Re-run the deliverable board_build.py --mode TestRun entry as a child
     process with a verifier stamp + --keep-remote; archive fresh logs and
     the JUnit XML (the remote build then backs the verifier ext fixture).
  2. Line-by-line [PASS] comparison: fresh core-tests log vs implementer r2
     archived log (663 cases, exact list equality) + independent JUnit parse
     (663/0) with the exact ITEM-024 health/log/recovery case enumeration.
  3. Independent assertions on the fresh health-probe log from the
     offline_selfcheck0 oracle (NOT from the implementer's archived text) +
     cross-round byte identity vs the archived r2 log (determinism).
  4. Verifier ext fixture (verify_health24_ext.cpp) linked against THIS
     round's build-core static libs: health threshold boundaries, JSON
     escaping, severity order, half-open windows, three-way keyword match,
     day-atom rolling sentinels, recovery progress math, camera_result
     ordering/criticality semantics.
  5. Verifier wiring scan of the delivered sources on the fresh remote copy
     (NOT the implementer's checker): RecoveryOverlay in main.qml, P6
     structured-log card + recovery card, bridge delegates to core,
     main.cpp injection, board pipeline runs the health probes.
  6. Determinism: re-run the health probe on the board and require
     byte-identical output.
  7. Regression artifacts from the same TestRun (sim-inject/status/alarm/
     clip/replay/ai/maintenance/permissions probes present).
  8. Cleanup the verifier workdir and /tmp/railhmi_build by name.
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


# offline_selfcheck0-derived oracle for the fresh health-probe log
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
    "healthy -> ok, no alerts",
    "health text",
    "temp+net warn",
    "very high temp -> error",
    "low disk -> warn",
    "log json with fields",
    "query by source",
    "query by min severity",
    "query by keyword",
    "query by time window",
    "log day key",
    "day-rolling keeps latest day",
    "recovery has 8 steps",
    "all ok -> completed",
    "step label",
    "single non-critical camera failure does not exit app",
    "degraded camera counted",
    "streaming step degraded, rest continue",
    "recovery still completes",
    "critical step failure -> app not alive",
    "config step failed recorded",
    "progress reflects terminal steps",
]


def assert_health_probe(text: str) -> list:
    fails: list = []
    for rx in ORACLE:
        if not re.search(rx, text, re.M):
            fails.append("missing line: %s" % rx[:70])
    if re.search(r'^type=acceptance\b.*result=FAIL', text, re.M):
        fails.append("acceptance verdict FAIL")
    # independent progress math (verifier-derived)
    got = [int(x) for x in re.findall(r'^type=recovery step=\S+ .*?progress=(\d+)$', text, re.M)]
    want = [i * 100 // 8 for i in range(1, 9)]
    if got != want:
        fails.append("progress math got=%s want=%s" % (got, want))
    return fails


def ext_run(client, args, wd, remote_dir, note) -> int:
    res = rrun(client, "mkdir -p %s" % wd, 30)
    note("mkdir-workdir", res)
    sftp = client.open_sftp()
    sftp.get_channel().settimeout(120)
    try:
        sftp.put(args.cpp, "%s/verify_health24_ext.cpp" % wd)
        note("upload-fixture", {"exit": 0, "output": args.cpp})
    finally:
        sftp.close()
    compile_cmd = ("cd %s && g++ -std=c++17 -O1 -Wall -Wextra "
                   "-Isrc/core/include -Isrc/platform/include "
                   "%s/verify_health24_ext.cpp build-core/src/core/librailhmi_core.a "
                   "build-core/src/platform/librailhmi_platform.a "
                   "-o %s/verify_health24_ext 2>&1; echo GXX_EXIT=$?"
                   % (remote_dir, wd, wd))
    cext = rrun(client, compile_cmd, 240)
    note("compile-ext", cext)
    if cext.get("exit") != 0 or "GXX_EXIT=0" not in cext.get("output", ""):
        return 1
    rext = rrun(client, "cd %s && %s/verify_health24_ext; echo EXT_EXIT=$?"
                % (remote_dir, wd), 180)
    note("run-ext", rext)
    m = re.search(r"RESULT: (\w+) passed=(\d+) failed=(\d+)", rext.get("output", ""))
    if rext.get("exit") != 0 or "EXT_EXIT=0" not in rext.get("output", "") or \
            not (m and m.group(1) == "PASS" and int(m.group(3)) == 0):
        return 1
    note("ext-summary", {"exit": 0 if m and m.group(1) == "PASS" else 1,
         "output": "ext passed=%s failed=%s" % (m.group(2), m.group(3))})
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="ITEM-024 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify024")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--impl-testrun-log", required=True,
                    help="implementer r2 core-tests log for line comparison")
    ap.add_argument("--impl-healthprobe-log", required=True,
                    help="implementer r2 health-probe log for byte identity")
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
                        "notes": ["ITEM-024: health monitoring + structured logs + "
                                  "power-on recovery; abort on fingerprint mismatch; "
                                  "no credentials logged"],
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

        # 4. verifier ext fixture against THIS round's static libs
        if ext_run(client, args, wd, remote_dir, note) != 0:
            ok = False

        # 5. verifier wiring scan of delivered sources (fresh remote copy)
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(60)
        try:
            sftp.putfo(io.BytesIO(SCAN_PY.encode("utf-8")), "%s/verify_scan24.py" % wd)
            note("upload-scan", {"exit": 0, "output": "verify_scan24.py"})
        finally:
            sftp.close()
        scan = rrun(client, "cd %s && python3 %s/verify_scan24.py; echo SCAN24_EXIT=$?"
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

        # 6. determinism: re-run the probe and diff
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

        # 7. regression artifacts from the same TestRun
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

        # 8. cleanup by name; trailing ls of removed dirs is the confirmation
        note("cleanup", rrun(client,
             "rm -rf %s && echo fixture_cleanup_done && echo LEFT_LIST && "
             "ls -d %s 2>&1" % (wd, wd), 30))
        cleanup_res = transcript["steps"][-1]
        cleanup_ok = ("fixture_cleanup_done" in cleanup_res.get("output", "")
                      and ("cannot access '%s'" % wd) in cleanup_res.get("output", ""))
        note("cleanup-verify", {"exit": 0 if cleanup_ok else 1,
                                "output": "cleanup_ok=%s (board /tmp/railhmi_build kept "
                                          "for post-run negative probe)" % cleanup_ok})
        if not cleanup_ok:
            ok = False

        findings = [
            {"independent_recheck": ("ITEM-024 health monitoring + structured logs + "
                                     "power-on recovery: deliverable TestRun entry re-run "
                                     "ok, core-tests 663 line-identical + JUnit 663/0 "
                                     "(22 ITEM-024 cases), health-probe oracle + "
                                     "determinism, ext fixtures and wiring scan both "
                                     "green on-board"),
             "confirmed": ok},
            {"observable_recovery_progress": ("restart flow shows ordered 8-step "
                                              "recovery with progress 12->100 and "
                                              "completion text; RecoveryOverlay + P6 "
                                              "recovery card wired via railhmiHealth"),
             "present": svar("T2_overlay_wired") == "True" and
                        svar("T4_p6_recovery_card") == "True"},
            {"observable_noncritical_isolation": ("single non-critical camera failure "
                                                  "-> StartStreaming degraded, "
                                                  "degraded_cameras=1, alive=true, "
                                                  "recovery completes"),
             "present": svar("N2_degraded_not_failed") == "True"},
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


# --- verifier wiring scan (delivered sources, fresh remote copy) — Item 024 ----

SCAN_PY = r'''import re
overlay = open('src/ui/qml/components/RecoveryOverlay.qml', encoding='utf-8').read()
main_qml = open('src/ui/qml/main.qml', encoding='utf-8').read()
p6 = open('src/ui/qml/pages/P6Maintenance.qml', encoding='utf-8').read()
bridge = open('src/ui/src/health_model.cpp', encoding='utf-8').read()
bridge_h = open('src/ui/src/health_model.h', encoding='utf-8').read()
mc = open('src/ui/src/main.cpp', encoding='utf-8').read()
bb = open('tools/board_build.py', encoding='utf-8').read()

# T1: overlay renders ordered steps with per-step status + progress + alive notice
print('T1_overlay_structure=',
      'recoveryOverlay' in overlay and 'recoveryStep-' in overlay and
      'recoveryText' in overlay and 'recoveryAlive' in overlay and
      'modelData.statusLabel' in overlay and 'modelData.label' in overlay and
      'progress' in overlay)

# T2: overlay wired into main.qml and driven by the railhmiHealth bridge
print('T2_overlay_wired=',
      'RecoveryOverlay' in main_qml and 'railhmiHealth' in overlay and
      'R.recovery()' in overlay)

# T3: P6 structured log card with level filter chips + rows via bridge logs()
print('T3_p6_log_card=',
      'maintLogQueryCard' in p6 and 'structuredLogRow' in p6 and
      'logFilterAll' in p6 and 'logFilterWarn' in p6 and 'logFilterError' in p6 and
      re.search(r'railhmiHealth\.logs\(', p6) is not None and
      'modelData.levelLabel' in p6 and 'modelData.source' in p6 and
      'modelData.message' in p6)

# T4: P6 recovery progress card with per-step rows via bridge recovery()
print('T4_p6_recovery_card=',
      'maintRecoveryCard' in p6 and 'recoveryProgressText' in p6 and
      'recoveryStepRow-' in p6 and
      re.search(r'railhmiHealth\.recovery\(\)', p6) is not None and
      'modelData.statusLabel' in p6 and 'modelData.detail' in p6)

# T5: bridge delegates to core (not UI-reimplemented logic)
print('T5_bridge_core=',
      'evaluate_health' in bridge and 'query_logs' in bridge and
      'log_record_json' in bridge and 'recovery_' in bridge and
      'railhmi::core::' in bridge)

# T6: main.cpp injects railhmiHealth and seeds a full 8-step recovery state
print('T6_main_inject=',
      'railhmiHealth' in mc and 'RailhmiHealthModel' in mc and
      'healthModel.seed(' in mc and
      'RecoveryStep::FrontAiReady' in mc and
      'RecoveryStep::StartStreaming' in mc)

# T7: board pipeline runs the health probe + health wiring checker
print('T7_board=',
      'check_health_wiring.py' in bb and 'railhmi-health-probe' in bb and
      'HEALTH_WIRE_EXIT' in bb and 'HEALTH_EXIT' in bb)

# T8: bridge log filter maps warn/error to min_severity (level filtering is
#     inclusive >=, not equality) and passes source/keyword through
print('T8_bridge_filter=',
      re.search(r'level == "warn"', bridge) is not None and
      re.search(r'level == "error"', bridge) is not None and
      'min_severity' in bridge and 'f.source' in bridge and 'f.keyword' in bridge)

# T9: overlay hides on completion, shows critical-failure notice on alive=false
print('T9_overlay_gating=',
      re.search(r'visible:\s*st\.completed === false', overlay) is not None and
      re.search(r'visible:\s*overlay\.st\.alive === false', overlay) is not None)

# N2 observable: degraded is distinct from failed in BOTH overlay and P6
# (single non-critical camera failure shows 降级, app stays alive)
print('N2_degraded_not_failed=',
      'degraded' in overlay and 'failed' in overlay and
      'degraded' in p6 and 'failed' in p6 and
      'Theme.color.degraded' in overlay and 'Theme.color.degraded' in p6)

# T10: tokens-only colors (strip // comments like the delivered checker)
overlay_l = [line.split('//', 1)[0] for line in overlay.splitlines()]
p6l = [line.split('//', 1)[0] for line in p6.splitlines()]
print('T10_no_hex=',
      not any(re.search(r'#[0-9a-fA-F]{3,8}\b', l) for l in overlay_l + p6l) and
      'Theme.color.' in overlay and 'Theme.color.' in p6)
'''

SCAN_KEYS = [
    "T1_overlay_structure", "T2_overlay_wired", "T3_p6_log_card",
    "T4_p6_recovery_card", "T5_bridge_core", "T6_main_inject", "T7_board",
    "T8_bridge_filter", "T9_overlay_gating", "N2_degraded_not_failed",
    "T10_no_hex",
]


if __name__ == "__main__":
    raise SystemExit(main())
