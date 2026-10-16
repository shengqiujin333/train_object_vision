# -*- coding: utf-8 -*-
"""ITEM-024 re-run of the verifier ext fixture only (v2 expectations).

Reuses the kept remote build tree /tmp/railhmi_build from the v1 round.
Finite timeouts; no credentials logged.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import re
import socket
import sys
import time
from pathlib import Path

import paramiko


def fingerprint(key) -> str:
    return "SHA256:" + base64.b64encode(
        hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")


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
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", default="192.168.0.103")
    ap.add_argument("--user", default="admin")
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--connect-timeout", type=int, default=10)
    args = ap.parse_args()

    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("credential env not loaded\n")
        return 2

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    tag = "VERIFY-EXT-v2-" + args.stamp
    wd = "/tmp/railhmi_verify024"
    remote_dir = "/tmp/railhmi_build"
    transcript: dict = {"invocation": {k: str(v) for k, v in vars(args).items()},
                        "steps": []}

    def note(step, res):
        transcript["steps"].append({"step": step, **res})

    def save():
        (outdir / (tag + ".transcript.json")).write_text(
            json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
        with (outdir / (tag + ".log")).open("w", encoding="utf-8") as fh:
            for s in transcript["steps"]:
                fh.write("\n===== [%s] exit=%s =====\n" % (s.get("step"), s.get("exit")))
                fh.write((s.get("output") or "") + "\n")

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    client.set_missing_host_key_policy(paramiko.RejectPolicy())
    try:
        client.connect(hostname=args.host, username=args.user, password=secret,
                       timeout=args.connect_timeout, auth_timeout=args.connect_timeout,
                       banner_timeout=args.connect_timeout,
                       look_for_keys=False, allow_agent=False)
    except Exception as exc:  # noqa: BLE001
        note("connect", {"exit": None, "output": "connect error: %r" % (exc,)})
        save()
        return 2

    try:
        tr = client.get_transport()
        fp = fingerprint(tr.get_remote_server_key()) if tr is not None else "(none)"
        if fp != args.expected_fingerprint:
            sys.stderr.write("fingerprint mismatch; aborting\n")
            save()
            return 2
        note("fingerprint", {"exit": 0, "output": fp})

        note("mkdir-workdir", rrun(client, "mkdir -p %s" % wd, 30))
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(120)
        try:
            sftp.put(args.cpp, "%s/verify_health24_ext.cpp" % wd)
            note("upload-fixture", {"exit": 0, "output": args.cpp})
        finally:
            sftp.close()
        cext = rrun(client, "cd %s && g++ -std=c++17 -O1 -Wall -Wextra "
                            "-Isrc/core/include -Isrc/platform/include "
                            "%s/verify_health24_ext.cpp build-core/src/core/librailhmi_core.a "
                            "build-core/src/platform/librailhmi_platform.a "
                            "-o %s/verify_health24_ext 2>&1; echo GXX_EXIT=$?"
                    % (remote_dir, wd, wd), 240)
        note("compile-ext", cext)
        if "GXX_EXIT=0" not in cext.get("output", ""):
            save()
            return 1
        rext = rrun(client, "cd %s && %s/verify_health24_ext; echo EXT_EXIT=$?"
                    % (remote_dir, wd), 180)
        note("run-ext", rext)
        m = re.search(r"RESULT: (\w+) passed=(\d+) failed=(\d+)", rext.get("output", ""))
        ok = (rext.get("exit") == 0 and "EXT_EXIT=0" in rext.get("output", "")
              and m and m.group(1) == "PASS" and int(m.group(3)) == 0)
        note("ext-summary", {"exit": 0 if ok else 1,
             "output": "ext passed=%s failed=%s" % (m.group(2), m.group(3)) if m else "(no result line)"})

        note("cleanup", rrun(client,
             "rm -rf %s && echo fixture_cleanup_done && echo LEFT_LIST && "
             "ls -d %s 2>&1" % (wd, wd), 30))
    finally:
        client.close()
        save()

    for s in transcript["steps"]:
        tail = (s.get("output") or "").strip().splitlines()
        print("[%s] exit=%s %s" % (s.get("step"), s.get("exit"), tail[-1] if tail else ""))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
