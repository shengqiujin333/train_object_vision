#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""software_verification fixture runner for ITEM-014 tri-panel layout &
render-path wiring (verifier-owned).

Independent board-side round over the registered Paramiko SSH entry:
  1. Reproduce the implementer's authoritative round: upload the snapshot of
     app/ + design_tokens.yaml, run the deliverable entry
     `bash tests/run_core_tests.sh` (builds railhmi_core incl. layout.cpp,
     runs all tests incl. test_layout.cpp 377 cases), archive its log + JUnit
     XML back; compare PASS lines with the implementer's archived log
     line-by-line (programmatic).
  2. Reproduce the tokens selftest + no-hardcode scan + sim-inject
     authoritative round (regression over the whole core suite).
  3. Verifier-owned layout ext fixtures (verify_layout14_ext.cpp) linked
     against the librailhmi_core.a built by the entry itself - assertions
     beyond the implementer's test_layout.cpp:
       - exact hand-computed geometry identity for both acceptance
         resolutions 1920x1080 / 1280x720 (every rect field)
       - criterion discrimination: fabricated bad layouts must be rejected
         (guards against always-true criteria); real output passes
       - resize response matrix over common window sizes (order, largest,
         adjacency, center-inside, banner-clear, ratios vs 1920x1080)
       - continuous height scan 720..2160 and width scan 640..3840
         (row-h identity, used==height invariant, all criteria hold)
       - measured banner-to-front-center clearance室 >0 on both tiers
       - determinism (field-by-field) and zero-size no-crash semantics
  4. Cleanup workdirs by name.
Fingerprint mismatch aborts. Credentials only from the named env var, never
logged. Every remote step has a finite timeout.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import io
import json
import os
import socket
import sys
import tarfile
import time
from pathlib import Path

try:
    import paramiko
except Exception as exc:  # pragma: no cover
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
    ap = argparse.ArgumentParser(description="ITEM-014 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify014")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True, help="stamp; bump per verification round")
    ap.add_argument("--impl-log", default=None,
                    help="implementer archived core-tests log for line-by-line comparison")
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--timeout-sec", type=int, default=1200,
                    help="finite ceiling for the entry build+test step")
    ap.add_argument("--app-root", default=str(Path(__file__).resolve().parents[4] / "app"),
                    help="local app/ tree to upload and build on the board")
    ap.add_argument("--tokens-yaml",
                    default=str(Path(__file__).resolve().parents[4] /
                                "docs/hmi/design_tokens.yaml"))
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
    board_wd = wd + "_board"
    transcript: dict = {"invocation": {k: str(v) for k, v in vars(args).items()},
                        "host_key_fingerprint": None,
                        "notes": ["aborts on fingerprint mismatch; no credentials logged"],
                        "steps": []}
    ok = True

    def note(step, res):
        transcript["steps"].append({"step": step, **res})

    def save() -> None:
        (outdir / (tag + ".transcript.json")).write_text(
            json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
        with (outdir / (tag + ".log")).open("w", encoding="utf-8") as fh:
            fh.write("host_key_fingerprint: %s\n" % transcript["host_key_fingerprint"])
            for s in transcript["steps"]:
                fh.write("\n===== [%s] exit=%s =====\n" % (s.get("step"), s.get("exit")))
                fh.write((s.get("output") or "") + "\n")

    def dl(sftp, remote_rel: str, local_name: str) -> bool:
        local_out = outdir / ("%s.%s" % (tag, local_name))
        try:
            sftp.get("%s/app/%s" % (board_wd, remote_rel), str(local_out))
            note("download", {"exit": 0, "output": local_name})
        except OSError as exc:
            note("download", {"exit": None, "output": "%s: %r" % (remote_rel, exc)})
            return False
        return True

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

        # 0. upload snapshot of app/ into an isolated dir
        note("mkdir", rrun(client, "rm -rf %s %s && mkdir -p %s/app %s"
                           % (wd, board_wd, board_wd, wd), 30))
        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:gz") as tf:
            for p in sorted(Path(args.app_root).rglob("*")):
                if p.is_file():
                    tf.add(str(p), arcname=str(p.relative_to(args.app_root)))
        data = buf.getvalue()
        buf.seek(0)
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(120)
        try:
            remote_tar = "%s/app.tgz" % wd
            sftp.putfo(buf, remote_tar, len(data))
            note("upload-app", {"exit": 0,
                 "output": "uploaded app tree (%d bytes compressed)" % len(data)})
            remote_yaml = "%s/design_tokens.yaml" % wd
            sftp.put(args.tokens_yaml, remote_yaml)
            note("upload-tokens-yaml", {"exit": 0,
                 "output": "uploaded %s" % (args.tokens_yaml,)})
            remote_cpp = "%s/verify_layout14_ext.cpp" % wd
            sftp.put(args.cpp, remote_cpp)
            note("upload-fixture", {"exit": 0,
                 "output": "uploaded %s -> %s" % (args.cpp, remote_cpp)})
        finally:
            sftp.close()

        note("extract", rrun(client,
             "cd %s && tar xzf %s/app.tgz -C %s/app && cp %s/design_tokens.yaml %s/app/design_tokens.yaml "
             "&& echo APP_FILES=$(cd %s/app && find . -type f | wc -l)"
             % (board_wd, wd, board_wd, wd, board_wd, board_wd), 60))

        # 1. deliverable entry: build core (+layout) + full test set; finite wait
        entry = rrun(client,
            "cd %s/app && bash tests/run_core_tests.sh --outdir core-evidence "
            "--stamp %s 2>&1; echo ENTRY_EXIT=$?" % (board_wd, args.stamp),
            args.timeout_sec)
        note("run-entry", entry)
        if entry["exit"] != 0 or "ENTRY_EXIT=0" not in entry.get("output", "") or \
                "RESULT: PASS passed=" not in entry.get("output", ""):
            ok = False

        sftp = client.open_sftp()
        sftp.get_channel().settimeout(120)
        try:
            remote_log = "core-evidence/core-tests-%s.log" % args.stamp
            ok &= dl(sftp, remote_log, "core-evidence_core-tests-%s.log" % args.stamp)
            ok &= dl(sftp, "core-evidence/core-tests-%s.xml" % args.stamp,
                     "core-evidence_core-tests-%s.xml" % args.stamp)
        finally:
            sftp.close()
        local_log = outdir / ("%s.core-evidence_core-tests-%s.log" % (tag, args.stamp))

        # 1b. programmatic line-by-line comparison with implementer's archived log
        if local_log.is_file() and args.impl_log and os.path.isfile(args.impl_log):
            try:
                impl_lines = [l for l in Path(args.impl_log).read_text(
                    encoding="utf-8", errors="replace").splitlines() if l.startswith("[PASS]")]
                ver_lines = [l for l in local_log.read_text(
                    encoding="utf-8", errors="replace").splitlines() if l.startswith("[PASS]")]
                same = impl_lines == ver_lines
                note("log-compare", {
                    "exit": 0 if same else 1,
                    "output": "impl_pass_lines=%d verify_pass_lines=%d identical=%s"
                              % (len(impl_lines), len(ver_lines), same)})
                if not same:
                    ok = False
            except Exception as exc:  # noqa: BLE001
                note("log-compare", {"exit": None, "output": "compare error: %r" % (exc,)})
                ok = False

        # 1c. programmatic layout-domains check from JUnit XML (independent parse)
        local_xml = outdir / ("%s.core-evidence_core-tests-%s.xml" % (tag, args.stamp))
        if local_xml.is_file():
            try:
                import re
                xtext = local_xml.read_text(encoding="utf-8", errors="replace")
                m = re.search(r'tests="(\d+)".*?failures="(\d+)"', xtext, re.S)
                if not m:
                    m = re.search(r'failures="(\d+)".*?tests="(\d+)"', xtext, re.S)
                    if m:
                        n_fail, n_tests = m.group(1), m.group(2)
                    else:
                        n_tests, n_fail = None, None
                else:
                    n_tests, n_fail = m.group(1), m.group(2)
                layout_cases = len(re.findall(r'name="[^"]*1[29]20x[178]80[^"]*"',
                                              xtext)) + len(
                    re.findall(r'name="[^"]*ratios preserved[^"]*"', xtext))
                note("junit-parse", {"exit": 0 if (n_tests and n_fail == "0") else 1,
                     "output": "junit tests=%s failures=%s layout_case_hint=%d"
                               % (n_tests, n_fail, layout_cases)})
                if not (n_tests and n_fail == "0"):
                    ok = False
            except Exception as exc:  # noqa: BLE001
                note("junit-parse", {"exit": None, "output": "junit parse error: %r" % (exc,)})
                ok = False

        # 2. tokens selftest + no-hardcode scan + sim-inject authoritative round
        tok = rrun(client,
            "cd %s/app && python3 tools/gen_tokens.py --selftest --yaml design_tokens.yaml; "
            "echo TOKENS_EXIT=$?" % board_wd, 120)
        note("tokens-selftest", tok)
        if tok["exit"] != 0 or "TOKENS_EXIT=0" not in tok.get("output", ""):
            ok = False

        scan = rrun(client,
            "cd %s/app && python3 tools/check_no_hardcoded_ui.py --root src/ui; echo SCAN_EXIT=$?"
            % board_wd, 120)
        note("ui-scan", scan)
        if scan["exit"] != 0 or "SCAN_EXIT=0" not in scan.get("output", ""):
            ok = False

        sim = rrun(client,
            "cd %s/app && ./build-core/tools/railhmi-sim-inject "
            "--fi 01,02,03,04,05,10 --duration 6000 "
            "--out core-evidence/sim-inject-%s.log "
            "--json core-evidence/sim-inject-%s.json 2>&1; echo SIM_EXIT=$?"
            % (board_wd, args.stamp, args.stamp), 60)
        note("sim-inject", sim)
        if sim["exit"] != 0 or "SIM_EXIT=0" not in sim.get("output", ""):
            ok = False
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(120)
        try:
            ok &= dl(sftp, "core-evidence/sim-inject-%s.log" % args.stamp,
                     "core-evidence_sim-inject-%s.log" % args.stamp)
            ok &= dl(sftp, "core-evidence/sim-inject-%s.json" % args.stamp,
                     "core-evidence_sim-inject-%s.json" % args.stamp)
        finally:
            sftp.close()

        # 3. verifier-owned layout ext fixtures
        probe = rrun(client, "cd %s/app && ls build-core/src/core/librailhmi_core.a; "
                             "echo PROBE_EXIT=$?" % board_wd, 60)
        note("probe-lib", probe)
        lib_path = ("%s/app/build-core/src/core/librailhmi_core.a" % board_wd)
        if probe.get("exit") != 0:
            alt = rrun(client,
                       "find %s/app/build-core -name 'librailhmi_core.a' 2>/dev/null | head -1"
                       % board_wd, 60)
            first = (alt.get("output") or "").strip().splitlines()
            lib_path = first[0].strip() if first else lib_path
        compile_cmd = ("cd %s/app && g++ -std=c++17 -O1 -Wall -Wextra -Isrc/core/include "
                       "%s/verify_layout14_ext.cpp %s -o %s/verify_layout14_ext 2>&1; "
                       "echo GXX_EXIT=$?" % (board_wd, wd, lib_path, wd))
        note("compile-ext", rrun(client, compile_cmd, 240))
        run_ext = rrun(client,
                       "cd %s/app && %s/verify_layout14_ext; echo EXT_EXIT=$?"
                       % (board_wd, wd), 180)
        note("run-ext", run_ext)
        if run_ext.get("exit") != 0 or "RESULT: PASS passed=" not in run_ext.get("output", ""):
            ok = False

        # 4. cleanup by name
        note("cleanup", rrun(client,
            "rm -rf %s %s && echo fixture_cleanup_done && echo LEFT_LIST && ls -d %s %s 2>&1"
            % (wd, board_wd, wd, board_wd), 30))
    finally:
        client.close()
        save()

    print("host_key_fingerprint:", transcript["host_key_fingerprint"])
    for s in transcript["steps"]:
        tail = (s.get("output") or "").strip().splitlines()
        print("[%s] exit=%s %s" % (s.get("step"), s.get("exit"), tail[-1] if tail else ""))
    if not ok:
        print("NOTE: one or more steps returned nonzero or failed assertions; see transcript")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
