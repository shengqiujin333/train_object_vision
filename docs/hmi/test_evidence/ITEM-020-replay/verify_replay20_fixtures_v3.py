# -*- coding: utf-8 -*-
"""ITEM-020 verification round v3 — independent board-side re-run (verifier-owned).

Round v3 targets the UI-review FIX round (stamp 20261016-ITEM020-uifix-r1),
addressing web_frontend.ui_implementation_review TEST_FAIL D-01..D-09
(REVIEW-ITEM-020.md):
  D-01 keyframes: bridge timeline() exports keyframes ({ts,thumb}); P3 renders a
                  64px thumbnail strip positioned by ts (keyframeThumb)
  D-02 gap hatch: P3 gap uses a Canvas hatch pattern + "录像缺失"
  D-03 playback:  play/pause toggle + speed chips (0.5×/1×/2×) wired to bridge
                  playback()/setPlaying()/setSpeed() over core PlaybackState
  D-04 levels:    AlarmMark/ts+level -> TimelineSegment.level & marker.level
                  exported by the bridge; P3 colors by Theme.alarm.<lvl>
  D-05 click:     segments/gaps/markers have their own MouseAreas (seek / select)
  D-06 cameras:   P3 uses the full railhmiCameras list; relatedCameras is only
                  the entry-time default lock
  D-07 detail:    detailRecordBar status bar + independent note field
  D-08 date:      dateError feedback (no silent epoch day) + FilterChip
                  touch_target_min with keyboard focus/keys
  D-09 sizes:     track 32px, keyframe strip 64px, cursor-top mono time

Steps (all finite timeouts):
  1. Re-run the deliverable board_build.py --mode TestRun entry as child process
     with verifier stamp + --keep-remote and archive fresh logs + JUnit XML.
  2. Line-by-line [PASS] comparison: fresh core-tests log vs implementer
     uifix-r1 archived log + independent JUnit parse (579/0) with an exact
     ITEM-020 replay-domain case enumeration (41 named cases = 36 v2 + 5 new).
  3. Independent assertions on the fresh replay-probe log (v3 line format):
     everything from v2 plus seg level, alarm_marker_level, keyframe_thumb,
     detail record/record_ok; cross-round line identity vs the implementer's
     archived uifix-r1 probe log.
  4. Verifier ext fixture v3 (verify_replay20_ext_v3.cpp) compiled against THIS
     round's build-core static libs: all v2 checks (regression, with the D-07
     contract change asserted explicitly) + v3 level/thumbnail/note additions.
  5. QML/C++ wiring scan v3 of the delivered sources on the fresh remote copy —
     v2 fix state must still hold AND the UI-fix state must hold (hatch canvas,
     playback wiring, level coloring functions, click handlers, full camera
     list, record bar + note, dateError path, chip touch target + keyboard,
     sizes, cursor-top mono time).
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
    ap = argparse.ArgumentParser(description="ITEM-020 verification fixtures (v3)")
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
                    help="implementer uifix-r1 core-tests log for line comparison")
    ap.add_argument("--impl-replayprobe-log", required=True,
                    help="implementer uifix-r1 replay-probe log for line comparison")
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
                        "notes": ["v3: asserts the UI-fix D-01..D-09 state; aborts on "
                                  "fingerprint mismatch; no credentials logged"],
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
        expected_pass = 579
        if tl.is_file() and args.impl_testrun_log:
            impl = parse_core_tests_log(Path(args.impl_testrun_log).read_text(
                encoding="utf-8", errors="replace"))
            ver = parse_core_tests_log(tl.read_text(encoding="utf-8", errors="replace"))
            same = impl["pass"] == ver["pass"]
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
            junit_ok = (n_tests == "579" and n_fail == "0" and len(found) == len(ITEM020_CASES))
            note("junit-parse", {"exit": 0 if junit_ok else 1,
                 "output": "junit tests=%s failures=%s item020_cases=%d/%d missing=%s"
                           % (n_tests, n_fail, len(found), len(ITEM020_CASES), missing)})
            if not junit_ok:
                ok = False
        else:
            note("junit-parse", {"exit": None, "output": "no fresh junit xml"})
            ok = False

        # 3. independent assertions on the fresh replay-probe log (v3 line format)
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

        # 4. ext fixture v3 compile+run against THIS round's static libs
        if ext_run(client, args, wd, remote_dir, note) != 0:
            ok = False

        # 5. QML/C++ wiring scan v3 of delivered sources (fresh remote copy, stack-run)
        scan_py = r'''import re
main = open('src/ui/qml/main.qml', encoding='utf-8').read()
p3 = open('src/ui/qml/pages/P3Replay.qml', encoding='utf-8').read()
p2 = open('src/ui/qml/pages/P2AlarmCenter.qml', encoding='utf-8').read()
mc = open('src/ui/src/main.cpp', encoding='utf-8').read()
chip = open('src/ui/qml/components/FilterChip.qml', encoding='utf-8').read()
bridge = open('src/ui/src/replay_model.cpp', encoding='utf-8').read()

def fbody(text, name):
    m = re.search(r'function\s+%s\s*\([^)]*\)\s*\{' % re.escape(name), text)
    if not m: return ''
    i = m.end(); depth = 1; out = []
    while i < len(text) and depth > 0:
        ch = text[i]
        if ch == '{': depth += 1
        elif ch == '}':
            depth -= 1
            if depth == 0: break
        out.append(ch); i += 1
    return ''.join(out)

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
# v2-state must persist
print('V2_objectnames=', all(o in p3 for o in ('timelineTrack', 'timelineCursor', 'alarmSegment', 'timelineGap', 'alarmMarker', 'keyframeStrip')))
print('V2_jump_chips=', all(c in p3 for c in ('-30s', '报警时刻', '+30s', '+60s')))
print('V2_detail_fields=', all(c in p3 for c in ('时间：', '类型：', '级别：', '距离：', '置信度：', '处理状态：', '备注：')))
print('V2_source_note_ui=', 'sourceStatus' in p3)
print('V2_selection_consumed=', all(x in p3 for x in ('selectedCameraId', 'selectedAlarmTs', 'relatedCameras', 'selectedDetail')))
# v3 D-01 keyframes
print('V3_bridge_keyframes=', '"keyframes"' in bridge)
print('V3_p3_thumb_repeater=', 'keyframeThumb' in p3)
print('V3_thumb_ts_positioned=', re.search(r'x:\s*page\.xOf\(modelData\.ts\)', p3) is not None)
print('V3_thumb_strip_64=', 'status_strip_height' in p3)
print('V3_sample_keyframes=', 'sampleKeyframes' in p3)
# v3 D-02 hatch
print('V3_gap_hatch_canvas=', bool(re.search(r'objectName:\s*"timelineGap"', p3)) and 'onPaint' in p3 and ('strokeStyle' in p3))
# v3 D-03 playback wiring
print('V3_bridge_playback=', ('playback()' in bridge or 'QVariantMap RailhmiReplayModel::playback' in bridge))
print('V3_bridge_setplaying=', 'setPlaying' in bridge)
print('V3_bridge_setspeed=', 'setSpeed' in bridge)
print('V3_p3_playback_toggle=', 'playbackToggle' in p3)
print('V3_p3_speed_chips=', all(("speed-%s" % s) in p3 for s in ("0.5", "1", "2")) and 'setSpeed(' in p3 and 'togglePlay' in p3)
print('V3_p3_playback_state=', 'onPlaybackChanged' in p3 and 'playState' in p3)
# v3 D-04 levels
print('V3_bridge_seg_level=', 'sm["level"]' in bridge)
print('V3_bridge_marker_level=', 'am["level"]' in bridge)
print('V3_p3_level_color_fn=', 'levelColorOf' in p3 and 'levelTintOf' in p3)
print('V3_p3_level_theme=', 'Theme.alarm.L1.accent' in p3 and 'Theme.alarm.L2.accent' in p3 and 'Theme.alarm.L3.accent' in p3)
print('V3_p3_seg_uses_level=', re.search(r'levelColorOf\(modelData\.level\)', p3) is not None)
print('V3_p3_marker_uses_level=', re.search(r'objectName:\s*"alarmMarker"', p3) is not None and re.search(r'color:\s*page\.levelColorOf\(modelData\.level\)', p3) is not None)
# v3 D-05 click targets
print('V3_seg_click=', bool(re.search(r'objectName:.*alarmSegment', p3)) and 'seekTo(modelData.t0)' in p3)
print('V3_gap_click=', 'acceptedButtons: Qt.LeftButton' in p3)
print('V3_marker_click=', 'selectAlarmAt' in p3)
# v3 D-06 full camera selection
print('V3_full_camera_list=', 'railhmiCameras' in p3 and 'allCams' in p3)
# v3 D-07 record bar + independent note
print('V3_detail_record_bar=', 'detailRecordBar' in p3 and 'recordLabel' in p3 and 'recordOk' in p3)
print('V3_record_bar_degraded=', 'degraded_bg' in p3)
# v3 D-08 date feedback + chip touch target / keyboard
print('V3_date_error_path=', 'dateError' in p3 and 'valid === false' in p3)
print('V3_chip_touch_target=', 'touch_target_min' in chip)
print('V3_chip_keyboard=', 'activeFocusOnTab' in chip and ('Keys.onReturnPressed' in chip or 'Keys.onEnterPressed' in chip))
# v3 D-09 sizes + cursor top mono
print('V3_track_32=', re.search(r'height:\s*Theme\.size\.button\.sm', p3) is not None and re.search(r'objectName:\s*"timelineTrack"', p3) is not None)
print('V3_cursor_top=', 'cursorTime' in p3 and re.search(r'family\.mono', p3) is not None)
mre = fbody(p3, 'reload')
print('V3_reload_handles_invalid=', 'valid === false' in mre and 'dateError' in mre)
'''
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(60)
        try:
            import io
            sftp.putfo(io.BytesIO(scan_py.encode("utf-8")), "%s/verify_scan_v3.py" % wd)
            note("upload-scan", {"exit": 0, "output": "verify_scan_v3.py"})
        finally:
            sftp.close()
        scan = rrun(client, "cd %s && python3 %s/verify_scan_v3.py; echo SCAN2_EXIT=$?"
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
            "W2_unavailable_state": "True",
            "V2_objectnames": "True", "V2_jump_chips": "True", "V2_detail_fields": "True",
            "V2_source_note_ui": "True", "V2_selection_consumed": "True",
            "V3_bridge_keyframes": "True", "V3_p3_thumb_repeater": "True",
            "V3_thumb_ts_positioned": "True", "V3_thumb_strip_64": "True",
            "V3_sample_keyframes": "True", "V3_gap_hatch_canvas": "True",
            "V3_bridge_playback": "True", "V3_bridge_setplaying": "True",
            "V3_bridge_setspeed": "True", "V3_p3_playback_toggle": "True",
            "V3_p3_speed_chips": "True", "V3_p3_playback_state": "True",
            "V3_bridge_seg_level": "True", "V3_bridge_marker_level": "True",
            "V3_p3_level_color_fn": "True", "V3_p3_level_theme": "True",
            "V3_p3_seg_uses_level": "True", "V3_p3_marker_uses_level": "True",
            "V3_seg_click": "True", "V3_gap_click": "True", "V3_marker_click": "True",
            "V3_full_camera_list": "True", "V3_detail_record_bar": "True",
            "V3_record_bar_degraded": "True", "V3_date_error_path": "True",
            "V3_chip_touch_target": "True", "V3_chip_keyboard": "True",
            "V3_track_32": "True", "V3_cursor_top": "True",
            "V3_reload_handles_invalid": "True",
        }
        observed = {k: svar(k) for k in wants}
        mismatch = sorted(k for k, v in wants.items() if observed[k] != v)
        wiring_ok = not mismatch and scan.get("exit") == 0 and "SCAN2_EXIT=0" in so
        note("wiring-scan-findings", {"exit": 0 if wiring_ok else 1,
             "output": ("UI-fix state: v1/v2 persisted (selection=%s literal-triple-absent=%s "
                        "syncState=%s) | v3 D-01 (bridge_keyframes=%s thumb_repeater=%s "
                        "ts_positioned=%s strip64=%s) | v3 D-02 (hatch=%s) | v3 D-03 "
                        "(playback_bridge=%s toggle=%s speed_chips=%s) | v3 D-04 "
                        "(seg_level=%s marker_level=%s level_fns=%s theme=%s "
                        "seg_uses=%s marker_uses=%s) | v3 D-05 (seg=%s gap=%s marker=%s) | "
                        "v3 D-06 (full_list=%s) | v3 D-07 (record_bar=%s degraded=%s) | "
                        "v3 D-08 (date_error=%s chip_touch=%s chip_kb=%s) | v3 D-09 "
                        "(track32=%s cursor_top_mono=%s) | mismatch=%s raw=%s")
                       % (observed["V2_selection_consumed"], observed["W2_cursor_positions_literal"],
                          observed["W2_syncState_consumed"],
                          observed["V3_bridge_keyframes"], observed["V3_p3_thumb_repeater"],
                          observed["V3_thumb_ts_positioned"], observed["V3_thumb_strip_64"],
                          observed["V3_gap_hatch_canvas"],
                          observed["V3_bridge_playback"], observed["V3_p3_playback_toggle"],
                          observed["V3_p3_speed_chips"],
                          observed["V3_bridge_seg_level"], observed["V3_bridge_marker_level"],
                          observed["V3_p3_level_color_fn"], observed["V3_p3_level_theme"],
                          observed["V3_p3_seg_uses_level"], observed["V3_p3_marker_uses_level"],
                          observed["V3_seg_click"], observed["V3_gap_click"],
                          observed["V3_marker_click"],
                          observed["V3_full_camera_list"],
                          observed["V3_detail_record_bar"], observed["V3_record_bar_degraded"],
                          observed["V3_date_error_path"], observed["V3_chip_touch_target"],
                          observed["V3_chip_keyboard"],
                          observed["V3_track_32"], observed["V3_cursor_top"],
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
            {"UI-review D-01..D-09": ("bridge exports keyframes/levels; P3 renders strip+hatch+"
                                      "playback+level colors+click targets+full cameras+record "
                                      "bar+date error+sizes; v1/v2 selection & sync fix state "
                                      "persisted"),
             "confirmed": not mismatch and wiring_ok},
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


# --- replay-probe log parsing (v3 line format) --------------------------------------

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
    # v3: level= after kind
    "seg": re.compile(r"seg kind=(\S+) level=(\d+) t0=(\d+) t1=(\d+) label=(.*)$"),
    "gap": re.compile(r"gap t0=(\d+) t1=(\d+) reason=(.*)$"),
    # v3: trailing alarm_marker_level / keyframe_thumb (conditional)
    "flags": re.compile(r"flags has_alarm_segment=(\w+) has_gap=(\w+)"
                        r"(?: alarm_marker_level=(\d+))?(?: keyframe_thumb=(\S+))?$"),
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
    # v3: trailing record= record_ok=
    "detail": re.compile(r"type=detail time=(.*?) type=(\S+) level=(\S+) distance=(.*?) "
                         r"confidence=(\S+) ack=(.*?) record=(\S+) record_ok=(\w+)$"),
    "acceptance": re.compile(r"type=acceptance alarm_segment=(\w+) gap=(\w+) jumps_ok=(\w+) "
                             r"sync_prompt_on_deviation=(\w+) lock_related_cameras=(\w+) "
                             r"sync_unavailable_without_clock=(\w+) nvr_fallback_local=(\w+) "
                             r"result=(\w+)$"),
}

# The 41 exact ITEM-020 core-tests case names (34 base+fix + 7 ui-fix-round additions).
# (36 v2 cases + 5 new; the 5 ui-fix additions are matched below in UIFIX_CASES.)
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
    # ui-fix-round additions (uifix-r1)
    "alarm segment carries real level (D-04)",
    "alarm markers carry level (D-04)",
    "keyframe carries thumbnail ref (D-01)",
    "detail record status (D-07)",
    "detail note independent from record status (D-07)",
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
        # v3 tuple: (kind, level, t0, t1, label)
        if s0[0] != "continuous" or s0[1] != "0" or s0[2] != str(P_DAY + 10000) or \
                s0[3] != str(P_DAY + 80000) or s0[4] != "连续录像":
            fails.append("seg0 %r" % (s0,))
        if s1[0] != "critical" or s1[1] != "1" or s1[2] != str(P_DAY + 90000) or \
                s1[3] != str(P_DAY + 200000) or s1[4] != "报警保护片段":
            fails.append("seg1 %r" % (s1,))
        if s2[0] != "continuous" or s2[1] != "0" or s2[2] != str(P_DAY + 260000) or \
                s2[3] != str(P_DAY + 300000):
            fails.append("seg2 %r" % (s2,))
    gaps = p["gaps"]
    if len(gaps) != 1 or gaps[0][0] != str(P_DAY + 210000) or \
            gaps[0][1] != str(P_DAY + 250000) or gaps[0][2] != "recording_interrupted":
        fails.append("gap row %r" % (gaps,))
    # v3 flags tuple: (has_alarm_segment, has_gap, alarm_marker_level, keyframe_thumb)
    if p["flags"] != ("true", "true", "1", "thumb/CAM-01/%d.bmp" % P_DAY):
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
    # fix-round evidence: front alarm locks left/front/right, in probe order
    lk = p["lock"]
    if not lk or lk != ("CAM-01", "CAM-02,CAM-01,CAM-03"):
        fails.append("lock line %r" % (lk,))
    # snapshot built from real offsets — 120/200 within threshold (in sync, dev=200),
    # 900 beyond (exact prompt), no clock -> unavailable with the exact UI text
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
    # v3 tuple: (time, type, level, distance, confidence, ack, record, record_ok)
    if not dt or dt[1] != want["type"] or dt[2] != want["level"] or \
            dt[3] != want["distance"] or dt[4] != want["confidence"] or \
            dt[5] != want["ack"] or not dt[0].startswith("2026-10-16 00:02:") or \
            dt[6] != "可回放" or dt[7] != "true":
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
        sftp.put(args.cpp, "%s/verify_replay20_ext_v3.cpp" % wd)
        note("upload-fixture", {"exit": 0, "output": args.cpp})
    finally:
        sftp.close()
    compile_cmd = ("cd %s && g++ -std=c++17 -O1 -Wall -Wextra "
                   "-DRAILHMI_CONFIG_DIR='\"config\"' "
                   "-Isrc/core/include -Isrc/platform/include "
                   "%s/verify_replay20_ext_v3.cpp build-core/src/core/librailhmi_core.a "
                   "build-core/src/platform/librailhmi_platform.a "
                   "-o %s/verify_replay20_ext_v3 2>&1; echo GXX_EXIT=$?"
                   % (remote_dir, wd, wd))
    cext = rrun(client, compile_cmd, 240)
    note("compile-ext", cext)
    if cext.get("exit") != 0 or "GXX_EXIT=0" not in cext.get("output", ""):
        return 1
    rext = rrun(client, "cd %s && %s/verify_replay20_ext_v3; echo EXT_EXIT=$?"
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
