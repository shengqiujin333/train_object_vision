#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-025 fix-round negative-control-only board driver (verifier-owned).

The v4 round passed every step except the negative control, whose failure
was a verifier fixture bug (staging() re-makedirs). The board build tree
/tmp/railhmi_build was already cleaned by that round's own cleanup step, so
this minimal driver re-uploads ONLY the files check_packaging.py reads
(checker + packaging/* + main.cpp + fault_catalog.{h,cpp} +
P6Maintenance.qml) to /tmp/railhmi_negfix and runs the FIXED NEG_FIX_PY
extracted from verify_packaging25_fixround.py against that copy.
Fingerprint-pinned; finite timeouts; cleanup by name.
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
import sys
import time
from pathlib import Path

try:
    import paramiko
except Exception as exc:  # noqa: BLE001
    sys.stderr.write("paramiko unavailable: %r\n" % (exc,))
    raise SystemExit(4)

UPLOAD = [
    "tools/check_packaging.py",
    "src/ui/src/main.cpp",
    "src/core/include/railhmi/core/fault_catalog.h",
    "src/core/src/fault_catalog.cpp",
    "src/ui/qml/pages/P6Maintenance.qml",
]
PACKAGING = ["railhmi.service", "install.sh", "VERSION", "UPGRADE.md", "RESOURCES.md"]


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
    ap = argparse.ArgumentParser(description="ITEM-025 negative-control-only driver")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--app-root", default=str(Path(__file__).resolve().parents[4] / "app"))
    args = ap.parse_args()

    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("credential env %s not loaded; refusing to guess\n"
                         % args.credential_env)
        return 2

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    tag = "VERIFY-NEGFIX-" + args.stamp
    wd = "/tmp/railhmi_negfix"
    app_root = Path(args.app_root)
    transcript: dict = {"invocation": {k: str(v) for k, v in vars(args).items()},
                        "host_key_fingerprint": None, "steps": []}
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

        # fresh upload of the checker-required file set
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(60)
        try:
            rrun(client, "rm -rf %s && mkdir -p %s/tools %s/packaging" % (wd, wd, wd), 30)
            for rel in UPLOAD:
                src = app_root / rel.replace("/", os.sep)
                dst = "%s/%s" % (wd, rel.replace(os.sep, "/"))
                parts = dst.rsplit("/", 1)
                rrun(client, "mkdir -p %s" % parts[0], 30)
                sftp.put(str(src), dst)
            note("upload-checker-set",
                 {"exit": 0, "output": "%d app files + packaging" % len(UPLOAD)})
            for f in PACKAGING:
                sftp.put(str(Path(__file__).resolve().parents[4] / "packaging" / f),
                         "%s/packaging/%s" % (wd, f))
            # NEG_FIX_PY from the fixed fix-round driver, board paths = wd
            body = (Path(__file__).resolve().parent
                    / "verify_packaging25_fixround.py").read_text(encoding="utf-8")
            m = re.search(r"NEG_FIX_PY = r'''\n(.*?)'''", body, re.S)
            if not m:
                note("neg-extract", {"exit": 1, "output": "NEG_FIX_PY block not found"})
                ok = False
            else:
                neg_src = m.group(1).replace(
                    "/tmp/railhmi_build/tools/check_packaging.py",
                    wd + "/tools/check_packaging.py").replace(
                    "/tmp/railhmi_build/src", wd + "/src").replace(
                    "/tmp/railhmi_build/packaging", wd + "/packaging")
                sftp.putfo(io.BytesIO(neg_src.encode("utf-8")), "%s/negfix.py" % wd)
                note("neg-extract", {"exit": 0, "output": "negfix.py (fixed staging)"})
        finally:
            sftp.close()
        if not ok:
            save()
            return 1

        neg = rrun(client, "cd %s && python3 negfix.py; echo NEG_DRIVER_EXIT=$?"
                   % wd, 180)
        note("negative-control", neg)
        no = neg.get("output", "")
        neg_ok = (neg.get("exit") == 0 and "NEG_DRIVER_EXIT=0" in no
                  and "NEG_HEALTHY_EXIT=0" in no and "NEG_D01_EXIT=1" in no
                  and "NEG_D02_EXIT=1" in no)
        note("negative-control-findings", {"exit": 0 if neg_ok else 1,
             "output": "neg_ok=%s" % neg_ok})
        if not neg_ok:
            ok = False

        res = note("cleanup", rrun(client,
             "rm -rf %s && echo negfix_cleanup_done && ls -d %s 2>&1" % (wd, wd), 30))
        cres = transcript["steps"][-1]
        cleanup_ok = ("negfix_cleanup_done" in cres.get("output", "")
                      and "cannot access" in cres.get("output", ""))
        note("cleanup-verify", {"exit": 0 if cleanup_ok else 1,
                                "output": "cleanup_ok=%s" % cleanup_ok})
        if not cleanup_ok:
            ok = False
    finally:
        client.close()
        save()

    print("host_key_fingerprint:", transcript["host_key_fingerprint"])
    for s in transcript["steps"]:
        tail = (s.get("output") or "").strip().splitlines()
        print("[%s] exit=%s %s" % (s.get("step"), s.get("exit"), tail[-1] if tail else ""))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
