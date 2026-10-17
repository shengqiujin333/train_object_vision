#!/usr/bin/env python3
"""UI 评审工具 — 目标板真实装载/渲染观察（REPAIR-n008 完整交付 UI 评审，complete_delivery）。

本工具属 ui_implementation_review 的**评审工具**（不修改产品源码/界面/验收判据）：
1) 上传 app/ 源码到目标 /tmp 工作目录并本机构建 `Release`（Qt/QML）；
2) 准备**临时**配置副本（simulation=true、default_role=admin，仅让页面有代表性数据；
   不改仓库内 config）；
3) 在**虚拟显示 Xvfb**（不占用操作员 X 会话）上启动程序，用 xdotool 逐页导航、ffmpeg x11grab 截屏；
4) 取回各页 PNG + 应用运行日志（供人工视觉核对）。

依赖（缺失时在 environment_repair(test) 授权内以 sudo -n 非交互安装）：xvfb。
截图用板上既有 ffmpeg(x11grab)。
"""
import argparse
import base64
import hashlib
import json
import os
import posixpath
import sys
import time
from pathlib import Path

import paramiko

SKIP_DIRS = {".git", "__pycache__", ".review_tmp", ".review_tmp3", ".review_tmp10",
             "build-target", "build-host", "build-core", "build-media"}

PAGES = ["p1_realtime", "p2_alarm_center", "p3_replay", "p4_all_cameras", "p5_ai_debug", "p6_maintenance"]


def fingerprint(key) -> str:
    return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")


class VerifyPolicy(paramiko.MissingHostKeyPolicy):
    def __init__(self, want: str) -> None:
        self.want = want

    def missing_host_key(self, client, hostname, key) -> None:
        got = fingerprint(key)
        if got != self.want:
            raise paramiko.SSHException("host key fingerprint mismatch: %s" % got)


CAPTURE_SCRIPT = r'''#!/bin/bash
set -u
RD="__RD__"
cd "$RD/app" || { echo "NO_APP"; exit 41; }
export QT_QPA_PLATFORM=xcb
export QT_QUICK_BACKEND=software
export QT_XCB_GL_INTEGRATION=none
export LIBGL_ALWAYS_SOFTWARE=1
DISPLAY=:99 ./build-target/src/ui/railhmi --config "$RD/config" >"$RD/app.log" 2>&1 &
APP=$!
UP=0
for i in $(seq 1 60); do DISPLAY=:99 xdotool search --name railhmi >/dev/null 2>&1 && { UP=1; break; }; sleep 0.5; done
echo "WIN_UP=$UP app_pid=$APP"
sleep 3
i=0
for name in __PAGES__; do
  y=$((92 + 88*i))
  DISPLAY=:99 xdotool mousemove 44 "$y" click 1
  sleep 1.5
  ffmpeg -y -loglevel error -f x11grab -draw_mouse 0 -video_size 1920x1080 -i :99 -frames:v 1 "$RD/$name.png"
  echo "CAP $name size=$(stat -c %s "$RD/$name.png" 2>/dev/null || echo 0)"
  i=$((i+1))
done
echo "=== app.log tail ==="
tail -40 "$RD/app.log"
kill $APP 2>/dev/null
wait $APP 2>/dev/null
echo CAPTURE_DONE
'''


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--stamp", default="capture")
    ap.add_argument("--remote-dir", default="/tmp/railhmi_uireview")
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--build-timeout", type=int, default=1200)
    ap.add_argument("--command-timeout", type=int, default=150)
    ap.add_argument("--repo", default=None)
    ap.add_argument("--keep-remote", action="store_true")
    args = ap.parse_args()

    pw = os.environ.get(args.credential_env)
    if not pw:
        print("credential env %s missing" % args.credential_env, file=sys.stderr)
        return 2

    repo = Path(args.repo) if args.repo else Path(__file__).resolve().parents[4]
    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    rd = args.remote_dir
    transcript = {"host": args.host, "stamp": args.stamp, "repo": str(repo), "steps": []}
    log_path = out_dir / ("ui-capture-%s.log" % args.stamp)

    def log(msg: str) -> None:
        line = "[%s] %s" % (time.strftime("%H:%M:%S"), msg)
        print(line)
        with open(log_path, "a", encoding="utf-8") as fh:
            fh.write(line + "\n")

    c = paramiko.SSHClient()
    c.load_system_host_keys()
    c.set_missing_host_key_policy(VerifyPolicy(args.expected_fingerprint))
    c.connect(args.host, port=args.port, username=args.user, password=pw,
              timeout=args.connect_timeout, banner_timeout=10, auth_timeout=10)

    def run(cmd: str, timeout: int = None) -> dict:
        timeout = timeout or args.command_timeout
        started = time.time()
        _, o, e = c.exec_command(cmd, timeout=timeout)
        out = o.read().decode("utf-8", "replace")
        err = e.read().decode("utf-8", "replace")
        rc = o.channel.recv_exit_status()
        res = {"cmd": cmd, "exit": rc, "elapsed_s": round(time.time() - started, 2),
               "output": (out + err).strip()}
        transcript["steps"].append(res)
        log("exit=%s %.1fs :: %s" % (rc, res["elapsed_s"], cmd[:110]))
        return res

    try:
        run("rm -rf %s && mkdir -p %s" % (rd, rd))

        have = run("command -v Xvfb >/dev/null 2>&1 && echo HAVE_XVFB || echo MISS_XVFB")
        if "HAVE_XVFB" not in have["output"]:
            log("installing xvfb (review tooling dep, test scope)")
            inst = run("sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y "
                       "--no-install-recommends xvfb", args.build_timeout)
            if inst["exit"] != 0:
                run("sudo -n env DEBIAN_FRONTEND=noninteractive apt-get update", args.build_timeout)
                inst = run("sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y "
                           "--no-install-recommends xvfb", args.build_timeout)
            transcript["xvfb_install_exit"] = inst["exit"]
        transcript["deps"] = run("command -v Xvfb && command -v ffmpeg && command -v xdotool && echo DEPS_OK")["output"]

        # 上传源码
        sftp = c.open_sftp()
        sftp.get_channel().settimeout(args.command_timeout)
        uploaded = 0
        try:
            for base_rel in ("app", "packaging"):
                root = repo / base_rel
                for dirpath, dirnames, filenames in os.walk(root):
                    dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
                    rel_dir = os.path.relpath(dirpath, repo)
                    remote_dir = posixpath.join(rd, rel_dir.replace("\\", "/")) if rel_dir != "." else rd
                    try:
                        sftp.stat(remote_dir)
                    except IOError:
                        sftp.mkdir(remote_dir)
                    for fn in filenames:
                        local = os.path.join(dirpath, fn)
                        rel = os.path.relpath(local, repo).replace("\\", "/")
                        sftp.put(local, posixpath.join(rd, rel))
                        uploaded += 1
        finally:
            sftp.close()
        transcript["uploaded"] = uploaded
        log("uploaded %d files" % uploaded)

        # 构建 Release
        build = run("cd %s/app && bash -o pipefail -c 'bash build.sh Release 2>&1 | tail -40'" % rd,
                    args.build_timeout)
        art = run("cd %s/app && ls -l build-target/src/ui/railhmi && file build-target/src/ui/railhmi "
                  "&& sha256sum build-target/src/ui/railhmi" % rd)
        transcript["build_exit"] = build["exit"]
        transcript["artifact"] = art["output"]
        if build["exit"] != 0 or "railhmi" not in art["output"]:
            transcript["blocked"] = "build failed"
            (out_dir / ("ui-capture-%s.transcript.json" % args.stamp)).write_text(
                json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
            return 3

        # 临时配置副本：simulation=true, default_role=admin（仅观察用）
        run("cp -r %s/app/config %s/config && sed -i 's/^  simulation: .*/  simulation: true/;"
            "s/^  default_role: .*/  default_role: admin/' %s/config/app.yaml "
            "&& sed -n '1,12p' %s/config/app.yaml" % (rd, rd, rd, rd))

        # 虚拟显示
        run("pkill -x Xvfb 2>/dev/null; rm -f /tmp/.X99-lock; "
            "nohup Xvfb :99 -screen 0 1920x1080x24 -nolisten tcp </dev/null >%s/xvfb.log 2>&1 & echo xvfb_pid=$!" % rd)
        up = run("for i in $(seq 1 20); do [ -S /tmp/.X11-unix/X99 ] && { echo XVFB_UP; break; }; sleep 0.5; done")
        transcript["xvfb_up"] = "XVFB_UP" in up["output"]
        if not transcript["xvfb_up"]:
            run("tail -20 %s/xvfb.log 2>&1" % rd)
            transcript["blocked"] = "xvfb not up"
            (out_dir / ("ui-capture-%s.transcript.json" % args.stamp)).write_text(
                json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
            return 3

        # 单一远程脚本：启动+导航+截屏+退出（避免后台 GUI 持久持有 channel stdout）
        script = CAPTURE_SCRIPT.replace("__RD__", rd).replace("__PAGES__", " ".join(PAGES))
        sftp = c.open_sftp()
        try:
            with sftp.open("%s/capture.sh" % rd, "w") as fh:
                fh.write(script)
        finally:
            sftp.close()
        cap = run("bash %s/capture.sh" % rd, timeout=max(240, args.command_timeout))
        transcript["capture_output"] = cap["output"]
        log(cap["output"][-2000:])

        # 取回 PNG + 日志
        sftp = c.open_sftp()
        try:
            for name in PAGES:
                try:
                    sftp.get("%s/%s.png" % (rd, name), str(out_dir / ("%s-%s.png" % (args.stamp, name))))
                except IOError as exc:
                    log("fetch %s failed: %r" % (name, exc))
            for fn in ("app.log", "xvfb.log"):
                try:
                    sftp.get("%s/%s" % (rd, fn), str(out_dir / ("%s-%s" % (args.stamp, fn))))
                except IOError:
                    pass
        finally:
            sftp.close()

        run("pkill -x railhmi 2>/dev/null; pkill -x Xvfb 2>/dev/null; echo cleaned")
        if not args.keep_remote:
            run("rm -rf %s" % rd)

        (out_dir / ("ui-capture-%s.transcript.json" % args.stamp)).write_text(
            json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
        log("done")
        return 0
    finally:
        c.close()


if __name__ == "__main__":
    raise SystemExit(main())
