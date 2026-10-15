#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 板端依赖基线探针运行器 (ITEM-001)

沿项目登记的 Paramiko SSH 入口（runtime environment.yaml: access.rk3588_inspect）
把只读探针 `env_probe.py` 上传到目标板 /tmp 并以有限等待执行，取回 Markdown/JSON/原始日志，
另存一条包含每条远程命令、退出码与原始输出的日志。

安全与边界（与 Unified Project Environment And Authorization 一致）:
  * 只读: 不安装、不改系统、不重启、不按进程名批量操作；只写目标 /tmp。
  * 使用受信任 known_hosts（load_system_host_keys + 默认 RejectPolicy）；
    不使用 AutoAddPolicy；主机指纹以 SHA256 打印，供用户核验。
  * 连接/命令/SFTP 均有显式 finite timeout。
  * 凭据从指定环境变量读取（默认 TRAIN_RK3588_SSH_CREDENTIAL），不打日志。

用法:
  python board_env_probe.py --host <addr> --user <user> \
      --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
      --out-dir docs/hmi/test_evidence/ENV-B-dependency \
      [--port 22] [--connect-timeout 10] [--command-timeout 90] [--stamp <tag>]

退出码: 0 成功；2 连接/认证失败（已记录实际错误）；3 远程探针失败。
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


def fingerprint(key) -> str:
    try:
        digest = hashlib.sha256(key.asbytes()).digest()
        return "SHA256:" + base64.b64encode(digest).decode("ascii").rstrip("=")
    except Exception:  # noqa: BLE001
        return "(unavailable)"


def run_remote(client, cmd: str, timeout: int) -> dict:
    """执行一条远程命令，带有限等待，返回 {cmd, exit, output}。"""
    started = time.time()
    try:
        _stdin, stdout, stderr = client.exec_command(cmd, timeout=timeout)
        out = stdout.read().decode("utf-8", "replace")
        err = stderr.read().decode("utf-8", "replace")
        rc = stdout.channel.recv_exit_status()
        return {"cmd": cmd, "exit": rc, "elapsed_s": round(time.time() - started, 2),
                "output": (out + err).strip()}
    except socket.timeout:
        return {"cmd": cmd, "exit": None, "elapsed_s": round(time.time() - started, 2),
                "output": "(timeout after %ss)" % timeout}
    except Exception as exc:  # noqa: BLE001
        return {"cmd": cmd, "exit": None, "elapsed_s": round(time.time() - started, 2),
                "output": "(error: %r)" % (exc,)}


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi board dependency probe runner")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--probe-script", default=str(Path(__file__).with_name("env_probe.py")))
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--stamp", default=time.strftime("%Y%m%d-%H%M%S"))
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--command-timeout", type=int, default=90)
    ap.add_argument("--remote-dir", default="/tmp")
    ap.add_argument("--probe-cameras", action="store_true",
                    help="pass --probe-cameras to the remote probe (camera/NVR TCP reachability)")
    args = ap.parse_args()

    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("凭据环境变量 %s 未在当前进程加载；不猜测凭据，标记未核验。\n"
                         % args.credential_env)
        return 2

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    base = out_dir / ("board-env-probe-%s-%s" % (args.host.replace(".", "_"), args.stamp))
    log_path = base.with_suffix(".commands.json")

    probe_src = Path(args.probe_script)
    if not probe_src.is_file():
        sys.stderr.write("找不到探针脚本: %s\n" % probe_src)
        return 3

    remote_probe = "%s/railhmi_env_probe.py" % args.remote_dir
    remote_md = "%s/railhmi_env_probe_board.md" % args.remote_dir
    remote_raw = "%s/railhmi_env_probe_board.raw.txt" % args.remote_dir
    remote_json = "%s/railhmi_env_probe_board.json" % args.remote_dir

    client = paramiko.SSHClient()
    client.load_system_host_keys()  # 受信任 known_hosts；默认 RejectPolicy（不自动接受未知主机）
    transcript: dict = {
        "invocation": {
            "host": args.host, "port": args.port, "user": args.user,
            "credential_env": args.credential_env, "out_dir": str(out_dir),
            "connect_timeout_s": args.connect_timeout,
            "command_timeout_s": args.command_timeout,
        },
        "host_key_fingerprint": None,
        "connected": False,
        "steps": [],
    }

    try:
        client.connect(
            hostname=args.host, port=args.port, username=args.user, password=secret,
            timeout=args.connect_timeout, auth_timeout=args.connect_timeout,
            banner_timeout=args.connect_timeout, look_for_keys=False, allow_agent=False,
        )
    except paramiko.BadHostKeyException as exc:
        transcript["steps"].append({"step": "connect", "exit": None,
                                    "output": "BadHostKey: %r" % (exc,)})
        log_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
        sys.stderr.write("主机密钥不在受信任 known_hosts；未接受，直接失败。详见 %s\n" % log_path)
        return 2
    except Exception as exc:  # noqa: BLE001
        transcript["steps"].append({"step": "connect", "exit": None,
                                    "output": "connect error: %r" % (exc,)})
        log_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
        sys.stderr.write("连接失败（未核验在线，不写成不可达）: %r；详见 %s\n" % (exc, log_path))
        return 2

    transcript["connected"] = True
    transport = client.get_transport()
    if transport is not None and transport.get_remote_server_key() is not None:
        transcript["host_key_fingerprint"] = fingerprint(transport.get_remote_server_key())

    try:
        for cmd in ("uname -a", "whoami && id", "python3 --version || python --version",
                    "cat /proc/device-tree/model 2>/dev/null | tr -d '\\0'; echo"):
            transcript["steps"].append(run_remote(client, cmd, args.connect_timeout))

        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(args.command_timeout)
            sftp.put(str(probe_src), remote_probe)
        finally:
            sftp.close()
        transcript["steps"].append({"step": "upload", "exit": 0,
                                    "output": "uploaded %s" % remote_probe})

        probe_cmd = ("python3 %s --out %s --raw-out %s --json-out %s --skip-slow%s"
                     % (remote_probe, remote_md, remote_raw, remote_json,
                        " --probe-cameras" if args.probe_cameras else ""))
        probe_res = run_remote(client, probe_cmd, args.command_timeout)
        transcript["steps"].append({"step": "probe", **probe_res})

        if probe_res.get("exit") == 0:
            sftp = client.open_sftp()
            try:
                sftp.get_channel().settimeout(args.command_timeout)
                sftp.get(remote_md, str(base) + ".md")
                sftp.get(remote_raw, str(base) + ".raw.txt")
                sftp.get(remote_json, str(base) + ".json")
                transcript["steps"].append({"step": "download", "exit": 0,
                                            "output": "retrieved md/raw/json"})
            finally:
                sftp.close()
        else:
            transcript["steps"].append({"step": "download", "exit": None,
                                        "output": "skipped (remote probe did not exit 0)"})
    finally:
        client.close()

    log_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
    print("host_key_fingerprint:", transcript["host_key_fingerprint"])
    for step in transcript["steps"]:
        print("[%s] exit=%s %s" % (step.get("step", "cmd"), step.get("exit"),
                                   (step.get("output") or "")[:120].replace("\n", " ")))
    print("transcript:", log_path)
    return 0 if any(s.get("step") == "download" and s.get("exit") == 0
                    for s in transcript["steps"]) else 3


if __name__ == "__main__":
    raise SystemExit(main())
