#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 录像压缩分支与滚动缓冲板端检查运行器（视频 ITEM-011）

沿登记 Paramiko SSH 入口：
  1) 上传 app/ 到目标 /tmp 独立工作目录；
  2) `build.sh CoreTests` + railhmi-core-tests（含 compressed_ring / recording_source 组）；
  3) `build.sh MediaCheck` + `railhmi-recording-tap --selftest`（管线映射/来源策略/缓冲边界）；
  4) 板端真实压缩管线运行：编码→parse→tee，一支解码当显示（慢消费），一支解码前 tap 录像，
     验证滚动缓冲字节/时长双上限受控、最旧淘汰计数、慢显示不影响录像压缩包、NVR 未配置显式回落；
  5) 取回日志/JSON，清理远端工作目录。

安全与边界:
  * 受信任 known_hosts（load_system_host_keys + 默认 RejectPolicy）；不使用 AutoAddPolicy。
  * 连接/命令/SFTP 均显式有限超时；SSH 凭据仅从环境变量读取，不落日志。
  * 仅写目标 /tmp 下独立工作目录；不安装/不修改系统、不重启、不按名杀进程。

用法:
  python app/tools/board_recording_tap_check.py --host <addr> --user <user> \
      --outdir docs/video/test_evidence/ITEM-011 --stamp <tag>
"""

from __future__ import annotations

import argparse
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


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi recording-tap board check (ITEM-011)")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--local-dir", default=str(Path(__file__).resolve().parents[1]))
    ap.add_argument("--remote-dir", default="/tmp/railhmi_rec")
    ap.add_argument("--outdir", default="docs/video/test_evidence/ITEM-011")
    ap.add_argument("--stamp", default=time.strftime("%Y%m%d-%H%M%S"))
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--build-timeout", type=int, default=900)
    ap.add_argument("--pre-seconds", type=int, default=3)
    ap.add_argument("--max-bytes", type=int, default=1024 * 1024)
    ap.add_argument("--byte-phase-bytes", type=int, default=200 * 1024,
                    help="第二阶段字节上限（更小，用于主动触发字节淘汰）")
    ap.add_argument("--duration", type=int, default=8)
    ap.add_argument("--display-pull-ms", type=int, default=200)
    ap.add_argument("--keep-remote", action="store_true")
    args = ap.parse_args()

    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("SSH 凭据环境变量 %s 未加载；不猜测凭据。\n" % args.credential_env)
        return 2

    local = Path(args.local_dir).resolve()
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    tag = "rec-%s-%s" % (args.host.replace(".", "_"), args.stamp)
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
    try:
        note("mkdir", rrun(client, "mkdir -p %s/core-evidence" % remote, 20))
        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(args.connect_timeout * 6)
            count = upload_tree(sftp, local, remote)
            note("upload", {"exit": 0, "output": "uploaded %d files from %s" % (count, local)})
        finally:
            sftp.close()

        note("build-core", rrun(client, "cd %s && bash build.sh CoreTests; echo BUILD_CORE_EXIT=$?" % remote,
                                args.build_timeout))
        note("core-tests", rrun(client, "cd %s && ./build-core/tests/railhmi-core-tests; echo CORE_EXIT=$?" % remote,
                                120))
        note("build-media", rrun(client, "cd %s && bash build.sh MediaCheck; echo BUILD_MEDIA_EXIT=$?" % remote,
                                 args.build_timeout))
        note("selftest", rrun(client, "cd %s && ./build-media/tools/railhmi-recording-tap --selftest; "
                                      "echo SELFTEST_EXIT=$?" % remote, 60))
        note("run-rec", rrun(
            client,
            "cd %s && ./build-media/tools/railhmi-recording-tap --config config --pre-seconds %d "
            "--max-bytes %d --duration %d --display-pull-ms %d "
            "--out core-evidence/recording-tap-%s.log --json core-evidence/recording-tap-%s.json; "
            "echo REC_EXIT=$?" % (remote, args.pre_seconds, args.max_bytes, args.duration,
                                  args.display_pull_ms, stamp, stamp),
            args.duration + 120))
        # 第二阶段：更小字节上限，主动触发字节淘汰（时长上限仍生效）。
        note("run-rec-byte", rrun(
            client,
            "cd %s && ./build-media/tools/railhmi-recording-tap --config config --pre-seconds %d "
            "--max-bytes %d --duration %d --display-pull-ms %d "
            "--out core-evidence/recording-tap-byte-%s.log --json core-evidence/recording-tap-byte-%s.json; "
            "echo RECB_EXIT=$?" % (remote, args.pre_seconds, args.byte_phase_bytes, args.duration,
                                   args.display_pull_ms, stamp, stamp),
            args.duration + 120))

        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(args.connect_timeout * 6)
            for rel in ("core-evidence/recording-tap-%s.log" % stamp,
                        "core-evidence/recording-tap-%s.json" % stamp,
                        "core-evidence/recording-tap-byte-%s.log" % stamp,
                        "core-evidence/recording-tap-byte-%s.json" % stamp):
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
