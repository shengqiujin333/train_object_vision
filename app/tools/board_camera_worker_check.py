#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 每路取流 worker 与状态机板端检查运行器（视频 ITEM-006）

沿登记 Paramiko SSH 入口：
  1) 上传 app/ 到目标 /tmp 独立工作目录；
  2) `build.sh MediaCheck`；
  3) `railhmi-camera-worker --selftest`（六态机冻结→重连→升级→恢复；pull 上限钳制）；
  4) freeze+restart 运行：live→frozen→reconnecting→offline(升级)→重建恢复、计数清零；
  5) cut(FI-01) 运行：reconnecting→升级；
  6) 若具备相机凭据，对真实 RTSP 再跑一次；
  7) 取回日志，清理远端工作目录。

安全与边界:
  * 受信任 known_hosts（load_system_host_keys + 默认 RejectPolicy）；不使用 AutoAddPolicy。
  * 连接/命令/SFTP 均显式有限超时；SSH 凭据仅从环境变量读取，不落日志。
  * 相机凭据只经 0600 临时文件注入并删除；命令串与日志不含其值。
  * 仅写目标 /tmp 下独立工作目录；不安装/不修改系统、不重启、不按名杀进程。

用法:
  python app/tools/board_camera_worker_check.py --host <addr> --user <user> \
      --outdir docs/video/test_evidence/ITEM-006 --stamp <tag> [--no-rtsp]
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
    ap = argparse.ArgumentParser(description="railhmi camera-worker board check (ITEM-006)")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--camera-credential-envs", default="TRAIN_FRONT_RTSP_CREDENTIAL,TRAIN_SIDE_RTSP_CREDENTIAL")
    ap.add_argument("--local-dir", default=str(Path(__file__).resolve().parents[1]))
    ap.add_argument("--remote-dir", default="/tmp/railhmi_worker")
    ap.add_argument("--outdir", default="docs/video/test_evidence/ITEM-006")
    ap.add_argument("--stamp", default=time.strftime("%Y%m%d-%H%M%S"))
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--build-timeout", type=int, default=900)
    ap.add_argument("--rtsp-url", default="rtsp://192.168.1.65:554/cam/realmonitor?channel=1&subtype=0")
    ap.add_argument("--rtsp-user", default="admin")
    ap.add_argument("--rtsp-credential-env", default="TRAIN_FRONT_RTSP_CREDENTIAL")
    ap.add_argument("--no-rtsp", action="store_true")
    ap.add_argument("--keep-remote", action="store_true")
    args = ap.parse_args()

    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("SSH 凭据环境变量 %s 未加载；不猜测凭据。\n" % args.credential_env)
        return 2

    local = Path(args.local_dir).resolve()
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    tag = "worker-%s-%s" % (args.host.replace(".", "_"), args.stamp)
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
    fast = ("--freeze-timeout-ms 500 --first-frame-timeout-ms 500 --backoff-ms 200 "
            "--reconnect-threshold 3")
    try:
        note("mkdir", rrun(client, "mkdir -p %s/core-evidence" % remote, 20))
        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(args.connect_timeout * 6)
            count = upload_tree(sftp, local, remote)
            note("upload", {"exit": 0, "output": "uploaded %d files from %s" % (count, local)})
        finally:
            sftp.close()

        note("build-media", rrun(client, "cd %s && bash build.sh MediaCheck; echo BUILD_MEDIA_EXIT=$?" % remote,
                                 args.build_timeout))
        note("selftest", rrun(client, "cd %s && ./build-media/tools/railhmi-camera-worker --selftest; "
                                      "echo SELFTEST_EXIT=$?" % remote, 60))
        note("run-freeze", rrun(
            client,
            "cd %s && ./build-media/tools/railhmi-camera-worker --source test --duration 9 "
            "--freeze-after 1 --restart-after 6 %s --out core-evidence/worker-freeze-%s.log; "
            "echo FREEZE_EXIT=$?" % (remote, fast, stamp), 120))
        note("run-cut", rrun(
            client,
            "cd %s && ./build-media/tools/railhmi-camera-worker --source test --duration 6 "
            "--cut-after 1 %s --out core-evidence/worker-cut-%s.log; echo CUT_EXIT=$?"
            % (remote, fast, stamp), 100))
        note("run-resume", rrun(
            client,
            "cd %s && ./build-media/tools/railhmi-camera-worker --source test --duration 9 "
            "--freeze-after 1 --resume-after 6 %s --out core-evidence/worker-resume-%s.log; "
            "echo RESUME_EXIT=$?" % (remote, fast, stamp), 120))

        cam_env_values = {n.strip(): os.environ.get(n.strip(), "")
                          for n in args.camera_credential_envs.split(",") if n.strip()}
        have_cam = bool(cam_env_values.get(args.rtsp_credential_env))
        if (not args.no_rtsp) and have_cam:
            lines = ["%s=%s" % (n, sh_single_quote(v)) for n, v in cam_env_values.items() if v]
            env_file = remote + "/.cam_env.sh"
            sftp = client.open_sftp()
            try:
                sftp.putfo(io.BytesIO(("\n".join(lines) + "\n").encode("utf-8")), env_file)
                sftp.chmod(env_file, 0o600)
            finally:
                sftp.close()
            cmd = (
                "cd %s && set -a && . ./.cam_env.sh && set +a && "
                "./build-media/tools/railhmi-camera-worker --source rtsp --url %s --user %s "
                "--password-env %s --duration 4 --out core-evidence/worker-rtsp-%s.log; rc=$?; "
                "rm -f ./.cam_env.sh; echo RTSP_EXIT=$rc"
                % (remote, sh_single_quote(args.rtsp_url), args.rtsp_user, args.rtsp_credential_env, stamp))
            note("run-rtsp", rrun(client, cmd, 120))
        else:
            note("run-rtsp", {"exit": 0, "output": "skipped (no camera credential or --no-rtsp)"})

        downloads = ["core-evidence/worker-freeze-%s.log" % stamp, "core-evidence/worker-cut-%s.log" % stamp,
                     "core-evidence/worker-resume-%s.log" % stamp]
        if (not args.no_rtsp) and have_cam:
            downloads.append("core-evidence/worker-rtsp-%s.log" % stamp)
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
