#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 板端构建运行器 (ITEM-003)

沿项目登记的 Paramiko SSH 入口，把 app/ 工程上传到目标板并以有限等待运行 build.sh 三档之一，
取回构建/测试输出与产物清单。用于在“目标本机构建”环境下验证工程骨架。

安全与边界:
  * 受信任 known_hosts（load_system_host_keys + 默认 RejectPolicy）；不使用 AutoAddPolicy；
    主机指纹以 SHA256 打印供用户核验。
  * 连接/命令/SFTP 全部显式有限超时；凭据仅从环境变量读取，不落日志。
  * 仅写入目标 /tmp 下的独立工作目录；不安装/不修改系统、不重启、不按名杀进程。

用法:
  python app/tools/board_build.py --host <addr> --user <user> \
      --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode CoreTests \
      --outdir docs/hmi/test_evidence/ITEM-003-build --stamp <tag> --run-tests

退出码: 0 完成（构建成功与否见 transcript）；2 连接/凭据失败；4 paramiko 缺失。
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
    sys.stderr.write("paramiko 不可用: %r\n" % (exc,))
    raise SystemExit(4)

SKIP_DIRS = {"build", "__pycache__", ".git", ".cache"}
SKIP_PREFIXES = ("build-",)


def fingerprint(key) -> str:
    try:
        return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")
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


def ensure_dirs(sftp, remote_dir: str, created: set) -> None:
    if remote_dir in created or remote_dir in ("", "/"):
        return
    parent = remote_dir.rsplit("/", 1)[0]
    if parent and parent != remote_dir:
        ensure_dirs(sftp, parent, created)
    try:
        sftp.stat(remote_dir)
    except OSError:
        try:
            sftp.mkdir(remote_dir)
        except OSError:
            pass
    created.add(remote_dir)


def upload_tree(sftp, local_dir: Path, remote_dir: str) -> int:
    created: set = set()
    n = 0
    for root, dirs, files in os.walk(local_dir):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS and not d.startswith(SKIP_PREFIXES)]
        for fn in files:
            lp = Path(root) / fn
            rel = lp.relative_to(local_dir).as_posix()
            rp = "%s/%s" % (remote_dir, rel)
            ensure_dirs(sftp, rp.rsplit("/", 1)[0], created)
            sftp.put(str(lp), rp)
            n += 1
    return n


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi board build runner")
    ap.add_argument("--host", default="")
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", default="")
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--local-dir", default=str(Path(__file__).resolve().parents[1]))
    ap.add_argument("--remote-dir", default="/tmp/railhmi_build")
    ap.add_argument("--mode", default="CoreTests", choices=["CoreTests", "Release", "Preview", "MediaCheck", "TestRun", "check"])
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", default=time.strftime("%Y%m%d-%H%M%S"))
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--build-timeout", type=int, default=900)
    ap.add_argument("--run-tests", action="store_true")
    ap.add_argument("--keep-remote", action="store_true")
    args = ap.parse_args()

    if not args.host or not args.user:
        sys.stderr.write("需要 --host 与 --user（连接参数来自项目环境配置，不写死）\n")
        return 2
    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("凭据环境变量 %s 未加载；不猜测凭据。\n" % args.credential_env)
        return 2

    local = Path(args.local_dir).resolve()
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    tag = "build-%s-%s-%s" % (args.mode, args.host.replace(".", "_"), args.stamp)
    transcript_path = outdir / (tag + ".transcript.json")

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    transcript: dict = {"invocation": vars(args), "host_key_fingerprint": None, "steps": []}

    def note(step, res):
        transcript["steps"].append({"step": step, **res})

    try:
        client.connect(hostname=args.host, port=args.port, username=args.user, password=secret,
                       timeout=args.connect_timeout, auth_timeout=args.connect_timeout,
                       banner_timeout=args.connect_timeout, look_for_keys=False, allow_agent=False)
    except Exception as exc:  # noqa: BLE001
        note("connect", {"exit": None, "output": "connect error: %r" % (exc,)})
        transcript_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
        sys.stderr.write("连接失败（未核验在线）: %r；见 %s\n" % (exc, transcript_path))
        return 2

    tr = client.get_transport()
    if tr is not None and tr.get_remote_server_key() is not None:
        transcript["host_key_fingerprint"] = fingerprint(tr.get_remote_server_key())

    remote = args.remote_dir
    try:
        note("mkdir", rrun(client, "mkdir -p %s" % remote, 20))
        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(args.connect_timeout * 6)
            count = upload_tree(sftp, local, remote)
            note("upload", {"exit": 0, "output": "uploaded %d files from %s" % (count, local)})
        finally:
            sftp.close()

        if args.mode == "TestRun":
            tokens_yaml = Path(args.local_dir).resolve().parent / "docs" / "hmi" / "design_tokens.yaml"
            if tokens_yaml.is_file():
                sftp = client.open_sftp()
                try:
                    sftp.put(str(tokens_yaml), remote + "/design_tokens.yaml")
                    note("upload-tokens", {"exit": 0, "output": "design_tokens.yaml"})
                finally:
                    sftp.close()
            perm_doc = Path(args.local_dir).resolve().parent / "docs" / "hmi" / "PERMISSION_MATRIX.md"
            if perm_doc.is_file():
                sftp = client.open_sftp()
                try:
                    sftp.put(str(perm_doc), remote + "/PERMISSION_MATRIX.md")
                    note("upload-perm-matrix", {"exit": 0, "output": "PERMISSION_MATRIX.md"})
                finally:
                    sftp.close()
            trun = rrun(client, "cd %s && bash tests/run_core_tests.sh --outdir core-evidence --stamp %s; "
                                "echo TESTRUN_EXIT=$?" % (remote, args.stamp), args.build_timeout)
            note("testrun", trun)
            note("tokens-selftest", rrun(
                client, "cd %s && python3 tools/gen_tokens.py --selftest --yaml design_tokens.yaml; "
                        "echo TOKENS_EXIT=$?" % remote, 120))
            note("ui-scan", rrun(
                client, "cd %s && python3 tools/check_no_hardcoded_ui.py --root src/ui; echo SCAN_EXIT=$?; "
                        "python3 tools/check_replay_wiring.py --root src/ui; echo REPLAY_WIRE_EXIT=$?; "
                        "python3 tools/check_ai_debug_wiring.py --root src/ui --tokens design_tokens.yaml; echo AIDBG_WIRE_EXIT=$?; "
                        "python3 tools/check_maintenance_wiring.py --root src/ui; echo MAINT_WIRE_EXIT=$?; "
                        "python3 tools/check_permissions_wiring.py --root src/ui --matrix PERMISSION_MATRIX.md; echo PERM_WIRE_EXIT=$?; "
                        "python3 tools/check_health_wiring.py --root src/ui; echo HEALTH_WIRE_EXIT=$?" % remote, 60))
            sim = rrun(client, "cd %s && ./build-core/tools/railhmi-sim-inject "
                              "--fi 01,02,03,04,05,10 --duration 6000 "
                              "--out core-evidence/sim-inject-%s.log --json core-evidence/sim-inject-%s.json; "
                              "echo SIM_EXIT=$?" % (remote, args.stamp, args.stamp), args.build_timeout)
            note("sim-inject", sim)
            note("status-probe", rrun(
                client, "cd %s && ./build-core/tools/railhmi-status-probe --scenario mixed --role maintainer "
                        "--out core-evidence/status-probe-%s.log; echo STATUS_EXIT=$?; "
                        "./build-core/tools/railhmi-status-probe --scenario mixed --role driver "
                        "--out core-evidence/status-probe-driver-%s.log; echo STATUS_EXIT=$?; "
                        "./build-core/tools/railhmi-ai-probe --config ./config "
                        "--out core-evidence/ai-probe-%s.log; echo AI_EXIT=$?; "
                        "./build-core/tools/railhmi-alarm-probe "
                        "--out core-evidence/alarm-probe-%s.log; echo ALARM_EXIT=$?; "
                        "./build-core/tools/railhmi-clip-probe --dir /tmp/railhmi_clips "
                        "--out core-evidence/clip-probe-%s.log; echo CLIP_EXIT=$?; "
                        "./build-core/tools/railhmi-replay-probe "
                        "--out core-evidence/replay-probe-%s.log; echo REPLAY_EXIT=$?; "
                        "./build-core/tools/railhmi-maintenance-probe "
                        "--out core-evidence/maintenance-probe-%s.log; echo MAINT_EXIT=$?; "
                        "./build-core/tools/railhmi-permissions-probe "
                        "--out core-evidence/permissions-probe-%s.log; echo PERM_EXIT=$?; "
                        "./build-core/tools/railhmi-health-probe "
                        "--out core-evidence/health-probe-%s.log; echo HEALTH_EXIT=$?" % (remote, args.stamp, args.stamp, args.stamp, args.stamp, args.stamp, args.stamp, args.stamp, args.stamp, args.stamp), 60))
            sftp = client.open_sftp()
            try:
                sftp.get_channel().settimeout(args.connect_timeout * 6)
                for rel in ("core-evidence/core-tests-%s.log" % args.stamp,
                            "core-evidence/core-tests-%s.xml" % args.stamp,
                            "core-evidence/sim-inject-%s.log" % args.stamp,
                            "core-evidence/sim-inject-%s.json" % args.stamp,
                            "core-evidence/status-probe-%s.log" % args.stamp,
                            "core-evidence/status-probe-driver-%s.log" % args.stamp,
                            "core-evidence/ai-probe-%s.log" % args.stamp,
                            "core-evidence/alarm-probe-%s.log" % args.stamp,
                            "core-evidence/clip-probe-%s.log" % args.stamp,
                            "core-evidence/replay-probe-%s.log" % args.stamp,
                            "core-evidence/maintenance-probe-%s.log" % args.stamp,
                            "core-evidence/permissions-probe-%s.log" % args.stamp,
                            "core-evidence/health-probe-%s.log" % args.stamp):
                    local_out = outdir / ("%s.%s" % (tag, rel.replace("/", "_")))
                    try:
                        sftp.get("%s/%s" % (remote, rel), str(local_out))
                        note("download", {"exit": 0, "output": local_out.name})
                    except OSError as exc:
                        note("download", {"exit": None, "output": "%s: %r" % (rel, exc)})
            finally:
                sftp.close()
        else:
            build = rrun(client, "cd %s && bash build.sh %s; echo BUILD_EXIT=$?" % (remote, args.mode),
                         args.build_timeout)
            note("build", build)

            if args.run_tests and args.mode == "CoreTests":
                run = rrun(client, "cd %s && ./build-core/tests/railhmi-core-tests; echo TEST_EXIT=$?" % remote,
                           args.build_timeout)
                note("tests", run)
                note("config-check-good", rrun(
                    client, "cd %s && ./build-core/tools/railhmi-config-check ./config; echo CFG_EXIT=$?" % remote, 60))
                bad = ("cd %s && rm -rf /tmp/railhmi_cfg_bad && mkdir -p /tmp/railhmi_cfg_bad "
                       "&& cp config/*.yaml /tmp/railhmi_cfg_bad/ "
                       "&& sed -i '/confirm_frames/d' /tmp/railhmi_cfg_bad/thresholds.yaml "
                       "&& ./build-core/tools/railhmi-config-check /tmp/railhmi_cfg_bad; echo CFG_EXIT=$?; "
                       "rm -rf /tmp/railhmi_cfg_bad" % remote)
                note("config-check-bad", rrun(client, bad, 60))

            if args.mode == "MediaCheck":
                note("stream-probe", rrun(
                    client, "cd %s && mkdir -p core-evidence && ./build-media/tools/railhmi-stream-probe "
                            "--source test --duration 6 --cut-after 3 "
                            "--out core-evidence/stream-probe-%s.log --json core-evidence/stream-probe-%s.json; "
                            "echo PROBE_EXIT=$?" % (remote, args.stamp, args.stamp), args.build_timeout))
                note("multi-probe", rrun(
                    client, "cd %s && ./build-media/tools/railhmi-multi-probe "
                            "--count 6 --source test --duration 8 --cut-index 3 --cut-after 4 "
                            "--out core-evidence/multi-probe-%s.log --json core-evidence/multi-probe-%s.json; "
                            "echo MULTI_EXIT=$?" % (remote, args.stamp, args.stamp), args.build_timeout))
                note("ai-runner", rrun(
                    client, "cd %s && ./build-media/tools/railhmi-ai-runner --config ./config "
                            "--duration 4 --out core-evidence/ai-runner-%s.log; echo AIRUN_EXIT=$?" % (remote, args.stamp),
                    args.build_timeout))
                sftp = client.open_sftp()
                try:
                    sftp.get_channel().settimeout(args.connect_timeout * 6)
                    for rel in ("core-evidence/stream-probe-%s.log" % args.stamp,
                                "core-evidence/stream-probe-%s.json" % args.stamp,
                                "core-evidence/multi-probe-%s.log" % args.stamp,
                                "core-evidence/multi-probe-%s.json" % args.stamp,
                                "core-evidence/ai-runner-%s.log" % args.stamp):
                        local_out = outdir / ("%s.%s" % (tag, rel.replace("/", "_")))
                        try:
                            sftp.get("%s/%s" % (remote, rel), str(local_out))
                            note("download", {"exit": 0, "output": local_out.name})
                        except OSError as exc:
                            note("download", {"exit": None, "output": "%s: %r" % (rel, exc)})
                finally:
                    sftp.close()

        art_targets = {
            "CoreTests": ["build-core/tests/railhmi-core-tests"],
            "MediaCheck": ["build-media/src/media/librailhmi_media.a", "build-media/tools/railhmi-stream-probe", "build-media/tools/railhmi-multi-probe", "build-media/tools/railhmi-ai-runner"],
            "Release": ["build-target/railhmi", "build-target/src/media/librailhmi_media.a"],
            "Preview": ["build-preview/railhmi"],
            "check": [],
        }.get(args.mode, [])
        art_cmd = "cd %s; %s true" % (
            remote,
            " ".join("ls -l %s 2>&1;" % t for t in art_targets) if art_targets else "echo '(check mode: no artifacts)';")
        note("artifacts", rrun(client, art_cmd, 20))

        if not args.keep_remote:
            note("cleanup", rrun(client, "rm -rf %s && echo cleaned" % remote, 20))
    finally:
        client.close()

    transcript_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
    # plain-text log for quick reading
    log_path = outdir / (tag + ".log")
    with log_path.open("w", encoding="utf-8") as fh:
        fh.write("host_key_fingerprint: %s\n" % transcript["host_key_fingerprint"])
        for s in transcript["steps"]:
            fh.write("\n===== [%s] exit=%s =====\n" % (s.get("step"), s.get("exit")))
            fh.write((s.get("output") or "") + "\n")

    print("host_key_fingerprint:", transcript["host_key_fingerprint"])
    for s in transcript["steps"]:
        tail = (s.get("output") or "").strip().splitlines()
        print("[%s] exit=%s %s" % (s.get("step"), s.get("exit"), tail[-1] if tail else ""))
    print("transcript:", transcript_path)
    print("log:", log_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
