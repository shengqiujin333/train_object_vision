# -*- coding: utf-8 -*-
"""ITEM-020 verification round v2 — independent board-side re-run (verifier-owned).

Round v2 targets the D-01/D-02 FIX round (stamp 20261016-ITEM020-fix-r1):
  D-01 fix: main.qml onReplayRequest now calls railhmiReplay.selectAlarm(cameraId, tsWallMs);
            P3Replay consumes selectedCameraId/selectedAlarmTs/relatedCameras/selectedDetail;
            core adds related_cameras() and the bridge seeds alarms + startup selectAlarm.
  D-02 fix: P3Replay.refreshSync uses R.syncState(cursor) (camera-offset built); the
            literal [cursor, cursor, cursor] is gone; "同步状态不可用" is the no-clock state;
            core adds sync_snapshot().

Steps (all finite timeouts):
  1. Re-run the deliverable board_build.py --mode TestRun entry as child
     process with verifier stamp + --keep-remote (core-tests incl. replay+selection
     cases, tokens selftest, ui scan incl. delivered check_replay_wiring, sim-inject,
     status/ai/alarm/clip/replay probes) and archive fresh logs + JUnit XML.
  2. Line-by-line [PASS] comparison: fresh core-tests log vs implementer FIX-round
     archived log + independent JUnit parse (574/0) with an exact ITEM-020
     replay-domain case enumeration (36 named cases, incl. the 7 fix-round cases).
  3. Independent assertions on the fresh replay-probe log (trusting the probe's own
     acceptance line is not enough): everything from v1 plus the new type=lock and
     type=sync-snapshot lines (locked left/front/right, deviation prompt reachable
     from real offsets, no-clock unavailable text); cross-round line identity vs
     the implementer's archived FIX-round probe log.
  4. Verifier ext fixture (verify_replay20_ext.cpp) compiled against THIS round's
     build-core static libs: all v1 logic-edge checks (regression) PLUS the
     fix-round boundary checks: related_cameras (front triple order, non-front
     single, unknown camera, empty), sync_snapshot (unavailable branches, cursor+
     offset position math independent of cursor, inclusive threshold boundary,
     exact prompt texts, single-camera snapshot).
  5. QML/C++ wiring scan of the delivered sources on the fresh remote copy — v2
     expects the FIXED state: handler uses cameraId/tsWallMs and calls selectAlarm;
     startup selectAlarm/seedAlarms/setCameraOffsets present in main.cpp; P3 consumes
     the four selection APIs and syncState; no [cursor, cursor, cursor] literal;
     "同步状态不可用" state text present; UI objectNames/chips/detail fields/source
     note still present.
  6. Negative CLI edges on the deliverable probe (--nope exit 2, --help 0) +
     determinism re-run diff vs the fresh archived log.
  7. Regression artifacts from the same TestRun archive (sim-inject, status,
     ai, alarm, clip probes present).
  8. Cleanup /tmp/railhmi_build and verifier workdir by name.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
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


def main() -> int:
    ap = argparse.ArgumentParser(description="ITEM-020 verification fixtures (v2)")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify020")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--impl-testrun-log", required=True,
                    help="implementer FIX-round core-tests log for line comparison")
    ap.add_argument("--impl-replayprobe-log", required=True,
                    help="implementer FIX-round replay-probe log for line comparison")
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--timeout-sec", type=int, default=1200)
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
                        "notes": ["v2: asserts the D-01/D-02 FIX state; aborts on fingerprint mismatch; no credentials logged"],
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
        rl = outdir / ("build-TestRun-%s-%s.core-evidence_replay-probe-%s.log"
                       % (host_u, stamp, stamp))
        if not (tl.is_file() and tx.is_file() and rl.is_file()):
            note("fresh-artifacts", {"exit": None,
                 "output": "missing fresh artifacts: %s %s %s"
                           % (tl.name, tx.name, rl.name)})
            ok = False
        else:
            note("fresh-artifacts", {"exit": 0,
                 "output": tl.name + " " + tx.name + " " + rl.name})

        # 2. line-by-line [PASS] comparison + independent JUnit parse
        if tl.is_file() and args.impl_testrun_log:
            impl = parse_core_tests_log(Path(args.impl_testrun_log).read_text(
                encoding="utf-8", errors="replace"))
            ver = parse_core_tests_log(tl.read_text(encoding="utf-8", errors="replace"))
            same = impl["pass"] == ver["pass"]
            expected_pass = 574
            note("log-compare", {"exit": 0 if same and not ver["fail"] and
                                 len(ver["pass"]) == expected_pass else 1,
                 "output": "impl=%s verify=%s impl_pass=%d verify_pass=%d "
                           "identical=%s verify_fails=%d expect=%d result=%s"
                           % (os.path.basename(args.impl_testrun_log), tl.name,
                              len(impl["pass"]), len(ver["pass"]),
                              same, len(ver["fail"]), expected_pass, ver["result"])})
            if not same or ver["fail"] or len(ver["pass"]) != expected_pass:
                ok = False
        if tx.is_file():
            xtext = tx.read_text(encoding="utf-8", errors="replace")
            m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', xtext, re.S)
            n_tests, n_fail = (m.group(1), m.group(2)) if m else (None, None)
            # JUnit XML escapes "->" in case names as -&gt;
            names_all = re.findall(r'name="([^"]+)"', xtext)
            names_all = [n2.replace("&gt;", ">").replace("&lt;", "<").replace("&amp;", "&")
                         for n2 in names_all]
            found = [c for c in ITEM020_CASES if c in names_all]
            missing = [c for c in ITEM020_CASES if c not in names_all]
            junit_ok = (n_tests == "574" and n_fail == "0" and len(found) == len(ITEM020_CASES))
            note("junit-parse", {"exit": 0 if junit_ok else 1,
                 "output": "junit tests=%s failures=%s item020_cases=%d/%d missing=%s"
                           % (n_tests, n_fail, len(found), len(ITEM020_CASES), missing)})
            if not junit_ok:
                ok = False
        else:
            note("junit-parse", {"exit": None, "output": "no fresh junit xml"})
            ok = False

        # 3. independent assertions on the fresh replay-probe log
        if rl.is_file():
            fresh_text = rl.read_text(encoding="utf-8", errors="replace")
            vp = parse_replay_probe(fresh_text)
            fails = assert_replay_probe(vp)
            note("replay-probe-assert", {"exit": 0 if not fails else 1,
                 "output": json.dumps({"fails": fails}, ensure_ascii=False)})
            if fails:
                ok = False
            impl_probe_text = Path(args.impl_replayprobe_log).read_text(
                encoding="utf-8", errors="replace")
            identical = impl_probe_text.splitlines() == fresh_text.splitlines()
            note("replay-probe-compare", {"exit": 0 if identical else 1,
                 "output": "deterministic reproduction identical=%s" % identical})
            if not identical:
                ok = False
        else:
            note("replay-probe-assert", {"exit": None, "output": "missing fresh replay-probe"})
            ok = False

        # 4. ext fixture compile+run against THIS round's static libs
        if ext_run(client, args, wd, remote_dir, note) != 0:
            ok = False

        # 5. QML/C++ wiring scan of delivered sources (fresh remote copy, stack-run)
        scan_py = r'''import re
main = open('src/ui/qml/main.qml', encoding='utf-8').read()
p3 = open('src/ui/qml/pages/P3Replay.qml', encoding='utf-8').read()
p2 = open('src/ui/qml/pages/P2AlarmCenter.qml', encoding='utf-8').read()
mc = open('src/ui/src/main.cpp', encoding='utf-8').read()
print('W1_emit_declaration=', bool(re.search(r'signal replayRequested\(string cameraId, double tsWallMs\)', p2)))
print('W1_emission=', 'replayRequested(modelData.cameraId, modelData.tsWallMs)' in p2)
mlocal = re.search(r'function onReplayRequest\([^)]*\)[^{]*\{([^}]*)\}', main)
body = mlocal.group(1) if mlocal else ''
print('W1_handler_params_used_camera=', 'cameraId' in body)
print('W1_handler_params_used_ts=', 'tsWallMs' in body)
print('W1_handler_calls_selectAlarm=', 'selectAlarm' in body)
print('W1_startup_selectAlarm=', 'selectAlarm("CAM-01"' in mc)
print('W1_seedAlarms=', 'seedAlarms' in mc)
print('W1_offsets_injected=', 'setCameraOffsets' in mc)
print('W2_cursor_positions_literal=', '[cursor, cursor, cursor]' in p3)
print('W2_syncState_consumed=', 'syncState' in p3)
print('W2_unavailable_state=', ('同步状态不可用' in p3))
print('W3_objectnames=', all(o in p3 for o in ('timelineTrack', 'timelineCursor', 'alarmSegment', 'timelineGap', 'alarmMarker', 'keyframeStrip')))
print('W3_jump_chips=', all(c in p3 for c in ('-30s', '报警时刻', '+30s', '+60s')))
print('W3_detail_fields=', all(c in p3 for c in ('时间：', '类型：', '级别：', '距离：', '置信度：', '处理状态：', '备注：')))
print('W3_source_note_ui=', 'sourceStatus' in p3)
print('W3_selection_consumed=', all(x in p3 for x in ('selectedCameraId', 'selectedAlarmTs', 'relatedCameras', 'selectedDetail')))
'''
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(60)
        try:
            import io
            sftp.putfo(io.BytesIO(scan_py.encode("utf-8")), "%s/verify_scan.py" % wd)
            note("upload-scan", {"exit": 0, "output": "verify_scan.py"})
        finally:
            sftp.close()
        scan = rrun(client, "cd %s && python3 %s/verify_scan.py; echo SCAN2_EXIT=$?"
                    % (remote_dir, wd), 60)
        note("qml-scan", scan)
        so = scan.get("output", "")

        def svar(name):
            m2 = re.search(r"%s\s*=\s*(\S+)" % re.escape(name), so)
            return m2.group(1) if m2 else None

        wants = {
            "W1_emit_declaration": "True", "W1_emission": "True",
            "W1_handler_params_used_camera": "True", "W1_handler_params_used_ts": "True",
            "W1_handler_calls_selectAlarm": "True", "W1_startup_selectAlarm": "True",
            "W1_seedAlarms": "True", "W1_offsets_injected": "True",
            "W2_cursor_positions_literal": "False", "W2_syncState_consumed": "True",
            "W2_unavailable_state": "True", "W3_objectnames": "True",
            "W3_jump_chips": "True", "W3_detail_fields": "True",
            "W3_source_note_ui": "True", "W3_selection_consumed": "True",
        }
        observed = {k: svar(k) for k in wants}
        mismatch = sorted(k for k, v in wants.items() if observed[k] != v)
        wiring_ok = not mismatch and scan.get("exit") == 0 and "SCAN2_EXIT=0" in so
        note("wiring-scan-findings", {"exit": 0 if wiring_ok else 1,
             "output": ("FIX state: D-01 wired (handler uses cameraId=%s tsWallMs=%s "
                        "selectAlarm=%s; startup=%s seed=%s offsets=%s; p3 selection=%s) | "
                        "D-02 fixed (literal triple absent=%s syncState consumed=%s "
                        "unavailable state=%s) | W3 presence objnames=%s chips=%s fields=%s "
                        "src=%s | mismatch=%s raw=%s")
                       % (observed["W1_handler_params_used_camera"],
                          observed["W1_handler_params_used_ts"],
                          observed["W1_handler_calls_selectAlarm"],
                          observed["W1_startup_selectAlarm"], observed["W1_seedAlarms"],
                          observed["W1_offsets_injected"], observed["W3_selection_consumed"],
                          observed["W2_cursor_positions_literal"],
                          observed["W2_syncState_consumed"], observed["W2_unavailable_state"],
                          observed["W3_objectnames"], observed["W3_jump_chips"],
                          observed["W3_detail_fields"], observed["W3_source_note_ui"],
                          mismatch, json.dumps(observed, ensure_ascii=False))})
        if not wiring_ok:
            ok = False

        # 6. negative CLI edge on the deliverable probe + determinism re-run
        n1 = rrun(client, "cd %s && ./build-core/tools/railhmi-replay-probe --nope; "
                          "echo NARG_EXIT=$?" % remote_dir, 30)
        note("replay-probe-negative-arg", n1)
        if "NARG_EXIT=2" not in n1.get("output", ""):
            ok = False
        n1b = rrun(client, "cd %s && ./build-core/tools/railhmi-replay-probe --help; "
                           "echo HELP_EXIT=$?" % remote_dir, 30)
        note("replay-probe-help", n1b)
        if "HELP_EXIT=0" not in n1b.get("output", ""):
            ok = False
        n2 = rrun(client, "cd %s && ./build-core/tools/railhmi-replay-probe "
                          "--out verify/replay-probe-rep.log; echo REPL_EXIT=$?" % remote_dir, 60)
        note("replay-probe-rep-run", n2)
        if "REPL_EXIT=0" not in n2.get("output", ""):
            ok = False
        n2b = rrun(client, "cd %s && diff %s/verify/replay-probe-rep.log "
                           "%s/core-evidence/replay-probe-%s.log && echo IDENTICAL"
                           % (remote_dir, remote_dir, remote_dir, stamp), 30)
        note("replay-probe-determinism", n2b)
        if "IDENTICAL" not in n2b.get("output", ""):
            ok = False

        # 7. fault-injection regression artifacts from the same TestRun
        reg = rrun(client, "cd %s && ls core-evidence/" % remote_dir, 30)
        note("regression-artifacts", reg)
        reg_ok = (reg.get("exit") == 0
                  and "sim-inject" in reg.get("output", "")
                  and "status-probe" in reg.get("output", "")
                  and "ai-probe" in reg.get("output", "")
                  and "alarm-probe" in reg.get("output", "")
                  and "clip-probe" in reg.get("output", ""))
        if not reg_ok:
            ok = False

        # 8. cleanup by name; trailing ls of removed dirs is the confirmation
        note("cleanup", rrun(client,
             "rm -rf %s /tmp/railhmi_build /tmp/railhmi_clips && echo fixture_cleanup_done "
             "&& echo LEFT_LIST && ls -d %s /tmp/railhmi_build /tmp/railhmi_clips 2>&1"
             % (wd, wd), 30))
        cleanup_res = transcript["steps"][-1]
        removed = (wd, "/tmp/railhmi_build", "/tmp/railhmi_clips")
        cleanup_ok = ("fixture_cleanup_done" in cleanup_res.get("output", "")
                      and all(("cannot access '%s'" % d) in cleanup_res.get("output", "")
                              for d in removed))
        note("cleanup-verify", {"exit": 0 if cleanup_ok else 1,
                                "output": "cleanup_ok=%s" % cleanup_ok})
        if not cleanup_ok:
            ok = False

        findings = [
            {"D-01": ("replayRequested(cameraId, tsWallMs) -> selectAlarm wired; "
                      "P3 consumes selection APIs; startup selectAlarm/seed/offsets present"),
             "confirmed_fixed": observed["W1_handler_calls_selectAlarm"] == "True" and
                                observed["W3_selection_consumed"] == "True" and
                                observed["W1_startup_selectAlarm"] == "True"},
            {"D-02": ("[cursor, cursor, cursor] literal removed; P3 uses syncState; "
                      "'同步状态不可用' state present; probe sync-snapshot reachable"),
             "confirmed_fixed": observed["W2_cursor_positions_literal"] == "False" and
                                observed["W2_syncState_consumed"] == "True" and
                                observed["W2_unavailable_state"] == "True"},
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


# --- replay-probe log parsing (fixed v2 line format) --------------------------------

# key=value tokens; greedy value keeps '=' inside the last token so that
# prompt=...（...） with the fullwidth parens still terminates at the space
# before the next key (learned in ITEM-017; prompt contains fullwidth chars
# but no ASCII spaces).
KV_PAIR = re.compile(r"(\S+?)=(\S+)(?:\s+(.*))?$")


def kv(s: str) -> dict:
    out = {}
    text = s
    while text:
        m = KV_PAIR.match(text)
        if not m:
            break
        key, val, rest = m.group(1), m.group(2), m.group(3)
        out[key] = val
        if rest is None:
            break
        text = rest
    return out


P_DAY = 1792108800000           # parse_date_utc_ms("2026-10-16"), fixed expectation
P_ALARM = P_DAY + 120000        # probe's alarm ts (deterministic)
P_THRESHOLD = 300

RX = {
    "timeline": re.compile(r"type=timeline cam=(\S+) day=(\d+) duration_ms=(\d+) "
                           r"segments=(\d+) gaps=(\d+) alarms=(\d+) keyframes=(\d+)$"),
    "seg": re.compile(r"seg kind=(\S+) t0=(\d+) t1=(\d+) label=(.*)$"),
    "gap": re.compile(r"gap t0=(\d+) t1=(\d+) reason=(.*)$"),
    "flags": re.compile(r"flags has_alarm_segment=(\w+) has_gap=(\w+)$"),
    "jumps": re.compile(r"type=jumps minus30=(\d+) alarm=(\d+) plus30=(\d+) plus60=(\d+)$"),
    "seek": re.compile(r"type=seek pos=(\d+)$"),
    "jumpz": re.compile(r"type=jump plus30 pos=(\d+)$"),
    "sync": re.compile(r"type=sync in_sync=(\w+) dev=(\d+) \| deviation=(\w+) dev=(\d+) "
                       r"prompt=(.*)$"),
    # FIX-round lines: lock + sync-snapshot (real camera offsets)
    "lock": re.compile(r"type=lock alarm_cam=(\S+) locked=(\S+)$"),
    "snap": re.compile(r"type=sync-snapshot in_sync_available=(\w+) dev=(\d+) "
                       r"\| deviation_prompt=(.*) \| no_clock=(.*)$"),
    "source": re.compile(r"type=source source=(\S+) label=(.*?) usable=(\w+) note=(.*)$"),
    "detail": re.compile(r"type=detail time=(.*?) type=(\S+) level=(\S+) distance=(.*?) "
                         r"confidence=(\S+) ack=(.*)$"),
    "acceptance": re.compile(r"type=acceptance alarm_segment=(\w+) gap=(\w+) jumps_ok=(\w+) "
                             r"sync_prompt_on_deviation=(\w+) lock_related_cameras=(\w+) "
                             r"sync_unavailable_without_clock=(\w+) nvr_fallback_local=(\w+) "
                             r"result=(\w+)$"),
}

# The 36 exact ITEM-020 core-tests case names (29 base + 7 fix-round additions).
ITEM020_CASES = [
    "timeline only CAM-01 segments", "continuous segment",
    "critical/alarm segment prominent", "gap per camera", "has_gap",
    "two alarm markers", "keyframe markers present", "day window duration",
    "jump -30s", "jump alarm moment", "jump +30s", "jump +60s",
    "seek within window", "seek clamps low", "seek clamps high",
    "jump_to sets position", "sync group left/front/right",
    "in sync within threshold", "deviation over threshold -> prompt",
    "prompt text", "nvr available", "nvr unavailable -> local clips with explicit note",
    "no source unavailable", "detail type", "detail level", "detail distance",
    "detail confidence", "detail ack", "detail time",
    # fix-round additions (test_selection_and_sync_snapshot)
    "front alarm locks left/front/right",
    "non-front alarm locks only its camera",
    "empty camera -> no lock",
    "no clocks -> sync unavailable",
    "missing a locked camera clock -> unavailable",
    "all clocks within threshold -> in sync",
    "deviation over threshold -> prompt (reachable)",
]


def parse_replay_probe(text: str) -> dict:
    p = {"timeline": None, "segments": [], "gaps": [], "flags": None, "jumps": None,
         "seek": None, "jumpz": None, "sync": None, "lock": None, "snap": None,
         "source": None, "detail": None, "acceptance": None}
    for line in text.splitlines():
        s = line.strip()
        first = s.split(" ", 1)[0] if s else ""
        if first.startswith("seg ") or first == "seg":
            key = "seg"
        elif first.startswith("gap ") or first == "gap":
            key = "gap"
        elif first.startswith("flags ") or first == "flags":
            key = "flags"
        elif s.startswith("type=jump plus30 "):
            key = "jumpz"   # type=jump plus30 pos=... shares the jump name
        elif first.startswith("type="):
            key = first[len("type="):]
        else:
            continue
        if key == "sync-snapshot":
            key = "snap"    # name held in own var; dedicated regex
        rx = RX.get(key)
        if rx is None:
            continue
        m = rx.fullmatch(s)
        if m:
            if key == "seg":
                p["segments"].append(m.groups())
            elif key == "gap":
                p["gaps"].append(m.groups())
            else:
                p[key] = m.groups()
    return p


def assert_replay_probe(p: dict, threshold: int = P_THRESHOLD) -> list:
    fails = []
    tl = p["timeline"]
    if not tl or tl[0] != "CAM-01" or tl[1] != str(P_DAY) or tl[2] != "86400000" or \
            (tl[3], tl[4], tl[5], tl[6]) != ("3", "1", "1", "1440"):
        fails.append("timeline composition %r" % (tl,))
    segs = p["segments"]
    if len(segs) != 3:
        fails.append("segment lines=%d want 3" % len(segs))
    else:
        s0, s1, s2 = segs
        if s0[0] != "continuous" or s0[1] != str(P_DAY + 10000) or \
                s0[2] != str(P_DAY + 80000) or s0[3] != "连续录像":
            fails.append("seg0 %r" % (s0,))
        if s1[0] != "critical" or s1[1] != str(P_DAY + 90000) or \
                s1[2] != str(P_DAY + 200000) or s1[3] != "报警保护片段":
            fails.append("seg1 %r" % (s1,))
        if s2[0] != "continuous" or s2[1] != str(P_DAY + 260000) or \
                s2[2] != str(P_DAY + 300000):
            fails.append("seg2 %r" % (s2,))
    gaps = p["gaps"]
    if len(gaps) != 1 or gaps[0][0] != str(P_DAY + 210000) or \
            gaps[0][1] != str(P_DAY + 250000) or gaps[0][2] != "recording_interrupted":
        fails.append("gap row %r" % (gaps,))
    if p["flags"] != ("true", "true"):
        fails.append("flags %r" % (p["flags"],))
    j = p["jumps"]
    if not j or j != (str(P_ALARM - 30000), str(P_ALARM), str(P_ALARM + 30000),
                      str(P_ALARM + 60000)):
        fails.append("jump offsets %r" % (j,))
    if not p["seek"] or p["seek"][0] != str(P_ALARM):
        fails.append("seek pos %r" % (p["seek"],))
    if not p["jumpz"] or p["jumpz"][0] != str(P_ALARM + 30000):
        fails.append("jump plus30 pos %r" % (p["jumpz"],))
    sy = p["sync"]
    # probe semantics (source-verified): deviation=(bad.in_sync ? false : true),
    # so "true" means the dev=900 side DOES deviate beyond the threshold
    if not sy or sy[0] != "true" or sy[1] != "200" or sy[2] != "true" or \
            sy[3] != "900" or sy[4] != "同步偏差 900 ms（阈值 %d ms）" % threshold:
        fails.append("sync two-sided %r" % (sy,))
    # D-01 fix evidence: front alarm locks left/front/right, in probe order
    lk = p["lock"]
    if not lk or lk != ("CAM-01", "CAM-02,CAM-01,CAM-03"):
        fails.append("lock line %r" % (lk,))
    # D-02 fix evidence: snapshot built from real offsets — 120/200 within
    # threshold (in sync, dev=200), 900 beyond (exact prompt), no clock ->
    # unavailable with the exact UI text
    sn = p["snap"]
    if not sn or sn[0] != "true" or sn[1] != "200" or \
            sn[2] != "同步偏差 900 ms（阈值 %d ms）" % threshold or \
            sn[3] != "同步状态不可用":
        fails.append("sync-snapshot %r" % (sn,))
    src = p["source"]
    if not src or src[0] != "local_clips" or src[2] != "true" or \
            src[3] != "NVR 录像不可用，回放本机片段" or src[1] != "本机片段":
        fails.append("source fallback %r" % (src,))
    dt = p["detail"]
    want = {"type": "前方轨道人员", "level": "一级", "distance": "12.3 m",
            "confidence": "87%", "ack": "已确认"}
    if not dt or dt[1] != want["type"] or dt[2] != want["level"] or \
            dt[3] != want["distance"] or dt[4] != want["confidence"] or \
            dt[5] != want["ack"] or not dt[0].startswith("2026-10-16 00:02:"):
        fails.append("detail fields %r" % (dt,))
    acc = p["acceptance"]
    if not acc or acc[:7] != ("true", "true", "true", "true", "true", "true", "true") or \
            acc[7] != "PASS":
        fails.append("acceptance line %r" % (acc,))
    return fails


def parse_core_tests_log(text: str) -> dict:
    return {"pass": [l.strip() for l in text.splitlines() if l.startswith("[PASS]")],
            "fail": [l.strip() for l in text.splitlines() if l.startswith("[FAIL]")],
            "result": next((l for l in text.splitlines() if l.startswith("RESULT:")), "")}


def ext_run(client, args, wd, remote_dir, note) -> int:
    res = rrun(client, "mkdir -p %s/verify" % remote_dir, 30)
    note("mkdir-verify", res)
    res2 = rrun(client, "rm -rf %s && mkdir -p %s" % (args.workdir, args.workdir), 30)
    note("mkdir-workdir", res2)
    sftp = client.open_sftp()
    sftp.get_channel().settimeout(120)
    try:
        sftp.put(args.cpp, "%s/verify_replay20_ext.cpp" % wd)
        note("upload-fixture", {"exit": 0, "output": args.cpp})
    finally:
        sftp.close()
    compile_cmd = ("cd %s && g++ -std=c++17 -O1 -Wall -Wextra "
                   "-DRAILHMI_CONFIG_DIR='\"config\"' "
                   "-Isrc/core/include -Isrc/platform/include "
                   "%s/verify_replay20_ext.cpp build-core/src/core/librailhmi_core.a "
                   "build-core/src/platform/librailhmi_platform.a "
                   "-o %s/verify_replay20_ext 2>&1; echo GXX_EXIT=$?"
                   % (remote_dir, wd, wd))
    cext = rrun(client, compile_cmd, 240)
    note("compile-ext", cext)
    if cext.get("exit") != 0 or "GXX_EXIT=0" not in cext.get("output", ""):
        return 1
    rext = rrun(client, "cd %s && %s/verify_replay20_ext; echo EXT_EXIT=$?"
                % (remote_dir, wd), 180)
    note("run-ext", rext)
    m = re.search(r"RESULT: (\w+) passed=(\d+) failed=(\d+)", rext.get("output", ""))
    if rext.get("exit") != 0 or "EXT_EXIT=0" not in rext.get("output", "") or \
            not (m and m.group(1) == "PASS" and int(m.group(3)) == 0):
        return 1
    note("ext-summary", {"exit": 0 if m and m.group(1) == "PASS" else 1,
         "output": "ext passed=%s failed=%s" % (m.group(2), m.group(3))})
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
