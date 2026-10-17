#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 视频管线工厂板端检查运行器（视频 ITEM-002）

沿登记 Paramiko SSH 入口：
  1) 上传 app/ 到目标 /tmp 独立工作目录；
  2) `build.sh MediaCheck` 板端本机构建（GStreamer + librga，无 Qt）；
  3) `railhmi-stream-probe --selftest`（纯字符串映射核对：三类源 + auto/h264/h265 + 脱敏）；
  4) test 源跑一段；
  5) 生成一个真实媒体文件（ffmpeg H.264，失败回退板端 mpph264enc），file 源跑同一管线；
  6) 若本机具备相机凭据，则经目标 /tmp 0600 临时 env 文件注入后，对真实 RTSP 分别跑
     auto 与显式 h265 固化路径，并立即删除该文件（凭据不进命令行/日志/工件）；
  7) 取回 JSON/日志，清理远端工作目录。

安全与边界:
  * 受信任 known_hosts（load_system_host_keys + 默认 RejectPolicy）；不使用 AutoAddPolicy。
  * 连接/命令/SFTP 均显式有限超时；SSH 凭据仅从环境变量读取，不落日志。
  * 相机凭据只经 0600 临时文件传给板端进程环境；命令串与日志均不含其值。
  * 仅写目标 /tmp 下独立工作目录；不安装/不修改系统、不重启、不按名杀进程。

用法:
  python app/tools/board_pipeline_check.py --host <addr> --user <user> \
      --outdir docs/video/test_evidence/ITEM-002 --stamp <tag> [--no-rtsp]

退出码: 0 完成（结果见 transcript）；2 连接/凭据失败；4 paramiko 缺失。
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
    ap = argparse.ArgumentParser(description="railhmi pipeline-factory board check (ITEM-002)")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--camera-credential-envs", default="TRAIN_FRONT_RTSP_CREDENTIAL,TRAIN_SIDE_RTSP_CREDENTIAL")
    ap.add_argument("--local-dir", default=str(Path(__file__).resolve().parents[1]))
    ap.add_argument("--remote-dir", default="/tmp/railhmi_pipe")
    ap.add_argument("--outdir", default="docs/video/test_evidence/ITEM-002")
    ap.add_argument("--stamp", default=time.strftime("%Y%m%d-%H%M%S"))
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--build-timeout", type=int, default=900)
    ap.add_argument("--duration", type=int, default=5)
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
    tag = "pipe-%s-%s" % (args.host.replace(".", "_"), args.stamp)
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
    dur = args.duration
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
        note("evidence-dir", rrun(client, "mkdir -p %s/core-evidence && echo ok" % remote, 20))
        note("selftest", rrun(client, "cd %s && ./build-media/tools/railhmi-stream-probe --selftest; "
                                      "echo SELFTEST_EXIT=$?" % remote, 60))

        media = "/tmp/railhmi_probe.mp4"
        gen = ("rm -f %s; "
               "if ffmpeg -hide_banner -loglevel error -y -f lavfi -i testsrc=size=1280x720:rate=25 -t 2 "
               "-c:v libx264 -pix_fmt yuv420p %s 2>/dev/null; then echo GEN=ffmpeg; "
               "elif gst-launch-1.0 -q -e videotestsrc num-buffers=50 is-live=false "
               "! video/x-raw,width=1280,height=720,framerate=25/1 ! mpph264enc ! h264parse ! qtmux "
               "! filesink location=%s >/dev/null 2>&1; then echo GEN=mpph264enc; "
               "else echo GEN=failed; fi; ls -l %s 2>&1" % (media, media, media, media))
        note("gen-file", rrun(client, gen, 120))

        note("run-test", rrun(
            client,
            "cd %s && ./build-media/tools/railhmi-stream-probe --source test --duration %d "
            "--out core-evidence/probe-test-%s.log --json core-evidence/probe-test-%s.json; echo TEST_EXIT=$?"
            % (remote, dur, stamp, stamp), dur + 60))
        note("run-file", rrun(
            client,
            "cd %s && ./build-media/tools/railhmi-stream-probe --source file --file %s --duration %d "
            "--out core-evidence/probe-file-%s.log --json core-evidence/probe-file-%s.json; echo FILE_EXIT=$?"
            % (remote, media, dur, stamp, stamp), dur + 60))

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
            for mode in ("auto", "h265"):
                cmd = (
                    "cd %s && set -a && . ./.cam_env.sh && set +a && "
                    "./build-media/tools/railhmi-stream-probe --source rtsp --url %s "
                    "--user %s --password-env %s --decode %s --duration %d "
                    "--out core-evidence/probe-rtsp-%s-%s.log --json core-evidence/probe-rtsp-%s-%s.json; "
                    "echo RTSP_%s_EXIT=$?"
                    % (remote, sh_single_quote(args.rtsp_url), args.rtsp_user,
                       args.rtsp_credential_env, mode, dur, mode, stamp, mode, stamp, mode.upper()))
                note("run-rtsp-%s" % mode, rrun(client, cmd, dur + 90))
            note("cleanup-envfile", rrun(client, "rm -f %s/.cam_env.sh && echo removed" % remote, 20))
        else:
            note("run-rtsp", {"exit": 0, "output": "skipped (no camera credential in runner env or --no-rtsp)"})

        downloads = ["core-evidence/probe-test-%s.log" % stamp, "core-evidence/probe-test-%s.json" % stamp,
                     "core-evidence/probe-file-%s.log" % stamp, "core-evidence/probe-file-%s.json" % stamp]
        if (not args.no_rtsp) and have_cam:
            for mode in ("auto", "h265"):
                downloads += ["core-evidence/probe-rtsp-%s-%s.log" % (mode, stamp),
                              "core-evidence/probe-rtsp-%s-%s.json" % (mode, stamp)]
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
