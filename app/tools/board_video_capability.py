#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 视频源能力探测板端运行器（视频 ITEM-001）

沿项目登记的 Paramiko SSH 入口：
  1) 上传 app/ 工程到目标板独立工作目录；
  2) 板端本机构建 MediaCheck（GStreamer + librga，无 Qt）；
  3) 运行 `railhmi-video-capability --selftest`（纯逻辑自检）；
  4) 无凭据运行能力探测（演示 CREDENTIAL_MISSING 与待实测 caps）；
  5) 若本机具备相机凭据环境变量，则经目标 /tmp 的 0600 临时 env 文件注入后
     以有界 caps 探测运行一次，并立即删除该文件（凭据不进命令行/日志/工件）。
  6) 取回 JSON/日志，清理远端工作目录。

安全与边界:
  * 受信任 known_hosts（load_system_host_keys + 默认 RejectPolicy）；不使用 AutoAddPolicy。
  * 连接/命令/SFTP 均显式有限超时；SSH 凭据仅从环境变量读取，不落日志。
  * 相机凭据只经 0600 临时文件传给板端进程环境，命令串与日志均不含其值。
  * 仅写目标 /tmp 下独立工作目录；不安装/不修改系统、不重启、不按名杀进程。

用法:
  python app/tools/board_video_capability.py --host <addr> --user <user> \
      --outdir docs/video/test_evidence/ITEM-001 --stamp <tag> [--no-caps]

退出码: 0 完成（构建/探测结果见 transcript）；2 连接/凭据失败；4 paramiko 缺失。
"""

from __future__ import annotations

import argparse
import io
import json
import os
import sys
import time
from pathlib import Path

try:
    import paramiko
except Exception as exc:  # pragma: no cover
    sys.stderr.write("paramiko 不可用: %r\n" % (exc,))
    raise SystemExit(4)

sys.path.insert(0, str(Path(__file__).resolve().parent))
from board_build import fingerprint, rrun, upload_tree  # noqa: E402


def sh_single_quote(s: str) -> str:
    return "'" + s.replace("'", "'\\''") + "'"


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi video capability board runner (ITEM-001)")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--camera-credential-envs", default="TRAIN_FRONT_RTSP_CREDENTIAL,TRAIN_SIDE_RTSP_CREDENTIAL")
    ap.add_argument("--local-dir", default=str(Path(__file__).resolve().parents[1]))
    ap.add_argument("--remote-dir", default="/tmp/railhmi_video")
    ap.add_argument("--outdir", default="docs/video/test_evidence/ITEM-001")
    ap.add_argument("--stamp", default=time.strftime("%Y%m%d-%H%M%S"))
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--build-timeout", type=int, default=900)
    ap.add_argument("--caps-timeout-ms", type=int, default=15000)
    ap.add_argument("--cameras", default="", help="comma-separated camera ids (default: all)")
    ap.add_argument("--no-caps", action="store_true")
    ap.add_argument("--keep-remote", action="store_true")
    args = ap.parse_args()

    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("SSH 凭据环境变量 %s 未加载；不猜测凭据。\n" % args.credential_env)
        return 2

    local = Path(args.local_dir).resolve()
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    tag = "video-cap-%s-%s" % (args.host.replace(".", "_"), args.stamp)
    transcript_path = outdir / (tag + ".transcript.json")
    log_path = outdir / (tag + ".log")

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    client.set_missing_host_key_policy(paramiko.RejectPolicy())
    transcript: dict = {"invocation": {k: v for k, v in vars(args).items()},
                        "host_key_fingerprint": None, "steps": []}

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
    stamp = args.stamp
    cam_filter = "".join(" --camera %s" % c.strip() for c in args.cameras.split(",") if c.strip())
    try:
        note("mkdir", rrun(client, "mkdir -p %s" % remote, 20))
        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(args.connect_timeout * 6)
            count = upload_tree(sftp, local, remote)
            note("upload", {"exit": 0, "output": "uploaded %d files from %s" % (count, local)})
        finally:
            sftp.close()

        note("build", rrun(client, "cd %s && bash build.sh MediaCheck; echo BUILD_EXIT=$?" % remote,
                           args.build_timeout))
        note("selftest", rrun(client, "cd %s && ./build-media/tools/railhmi-video-capability --selftest; "
                                      "echo SELFTEST_EXIT=$?" % remote, 60))
        note("reachability", rrun(
            client,
            "for ip in 192.168.1.65 192.168.1.160 192.168.1.162 192.168.1.163 192.168.1.165 192.168.1.167; do "
            "if timeout 3 bash -c \"echo > /dev/tcp/$ip/554\" 2>/dev/null; then echo \"$ip:554=open\"; "
            "else echo \"$ip:554=closed\"; fi; done; ip -4 addr show eth1 | sed -n 's/.*inet //p'; "
            "echo link_speed_mbps=$(cat /sys/class/net/eth1/speed 2>/dev/null)", 60))
        note("capability-nocred", rrun(
            client,
            "cd %s && mkdir -p core-evidence && ./build-media/tools/railhmi-video-capability "
            "--config ./config%s --json core-evidence/capability-nocred-%s.json "
            "--out core-evidence/capability-nocred-%s.log; echo CAP_NOCRED_EXIT=$?"
            % (remote, cam_filter, stamp, stamp), 90))

        cam_env_names = [e.strip() for e in args.camera_credential_envs.split(",") if e.strip()]
        cam_env_values = {n: os.environ.get(n, "") for n in cam_env_names}
        have_cam_creds = any(cam_env_values.values())
        if (not args.no_caps) and have_cam_creds:
            lines = ["%s=%s" % (n, sh_single_quote(v)) for n, v in cam_env_values.items() if v]
            env_file = remote + "/.cam_env.sh"
            sftp = client.open_sftp()
            try:
                sftp.putfo(io.BytesIO(("\n".join(lines) + "\n").encode("utf-8")), env_file)
                sftp.chmod(env_file, 0o600)
            finally:
                sftp.close()
            caps_cmd = (
                "cd %s && set -a && . ./.cam_env.sh && set +a && "
                "./build-media/tools/railhmi-video-capability --config ./config%s --probe-caps "
                "--caps-timeout-ms %d --json core-evidence/capability-caps-%s.json "
                "--out core-evidence/capability-caps-%s.log; rc=$?; "
                "rm -f ./.cam_env.sh; echo CAP_CAPS_EXIT=$rc"
                % (remote, cam_filter, args.caps_timeout_ms, stamp, stamp))
            note("capability-caps", rrun(client, caps_cmd,
                                         int(args.caps_timeout_ms / 1000) * 12 + 120))
        else:
            note("capability-caps", {"exit": 0,
                                    "output": "skipped (no camera credentials in runner env or --no-caps)"})

        downloads = [
            "core-evidence/capability-nocred-%s.json" % stamp,
            "core-evidence/capability-nocred-%s.log" % stamp,
        ]
        if (not args.no_caps) and have_cam_creds:
            downloads += [
                "core-evidence/capability-caps-%s.json" % stamp,
                "core-evidence/capability-caps-%s.log" % stamp,
            ]
        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(args.connect_timeout * 6)
            for rel in downloads:
                local_out = outdir / ("%s.%s" % (tag, rel.replace("/", "_")))
                try:
                    sftp.get("%s/%s" % (remote, rel), str(local_out))
                    note("download", {"exit": 0, "output": local_out.name})
                except OSError as exc:
                    note("download", {"exit": None, "output": "%s: %r" % (rel, exc)})
        finally:
            sftp.close()

        note("artifacts", rrun(
            client,
            "cd %s; ls -l build-media/tools/railhmi-video-capability build-media/src/media/librailhmi_media.a 2>&1" % remote,
            20))

        if not args.keep_remote:
            note("cleanup", rrun(client, "rm -rf %s && echo cleaned" % remote, 20))
    finally:
        client.close()

    transcript_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
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
