#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""software_verification fixture runner for ITEM-005 (verifier-owned).

Runs on the registered execution PC, talks to the registered compute_host SSH
entry only. Compiles and runs docs/hmi/test_evidence/ITEM-005-stream/
verify_stream_ext.cpp against the app tree kept on the board by board_build.py
(--keep-remote), then exercises the new thresholds validation through
railhmi-config-check with verifier-owned negative fixtures.

Aborts when observed host fingerprint differs from expected. Credentials are
only read from the named environment variable and never logged. Every remote
command uses an explicit finite timeout. Cleans up all /tmp working dirs it
requested (plus the build dir kept for this round) and records cleanup in the
transcript.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import socket
import sys
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
    ap = argparse.ArgumentParser(description="ITEM-005 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--remote-build-dir", default="/tmp/railhmi_build")
    ap.add_argument("--workdir", default="/tmp/railhmi_verify")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--connect-timeout", type=int, default=10)
    args = ap.parse_args()

    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("credential env %s not loaded; refusing to guess\n"
                         % args.credential_env)
        return 2

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    tag = "VERIFY-FIXTURES-" + args.stamp
    transcript: dict = {"invocation": vars(args),
                        "host_key_fingerprint": None,
                        "notes": ["aborts on fingerprint mismatch; no credentials logged"],
                        "steps": []}

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

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    client.set_missing_host_key_policy(paramiko.RejectPolicy())
    fp = None
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
        if tr is not None and tr.get_remote_server_key() is not None:
            fp = fingerprint(tr.get_remote_server_key())
        transcript["host_key_fingerprint"] = fp
        note("fingerprint", {"exit": 0 if fp == args.expected_fingerprint else 1,
                             "output": "observed=%s expected=%s"
                                       % (fp, args.expected_fingerprint)})
        if fp != args.expected_fingerprint:
            save()
            sys.stderr.write("host fingerprint mismatch; aborting\n")
            return 2

        note("mkdir", rrun(client, "mkdir -p %s" % args.workdir, 20))
        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(60)
            remote_cpp = "%s/verify_stream_ext.cpp" % args.workdir
            sftp.put(args.cpp, remote_cpp)
            note("upload", {"exit": 0, "output": "uploaded %s -> %s" % (args.cpp, remote_cpp)})
        finally:
            sftp.close()

        compile_cmd = ("cd %s && g++ -std=c++17 -O0 -Wall -Isrc/core/include %s/verify_stream_ext.cpp "
                       "src/core/src/stream_state.cpp -o %s/verify_stream_ext 2>&1; echo GXX_EXIT=$?"
                       % (args.remote_build_dir, args.workdir, args.workdir))
        note("compile", rrun(client, compile_cmd, 60))
        note("run-ext", rrun(client,
             "cd %s && %s/verify_stream_ext; echo EXT_EXIT=$?"
             % (args.remote_build_dir, args.workdir), 60))

        cfg_src = "%s/config" % args.remote_build_dir
        f1 = ("rm -rf /tmp/railhmi_fx && mkdir -p /tmp/railhmi_fx && cp %s/*.yaml /tmp/railhmi_fx/ "
              "&& sed -i 's/first_frame_timeout_ms: 5000/first_frame_timeout_ms: 0/' /tmp/railhmi_fx/thresholds.yaml "
              "&& %s/build-core/tools/railhmi-config-check /tmp/railhmi_fx 2>&1; echo FX1_EXIT=$?"
              % (cfg_src, args.remote_build_dir))
        note("fx1_first_frame_timeout_zero", rrun(client, f1, 60))
        f2 = ("rm -rf /tmp/railhmi_fx && mkdir -p /tmp/railhmi_fx && cp %s/*.yaml /tmp/railhmi_fx/ "
              "&& sed -i 's/reconnect_backoff_ms: 1000/reconnect_backoff_ms: -1/' /tmp/railhmi_fx/thresholds.yaml "
              "&& %s/build-core/tools/railhmi-config-check /tmp/railhmi_fx 2>&1; echo FX2_EXIT=$?"
              % (cfg_src, args.remote_build_dir))
        note("fx2_reconnect_backoff_negative", rrun(client, f2, 60))
        f3 = ("rm -rf /tmp/railhmi_fx && mkdir -p /tmp/railhmi_fx && cp %s/*.yaml /tmp/railhmi_fx/ "
              "&& %s/build-core/tools/railhmi-config-check /tmp/railhmi_fx 2>&1; echo FX3_EXIT=$?"
              % (cfg_src, args.remote_build_dir))
        note("fx3_good_reference", rrun(client, f3, 60))

        note("cleanup", rrun(client,
             "rm -rf /tmp/railhmi_verify /tmp/railhmi_fx %s && echo fixture_cleanup_done"
             % args.remote_build_dir, 20))
    finally:
        client.close()
        save()

    print("host_key_fingerprint:", transcript["host_key_fingerprint"])
    for s in transcript["steps"]:
        tail = (s.get("output") or "").strip().splitlines()
        print("[%s] exit=%s %s" % (s.get("step"), s.get("exit"), tail[-1] if tail else ""))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
