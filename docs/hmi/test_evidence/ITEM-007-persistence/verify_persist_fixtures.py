#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""software_verification fixture runner for ITEM-007 persistence layer (verifier-owned).

Runs on the registered execution PC, talks to the registered compute_host SSH entry only:
  1. Upload an app/ tree snapshot, build CoreTests on the board and run railhmi-core-tests
     to independently reproduce the implementer's 230/230 board run.
  2. Upload verify_persist_ext.cpp into a verifier-owned workdir, compile it against the
     app snapshot kept on the board, and run it (extended edge fixtures: SIGKILK mid-write
     WAL recovery, retention boundaries, gap edges, DDL conformity, repo semantics).
  3. railhmi-config-check good/bad sanity from the same snapshot.
Fingerprint mismatch aborts. Credentials only from the named env var, never logged.
Every remote command uses a finite timeout; all workdirs created for this round are
removed by name at the end and the cleanup is recorded.
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
    ap = argparse.ArgumentParser(description="ITEM-007 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify007")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--app-root", required=True,
                    help="local app/ tree to upload and build on the board")
    ap.add_argument("--timeout-sec", type=int, default=1200,
                    help="finite ceiling for the CoreTests build+test step")
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

    ok = True
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

        # 1. 独立复现实现者权威轮：上传 app/ 快照并板端构建 + 运行 CoreTests。
        note("mkdir", rrun(client, "rm -rf %s %s && mkdir -p %s/app %s" %
                           (wd, board_wd, board_wd + "/app", wd), 30))

        buf = io.BytesIO()
        with tarfile.open(fileobj=buf, mode="w:gz") as tf:
            for p in sorted(Path(args.app_root).rglob("*")):
                if p.is_file():
                    tf.add(str(p), arcname=str(p.relative_to(args.app_root)))
        data = buf.getvalue()
        buf.seek(0)
        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(120)
            remote_tar = "%s/app.tgz" % wd
            sftp.putfo(buf, remote_tar, len(data))
            note("upload-app", {"exit": 0,
                 "output": "uploaded app tree (%d bytes compressed)" % len(data)})
            remote_cpp = "%s/verify_persist_ext.cpp" % wd
            sftp.put(args.cpp, remote_cpp)
            note("upload-fixture", {"exit": 0,
                 "output": "uploaded %s -> %s" % (args.cpp, remote_cpp)})
        finally:
            sftp.close()

        note("extract", rrun(client,
             "cd %s && tar xzf %s/app.tgz -C %s/app && echo APP_FILES=$(find %s/app -type f | wc -l)"
             % (board_wd, wd, board_wd, board_wd), 60))

        # 板端构建 CoreTests（有限等待 1200s；build.sh 模式名大小写敏感 = CoreTests）
        build = rrun(client,
            "cd %s/app && bash build.sh CoreTests 2>&1; echo BUILDEXIT=$?" % board_wd,
            args.timeout_sec)
        note("build-coretests", build)
        if build["exit"] != 0 and build["exit"] is not None:
            ok = False

        tests = rrun(client,
            "cd %s/app/build-core/tests && ./railhmi-core-tests; echo TESTEXIT=$?" % board_wd, 180)
        note("run-coretests", tests)
        if "0 failed" not in tests["output"]:
            ok = False

        note("config-good", rrun(client,
            "cd %s/app && ./build-core/tools/railhmi-config-check ./config; echo CFG_EXIT=$?" % board_wd, 60))
        bad_cfg_cmd = ("rm -rf /tmp/railhmi_cfg_bad && mkdir -p /tmp/railhmi_cfg_bad && "
            "cp %s/app/config/*.yaml /tmp/railhmi_cfg_bad/ && "
            "sed -i '/confirm_frames:/d' /tmp/railhmi_cfg_bad/thresholds.yaml && "
            "%s/app/build-core/tools/railhmi-config-check /tmp/railhmi_cfg_bad 2>&1; echo CFG_EXIT=$?"
            % (board_wd, board_wd))
        note("config-bad", rrun(client, bad_cfg_cmd, 60))

        # 2. 验证自有扩展夹具：g++ 直编 core/platform 业务源 + 运行（与主要测试独立）
        compile_cmd = ("cd %s/app && g++ -std=c++17 -O0 -Wall -Wextra "
            "-Isrc/core/include -Isrc/platform/include -Ithird_party/sqlite "
            "-DRAILHMI_HAVE_SQLITE=1 %s/verify_persist_ext.cpp "
            "src/core/src/persistence_model.cpp src/core/src/retention.cpp src/core/src/timebase.cpp "
            "src/platform/src/sqlite_store.cpp src/platform/src/repository.cpp "
            "-L/lib/aarch64-linux-gnu -l:libsqlite3.so.0 "
            "-o %s/verify_persist_ext 2>&1; echo GXX_EXIT=$?" % (board_wd, wd, wd))
        note("compile-ext", rrun(client, compile_cmd, 240))
        run_ext = rrun(client,
            "cd %s/app && %s/verify_persist_ext; echo EXT_EXIT=$?" % (board_wd, wd), 180)
        note("run-ext", run_ext)
        if run_ext.get("output", "") and "0 failed" not in run_ext["output"]:
            ok = False

        # 3. 清理按名：本轮创建的板端目录 + 坏配置夹具
        note("cleanup", rrun(client,
            "rm -rf %s %s /tmp/railhmi_cfg_bad && echo fixture_cleanup_done && echo LEFT_LIST && "
            "ls -d %s %s 2>&1" % (wd, board_wd, wd, board_wd), 30))
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
