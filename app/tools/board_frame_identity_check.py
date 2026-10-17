#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 帧身份与跨重连代际板端检查运行器（视频 ITEM-003）

沿登记 Paramiko SSH 入口：
  1) 上传 app/ 到目标 /tmp 独立工作目录；
  2) `build.sh CoreTests` + 运行 railhmi-core-tests（含 frame_identity 组）
     —— 纯逻辑：seq 同代严格递增、重建代际+seq 归零、接纳拒绝旧 session/
     generation/重复/乱序；
  3) `build.sh MediaCheck` + `railhmi-stream-probe --selftest`（含帧身份纯映射）；
  4) `railhmi-stream-probe --source test --rebuild-after <s>` —— 真实重建后
     generation 递增、seq 重置、旧代际帧被接纳器拒绝；
  5) 取回日志/JSON，清理远端工作目录。

安全与边界:
  * 受信任 known_hosts（load_system_host_keys + 默认 RejectPolicy）；不使用 AutoAddPolicy。
  * 连接/命令/SFTP 均显式有限超时；SSH 凭据仅从环境变量读取，不落日志。
  * 仅写目标 /tmp 下独立工作目录；不安装/不修改系统、不重启、不按名杀进程。

用法:
  python app/tools/board_frame_identity_check.py --host <addr> --user <user> \
      --outdir docs/video/test_evidence/ITEM-003 --stamp <tag>

退出码: 0 完成（结果见 transcript）；2 连接/凭据失败；4 paramiko 缺失。
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
    ap = argparse.ArgumentParser(description="railhmi frame-identity board check (ITEM-003)")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--local-dir", default=str(Path(__file__).resolve().parents[1]))
    ap.add_argument("--remote-dir", default="/tmp/railhmi_identity")
    ap.add_argument("--outdir", default="docs/video/test_evidence/ITEM-003")
    ap.add_argument("--stamp", default=time.strftime("%Y%m%d-%H%M%S"))
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--build-timeout", type=int, default=900)
    ap.add_argument("--duration", type=int, default=6)
    ap.add_argument("--rebuild-after", type=int, default=2)
    ap.add_argument("--keep-remote", action="store_true")
    args = ap.parse_args()

    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("SSH 凭据环境变量 %s 未加载；不猜测凭据。\n" % args.credential_env)
        return 2

    local = Path(args.local_dir).resolve()
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    tag = "identity-%s-%s" % (args.host.replace(".", "_"), args.stamp)
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
        note("core-tests", rrun(
            client,
            "cd %s && ./build-core/tests/railhmi-core-tests; echo CORE_EXIT=$?" % remote, 120))
        note("build-media", rrun(client, "cd %s && bash build.sh MediaCheck; echo BUILD_MEDIA_EXIT=$?" % remote,
                                 args.build_timeout))
        note("selftest", rrun(client, "cd %s && ./build-media/tools/railhmi-stream-probe --selftest; "
                                      "echo SELFTEST_EXIT=$?" % remote, 60))
        note("identity-run", rrun(
            client,
            "cd %s && ./build-media/tools/railhmi-stream-probe --source test --duration %d "
            "--rebuild-after %d --out core-evidence/identity-%s.log "
            "--json core-evidence/identity-%s.json; echo IDENTITY_EXIT=$?"
            % (remote, args.duration, args.rebuild_after, stamp, stamp), args.duration + 60))

        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(args.connect_timeout * 6)
            for rel in ("core-evidence/identity-%s.log" % stamp,
                        "core-evidence/identity-%s.json" % stamp):
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
