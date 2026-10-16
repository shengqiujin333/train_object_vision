# -*- coding: utf-8 -*-
"""ITEM-022 verification round — independent board-side re-run (verifier-owned).

Work item: 设备维护页 (ITEM-022).
Subject: implementer fix round commit f6acafb, stamp
20261016-ITEM022-fix-r1 (617/0 core-tests incl. D-01 reconnect log +
D-02 day-key/rolling assertions, maintenance-probe PASS with
type=maint-log action=reconnect + type=log-days, wiring scan).

Re-verifies the v3 TEST_FAIL findings:
  D-01 manual reconnect entry (per-camera reconnect control +
  Q_INVOKABLE reconnect that executes, counts and writes a
  action=reconnect maintenance log row).
  D-02 day-rolling maintenance logs (core log_day_key / roll_logs_by_day,
  bridge logsByDay grouping, P6 day-grouped log card).

Steps (all finite timeouts):
  1. Re-run the deliverable board_build.py --mode TestRun entry as a child
     process with a verifier stamp + --keep-remote; archive fresh logs and
     the JUnit XML (the remote build then backs the verifier ext fixture).
  2. Line-by-line [PASS] comparison: fresh core-tests log vs implementer r1
     archived log + independent JUnit parse (612/0) with an exact ITEM-022
     maintenance case enumeration.
  3. Independent assertions on the fresh maintenance-probe log, expectations
     derived from ptz_check/recording_status/health_text formulas (NOT from
     the implementer's archived text) + cross-round byte identity vs the
     implementer's archived r1 log (determinism).
  4. Verifier ext fixture (verify_maint22_ext.cpp) compiled against THIS
     round's build-core static libs: tolerance boundary/inclusive <=,
     verifier-side delta recomputation, action/log matrix, front-AI truth
     table, maintenance rows (stale boundary/missing runtime/Degraded/
     reconnect text/quality combos), recording/health formatting and
     channel-view helper regression.
  5. Verifier wiring scan of the delivered sources on the fresh remote copy
     (not the implementer's checker): P6 section cards, PTZ target vs actual
     display + check + front AI, home/preset controls + confirm modal,
     recentLogs, NVR card with no auto-modify entry, camera/AI fields,
     bridge delegating to core, main.cpp injection, board_build.py pipeline;
     PLUS two source-level defect probes (manual reconnect entry absent,
     maintenance log not day-rolling) reported as findings.
  6. Determinism: fresh re-run of the maintenance probe diff IDENTICAL.
  7. Regression artifacts from the same TestRun (sim-inject/status/alarm/
     clip/replay/ai probes present).
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
    ap = argparse.ArgumentParser(description="ITEM-022 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify022")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--impl-testrun-log", required=True,
                    help="implementer r1 core-tests log for line comparison")
    ap.add_argument("--impl-maintprobe-log", required=True,
                    help="implementer r1 maintenance-probe log for byte-identity check")
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
                        "notes": ["ITEM-022: device maintenance page; "
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
        pl = outdir / ("build-TestRun-%s-%s.core-evidence_maintenance-probe-%s.log"
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
        expected_pass = 617
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
            found = [c for c in ITEM022_CASES if c in names_all]
            missing = [c for c in ITEM022_CASES if c not in names_all]
            junit_ok = (n_tests == "617" and n_fail == "0"
                        and len(found) == len(ITEM022_CASES))
            note("junit-parse", {"exit": 0 if junit_ok else 1,
                 "output": "junit tests=%s failures=%s item022_cases=%d/%d missing=%s"
                           % (n_tests, n_fail, len(found), len(ITEM022_CASES), missing)})
            if not junit_ok:
                ok = False
        else:
            note("junit-parse", {"exit": None, "output": "no fresh junit xml"})
            ok = False

        # 3. independent maintenance-probe assertions (formulas) + byte identity
        if pl.is_file():
            fresh_text = pl.read_text(encoding="utf-8", errors="replace")
            fails = assert_maintenance_probe(fresh_text)
            note("maint-probe-assert", {"exit": 0 if not fails else 1,
                 "output": json.dumps({"fails": fails}, ensure_ascii=False)})
            if fails:
                ok = False
            impl_probe_text = Path(args.impl_maintprobe_log).read_text(
                encoding="utf-8", errors="replace")
            identical = impl_probe_text.splitlines() == fresh_text.splitlines()
            note("maint-probe-compare", {"exit": 0 if identical else 1,
                 "output": "cross-round deterministic reproduction identical=%s" % identical})
            if not identical:
                ok = False
        else:
            note("maint-probe-assert", {"exit": None, "output": "missing fresh maintenance-probe"})
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
            sftp.putfo(io.BytesIO(scan_py.encode("utf-8")), "%s/verify_scan22.py" % wd)
            note("upload-scan", {"exit": 0, "output": "verify_scan22.py"})
        finally:
            sftp.close()
        scan = rrun(client, "cd %s && python3 %s/verify_scan22.py; echo SCAN22_EXIT=$?"
                    % (remote_dir, wd), 60)
        note("wiring-scan", scan)
        so = scan.get("output", "")

        def svar(name):
            m3 = re.search(r"%s\s*=\s*(\S+)" % re.escape(name), so)
            return m3.group(1) if m3 else None

        wants = {k: "True" for k in SCAN_KEYS}
        observed = {k: svar(k) for k in wants}
        mismatch = sorted(k for k, v in wants.items() if observed[k] != v)
        scan_exit_ok = scan.get("exit") == 0 and "SCAN22_EXIT=0" in so
        wiring_ok = not mismatch and scan_exit_ok
        note("wiring-scan-findings", {"exit": 0 if wiring_ok else 1,
             "output": "mismatch=%s scan_exit_ok=%s raw=%s"
                       % (mismatch, scan_exit_ok, json.dumps(observed, ensure_ascii=False))})
        if not wiring_ok:
            ok = False

        # 5b. source-level defect probes (reported findings, part of evidence)
        d1 = svar("D1_manual_reconnect_entry_present")
        d2 = svar("D2_log_day_rolling_present")
        note("defect-probes", {"exit": 0,
             "output": "D1_manual_reconnect_entry_present=%s "
                       "D2_log_day_rolling_present=%s" % (d1, d2)})

        # 6. determinism: re-run the probe and diff
        n2 = rrun(client, "cd %s && mkdir -p verify && ./build-core/tools/railhmi-maintenance-probe "
                          "--out verify/maintenance-probe-rep.log; echo MAINTREP_EXIT=$?"
                  % remote_dir, 60)
        note("maint-probe-rep-run", n2)
        if "MAINTREP_EXIT=0" not in n2.get("output", ""):
            ok = False
        n2b = rrun(client, "cd %s && diff verify/maintenance-probe-rep.log "
                           "core-evidence/maintenance-probe-%s.log && echo IDENTICAL"
                   % (remote_dir, stamp), 30)
        note("maint-probe-determinism", n2b)
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
                  and "maintenance-probe" in reg.get("output", ""))
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
            {"independent_recheck": ("fix-round re-verify: deliverable TestRun entry "
                                     "re-run ok, core-tests 617 line-identical + JUnit "
                                     "617/0 (16 ITEM-022 cases), maintenance-probe "
                                     "formula oracle + determinism, ext fixtures and "
                                     "wiring scan both green on-board; v3 D-01/D-02 "
                                     "re-examined below"),
             "confirmed": ok},
            {"D-01_P2": ("fix: per-camera 重连 control (reconnect-<id> -> "
                         "doReconnect -> Q_INVOKABLE reconnect) executing, counting "
                         "and writing an action=reconnect maintenance log row; "
                         "reconnectText consumed in P6"),
             "present": d1 == "True"},
            {"D-02_P3": ("fix: core log_day_key (UTC YYYY-MM-DD) + roll_logs_by_day "
                         "keep-latest-N-days, bridge logsByDay(7) grouped rows, P6 "
                         "day-grouped log card (maintLogDay + entries)"),
             "present": d2 == "True"},
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


# --- maintenance-probe log parsing with formula-derived expectations -----------

# shape (from maintenance_probe.cpp, static deterministic inputs):
#   type=ptz-check result_ok=ok text="Pan Δ0.20° Tilt Δ0.30° Zoom Δ0.05 → 容差内"
#   type=ptz-check result_bad=out_of_tolerance text="Pan Δ0.20° Tilt Δ4.00° Zoom Δ0.05 → 超差"
#   type=action kind=<kind> confirm=true applied=true result=<ok|out_of_tolerance>
#   type=maint-log action=<kind> user=maintainer result=<r> detail="<label> <check text>"
#   type=front-ai ptz_ok=READY ptz_over=NOT_READY
#   type=cam ... (note "fps=25.0 fps last=..." — fps_text carries a trailing unit)
#   type=recording running=true used_pct=75 free=5.0 GB state=录像中
#   type=health text="NPU 42% / CPU 33% / 温度 58°C / 网络错误 3"
#   type=acceptance ... result=PASS


def assert_maintenance_probe(text: str) -> list:
    fails = []
    fails = []

    def want_line(rx):
        if not re.search(rx, text, re.M):
            fails.append("missing line: %s" % rx)

    # ptz tolerance from probe inputs: good (0.2,−0.3,1.05 vs 0,0,1 → 0.20/0.30/0.05
    # ≤ 1.0/1.0/0.1 → 容差内) and bad (tilt 4.0 → 0.20/4.00/0.05 → 超差)
    want_line(r'^type=ptz-check result_ok=ok text="Pan Δ0\.20° Tilt Δ0\.30° Zoom Δ0\.05 → 容差内"$')
    want_line(r'^type=ptz-check result_bad=out_of_tolerance '
              r'text="Pan Δ0\.20° Tilt Δ4\.00° Zoom Δ0\.05 → 超差"$')
    # actions: both require confirm and applied, goodActionResult ok / bad out
    want_line(r'^type=action kind=home confirm=true applied=true result=ok$')
    want_line(r'^type=action kind=preset_front confirm=true applied=true result=ok$')
    want_line(r'^type=action kind=preset_front result=out_of_tolerance$')
    # logs cover both ok and failed action
    want_line(r'^type=maint-log action=home user=maintainer result=ok '
              r'detail="一键归位 Pan Δ0\.20° Tilt Δ0\.30° Zoom Δ0\.05 → 容差内"$')
    want_line(r'^type=maint-log action=preset_front result=out_of_tolerance '
              r'detail="重调前方预置位 Pan Δ0\.20° Tilt Δ4\.00° Zoom Δ0\.05 → 超差"$')
    # fix round (D-01/D-02), expectations derived from the probe source:
    #   generic log row action=reconnect/target=CAM-02/result=ok/detail=手动重连
    want_line(r'^type=maint-log action=reconnect target=CAM-02 result=ok detail="手动重连"$')
    #   day rolling: 4 logs total; ts 1760000000000 ms -> UTC 2025-10-09 (3 rows:
    #   home/preset_front/reconnect), the 5-day-old row -> 2025-10-04; with
    #   max_days=1 only 2025-10-09 (3 rows) survives, so total=4 rolled=3
    want_line(r'^type=log-days total=4 rolled=3 max_days=1 day_key=2025-10-09$')
    # front AI readiness: PTZ ok -> READY; over-tolerance -> NOT_READY (交互 §5.6)
    want_line(r'^type=front-ai ptz_ok=READY ptz_over=NOT_READY$')

    # camera rows: formula from probe inputs (now=100000)
    #   CAM-01 Live fps25 last=99500 → 在线/实时/25.0 fps/<1 秒前/正常
    #   CAM-02 Frozen last=90000+quality.too_dark → 离线/画面冻结/—/10 秒前/过暗
    # fps value carries a trailing unit ("25.0 fps") — match it lazily up to " last="
    cam_rx = r"^type=cam id=(\S+) online=(\w+) state=(\S+) fps=(.+?) last=(.+?) quality=(\S+)$"
    cams = re.findall(cam_rx, text, re.M)
    if len(cams) != 2:
        fails.append("cam lines=%d want 2" % len(cams))
    want = {
        "CAM-01": {"online": "true", "state": "实时", "fps": "25.0 fps",
                    "last": "<1 秒前", "quality": "正常"},
        "CAM-02": {"online": "false", "state": "画面冻结", "fps": "—",
                    "last": "10 秒前", "quality": "过暗"},
    }
    for cam_id, online, state, fps, last, quality in cams:
        w = want.get(cam_id)
        if not w:
            fails.append("unexpected cam %s" % cam_id)
            continue
        if online != w["online"]:
            fails.append("%s online=%s want %s" % (cam_id, online, w["online"]))
        if state != w["state"]:
            fails.append("%s state=%s want %s" % (cam_id, state, w["state"]))
        if fps != w["fps"]:
            fails.append("%s fps=%r want %r" % (cam_id, fps, w["fps"]))
        if last != w["last"]:
            fails.append("%s last=%r want %r" % (cam_id, last, w["last"]))
        if quality != w["quality"]:
            fails.append("%s quality=%r want %r" % (cam_id, quality, w["quality"]))

    # recording: (20GiB-5GiB)/20GiB=75%, free 5.0 GB, state 录像中
    want_line(r"^type=recording running=true used_pct=75 free=5\.0 GB state=录像中$")
    # health text with the four probe values
    want_line(r'^type=health text="NPU 42% / CPU 33% / 温度 58°C / 网络错误 3"$')
    # acceptance flags (five observable booleans incl. fix-round ones) + PASS
    want_line(r"^type=acceptance ptz_within_ok=true ptz_over_detected=true home_logged=true "
              r"reconnect_logged=true log_day_rolling=true "
              r"over_tolerance_blocks_front_ai=true result=PASS$")
    # inverse guard: a fresh log set that did NOT roll (dropping the 2025-10-04
    # row) would show rolled=4 -> the delivered rolling gate must exclude the
    # old day; assert the exact sibling line absent to catch a no-op rolling.
    if re.search(r"^type=log-days total=4 rolled=4\b", text, re.M):
        fails.append("rolling no-op: rolled=4 observed (old day not dropped)")
    if re.search(r"^type=acceptance\b.*\breconnect_logged=false\b", text, re.M):
        fails.append("reconnect not logged (reconnect_logged=false)")
    return fails


ITEM022_CASES = [
    "within tolerance -> ok",
    "over tolerance -> out",
    "home requires confirm then applied",
    "ptz log action/user",
    "ptz log result ok",
    "failed action logged as out_of_tolerance",
    "ptz over-tolerance -> front AI NOT ready",
    "live camera row",
    "frozen + quality flags",
    "recording used pct",
    "health text",
    # fix round (D-01/D-02) new cases from the delivered test_maintenance.cpp
    "log day key (D-02)",
    "roll keeps latest 2 days (D-02)",
    "rolled entries are the most recent days",
    "max_days=0 keeps all",
    "generic maintenance log (reconnect)",
]


def parse_core_tests_log(text: str) -> dict:
    return {"pass": [l.strip() for l in text.splitlines() if l.startswith("[PASS]")],
            "fail": [l.strip() for l in text.splitlines() if l.startswith("[FAIL]")],
            "result": next((l for l in text.splitlines() if l.startswith("RESULT:")), "")}


# --- verifier wiring scan (delivered sources, fresh remote copy) -----------------

SCAN_PY = r'''import os, re
p6 = open('src/ui/qml/pages/P6Maintenance.qml', encoding='utf-8').read()
mainq = open('src/ui/qml/main.qml', encoding='utf-8').read()
mc = open('src/ui/src/main.cpp', encoding='utf-8').read()
bridge = open('src/ui/src/maintenance_model.cpp', encoding='utf-8').read()
bridge_h = open('src/ui/src/maintenance_model.h', encoding='utf-8').read()
bb = open('tools/board_build.py', encoding='utf-8').read()
check_tools = open('tools/check_maintenance_wiring.py', encoding='utf-8').read()

CARD_RE = r'objectName:\s*"%s"'
def card(n):
    return re.search(CARD_RE % n, p6) is not None

# T1: seven section cards per visual v2 §8.6
print('T1_cards=', all(card(n) for n in (
    'maintCameraCard', 'maintAiCard', 'maintRecordingCard', 'maintHealthCard',
    'maintPtzCard', 'maintLogCard', 'maintNvrCard')))

# T2: PTZ target vs actual + tolerance check + front AI readiness displayed
print('T2_ptz_values_display=', ('panActual' in p6 and 'panTarget' in p6 and
      'tiltActual' in p6 and 'tiltTarget' in p6 and
      'zoomActual' in p6 and 'zoomTarget' in p6 and 'Pan 实际' in p6 and '目标' in p6))
print('T2_ptz_check_text=', 'ptzCheckText' in p6 and
      ('容差' in p6 or 'checkText' in p6) and 'check.ok' in p6)
print('T2_front_ai_text=', 'ptzFrontAi' in p6 and 'NOT_READY' in p6 and 'READY' in p6)

# T3: home/preset controls + secondary confirmation modal + reset + reload
print('T3_controls=', 'ptzHome' in p6 and 'ptzPreset' in p6 and
      '一键归位' in p6 and '重调前方预置位' in p6)
print('T3_calls=', 'askPtz("home")' in p6 and 'askPtz("preset_front")' in p6)
print('T3_modal=', 'ptzConfirmModal' in p6 and 'pendingKind.length > 0' in p6 and
      re.search(r'visible:\s*page\.pendingKind\.length\s*>\s*0', p6) is not None)
print('T3_confirm_buttons=', 'objectName: "ptzConfirmYes"' in p6 and
      'objectName: "ptzConfirmNo"' in p6 and
      re.search(r'onClicked:\s*page\.confirmPtz\(\)', p6) is not None and
      re.search(r'onClicked:\s*page\.pendingKind\s*=\s*""', p6) is not None)
print('T3_confirm_executes=',
      re.search(r'page\.ptzResult\s*=\s*M\.ptzAction\(page\.pendingKind\)', p6) is not None)
print('T3_reload_after=', re.search(
    r'function confirmPtz\(\)[\s\S]*?page\.pendingKind = ""[\s\S]*?reload\(\)', p6) is not None)

# T4 maintenance log list consumes logsByDay (fix D-02): day-grouped card with
# day heading (maintLogDay with day+count) and per-entry column rendering.
print('T4_recent_logs=', 'logsByDay(' in p6 and 'maintLogDay' in p6 and
      'modelData.day' in p6 and 'modelData.count' in p6 and
      'modelData.entries' in p6 and 'modelData.action' in p6 and
      'modelData.user' in p6 and 'modelData.result' in p6 and
      'modelData.detail' in p6)

# T5 NVR card: registered/state/channel mapping shown; NO auto-modify entry
print('T5_nvr_card=', 'maintNvrCard' in p6 and 'channelMapping' in p6 and
      'modelData.registered' in p6 and 'modelData.state' in p6 and
      'DeviceCard' in p6)
code_lines = [line.split('//', 1)[0] for line in p6.splitlines()]
code = '\n'.join(code_lines)
print('T5_no_auto_modify=', '修改' not in code and 'setChannel' not in code and
      'editNvr' not in code)

# T6 camera card visible fields (online/state/fps/last frame/quality)
print('T6_camera_fields=', 'railhmiMaintenanceCameras' in p6 and
      'modelData.online' in p6 and 'modelData.stateLabel' in p6 and
      'modelData.fpsText' in p6 and 'modelData.lastFrameText' in p6 and
      'modelData.qualityLabel' in p6)

# T7 AI card fields status/model version/FPS/last result
print('T7_ai_fields=', 'railhmiAiDebug' in p6 and
      'modelData.modelVersion' in p6 and 'modelData.fpsText' in p6 and
      'modelData.lastResultText' in p6 and 'modelData.available' in p6)

# T8 reconnect count bridged in main.cpp AND consumed by P6 (fix D-01): the
# v3 round recorded consumed_in_p6=False as the D-01 companion observation;
# the fix must flip it to True.
print('T8_reconnect_text_bridged_in_main=', 'reconnectText' in mc)
print('T8_reconnect_text_consumed_in_p6=', 'reconnectText' in p6)

# T9 bridge delegates actions/logs/readiness to delivered core
print('T9_bridge_core_calls=', all(x in bridge for x in (
    'ptz_check', 'ptz_home', 'ptz_recall_front_preset', 'build_ptz_log',
    'front_ai_ready')) and 'seed(' in bridge_h)
print('T9_main_injects=', 'railhmiMaintenance' in mc and
      'railhmiMaintenanceCameras' in mc and
      'RailhmiMaintenanceModel' in mc and 'build_maintenance_cameras' in mc)

# T10 pipeline: TestRun runs the implementer checker + downloads its probe log
print('T10_board_build=', 'check_maintenance_wiring.py' in bb and
      'MAINT_WIRE_EXIT' in bb and 'railhmi-maintenance-probe' in bb and
      'MAINT_EXIT' in bb)
print('T10_checker_covers=', all(x in check_tools for x in (
    'ptzCheckText', 'ptzConfirmModal', 'channelMapping', 'logsByDay',
    'reconnect-')))

# T11 tokens-only colors: P6 has no hardcoded hex in code (strip same as checker)
print('T11_no_hexliteral_in_p6=', any(
    re.search(r'#[0-9a-fA-F]{3,8}\b', line) for line in code_lines) is False)
print('T11_theme_usage=', 'Theme.color.' in p6)

# D-probe 1 (D-01, fix round): manual reconnect must now be a REAL entry — a
# per-camera named control (reconnect-<id>) wired to a bridge reconnect
# invokable. Passive display of reconnectText alone does not count.
reconnect_ctl = ((re.search(r'objectName:\s*"reconnect-"\s*\+\s*modelData\.id', p6) is not None) and
                 re.search(r'function\s+doReconnect\s*\(', p6) is not None and
                 re.search(r'Q_INVOKABLE[^\n]*reconnect\s*\(', bridge_h) is not None)
print('D1_manual_reconnect_entry_present=', reconnect_ctl)

# D-probe 2 (D-02, fix round): day-rolling must be delivered in core AND
# shown by the page: core roll_logs_by_day + bridge logsByDay grouped display.
day_roll = (re.search(r'roll_logs_by_day|log_day_key', bridge) is not None
            and 'logsByDay' in bridge_h and 'logsByDay(' in p6
            and 'maintLogDay' in p6)
print('D2_log_day_rolling_present=', day_roll)
'''

SCAN_KEYS = [
    "T1_cards", "T2_ptz_values_display", "T2_ptz_check_text", "T2_front_ai_text",
    "T3_controls", "T3_calls", "T3_modal", "T3_confirm_buttons",
    "T3_confirm_executes", "T3_reload_after",
    "T4_recent_logs", "T5_nvr_card", "T5_no_auto_modify", "T6_camera_fields",
    "T7_ai_fields", "T8_reconnect_text_bridged_in_main",
    "T8_reconnect_text_consumed_in_p6",
    "T9_bridge_core_calls",
    "T9_main_injects", "T10_board_build", "T10_checker_covers",
    "T11_no_hexliteral_in_p6", "T11_theme_usage",
    "D1_manual_reconnect_entry_present", "D2_log_day_rolling_present",
]


def ext_run(client, args, wd, remote_dir, note) -> int:
    res = rrun(client, "mkdir -p %s/verify" % remote_dir, 30)
    note("mkdir-verify", res)
    res2 = rrun(client, "rm -rf %s && mkdir -p %s" % (wd, wd), 30)
    note("mkdir-workdir", res2)
    sftp = client.open_sftp()
    sftp.get_channel().settimeout(120)
    try:
        sftp.put(args.cpp, "%s/verify_maint22_ext.cpp" % wd)
        note("upload-fixture", {"exit": 0, "output": args.cpp})
    finally:
        sftp.close()
    compile_cmd = ("cd %s && g++ -std=c++17 -O1 -Wall -Wextra "
                   "-DRAILHMI_CONFIG_DIR='\"config\"' "
                   "-Isrc/core/include -Isrc/platform/include "
                   "%s/verify_maint22_ext.cpp build-core/src/core/librailhmi_core.a "
                   "build-core/src/platform/librailhmi_platform.a "
                   "-o %s/verify_maint22_ext 2>&1; echo GXX_EXIT=$?"
                   % (remote_dir, wd, wd))
    cext = rrun(client, compile_cmd, 240)
    note("compile-ext", cext)
    if cext.get("exit") != 0 or "GXX_EXIT=0" not in cext.get("output", ""):
        return 1
    rext = rrun(client, "cd %s && %s/verify_maint22_ext; echo EXT_EXIT=$?"
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
