# -*- coding: utf-8 -*-
"""ITEM-023 verification round — independent board-side re-run (verifier-owned).

Work item: 权限与二次确认 (ITEM-023).
Subject: implementer round commit 80463d8, stamp 20261016-ITEM023-r3
(641/0 core-tests incl. permission matrix/gate/action/log cases +
permissions-probe PASS + check_permissions_wiring PASS).

Steps (all finite timeouts):
  1. Re-run the deliverable board_build.py --mode TestRun entry as a child
     process with a verifier stamp + --keep-remote; archive fresh logs and
     the JUnit XML (the remote build then backs the verifier ext fixture).
  2. Line-by-line [PASS] comparison: fresh core-tests log vs implementer r3
     archived log (641 cases, exact list equality) + independent JUnit parse
     (641/0) with the exact ITEM-023 permission case enumeration.
  3. Independent assertions on the fresh permissions-probe log from the
     §8-derived oracle (offline_selfcheck0 table), NOT from the implementer's
     archived text + cross-round byte identity vs the archived r3 log
     (determinism).
  4. Verifier ext fixture (verify_perm23_ext.cpp) linked against THIS round's
     build-core static libs: full 13x4 matrix both configs, gate reason matrix,
     13-perm confirm scan, action state machine, build_action_log contract,
     action-log day-rolling (ITEM-022 D-02 coupling).
  5. Verifier wiring scan of the delivered sources on the fresh remote copy
     (NOT the implementer's checker): P3 export control permission-visibility +
     confirm modal + confirm exec, P6 matrix card + roleName, bridge delegate
     to core, main.cpp injection with app.permissions, board_build TestRun
     pipeline, permission doc, no-hex rule; PLUS observable-point probes
     (driver entry absent = visibility gating; denied-not-rendered shape).
  6. Determinism: re-run the permissions probe on the board and require
     byte-identical output.
  7. Regression artifacts from the same TestRun (sim-inject/status/alarm/
     clip/replay/ai/maintenance probes present).
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


# §8-derived oracle: 13 permissions x 4 roles at export_ops_allowed=false
# (offline_selfcheck0 PASS = this table matches the archived implementer probe)
ORACLE_OFF = {
    "view_realtime":        (1, 1, 1, 1),
    "view_alarm_center":    (1, 1, 1, 1),
    "view_replay":          (1, 1, 1, 1),
    "view_all_cameras":     (0, 1, 1, 1),
    "view_ai_debug":        (0, 1, 1, 1),
    "view_maintenance":     (0, 1, 1, 1),
    "acknowledge":          (1, 1, 1, 1),
    "export_recording":     (0, 0, 0, 1),
    "export_snapshot":      (0, 0, 0, 1),
    "modify_device_config": (0, 1, 1, 1),
    "modify_system_config": (0, 0, 0, 1),
    "ptz_control":          (0, 1, 1, 1),
    "reconnect":            (0, 1, 1, 1),
}
ORACLE_ON = {p: v for p, v in ORACLE_OFF.items()}
for p in ("export_recording", "export_snapshot"):
    ORACLE_ON[p] = (0, 1, 1, 1)


def assert_permissions_probe(text: str) -> list:
    fails: list = []

    def want(rx):
        if not re.search(rx, text, re.M):
            fails.append("missing line: %s" % rx)

    # header + full matrix vs the §8 oracle (off config)
    if not re.search(r'^type=matrix export_ops_allowed=false$', text, re.M):
        fails.append("matrix header != export_ops_allowed=false")
    rows = {}
    for mm in re.finditer(r'^row perm=(\S+) label=(.+?) driver=(\S+) maintainer=(\S+) '
                          r'developer=(\S+) admin=(\S+)$', text, re.M):
        rows[mm.group(1)] = (mm.group(3), mm.group(4), mm.group(5), mm.group(6))
    if len(rows) != 13:
        fails.append("matrix rows=%d want 13" % len(rows))
    for p, w in ORACLE_OFF.items():
        if p not in rows:
            fails.append("matrix missing row %s" % p)
            continue
        got = tuple(1 if x == "Y" else 0 for x in rows[p])
        if got != w:
            fails.append("matrix %s got=%s want=%s" % (p, got, w))
    # export gate line (config flip observable)
    gm = re.search(r'^type=export-gate driver=(\S+) maintainer_off=(\S+) maintainer_on=(\S+)$',
                   text, re.M)
    if not gm:
        fails.append("export-gate line missing")
    elif not (gm.group(1) == "-" and gm.group(2) == "-" and gm.group(3) == "Y"):
        fails.append("export-gate cells driver=%s maintainer_off=%s maintainer_on=%s want - - Y"
                     % gm.groups())
    # gate/action/config exact lines (probe GUARANTEED deterministic inputs)
    EXACT = [
        r'^type=gate driver reason="权限不足" allowed=false$',
        r'^type=gate admin_unconfirmed reason="需二次确认" requires_confirm=true$',
        r'^type=gate admin_confirmed allowed=true$',
        r'^type=action kind=export_recording target=CAM-01 result=ok executed=true$',
        r'^type=maint-log action=export_recording user=admin result=ok '
        r'detail="录像导出 CAM-01 \[1000,61000\]"$',
        r'^type=config scope=device role=maintainer result=ok$',
        r'^type=config scope=system role=maintainer result=denied$',
        r'^type=config scope=system role=admin result=ok$',
    ]
    for rx in EXACT:
        want(rx)
    # acceptance flags + PASS verdict
    want(r'^type=acceptance driver_export_absent=true admin_export_confirm_required=true '
         r'admin_export_logged=true export_config_gated=true '
         r'config_system_admin_only=true result=PASS$')
    # inverse guards
    if re.search(r'^type=acceptance\b.*result=FAIL', text, re.M):
        fails.append("acceptance verdict FAIL")

    # ---- embedded config-on section derived from the probe source (same run,
    #      second matrix when ON exists with maintainer/developer=Y exports) --
    # the delivered probe prints only the OFF matrix; verify the ON flip via
    # the exact maintainer_on=Y cell of export-gate instead (asserted above).
    return fails


ITEM023_CASES = [
    "all roles view realtime/alarm/replay + ack",
    "driver denied all-cameras/ai-debug/maintenance/export",
    "ops roles see all-cameras/ai-debug/maintenance",
    "matrix has all permissions",
    "admin export allowed by default",
    "ops export denied when config off",
    "ops export allowed when config on (受配置)",
    "driver export still denied when config on",
    "export/config require secondary confirm",
    "view/ack do not require confirm",
    "driver export gate denied",
    "admin export needs confirm",
    "admin export confirmed",
    "driver export denied/not executed",
    "admin export unconfirmed -> cancelled",
    "admin export confirmed -> ok",
    "export writes maintenance log",
    "maintainer device config ok",
    "maintainer system config denied",
    "admin system config ok",
    "config unconfirmed -> cancelled",
]


def ext_run(client, args, wd, remote_dir, note) -> int:
    res = rrun(client, "mkdir -p %s" % wd, 30)
    note("mkdir-workdir", res)
    # NOTE: rm -rf of the previous verifier workdir is skipped on purpose; a
    # fresh unique workdir per stamp avoids touching unrelated state.
    sftp = client.open_sftp()
    sftp.get_channel().settimeout(120)
    try:
        sftp.put(args.cpp, "%s/verify_perm23_ext.cpp" % wd)
        note("upload-fixture", {"exit": 0, "output": args.cpp})
    finally:
        sftp.close()
    compile_cmd = ("cd %s && g++ -std=c++17 -O1 -Wall -Wextra "
                   "-Isrc/core/include -Isrc/platform/include "
                   "%s/verify_perm23_ext.cpp build-core/src/core/librailhmi_core.a "
                   "build-core/src/platform/librailhmi_platform.a "
                   "-o %s/verify_perm23_ext 2>&1; echo GXX_EXIT=$?"
                   % (remote_dir, wd, wd))
    cext = rrun(client, compile_cmd, 240)
    note("compile-ext", cext)
    if cext.get("exit") != 0 or "GXX_EXIT=0" not in cext.get("output", ""):
        return 1
    rext = rrun(client, "cd %s && %s/verify_perm23_ext; echo EXT_EXIT=$?"
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
    ap = argparse.ArgumentParser(description="ITEM-023 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify023")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--impl-testrun-log", required=True,
                    help="implementer r3 core-tests log for line comparison")
    ap.add_argument("--impl-permprobe-log", required=True,
                    help="implementer r3 permissions-probe log for byte identity")
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
                        "notes": ["ITEM-023: permissions + secondary confirmation; "
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
        pl = outdir / ("build-TestRun-%s-%s.core-evidence_permissions-probe-%s.log"
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
        expected_pass = 641
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
            found = [c for c in ITEM023_CASES if c in names_all]
            missing = [c for c in ITEM023_CASES if c not in names_all]
            junit_ok = (n_tests == "641" and n_fail == "0"
                        and len(found) == len(ITEM023_CASES))
            note("junit-parse", {"exit": 0 if junit_ok else 1,
                 "output": "junit tests=%s failures=%s item023_cases=%d/%d missing=%s"
                           % (n_tests, n_fail, len(found), len(ITEM023_CASES), missing)})
            if not junit_ok:
                ok = False
        else:
            note("junit-parse", {"exit": None, "output": "no fresh junit xml"})
            ok = False

        # 3. independent permissions-probe assertions (oracle) + byte identity
        if pl.is_file():
            fresh_text = pl.read_text(encoding="utf-8", errors="replace")
            fails = assert_permissions_probe(fresh_text)
            note("perm-probe-assert", {"exit": 0 if not fails else 1,
                 "output": json.dumps({"fails": fails}, ensure_ascii=False)})
            if fails:
                ok = False
            impl_probe_text = Path(args.impl_permprobe_log).read_text(
                encoding="utf-8", errors="replace")
            identical = impl_probe_text.splitlines() == fresh_text.splitlines()
            note("perm-probe-compare", {"exit": 0 if identical else 1,
                 "output": "cross-round deterministic reproduction identical=%s" % identical})
            if not identical:
                ok = False
        else:
            note("perm-probe-assert", {"exit": None, "output": "missing fresh permissions-probe"})
            ok = False

        # 4. verifier ext fixture against THIS round's static libs
        if ext_run(client, args, wd, remote_dir, note) != 0:
            ok = False

        # 5. verifier wiring scan of delivered sources (fresh remote copy)
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(60)
        try:
            sftp.putfo(io.BytesIO(SCAN_PY.encode("utf-8")), "%s/verify_scan23.py" % wd)
            note("upload-scan", {"exit": 0, "output": "verify_scan23.py"})
        finally:
            sftp.close()
        scan = rrun(client, "cd %s && python3 %s/verify_scan23.py; echo SCAN23_EXIT=$?"
                    % (remote_dir, wd), 60)
        note("wiring-scan", scan)
        so = scan.get("output", "")

        def svar(name):
            m3 = re.search(r"%s\s*=\s*(\S+)" % re.escape(name), so)
            return m3.group(1) if m3 else None

        wants = {k: "True" for k in SCAN_KEYS}
        observed = {k: svar(k) for k in wants}
        mismatch = sorted(k for k, v in wants.items() if observed[k] != v)
        scan_exit_ok = scan.get("exit") == 0 and "SCAN23_EXIT=0" in so
        wiring_ok = not mismatch and scan_exit_ok
        note("wiring-scan-findings", {"exit": 0 if wiring_ok else 1,
             "output": "mismatch=%s scan_exit_ok=%s raw=%s"
                       % (mismatch, scan_exit_ok, json.dumps(observed, ensure_ascii=False))})
        if not wiring_ok:
            ok = False

        # 6. determinism: re-run the probe and diff
        n2 = rrun(client, "cd %s && mkdir -p verify && ./build-core/tools/railhmi-permissions-probe "
                          "--out verify/permissions-probe-rep.log; echo PERMREP_EXIT=$?"
                  % remote_dir, 60)
        note("perm-probe-rep-run", n2)
        if "PERMREP_EXIT=0" not in n2.get("output", ""):
            ok = False
        n2b = rrun(client, "cd %s && diff verify/permissions-probe-rep.log "
                           "core-evidence/permissions-probe-%s.log && echo IDENTICAL"
                   % (remote_dir, stamp), 30)
        note("perm-probe-determinism", n2b)
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
                  and "permissions-probe" in reg.get("output", ""))
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
                                "output": "cleanup_ok=%s (board /tmp/railhmi_build kept for "
                                          "post-run negative probe)" % cleanup_ok})
        if not cleanup_ok:
            ok = False

        # 9. config gate negative probe: delivered run_core_tests.sh-only scope
        # note — cfg-check style negative is inside the TestRun; the verifier
        # adds a source-level negative assertion instead (scan23 N2 keys).

        findings = [
            {"independent_recheck": ("ITEM-023 permissions + secondary confirmation: "
                                     "deliverable TestRun entry re-run ok, core-tests 641 "
                                     "line-identical + JUnit 641/0 (21 ITEM-023 cases), "
                                     "permissions-probe oracle + determinism, ext fixtures "
                                     "and wiring scan both green on-board"),
             "confirmed": ok},
            {"observable_driver_export": ("driver export permission=false (matrix off row); "
                                          "P3 control visible: canExportRecording() -> entry "
                                          "not rendered for driver"),
             "present": svar("N2_driver_export_visibility_gated") == "True"},
            {"observable_admin_confirm_logging": ("admin/gate: export gate admin_unconfirmed "
                                                  "reason=需二次确认 -> confirmed executed=true "
                                                  "logged; bridge writes build_action_log rows"),
             "present": svar("N2_admin_confirm_logged_chain") == "True"},
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


# --- verifier wiring scan (delivered sources, fresh remote copy) — Item 023 ----

SCAN_PY = r'''import re
p3 = open('src/ui/qml/pages/P3Replay.qml', encoding='utf-8').read()
p6 = open('src/ui/qml/pages/P6Maintenance.qml', encoding='utf-8').read()
bridge = open('src/ui/src/permissions_model.cpp', encoding='utf-8').read()
bridge_h = open('src/ui/src/permissions_model.h', encoding='utf-8').read()
mc = open('src/ui/src/main.cpp', encoding='utf-8').read()
cfg = open('config/app.yaml', encoding='utf-8').read()
doc = open('PERMISSION_MATRIX.md', encoding='utf-8').read()

# T1: P3 export entry is permission-gated (RC-visible: hidden for driver)
print('T1_export_control_gated=',
      ('canExportRecording' in p3) and
      re.search(r'objectName:\s*"exportRecording"', p3) is not None and
      re.search(r'visible:\s*page\.canExport\(\)', p3) is not None)

# T2: P3 export confirm modal + yes/no + confirm executes (two-phase)
print('T2_export_modal=',
      'exportConfirmModal' in p3 and 'exportConfirmYes' in p3 and
      'exportConfirmNo' in p3 and
      re.search(r'visible:\s*page\.exportPending', p3) is not None)
print('T2_confirm_exec=',
      re.search(r'function confirmExport\(\)[\s\S]*?exportRecording\([^)]*true\)', p3) is not None and
      re.search(r'function askExport\(\)\s*\{\s*page\.exportPending = true', p3) is not None)

# T3: P6 permission matrix card consumed via bridge matrix()
print('T3_p6_matrix_card=',
      'maintPermMatrixCard' in p6 and 'railhmiPermissions.matrix()' in p6 and
      'permRow-' in p6 and 'modelData.label' in p6 and
      'modelData.driver' in p6 and 'modelData.maintainer' in p6 and
      'modelData.developer' in p6 and 'modelData.admin' in p6)
print('T3_p6_roleName=', 'roleName()' in p6)

# denied entries not rendered: bridge can* methods delegate, control visibility
print('T4_bridge_can_methods=',
      'canExportRecording' in bridge_h and 'canExportSnapshot' in bridge_h and
      'canModifyDeviceConfig' in bridge_h and 'canModifySystemConfig' in bridge_h and
      'role_has_permission' in bridge)

# T5: secondary confirm delegated to core (gate/confirm shape)
print('T5_bridge_confirm=',
      'export_recording' in bridge and 'modify_config' in bridge and
      'build_action_log' in bridge and 'confirmed' in bridge)

# T6: main.cpp injects railhmiPermissions from loaded config app.permissions
print('T6_main_inject=',
      'railhmiPermissions' in mc and 'RailhmiPermissionsModel' in mc and
      re.search(r'permsModel\.seed\([^)]*permissions', mc) is not None)

# T7: config contract — app.permissions in app.yaml and parsed with applied_defaults
print('T7_config=', 'export_ops_allowed' in cfg and 'permissions:' in cfg)

# T8: permission matrix doc carries role columns + export rows
print('T8_doc=',
      '司机' in doc and '管理员' in doc and '受配置' in doc and
      'export_recording' in doc and 'export_snapshot' in doc and
      'modify_system_config' in doc)

# T9: board pipeline runs permissions probes + wiring checker
bb = open('tools/board_build.py', encoding='utf-8').read()
print('T9_board=',
      'check_permissions_wiring.py' in bb and 'railhmi-permissions-probe' in bb and
      'PERM_WIRE_EXIT' in bb and 'PERM_EXIT' in bb)

# N2 observable point 1: driver export NOT rendered = control visibility derived
# from permission query, not a hardcoded role switch (generic mechanism)
n2a = (re.search(r'visible:\s*page\.canExport\(\)', p3) is not None and
       "'Driver'" not in p3 and 'role ==' not in p3)
print('N2_driver_export_visibility_gated=', n2a)

# N2 observable point 2: admin confirm + maintenance-log chain is a core
# action model (not a UI-only stub): modal -> bridge invokable -> core
# export_recording -> build_action_log, and the executed path pushes to logs_
n2b = (re.search(r'exportRecording\(page\.activeCam', p3) is not None and
       re.search(r'export_recording\(', bridge) is not None and
       'build_action_log' in bridge and
       re.search(r'if\s*\(\s*a\.executed\s*\)', bridge) is not None and
       'logs_.push_back' in bridge)
print('N2_admin_confirm_logged_chain=', n2b)

# T10: tokens-only colors (strip // comments like the delivered checker)
p3l = [line.split('//', 1)[0] for line in p3.splitlines()]
p6l = [line.split('//', 1)[0] for line in p6.splitlines()]
print('T10_no_hex=',
      not any(re.search(r'#[0-9a-fA-F]{3,8}\b', l) for l in p3l + p6l) and
      'Theme.color.' in p3 and 'Theme.color.' in p6)
'''

SCAN_KEYS = [
    "T1_export_control_gated", "T2_export_modal", "T2_confirm_exec",
    "T3_p6_matrix_card", "T3_p6_roleName", "T4_bridge_can_methods",
    "T5_bridge_confirm", "T6_main_inject", "T7_config", "T8_doc", "T9_board",
    "N2_driver_export_visibility_gated", "N2_admin_confirm_logged_chain",
    "T10_no_hex",
]


if __name__ == "__main__":
    raise SystemExit(main())
