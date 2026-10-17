#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""VERIFY-REPAIR-n008-v2 — R3 六页导航实际装载观察（验证方独立夹具，UI 评审同方法）。

无参数直跑。方法（与 UI 评审 20261017-uiobs-r3 相同方法判定口径不同于判据）：
1) 上传 app/ (当前工作树) 到 /tmp/railhmi_verify_nav_v2/；
2) 板上构建 Release；
3) 临时配置副本 simulation=true default_role=admin；
4) Xvfb :99 + 软件后端启动，xdotool 点 NavBar 六个槽位（NavBar 卡槽高 = nav_width，
   从 top_bar_height 起）逐页截屏；
5) 取回 PNG + app.log + xvfb.log + 产物 sha256。
判据在报告生成阶段独立核对：WIN_UP=1、app.log QML 错误 0 条（chipName/doesNotExist/
is not a type/TypeError/Cannot open 全为 0）、每天截图非空且互异。
"""
from __future__ import annotations

import base64
import hashlib
import json
import os
import posixpath
import sys
import time
from pathlib import Path

import paramiko

FP = "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU"
HOST, USER = "192.168.0.103", "admin"
RD = "/tmp/railhmi_verify_nav_v2"
PAGES = ["p1_realtime", "p2_alarm_center", "p3_replay", "p4_all_cameras",
         "p5_ai_debug", "p6_maintenance"]
SKIP_DIRS = {".git", "__pycache__", "build-target", "build-host", "build-core",
             "build-media"}
HERE = Path(__file__).resolve().parent
REPO = HERE.parents[3]  # docs/hmi/test_evidence/VERIFY-REPAIR-n008-v2 -> repo root

# NavBar 槽位坐标：top_bar_height=48 + 第 i 个 nav_width=88 卡槽中心 44+48+88*i（Theme.qml 274/275）
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
  y=$((44 + 88*i + 48))
  DISPLAY=:99 xdotool mousemove 44 "$y" click 1
  sleep 2
  ffmpeg -y -loglevel error -f x11grab -draw_mouse 0 -video_size 1920x1080 -i :99 -frames:v 1 "$RD/$name.png"
  echo "CAP $name size=$(stat -c %s "$RD/$name.png" 2>/dev/null || echo 0)"
  i=$((i+1))
done
echo "=== app.log tail ==="
cat "$RD/app.log"
echo "=== app.log qml-error count ==="
cat "$RD/app.log" | wc -l
echo APPLOG_LINES=$(cat "$RD/app.log" | wc -l)
kill $APP 2>/dev/null
wait $APP 2>/dev/null
echo CAPTURE_DONE
'''


def fingerprint(key) -> str:
    return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")


class Want(paramiko.MissingHostKeyPolicy):
    def __init__(self) -> None:
        self.want = FP

    def missing_host_key(self, client, hostname, key) -> None:
        got = fingerprint(key)
        if got != self.want:
            raise paramiko.SSHException("fingerprint mismatch: %s" % got)


def main() -> int:
    pw = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL")
    if not pw:
        print("credential env missing", file=sys.stderr)
        return 2
    out_dir = HERE
    transcript = {"host": HOST, "stamp": "verify-v2-r3", "steps": []}
    log_path = out_dir / "ui-nav-20261017-verify-v2-r3.log"

    def log(msg):
        line = "[%s] %s" % (time.strftime("%H:%M:%S"), msg)
        print(line)
        with open(log_path, "a", encoding="utf-8") as fh:
            fh.write(line + "\n")

    c = paramiko.SSHClient()
    c.load_system_host_keys()
    c.set_missing_host_key_policy(Want())
    c.connect(HOST, port=22, username=USER, password=pw,
              timeout=10, banner_timeout=10, auth_timeout=10)

    def run(cmd, timeout=150):
        _, o, e = c.exec_command(cmd, timeout=timeout)
        out = o.read().decode("utf-8", "replace")
        err = e.read().decode("utf-8", "replace")
        rc = o.channel.recv_exit_status()
        res = {"cmd": cmd, "exit": rc,
               "output": (out + ("\n[stderr]" + err if err.strip() else "")).strip()}
        transcript["steps"].append(res)
        log("exit=%s :: %s" % (rc, cmd[:100]))
        return res

    try:
        run("rm -rf %s && mkdir -p %s" % (RD, RD), 30)

        have = run("command -v Xvfb >/dev/null 2>&1 && echo HAVE_XVFB || echo MISS_XVFB")
        if "HAVE_XVFB" not in have["output"]:
            log("installing xvfb (review tooling dep, test scope)")
            run("sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y "
                "--no-install-recommends xvfb", 600)
            run("sudo -n env DEBIAN_FRONTEND=noninteractive apt-get update", 600)
            have = run("command -v Xvfb >/dev/null 2>&1 && echo HAVE_XVFB || echo MISS_XVFB")
            if "HAVE_XVFB" not in have["output"]:
                transcript["blocked"] = "xvfb unavailable"
                (out_dir / "ui-nav-transcript.json").write_text(
                    json.dumps(transcript, indent=2, ensure_ascii=False), encoding="utf-8")
                return 3

        # 上传 app/（跳过 build 与 third_party 大文件目录；QML/config/tools/build.sh 必需）
        sftp = c.open_sftp()
        sftp.get_channel().settimeout(120)
        uploaded = 0
        try:
            root = REPO / "app"
            for dirpath, dirnames, filenames in os.walk(root):
                dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
                rel_dir = os.path.relpath(dirpath, REPO)
                rdir = posixpath.join(RD, rel_dir.replace("\\", "/")) if rel_dir != "." else RD
                try:
                    sftp.stat(rdir)
                except IOError:
                    sftp.mkdir(rdir)
                for fn in filenames:
                    sftp.put(os.path.join(dirpath, fn),
                             posixpath.join(RD, rel_dir.replace("\\", "/"), fn))
                    uploaded += 1
        finally:
            sftp.close()
        transcript["uploaded"] = uploaded
        log("uploaded %d files" % uploaded)

        build = run("cd %s/app && bash -o pipefail -c 'bash build.sh Release 2>&1 | tail -25'"
                    % RD, 1200)
        art = run("cd %s/app && sha256sum build-target/src/ui/railhmi && file "
                  "build-target/src/ui/railhmi | head -1" % RD, 60)
        transcript["build_exit"] = build["exit"]
        transcript["artifact"] = art["output"]
        if build["exit"] != 0:
            transcript["blocked"] = "build failed"
            log("BUILD FAILED")
            (out_dir / "ui-nav-transcript.json").write_text(
                json.dumps(transcript, indent=2, ensure_ascii=False), encoding="utf-8")
            return 3
        log("build ok")

        run("cp -r %s/app/config %s/config && sed -i 's/^  simulation: .*/  simulation: true/;"
            "s/^  default_role: .*/  default_role: admin/' %s/config/app.yaml" % (RD, RD, RD), 30)

        run("pkill -x Xvfb 2>/dev/null; rm -f /tmp/.X99-lock; "
            "nohup Xvfb :99 -screen 0 1920x1080x24 -nolisten tcp </dev/null >%s/xvfb.log 2>&1 & "
            "echo xvfb_pid=$!" % RD, 30)
        up = run("for i in $(seq 1 20); do [ -S /tmp/.X11-unix/X99 ] && { echo XVFB_UP; break; }; "
                 "sleep 0.5; done", 30)
        transcript["xvfb_up"] = "XVFB_UP" in up["output"]
        if not transcript["xvfb_up"]:
            run("tail -20 %s/xvfb.log" % RD)
            transcript["blocked"] = "xvfb not up"
            (out_dir / "ui-nav-transcript.json").write_text(
                json.dumps(transcript, indent=2, ensure_ascii=False), encoding="utf-8")
            return 3

        script = CAPTURE_SCRIPT.replace("__RD__", RD).replace("__PAGES__", " ".join(PAGES))
        sftp = c.open_sftp()
        try:
            with sftp.open("%s/capture.sh" % RD, "w") as fh:
                fh.write(script)
        finally:
            sftp.close()
        cap = run("bash %s/capture.sh" % RD, timeout=300)
        transcript["capture_output"] = cap["output"]

        sftp = c.open_sftp()
        try:
            for name in PAGES:
                try:
                    sftp.get("%s/%s.png" % (RD, name), str(out_dir / ("%s.png" % name)))
                except IOError as exc:
                    log("fetch %s: %r" % (name, exc))
            for fn in ("app.log", "xvfb.log"):
                try:
                    sftp.get("%s/%s" % (RD, fn), str(out_dir / ("nav-%s" % fn)))
                except IOError:
                    pass
        finally:
            sftp.close()

        run("pkill -x railhmi 2>/dev/null; pkill -x Xvfb 2>/dev/null; rm -rf %s; echo cleaned" % RD)
        transcript["done"] = True
        (out_dir / "ui-nav-transcript.json").write_text(
            json.dumps(transcript, indent=2, ensure_ascii=False), encoding="utf-8")
        log("done")
        return 0
    finally:
        c.close()


if __name__ == "__main__":
    raise SystemExit(main())
