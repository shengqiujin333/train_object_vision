#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""software_verification fixture runner for ITEM-009 simulation/fault-injection (verifier-owned).

Independent board-side round over the registered Paramiko SSH entry:
  1. Reproduce the implementer's authoritative round: upload the app/ tree snapshot,
     run the deliverable entry `bash tests/run_core_tests.sh` (builds railhmi_core with
     simulation.cpp + railhmi-sim-inject, runs 274 tests incl. test_simulation.cpp),
     archive its log+JUnit XML back.
  2. Reproduce the sim-inject CLI round with the implementer's exact arguments
     (--fi 01,02,03,04,05,10 --duration 6000) and archive log+JSON back.
  3. CLI negatives and determinism: invalid --fi -> exit 2, unknown arg -> exit 2,
     --list -> 0; two runs with identical args are byte-identical (determinism at the
     CLI boundary); custom camera/profile/step args produce an injection line recording
     the parameters.
  4. Verifier-owned ext fixtures (verify_sim_ext.cpp): fault window containment and
     recovery, FI-01-over-FI-02 precedence, FI-02 full state progression, FI-10 gap
     merge envelope, FI-04/FI-05 on/off partitioning, FI-03 no-recovery, determinism on
     a second config, observation format contract, parse_fault alias/negative matrix.
     Links against librailhmi_core.a built by the entry itself.
Fingerprint mismatch aborts. Credentials only from the named env var, never logged.
Every remote step has a finite timeout; workdirs are removed by name at the end.
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
    ap = argparse.ArgumentParser(description="ITEM-009 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify009")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True, help="stamp; bump per verification round")
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--timeout-sec", type=int, default=1200,
                    help="finite ceiling for the entry build+test step")
    ap.add_argument("--app-root", default=str(Path(__file__).resolve().parents[4] / "app"),
                    help="local app/ tree to upload and build on the board")
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
    transcript: dict = {"invocation": vars(args),
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

    def dl(sftp, remote_rel: str, local_name: str) -> None:
        local_out = outdir / ("%s.%s" % (tag, local_name))
        try:
            sftp.get("%s/%s" % (board_wd, remote_rel), str(local_out))
            note("download", {"exit": 0, "output": local_name})
        except OSError as exc:
            note("download", {"exit": None, "output": "%s: %r" % (remote_rel, exc)})
            return False
        return True

    def sftp_channel_timeout(client, seconds: int):
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(seconds)
        return sftp

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

        # 0. 独立复现实现者权威轮：上传 app 快照到独立目录
        note("mkdir", rrun(client, "rm -rf %s %s && mkdir -p %s/app %s"
                           % (wd, board_wd, board_wd, wd), 30))

        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:gz") as tf:
            for p in sorted(Path(args.app_root).rglob("*")):
                if p.is_file():
                    tf.add(str(p), arcname=str(p.relative_to(args.app_root)))
        data = buf.getvalue()
        buf.seek(0)
        sftp = sftp_channel_timeout(client, 120)
        try:
            remote_tar = "%s/app.tgz" % wd
            sftp.putfo(buf, remote_tar, len(data))
            note("upload-app", {"exit": 0,
                 "output": "uploaded app tree (%d bytes compressed)" % len(data)})
            remote_cpp = "%s/verify_sim_ext.cpp" % wd
            sftp.put(args.cpp, remote_cpp)
            note("upload-fixture", {"exit": 0,
                 "output": "uploaded %s -> %s" % (args.cpp, remote_cpp)})
        finally:
            sftp.close()

        note("extract", rrun(client,
             "cd %s && tar xzf %s/app.tgz -C %s/app && echo APP_FILES=$(find %s/app -type f | wc -l)"
             % (board_wd, wd, board_wd, board_wd), 60))

        # 1. 交付入口本身（构建 railhmi_core + sim-inject + 274 测试；有限等待）
        entry = rrun(client,
            "cd %s/app && bash tests/run_core_tests.sh --outdir core-evidence "
            "--stamp %s 2>&1; echo ENTRY_EXIT=$?" % (board_wd, args.stamp),
            args.timeout_sec)
        note("run-entry", entry)
        if entry["exit"] != 0 or "ENTRY_EXIT=0" not in entry.get("output", "") or \
                "RESULT: PASS passed=" not in entry.get("output", ""):
            ok = False

        sftp = sftp_channel_timeout(client, 120)
        try:
            ok &= dl(sftp, "app/core-evidence/core-tests-%s.log" % args.stamp,
                     "core-evidence_core-tests-%s.log" % args.stamp)
            ok &= dl(sftp, "app/core-evidence/core-tests-%s.xml" % args.stamp,
                     "core-evidence_core-tests-%s.xml" % args.stamp)
        finally:
            sftp.close()

        # 2. sim-inject 权威轮复现（实现者同参数）
        sim = rrun(client,
            "cd %s/app && ./build-core/tools/railhmi-sim-inject "
            "--fi 01,02,03,04,05,10 --duration 6000 "
            "--out core-evidence/sim-inject-%s.log "
            "--json core-evidence/sim-inject-%s.json 2>&1; echo SIM_EXIT=$?"
            % (board_wd, args.stamp, args.stamp), 60)
        note("sim-inject", sim)
        if sim["exit"] != 0 or "SIM_EXIT=0" not in sim.get("output", ""):
            ok = False
        sftp = sftp_channel_timeout(client, 120)
        try:
            ok &= dl(sftp, "app/core-evidence/sim-inject-%s.log" % args.stamp,
                     "core-evidence_sim-inject-%s.log" % args.stamp)
            ok &= dl(sftp, "app/core-evidence/sim-inject-%s.json" % args.stamp,
                     "core-evidence_sim-inject-%s.json" % args.stamp)
        finally:
            sftp.close()

        # 3. CLI 负例与确定性
        negfi = rrun(client,
            "cd %s/app && ./build-core/tools/railhmi-sim-inject --fi 01,99 2>&1; echo NEGFI_EXIT=$?"
            % board_wd, 60)
        note("neg-invalid-fi", negfi)
        if "NEGFI_EXIT=2" not in negfi.get("output", ""):
            ok = False
        negarg = rrun(client,
            "cd %s/app && ./build-core/tools/railhmi-sim-inject --bogus-arg 2>&1; echo NEGARG_EXIT=$?"
            % board_wd, 60)
        note("neg-unknown-arg", negarg)
        if "NEGARG_EXIT=2" not in negarg.get("output", ""):
            ok = False
        listing = rrun(client,
            "cd %s/app && ./build-core/tools/railhmi-sim-inject --list; echo LIST_EXIT=$?"
            % board_wd, 60)
        note("sim-list", listing)
        if "LIST_EXIT=0" not in listing.get("output", "") or \
                "FI-05 ptz_out_of_tolerance" not in listing.get("output", ""):
            ok = False
        det = rrun(client,
            "cd %s/app && ./build-core/tools/railhmi-sim-inject --camera CAM-99 "
            "--profile front --duration 4000 --step 250 --onset 1000 --recovery 3000 "
            "--fi 01 --out /tmp/sim_det_a.log >/dev/null 2>&1 && "
            "./build-core/tools/railhmi-sim-inject --camera CAM-99 "
            "--profile front --duration 4000 --step 250 --onset 1000 --recovery 3000 "
            "--fi 01 --out /tmp/sim_det_b.log >/dev/null 2>&1 && "
            "sha256sum /tmp/sim_det_a.log /tmp/sim_det_b.log; echo DET_EXIT=$?"
            % board_wd, 60)
        note("sim-determinism", det)
        det_lines = det.get("output", "").splitlines()
        hashes = [l.split()[0] for l in det_lines if len(l.split()) >= 2 and
                  l.split()[1] in ("/tmp/sim_det_a.log", "/tmp/sim_det_b.log")]
        if det.get("exit") != 0 or len(hashes) != 2 or hashes[0] != hashes[1]:
            ok = False
        cust = rrun(client,
            "cd %s/app && ./build-core/tools/railhmi-sim-inject --camera CAM-42 "
            "--profile pantograph --duration 1000 --step 100 --onset 0 --recovery 0 "
            "--fi 05 --no-detections; echo CUST_EXIT=$?" % board_wd, 60)
        note("sim-custom-args", cust)
        if "CUST_EXIT=0" not in cust.get("output", "") or \
                "camera=CAM-42" not in cust.get("output", "") or \
                "profile=pantograph" not in cust.get("output", "") or \
                "tolerance_ok=false" not in cust.get("output", ""):
            ok = False

        # 4. 验证自有扩展夹具：链接入口刚构建出的 librailhmi_core.a
        probe = rrun(client, "find %s -name 'librailhmi_core.a'; echo PROBE_EXIT=$?"
                     % r"%s/app/build-core" % board_wd, 60)
        note("probe-lib", probe)
        lib_path = probe.get("output", "").splitlines()[0].strip() if probe.get("output") else ""
        if not lib_path or "librailhmi_core.a" not in lib_path:
            lib_path = r"%s/app/build-core/src/core/librailhmi_core.a" % board_wd
        compile_cmd = ("cd %s/app && g++ -std=c++17 -O1 -Wall -Wextra -Isrc/core/include "
            "%s/verify_sim_ext.cpp %s -o %s/verify_sim_ext 2>&1; echo GXX_EXIT=$?"
            % (board_wd, wd, lib_path, wd))
        note("compile-ext", rrun(client, compile_cmd, 240))
        run_ext = rrun(client,
            "%s/verify_sim_ext; echo EXT_EXIT=$?" % (wd,), 180)
        note("run-ext", run_ext)
        if "RESULT: PASS passed=" not in run_ext.get("output", "") or \
                run_ext.get("exit") != 0:
            ok = False

        # 5. 清理按名
        note("cleanup", rrun(client,
            "rm -rf %s %s /tmp/sim_det_a.log /tmp/sim_det_b.log && echo fixture_cleanup_done "
            "&& echo LEFT_LIST && ls -d %s %s 2>&1" % (wd, board_wd, wd, board_wd), 30))
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
