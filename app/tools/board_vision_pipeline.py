#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 真实检测器接入与调度板端执行入口（视觉推理 ITEM-009）

沿项目登记的 Paramiko SSH 入口，一次有界会话内完成：
  1) 上传 app/ 工程与 models/ 资产到目标板独立临时目录（仅 /tmp）；
  2) 构建 CoreTests 并运行 railhmi-core-tests（含工作线程调度/温度语义单测），取回 JUnit；
  3) 构建 MediaCheck（GStreamer + librga + dlopen 版 RKNN 会话）；
  4) 运行 railhmi-vision-pipeline --selftest（确定性线程/队列/调度/温度自检，stub 检测器）；
  5) 运行真实链路 --mode chain（test 源 -> FrameLease -> ai_dispatcher 队列 -> RKNN -> envelope）；
  6) 运行 4 路 + 预算裁剪的真实派发（--budget 1）证明预算与无饥饿轮转；
  7) 取回全部证据并清理远端临时目录。

安全与边界：受信任 known_hosts + 默认 RejectPolicy；连接/命令/SFTP 全部显式有限超时；
凭据仅从环境变量读取、不落日志；不安装/不修改系统、不重启、不按名杀进程；只写目标 /tmp。
不连接任何真实相机（RTSP 属 ENV-D，不在本轮）。

用法:
  python app/tools/board_vision_pipeline.py --host <addr> --user <user> \
      --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
      --outdir docs/vision/test_evidence/ITEM-009 --stamp <tag> \
      [--only build,core-tests,selftest,chain,chain4] [--keep-remote]

退出码: 0 完成；2 连接/凭据失败；4 paramiko 缺失。
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


def upload_tree(sftp, local_dir: Path, remote_dir: str, skip_dirs=SKIP_DIRS) -> int:
    created: set = set()
    n = 0
    for root, dirs, files in os.walk(local_dir):
        dirs[:] = [d for d in dirs if d not in skip_dirs and not d.startswith(SKIP_PREFIXES)]
        for fn in files:
            lp = Path(root) / fn
            rel = lp.relative_to(local_dir).as_posix()
            rp = "%s/%s" % (remote_dir, rel)
            ensure_dirs(sftp, rp.rsplit("/", 1)[0], created)
            sftp.put(str(lp), rp)
            n += 1
    return n


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi board vision pipeline runner (ITEM-009)")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--local-app", default=str(Path(__file__).resolve().parents[1]))
    ap.add_argument("--local-models", default=str(Path(__file__).resolve().parents[2] / "models"))
    ap.add_argument("--remote-dir", default="")
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", default=time.strftime("%Y%m%d-%H%M%S"))
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--build-timeout", type=int, default=1200)
    ap.add_argument("--command-timeout", type=int, default=300)
    ap.add_argument("--chain-duration", type=int, default=12)
    ap.add_argument("--chain4-duration", type=int, default=10)
    ap.add_argument("--only", default="build,core-tests,selftest,chain,chain4")
    ap.add_argument("--keep-remote", action="store_true")
    args = ap.parse_args()

    steps = {s.strip() for s in args.only.split(",") if s.strip()}
    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("凭据环境变量 %s 未加载；不猜测凭据。\n" % args.credential_env)
        return 2

    local_app = Path(args.local_app).resolve()
    local_models = Path(args.local_models).resolve()
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    remote = args.remote_dir or ("/tmp/railhmi_vision_item009_%s" % args.stamp)
    tag = "vision-item009-%s-%s" % (args.host.replace(".", "_"), args.stamp)
    transcript_path = outdir / (tag + ".transcript.json")
    log_path = outdir / (tag + ".log")
    ev = "%s/app/core-evidence" % remote

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

    downloads = []
    rels = []
    try:
        note("env", rrun(client, "uname -smr; nproc; g++ --version | head -1; "
                                 "ls -l /usr/lib/librknnrt.so /usr/lib/aarch64-linux-gnu/librknnrt.so 2>&1 | head -5; "
                                 "ls -d /sys/class/thermal/thermal_zone* 2>&1 | head -8; "
                                 "cat /sys/class/thermal/thermal_zone0/temp 2>&1", args.connect_timeout))
        note("mkdir", rrun(client, "mkdir -p %s %s/model-assets && echo ok" % (ev, remote), 20))

        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(args.connect_timeout * 12)
            count = upload_tree(sftp, local_app, remote + "/app")
            note("upload-app", {"exit": 0, "output": "uploaded %d files" % count})
            for name in ("yolov8s-seg.rknn", "yolov5s.rknn"):
                src = local_models / name
                if src.is_file():
                    sftp.put(str(src), "%s/model-assets/%s" % (remote, name))
                    note("upload-model", {"exit": 0, "output": name})
                else:
                    note("upload-model", {"exit": None, "output": "missing local %s" % src})
        finally:
            sftp.close()

        if "build" in steps:
            note("build-core", rrun(client, "cd %s/app && bash build.sh CoreTests; echo BUILD_CORE_EXIT=$?" % remote,
                                    args.build_timeout))
            note("build-media", rrun(client, "cd %s/app && bash build.sh MediaCheck; echo BUILD_MEDIA_EXIT=$?" % remote,
                                     args.build_timeout))

        if "core-tests" in steps:
            note("core-tests", rrun(
                client, "cd %s/app && ./build-core/tests/railhmi-core-tests "
                        "--junit core-evidence/item009-core-tests-%s.xml; echo TEST_EXIT=$?" % (remote, args.stamp),
                args.build_timeout))
            rels.append("app/core-evidence/item009-core-tests-%s.xml" % args.stamp)

        if "selftest" in steps:
            note("selftest", rrun(
                client, "cd %s/app && ./build-media/tools/railhmi-vision-pipeline --selftest --config ./config "
                        "--out core-evidence/item009-selftest-%s.log --json core-evidence/item009-selftest-%s.json; "
                        "echo SELFTEST_EXIT=$?" % (remote, args.stamp, args.stamp),
                args.command_timeout))
            rels.append("app/core-evidence/item009-selftest-%s.log" % args.stamp)
            rels.append("app/core-evidence/item009-selftest-%s.json" % args.stamp)

        if "chain" in steps:
            note("chain", rrun(
                client, "cd %s/app && ./build-media/tools/railhmi-vision-pipeline --mode chain --config ./config "
                        "--models-dir %s/model-assets --source test --duration %d --lanes CAM-01 "
                        "--out core-evidence/item009-chain-%s.log --json core-evidence/item009-chain-%s.json; "
                        "echo CHAIN_EXIT=$?" % (remote, remote, args.chain_duration, args.stamp, args.stamp),
                args.command_timeout))
            rels.append("app/core-evidence/item009-chain-%s.log" % args.stamp)
            rels.append("app/core-evidence/item009-chain-%s.json" % args.stamp)

        if "chain4" in steps:
            note("chain4", rrun(
                client, "cd %s/app && ./build-media/tools/railhmi-vision-pipeline --mode chain --config ./config "
                        "--models-dir %s/model-assets --source test --duration %d "
                        "--lanes CAM-01,CAM-04,CAM-05,CAM-06 --budget 1 "
                        "--out core-evidence/item009-chain4-%s.log --json core-evidence/item009-chain4-%s.json; "
                        "echo CHAIN4_EXIT=$?" % (remote, remote, args.chain4_duration, args.stamp, args.stamp),
                args.command_timeout))
            rels.append("app/core-evidence/item009-chain4-%s.log" % args.stamp)
            rels.append("app/core-evidence/item009-chain4-%s.json" % args.stamp)

        if "ai-runner" in steps:
            # 回归：替代输入链路（SimDetector + 显式 synthetic_tick）仍可运行，未被真实路径破坏。
            note("ai-runner", rrun(
                client, "cd %s/app && ./build-media/tools/railhmi-ai-runner --config ./config --duration 4 "
                        "--out core-evidence/item009-ai-runner-%s.log; echo AI_RUNNER_EXIT=$?" % (remote, args.stamp),
                args.command_timeout))
            rels.append("app/core-evidence/item009-ai-runner-%s.log" % args.stamp)

        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(args.connect_timeout * 12)
            for rel in rels:
                local_out = outdir / ("%s.%s" % (tag, rel.replace("/", "_")))
                try:
                    sftp.get("%s/%s" % (remote, rel), str(local_out))
                    if local_out.stat().st_size == 0:
                        local_out.unlink()
                        note("download", {"exit": None, "output": "%s: zero bytes (not produced)" % rel})
                        continue
                    downloads.append(local_out.name)
                    note("download", {"exit": 0, "output": local_out.name})
                except OSError as exc:
                    note("download", {"exit": None, "output": "%s: %r" % (rel, exc)})
        finally:
            sftp.close()

        if not args.keep_remote:
            note("cleanup", rrun(client, "rm -rf %s && echo cleaned" % remote, 30))
    finally:
        client.close()

    transcript_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
    with log_path.open("w", encoding="utf-8") as fh:
        fh.write("host_key_fingerprint: %s\n" % transcript["host_key_fingerprint"])
        fh.write("remote_dir: %s\n" % remote)
        for s in transcript["steps"]:
            fh.write("\n===== [%s] exit=%s =====\n" % (s.get("step"), s.get("exit")))
            fh.write((s.get("output") or "") + "\n")

    print("host_key_fingerprint:", transcript["host_key_fingerprint"])
    for s in transcript["steps"]:
        tail = (s.get("output") or "").strip().splitlines()
        print("[%s] exit=%s %s" % (s.get("step"), s.get("exit"), tail[-1] if tail else ""))
    print("transcript:", transcript_path)
    print("log:", log_path)
    print("downloads:", len(downloads))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
