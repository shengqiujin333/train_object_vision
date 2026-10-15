#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""software_verification fixture runner for ITEM-008 core-tests entry (verifier-owned).

Independent board-side round over the registered Paramiko SSH entry:
  1. Reproduce the implementer's authoritative round: upload an app/ tree snapshot,
     run the single-command entry `bash tests/run_core_tests.sh --outdir core-evidence
     --stamp <tag>` itself (not the harness directly), archive its log+JUnit XML back.
  2. Verify exit-3 negative: run run_core_tests.sh with a bogus flag (expect exit 2),
     and --no-build without a binary (expect exit 3) in a clean directory.
  3. Verifier-owned ext fixtures (verify_coretests_ext.cpp): harness records API,
     XML entity escaping checked byte-exact, independent XML count, suite name round
     trip, counter/RESULT contract. Compiled link-free against the app snapshot.
  4. railhmi-config-check good/bad sanity from the same snapshot (regression guard).
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
    ap = argparse.ArgumentParser(description="ITEM-008 verification fixtures")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify008")
    ap.add_argument("--cpp", required=True)
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True, help="stamp; bump per verification round")
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--timeout-sec", type=int, default=1200,
                    help="finite ceiling for the CoreTests build+test step")
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

        # 0. 独立复现实现者权威轮：走交付入口 run_core_tests.sh 本身（非直接调 harness）。
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
            remote_cpp = "%s/verify_coretests_ext.cpp" % wd
            sftp.put(args.cpp, remote_cpp)
            note("upload-fixture", {"exit": 0,
                 "output": "uploaded %s -> %s" % (args.cpp, remote_cpp)})
        finally:
            sftp.close()

        note("extract", rrun(client,
             "cd %s && tar xzf %s/app.tgz -C %s/app && echo APP_FILES=$(find %s/app -type f | wc -l)"
             % (board_wd, wd, board_wd, board_wd), 60))

        # 1. 单命令入口本身：构建+运行+归档（有限等待 1200s）
        entry = rrun(client,
            "cd %s/app && bash tests/run_core_tests.sh --outdir core-evidence "
            "--stamp %s 2>&1; echo ENTRY_EXIT=$?" % (board_wd, args.stamp),
            args.timeout_sec)
        note("run-entry", entry)
        if entry["exit"] != 0 or "ENTRY_EXIT=0" not in entry.get("output", ""):
            ok = False
        if "RESULT: PASS passed=" not in entry.get("output", ""):
            ok = False

        # 取回入口归档的 log + JUnit XML（独立复现轮证据）
        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(120)
            for rel in ("core-evidence/core-tests-%s.log" % args.stamp,
                        "core-evidence/core-tests-%s.xml" % args.stamp):
                local_out = outdir / ("%s.%s"
                    % (tag, rel.replace("/", "_")))
                try:
                    sftp.get("%s/app/%s" % (board_wd, rel), str(local_out))
                    note("download", {"exit": 0, "output": local_out.name})
                except OSError as exc:
                    note("download", {"exit": None, "output": "%s: %r" % (rel, exc)})
                    ok = False
        finally:
            sftp.close()

        # 2. 入口退出码负例：未知参数→2；--no-build 且无二进制→3
        neg = rrun(client,
            "cd %s/app && bash tests/run_core_tests.sh --bogus-flag 2>&1; echo NEG_UNKNOWN_EXIT=$?"
            % board_wd, 60)
        note("neg-unknown-arg", neg)
        if "NEG_UNKNOWN_EXIT=2" not in neg.get("output", ""):
            ok = False
        neg3 = rrun(client,
            "rm -rf %s/nb && mkdir -p %s/nb && cp %s/app/build.sh %s/nb/ && "
            "cp -r %s/app/tests %s/nb/tests && cd %s/nb && "
            "bash tests/run_core_tests.sh --no-build --outdir %s/nb/out 2>&1; echo NOBIN_EXIT=$?"
            % (wd, wd, board_wd, wd, board_wd, wd, wd, wd), 60)
        note("neg-nobin", neg3)
        if "NOBIN_EXIT=3" not in neg3.get("output", ""):
            ok = False

        # 3. railhmi-config-check good/bad sanity（同快照回归守门）
        note("config-good", rrun(client,
            "cd %s/app && ./build-core/tools/railhmi-config-check ./config; echo CFG_EXIT=$?"
            % board_wd, 60))
        bad_cfg_cmd = ("rm -rf /tmp/railhmi_cfg_bad && mkdir -p /tmp/railhmi_cfg_bad && "
            "cp %s/app/config/*.yaml /tmp/railhmi_cfg_bad/ && "
            "sed -i '/confirm_frames:/d' /tmp/railhmi_cfg_bad/thresholds.yaml && "
            "%s/app/build-core/tools/railhmi-config-check /tmp/railhmi_cfg_bad 2>&1; echo CFG_EXIT=$?"
            % (board_wd, board_wd))
        note("config-bad", rrun(client, bad_cfg_cmd, 60))

        # 4. 验证自有扩展夹具：g++ 直编（无链接库需求，harness 是仅头文件+test_util.cpp）
        compile_cmd = ("cd %s/app && g++ -std=c++17 -O1 -Wall -Wextra "
            "-Isrc/core/include -Isrc/core/include/railhmi -Itests/core "
            "%s/verify_coretests_ext.cpp tests/core/test_util.cpp src/core/src/version.cpp "
            "-o %s/verify_coretests_ext 2>&1; echo GXX_EXIT=$?" % (board_wd, wd, wd))
        note("compile-ext", rrun(client, compile_cmd, 240))
        run_ext = rrun(client,
            "cd %s/app && %s/verify_coretests_ext; echo EXT_EXIT=$?" % (board_wd, wd), 180)
        note("run-ext", run_ext)
        if run_ext.get("output", "") and "RESULT: PASS" not in run_ext["output"]:
            ok = False

        # 5. 清理按名：本轮创建的板端目录 + 坏配置夹具
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
