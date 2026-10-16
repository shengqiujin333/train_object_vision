# -*- coding: utf-8 -*-
"""ITEM-021 verification round — independent board-side re-run (verifier-owned).

Work item: AI 调试页与司机视图隔离 (ITEM-021).
Subject: implementer commit efdabac, authoritative round stamp
20261016-ITEM021-r2 (586/0 core-tests + ai-probe distance/driver-isolation +
wiring scan PASS).

Steps (all finite timeouts):
  1. Re-run the deliverable board_build.py --mode TestRun entry as a child
     process with a verifier stamp + --keep-remote; archive fresh logs and
     the JUnit XML (the remote build then backs the verifier ext fixture).
  2. Line-by-line [PASS] comparison: fresh core-tests log vs implementer r2
     archived log + independent JUnit parse (586/0) with an exact ITEM-021
     AI-debug case enumeration (distance/driver-isolation/access cases).
  3. Independent assertions on the fresh ai-probe log, expectations derived
     from ai_probe.cpp formulas (NOT from the implementer's archived text):
     per-channel distance=8.0+priority -> 9.0/11.0/10.0/12.0 m, latency
     30+3*priority -> 33/39/36/42, conf=80, fps=5, worker-down -> AI 不可用
     with debug_all_unavailable/driver_debug_denied; cross-round byte
     identity vs the implementer's archived r2 ai-probe log (determinism).
  4. Verifier ext fixture (verify_ai21_ext.cpp) compiled against THIS round's
     build-core static libs: distance selection/formatting boundaries,
     driver-isolation matrix vs the navigation permission source, direct
     navigation denial, visible-page set, ITEM-017 base semantics regression.
  5. Verifier wiring scan of the delivered sources on the fresh remote copy
     (not the implementer's checker): four overlay toggles default-false with
     chips + allOff, debug tokens consumed and debug colors distinct from the
     alarm red (#FF6B6B), the old single overlayVisible toggle gone, no
     Theme.color.ok debug leftovers, aiDebugDenied guard + role!=driver,
     main.qml routing/DeniedView/railhmiNav filtering, P1 has no debug overlay
     identifiers, distance keys (distanceText/distanceM) bridged in main.cpp,
     tokens.yaml/tokens.json/Theme.qml/Tokens.qml token set, board_build.py
     runs the ITEM-021 wiring check in TestRun.
  6. Negative CLI edges on the deliverable ai-probe (unknown arg exit 2,
     --help exit 0) + a fresh re-run determinism diff.
  7. Regression artifacts from the same TestRun (sim-inject/status/alarm/
     clip/replay probes present).
  8. Cleanup the verifier workdir and /tmp/railhmi_build by name.
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
    ap = argparse.ArgumentParser(description="ITEM-021 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify021")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--impl-testrun-log", required=True,
                    help="implementer r2 core-tests log for line comparison")
    ap.add_argument("--impl-aiprobe-log", required=True,
                    help="implementer r2 ai-probe log for byte-identity check")
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
                        "notes": ["ITEM-021: AI debug page & driver isolation; "
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
        pl = outdir / ("build-TestRun-%s-%s.core-evidence_ai-probe-%s.log"
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
        expected_pass = 586
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
            found = [c for c in ITEM021_CASES if c in names_all]
            missing = [c for c in ITEM021_CASES if c not in names_all]
            junit_ok = (n_tests == "586" and n_fail == "0"
                        and len(found) == len(ITEM021_CASES))
            note("junit-parse", {"exit": 0 if junit_ok else 1,
                 "output": "junit tests=%s failures=%s item021_cases=%d/%d missing=%s"
                           % (n_tests, n_fail, len(found), len(ITEM021_CASES), missing)})
            if not junit_ok:
                ok = False
        else:
            note("junit-parse", {"exit": None, "output": "no fresh junit xml"})
            ok = False

        # 3. independent ai-probe assertions (formulas) + byte identity vs r2
        if pl.is_file():
            fresh_text = pl.read_text(encoding="utf-8", errors="replace")
            fails = assert_ai_probe(fresh_text)
            note("ai-probe-assert", {"exit": 0 if not fails else 1,
                 "output": json.dumps({"fails": fails}, ensure_ascii=False)})
            if fails:
                ok = False
            impl_probe_text = Path(args.impl_aiprobe_log).read_text(
                encoding="utf-8", errors="replace")
            identical = impl_probe_text.splitlines() == fresh_text.splitlines()
            note("ai-probe-compare", {"exit": 0 if identical else 1,
                 "output": "cross-round deterministic reproduction identical=%s" % identical})
            if not identical:
                ok = False
        else:
            note("ai-probe-assert", {"exit": None, "output": "missing fresh ai-probe"})
            ok = False

        # 4. verifier ext fixture against THIS round's static libs
        if ext_run(client, args, wd, remote_dir, note) != 0:
            ok = False

        # 5. verifier wiring scan of delivered sources (fresh remote copy)
        scan_py = SCAN_PY
        import io
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(60)
        try:
            sftp.putfo(io.BytesIO(scan_py.encode("utf-8")), "%s/verify_scan21.py" % wd)
            note("upload-scan", {"exit": 0, "output": "verify_scan21.py"})
        finally:
            sftp.close()
        scan = rrun(client, "cd %s && python3 %s/verify_scan21.py; echo SCAN21_EXIT=$?"
                    % (remote_dir, wd), 60)
        note("wiring-scan", scan)
        so = scan.get("output", "")

        def svar(name):
            m2 = re.search(r"%s\s*=\s*(\S+)" % re.escape(name), so)
            return m2.group(1) if m2 else None

        wants = {k: "True" for k in SCAN_KEYS}
        observed = {k: svar(k) for k in wants}
        mismatch = sorted(k for k, v in wants.items() if observed[k] != v)
        scan_exit_ok = scan.get("exit") == 0 and "SCAN21_EXIT=0" in so
        wiring_ok = not mismatch and scan_exit_ok
        note("wiring-scan-findings", {"exit": 0 if wiring_ok else 1,
             "output": "mismatch=%s scan_exit_ok=%s raw=%s"
                       % (mismatch, scan_exit_ok, json.dumps(observed, ensure_ascii=False))})
        if not wiring_ok:
            ok = False

        # 6. negative CLI edges + determinism on the deliverable probe
        n1 = rrun(client, "cd %s && ./build-core/tools/railhmi-ai-probe --nope; "
                          "echo NARG_EXIT=$?" % remote_dir, 30)
        note("ai-probe-negative-arg", n1)
        if "NARG_EXIT=2" not in n1.get("output", ""):
            ok = False
        n1b = rrun(client, "cd %s && ./build-core/tools/railhmi-ai-probe --help; "
                           "echo HELP_EXIT=$?" % remote_dir, 30)
        note("ai-probe-help", n1b)
        if "HELP_EXIT=0" not in n1b.get("output", ""):
            ok = False
        n2 = rrun(client, "cd %s && mkdir -p verify && ./build-core/tools/railhmi-ai-probe "
                          "--config ./config --out verify/ai-probe-rep.log; echo AIREP_EXIT=$?"
                  % remote_dir, 60)
        note("ai-probe-rep-run", n2)
        if "AIREP_EXIT=0" not in n2.get("output", ""):
            ok = False
        n2b = rrun(client, "cd %s && diff verify/ai-probe-rep.log core-evidence/ai-probe-%s.log "
                           "&& echo IDENTICAL" % (remote_dir, stamp), 30)
        note("ai-probe-determinism", n2b)
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
                  and "replay-probe" in reg.get("output", ""))
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
            {"ITEM-021": ("P5 overlay per-item toggles default-off with distinct "
                          "high-contrast debug stroke tokens (debug_box/debug_roi), "
                          "distance/end-to-end latency fields bridged, driver isolated "
                          "(nav filter + DeniedView + P5 guard + no debug overlay in P1); "
                          "core distance/visibility boundaries held on-board"),
             "confirmed": ok},
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


# --- ai-probe log parsing with formula-derived expectations ---------------------

# value may contain spaces (e.g. "9.0 m", "<1 秒前"); capture to next key=
DEBUG_RX = re.compile(
    r"^type=debug camera=(\S+) profile=(\S+) available=(\w+) model=(\S+) "
    r"latency=(\d+) boxes=(\d+) conf=(\d+) distance=(\S+ \S+) last=(.*)$", re.M)

# per-camera expectations: priority front=1 wheel=2 pantograph=3 cab=4
# distance = 8.0 + priority; latency = 30 + 3*priority (ai_probe.cpp)
PROBE_WANT = {
    "CAM-01": {"profile": "front",      "distance": "9.0 m",  "latency": "33"},
    "CAM-04": {"profile": "pantograph", "distance": "11.0 m", "latency": "39"},
    "CAM-05": {"profile": "wheel_rail", "distance": "10.0 m", "latency": "36"},
    "CAM-06": {"profile": "cab_door",   "distance": "12.0 m", "latency": "42"},
}


def assert_ai_probe(text: str) -> list:
    fails = []
    cfg = re.search(r"^type=config interval=5 depth=2 budget=4 temp_limit=85 models=4$",
                    text, re.M)
    if not cfg:
        fails.append("config line (interval=5 depth=2 budget=4 temp=85)")
    queues = re.findall(r"type=queue camera=(\S+) profile=(\S+) depth=(\d+) dropped=(\d+) "
                        r"inferred=(\d+) requested=(\d+) fps=(\d+) model=(\S+)", text)
    if len(queues) != 4:
        fails.append("queue lines=%d want 4" % len(queues))
    for cam, prof, depth, dropped, inferred, requested, fps, model in queues:
        w = PROBE_WANT.get(cam)
        if not w:
            fails.append("unexpected queue camera %s" % cam)
            continue
        if prof != w["profile"]:
            fails.append("%s profile %s want %s" % (cam, prof, w["profile"]))
        if fps != "5":
            fails.append("%s fps=%s want 5" % (cam, fps))
        if model != "rknn-2.3.2/yolov5s" and cam != "CAM-05":
            fails.append("%s model=%s" % (cam, model))
        if cam == "CAM-05" and model != "rknn-2.3.2/yolov8s-seg":
            fails.append("CAM-05 model=%s want rknn-2.3.2/yolov8s-seg" % model)
    # schedule: normal temp -> 4 run; over-temp -> budget halved to 2, guard=true
    s60 = re.search(r"^type=schedule temp=60 run=CAM-01,CAM-05,CAM-04,CAM-06 skipped= guard=false$", text, re.M)
    if not s60:
        fails.append("schedule 60c priority order/front-first")
    s90 = re.search(r"^type=schedule temp=90 run=CAM-01,CAM-05 skipped=CAM-04,CAM-06 guard=true$", text, re.M)
    if not s90:
        fails.append("schedule 90c guard halves budget")
    dbg = DEBUG_RX.findall(text)
    if len(dbg) != 4:
        fails.append("debug lines=%d want 4" % len(dbg))
    for cam, prof, avail, model, lat, boxes, conf, dist, last in dbg:
        w = PROBE_WANT.get(cam)
        if not w:
            fails.append("unexpected debug camera %s" % cam)
            continue
        if avail != "true":
            fails.append("%s available=%s want true" % (cam, avail))
        if dist != w["distance"]:
            fails.append("%s distance=%s want %s (8.0+priority)" % (cam, dist, w["distance"]))
        if lat != w["latency"]:
            fails.append("%s latency=%s want %s (30+3*priority)" % (cam, lat, w["latency"]))
        if boxes != "1":
            fails.append("%s boxes=%s" % (cam, boxes))
        if conf != "80":
            fails.append("%s conf=%s want 80" % (cam, conf))
        if last != "<1 秒前":
            fails.append("%s last=%r want <1 秒前" % (cam, last))
    if not re.search(r"^type=ai-status stopped=AI 不可用$", text, re.M):
        fails.append("worker-stop status line")
    acc = re.search(r"^type=acceptance worker_down_status=AI 不可用 alarm_annotation=AI 不可用 "
                    r"debug_all_unavailable=true temp_guard_degrades=true driver_debug_denied=true "
                    r"wide_angle_excluded=true$", text, re.M)
    if not acc:
        fails.append("acceptance flags (worker_down/annotation/debug_all_unavailable/"
                     "temp_guard/driver_debug_denied/wide_angle_excluded)")
    if not re.search(r"^type=result PASS$", text, re.M):
        fails.append("result PASS line")
    return fails


ITEM021_CASES = [
    "debug distance from box (ITEM-021)",
    "driver cannot see AI debug/overlay (ITEM-021)",
    "maintainer/developer/admin can see AI debug",
    # ITEM-011 navigation matrix points the P5 isolation is built on
    "driver cannot AI debug",
    "driver direct AI debug -> denied (show no-permission + return)",
    "developer lands on AI debug",
    "driver nav excludes AI debug/maintenance/all cameras",
    # ITEM-017 debug-view contract cases P5 consumes
    "AI debug shows only ai_enabled cameras (4)",
    "CAM-01 available",
    "CAM-01 shows box in ROI",
    "model version reported",
    "latency from latest result",
    "last result age",
    "worker stopped -> every AI channel marked unavailable (not silent)",
]


def parse_core_tests_log(text: str) -> dict:
    return {"pass": [l.strip() for l in text.splitlines() if l.startswith("[PASS]")],
            "fail": [l.strip() for l in text.splitlines() if l.startswith("[FAIL]")],
            "result": next((l for l in text.splitlines() if l.startswith("RESULT:")), "")}


# --- verifier wiring scan (delivered sources, fresh remote copy) -----------------

SCAN_PY = r'''import os, re
p5 = open('src/ui/qml/pages/P5AiDebug.qml', encoding='utf-8').read()
main = open('src/ui/qml/main.qml', encoding='utf-8').read()
p1 = open('src/ui/qml/pages/P1Realtime.qml', encoding='utf-8').read()
mc = open('src/ui/src/main.cpp', encoding='utf-8').read()
theme = open('src/ui/qml/Theme.qml', encoding='utf-8').read()
tokens_qml = open('src/ui/qml/Tokens.qml', encoding='utf-8').read()
tokens_json = open('resources/tokens.json', encoding='utf-8').read()
tokens_yaml = open('design_tokens.yaml', encoding='utf-8').read()
bb = open('tools/board_build.py', encoding='utf-8').read()
nav = open('src/core/src/navigation.cpp', encoding='utf-8').read()
inf = open('src/core/src/inference.cpp', encoding='utf-8').read()
check_tools = open('tools/check_ai_debug_wiring.py', encoding='utf-8').read()

# overhead toggles: four per-item, each default false, each with a chip + handler
print('T1_toggles_default_off=', all(
    re.search(r'property\s+bool\s+%s\s*:\s*false' % n, p5)
    for n in ('overlayBoxes', 'overlayRoi', 'overlayLabels', 'overlayMasks')))
print('T1_toggle_chips=', all(
    ('objectName: "%s"' % n) in p5 for n in ('overlayBoxes', 'overlayRoi', 'overlayLabels', 'overlayMasks')))
print('T1_chip_handlers_wired=', all(
    re.search(r'page\.%s\s*=\s*!page\.%s' % (n, n), p5)
    for n in ('overlayBoxes', 'overlayRoi', 'overlayLabels', 'overlayMasks')))
print('T1_all_off_chip=', ('objectName: "overlayAllOff"' in p5) and ('allOff()' in p5) and
      ('overlayAny' in p5))
print('T1_toggles_note_default_close_text=', '默认关闭' in p5)

# debug stroke tokens: defined, consumed by P5, and distinct from alarm red
print('T2_token_yaml=', bool(re.search(r'debug_box:\s*"#00E5FF"', tokens_yaml)) and
      bool(re.search(r'debug_roi:\s*"#FFD400"', tokens_yaml)))
print('T2_token_json=', ('"debug_box": "#00E5FF"' in tokens_json) and ('"debug_roi": "#FFD400"' in tokens_json))
print('T2_token_theme=', ('property color debug_box: "#00E5FF"' in theme) and
      ('property color debug_roi: "#FFD400"' in theme))
print('T2_token_tokens_qml=', ('property color debug_box: "#00E5FF"' in tokens_qml) and
      ('property color debug_roi: "#FFD400"' in tokens_qml))
print('T2_p5_consumes_box_token=', 'Theme.color.debug_box' in p5)
print('T2_p5_consumes_roi_token=', 'Theme.color.debug_roi' in p5)
print('T2_distinct_from_alarm_red=', '#00E5FF' != '#FF6B6B' and '#FFD400' != '#FF6B6B')
# no hex literal in P5 CODE (strip // comments first, same convention as the
# delivered checker — Theme token references must not be bypassed)
print('T2_no_hexliteral_in_p5=', any(
    re.search(r'#[0-9a-fA-F]{3,8}\b', line) for line in
    (l.split('//', 1)[0] for l in p5.splitlines())) is False)

# fields DISPLAYED on P5: boxes/roi + confidence/distance/fps/e2e latency
# /model version/last-result time
print('T3_fields=', all(tok in p5 for tok in ('aiBox', 'aiRoi', 'aiMask', 'aiLabel',
      'confidenceText', 'distanceText', 'fpsText', '端到端延迟', 'modelVersion',
      'lastResultText')))
print('T3_distance_via_boxes_bridge=', 'distanceM' in p5)

# driver isolation in P5: role guard + denied placeholder + hidden content row
print('T4_p5_role_guard=', 'railhmiRole' in p5 and ('railhmiRole !== "driver"' in p5))
print('T4_p5_denied_placeholder=', 'aiDebugDenied' in p5 and '无权限' in p5)
print('T4_p5_row_hidden_for_driver=', re.search(r'visible:\s*page\.allowed', p5) is not None)
print('T4_denied_visible_when_not_allowed=', re.search(r'visible:\s*!page\.allowed', p5) is not None)
# shell isolation: nav filtered by role, direct navigation to ai_debug for driver denied
print('T4_main_routes_routed=', 'if (id === "ai_debug") return "pages/P5AiDebug.qml"' in main)
print('T4_main_denied_view=', 'DeniedView' in main)
print('T4_main_nav_filtered=', 'entries: railhmiNav' in main)
print('T4_main_role_prop=', 'railhmiRole' in main)
# core permission source: driver cannot access AiDebug
print('T5_core_driver_cannot=', 'case Role::Driver:' in nav and 'return page == Page::Realtime || page == Page::AlarmCenter || page == Page::Replay;' in nav)
print('T5_core_visible_helper=', 'ai_debug_visible' in inf)
# other pages contain no debug overlay identifiers (driver main view clean)
overlay_ids = ('aiBox', 'aiRoi', 'aiMask', 'aiLabel')
print('T6_p1_no_debug_overlays=', not any(tok in p1 for tok in overlay_ids))
# bridge: distanceText and boxes[].distanceM wired to QML from build_ai_debug
print('T7_bridge_distance_text=', 'm["distanceText"]' in mc)
print('T7_bridge_distance_m=', 'bm["distanceM"]' in mc)
print('T7_bridge_from_build_ai_debug=', 'build_ai_debug' in mc)
print('T7_bridge_snapshot_fields=', all(x in mc for x in ('fpsText', 'latencyMs', 'lastResultText', 'confidenceText', 'modelVersion')))
# tokens generated + config check wired into TestRun pipeline
print('T8_board_build_runs_check=', 'check_ai_debug_wiring.py' in bb and 'AIDBG_WIRE_EXIT' in bb)
print('T8_board_build_uses_tokens_yaml=', '--tokens design_tokens.yaml' in bb)
# implementer checker still covers the four-toggle + token contract
print('T8_checker_covers=', all(x in check_tools for x in ('overlayBoxes', 'overlayRoi', 'overlayLabels', 'overlayMasks', 'debug_box', 'debug_roi', 'aiDebugDenied')))
'''

SCAN_KEYS = [
    "T1_toggles_default_off", "T1_toggle_chips", "T1_chip_handlers_wired",
    "T1_all_off_chip", "T1_toggles_note_default_close_text",
    "T2_token_yaml", "T2_token_json", "T2_token_theme", "T2_token_tokens_qml",
    "T2_p5_consumes_box_token", "T2_p5_consumes_roi_token",
    "T2_distinct_from_alarm_red", "T2_no_hexliteral_in_p5",
    "T3_fields", "T3_distance_via_boxes_bridge",
    "T4_p5_role_guard", "T4_p5_denied_placeholder", "T4_p5_row_hidden_for_driver",
    "T4_denied_visible_when_not_allowed", "T4_main_routes_routed",
    "T4_main_denied_view", "T4_main_nav_filtered", "T4_main_role_prop",
    "T5_core_driver_cannot", "T5_core_visible_helper", "T6_p1_no_debug_overlays",
    "T7_bridge_distance_text", "T7_bridge_distance_m", "T7_bridge_from_build_ai_debug",
    "T7_bridge_snapshot_fields", "T8_board_build_runs_check",
    "T8_board_build_uses_tokens_yaml", "T8_checker_covers",
]


def ext_run(client, args, wd, remote_dir, note) -> int:
    res = rrun(client, "mkdir -p %s/verify" % remote_dir, 30)
    note("mkdir-verify", res)
    res2 = rrun(client, "rm -rf %s && mkdir -p %s" % (wd, wd), 30)
    note("mkdir-workdir", res2)
    sftp = client.open_sftp()
    sftp.get_channel().settimeout(120)
    try:
        sftp.put(args.cpp, "%s/verify_ai21_ext.cpp" % wd)
        note("upload-fixture", {"exit": 0, "output": args.cpp})
    finally:
        sftp.close()
    compile_cmd = ("cd %s && g++ -std=c++17 -O1 -Wall -Wextra "
                   "-DRAILHMI_CONFIG_DIR='\"config\"' "
                   "-Isrc/core/include -Isrc/platform/include "
                   "%s/verify_ai21_ext.cpp build-core/src/core/librailhmi_core.a "
                   "build-core/src/platform/librailhmi_platform.a "
                   "-o %s/verify_ai21_ext 2>&1; echo GXX_EXIT=$?"
                   % (remote_dir, wd, wd))
    cext = rrun(client, compile_cmd, 240)
    note("compile-ext", cext)
    if cext.get("exit") != 0 or "GXX_EXIT=0" not in cext.get("output", ""):
        return 1
    rext = rrun(client, "cd %s && %s/verify_ai21_ext; echo EXT_EXIT=$?"
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
