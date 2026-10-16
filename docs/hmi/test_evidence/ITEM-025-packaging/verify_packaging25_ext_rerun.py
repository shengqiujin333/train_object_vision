# -*- coding: utf-8 -*-
"""ITEM-025 verification — board re-run mini driver (~3-5 min).

The 20261017-v1 round already established (build stamp VERIFY-20261017-v1,
keep-remote did NOT persist because cleanup still ran via board_build's
cleanup? No: --keep-remote was passed; the transcript shows cleanup step
exited 2 = already absent, so /tmp/railhmi_build was REMOVED by board_build?
Actually with --keep-remote board_build.py skips its own cleanup; the
fixtures driver's own cleanup step at the end ran `rm -rf /tmp/railhmi_build`.
So the build tree is gone and this driver must NOT depend on it.

What still needs board evidence after the v1 fixture-driver fixes:
  1. ext fixture compile+run (was compile-error: comment block format bug)
  2. negative control with --app-src src (was exit 1/1 masking)
The 663-line compare, JUnit parse, oracle/byte-identity, determinism,
packaging scan, cleanup already ran green (except junit unescape, fixed and
re-validated offline) — they do NOT need re-running to establish the verdict,
but this driver re-runs the two failed fixture steps only, using board_build
TestRun with --keep-remote again (it rebuilds+rearchives everything anyway,
same cost), then runs only the fixed steps.
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


def main() -> int:
    ap = argparse.ArgumentParser(description="ITEM-025 re-run driver (fixed steps only)")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify025b")
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
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
                        "notes": ["ITEM-025 re-run of the two v1-failed fixture steps: "
                                  "ext compile/run (fixed cpp comment block) and "
                                  "negative control (with --app-src src); "
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

        # 1. TestRun again with --keep-remote (rebuild required: v1 build tree
        #    was removed by the v1 driver's own cleanup step).
        cmd = [sys.executable, str(Path(args.app_root) / "tools" / "board_build.py"),
               "--host", args.host, "--user", args.user,
               "--credential-env", args.credential_env,
               "--mode", "TestRun",
               "--outdir", str(outdir),
               "--stamp", args.stamp,
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

        # 2. cross-round sanity: fresh fault-probe must be byte-identical to
        #    the implementer r1 archive (and to the v1 verifier archive).
        host_u = args.host.replace(".", "_")
        stamp = args.stamp
        pl = outdir / ("build-TestRun-%s-%s.core-evidence_fault-probe-%s.log"
                       % (host_u, stamp, stamp))
        impl_probe = (outdir / ("build-TestRun-192_168_0_103-20261016-ITEM025-r1"
                                ".core-evidence_fault-probe-20261016-ITEM025-r1.log"))
        if not pl.is_file():
            note("fresh-probe", {"exit": None, "output": "missing fresh fault-probe"})
            ok = False
        else:
            fresh = pl.read_text(encoding="utf-8", errors="replace").splitlines()
            impl = impl_probe.read_text(encoding="utf-8", errors="replace").splitlines()
            note("fresh-probe-compare", {"exit": 0 if fresh == impl else 1,
                 "output": "fresh==impl r1: %s" % (fresh == impl)})
            if fresh != impl:
                ok = False

        # 3. fixed ext fixture compile+run
        rrun(client, "mkdir -p %s" % wd, 30)
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(120)
        try:
            sftp.put(str(Path(__file__).resolve().parent / "verify_packaging25_ext.cpp"),
                     "%s/verify_packaging25_ext.cpp" % wd)
            note("upload-ext", {"exit": 0, "output": "verify_packaging25_ext.cpp"})
        finally:
            sftp.close()
        cext = rrun(client, "cd %s && g++ -std=c++17 -O1 -Wall -Wextra "
                            "-Isrc/core/include -Isrc/platform/include "
                            "%s/verify_packaging25_ext.cpp "
                            "build-core/src/core/librailhmi_core.a "
                            "build-core/src/platform/librailhmi_platform.a "
                            "-o %s/verify_packaging25_ext 2>&1; echo GXX_EXIT=$?"
                    % (remote_dir, wd, wd), 240)
        note("compile-ext", cext)
        if "GXX_EXIT=0" not in cext.get("output", ""):
            ok = False
        else:
            rext = rrun(client, "cd %s && %s/verify_packaging25_ext; echo EXT_EXIT=$?"
                        % (remote_dir, wd), 180)
            note("run-ext", rext)
            m = re.search(r"RESULT: (\w+) passed=(\d+) failed=(\d+)", rext.get("output", ""))
            if rext.get("exit") != 0 or not (m and m.group(1) == "PASS"
                                             and int(m.group(3)) == 0):
                ok = False
            else:
                note("ext-summary", {"exit": 0,
                     "output": "passed=%s failed=%s" % (m.group(2), m.group(3))})

        # 4. fixed negative control (with --app-src src)
        neg_src = (Path(__file__).resolve().parent / "verify_packaging25_fixtures.py").read_text(
            encoding="utf-8")
        m2 = re.search(r"NEG_PY = r'''\n(.*?)'''", neg_src, re.S)
        sftp = client.open_sftp()
        if not m2:
            note("neg-extract", {"exit": 1, "output": "NEG_PY block not found"})
            ok = False
        else:
            try:
                sftp.putfo(io.BytesIO(m2.group(1).encode("utf-8")), "%s/verify_neg25.py" % wd)
                note("neg-extract", {"exit": 0, "output": "verify_neg25.py"})
            finally:
                sftp.close()
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

        # 5. packaging scan on the fresh copy (expected green; D-01/D-02 False)
        scan_src = (Path(__file__).resolve().parent / "verify_packaging25_scan.py").read_text(
            encoding="utf-8")
        sftp = client.open_sftp()
        try:
            sftp.putfo(io.BytesIO(scan_src.encode("utf-8")), "%s/verify_scan25.py" % wd)
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
        pos_bad = [k for k in POSITIVE if svar(k) != "True"]
        d01 = svar("D01_execstartpre_provenance")
        d01b = svar("D01b_upgrade_doc_bin_tools")
        d02 = svar("D02_config_arg_consumed")
        scan_ok = (scan.get("exit") == 0 and not pos_bad
                   and d01 == "False" and d01b == "False" and d02 == "False")
        note("packaging-scan-findings", {"exit": 0 if scan_ok else 1,
             "output": "positive_failing=%s d01=%s d01b=%s d02=%s" % (pos_bad, d01, d01b, d02)})
        if not scan_ok:
            ok = False

        # 6. cleanup by name
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
            {"v2_fixups": ("ext fixture compiled+ran on-board (RESULT: PASS, 0 failed); "
                           "negative control NEG_HEALTHY_EXIT=0/NEG_MUTATED_EXIT=1; "
                           "packaging scan with D-01/D-01b/D-02 False re-confirmed"),
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


if __name__ == "__main__":
    raise SystemExit(main())
