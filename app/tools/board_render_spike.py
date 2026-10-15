#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 渲染路径选型 spike 板端运行器 (ITEM-002)

沿项目登记的 Paramiko SSH 入口，把 `app/spikes/render_path` 上传到目标板，
以有限等待执行依赖核对 / cmake 构建 / 两条路径测量，取回日志与 JSON，
再用本地 `analyze.py` 依据实测数据给出主/回退路径选择。

安全与边界:
  * 使用受信任 known_hosts（load_system_host_keys + 默认 RejectPolicy），不使用 AutoAddPolicy；
    主机指纹以 SHA256 打印供用户核验。
  * 连接 / 命令 / SFTP 全部显式有限超时；凭据仅从环境变量读取，不落日志。
  * 仅写入目标 /tmp 下的独立工作目录；不安装/不修改系统、不重启、不按名杀进程。

用法:
  python app/tools/board_render_spike.py --host <addr> --user <user> \
      --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
      --outdir docs/hmi/test_evidence/ITEM-002-render-path \
      --stamp <tag> [--source test|file|rtsp] [--file CLIP] [--url URL] \
      [--only deps|build|run|all] [--qpa offscreen]

退出码: 0 完成（选择可能为 SELECTED/NONE_USABLE/UNMEASURED，均在 JSON/MD 中说明）；2 连接失败；4 paramiko 缺失。
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import shlex
import socket
import subprocess
import sys
import time
from pathlib import Path

try:
    import paramiko
except Exception as exc:  # pragma: no cover
    sys.stderr.write("paramiko 不可用: %r\n" % (exc,))
    raise SystemExit(4)

SPIKE_FILES = ["main.cpp", "GlView.qml", "CpuView.qml", "qml.qrc",
               "CMakeLists.txt", "analyze.py", "README.md", "spike_core.c"]


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
                "output": (so + se)}
    except socket.timeout:
        return {"cmd": cmd, "exit": None, "elapsed_s": round(time.time() - started, 2),
                "output": "(timeout after %ss)" % timeout}
    except Exception as exc:  # noqa: BLE001
        return {"cmd": cmd, "exit": None, "elapsed_s": round(time.time() - started, 2),
                "output": "(error: %r)" % (exc,)}


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi board render-path spike runner")
    ap.add_argument("--host", default="")
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", default="")
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--local-dir", default=str(Path(__file__).resolve().parents[1] / "spikes" / "render_path"))
    ap.add_argument("--remote-dir", default="/tmp/railhmi_render_spike")
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", default=time.strftime("%Y%m%d-%H%M%S"))
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--build-timeout", type=int, default=600)
    ap.add_argument("--run-timeout", type=int, default=90)
    ap.add_argument("--source", default="test", choices=["test", "file", "rtsp"])
    ap.add_argument("--file", default="")
    ap.add_argument("--url", default="")
    ap.add_argument("--duration", type=int, default=8)
    ap.add_argument("--min-fps", type=float, default=20.0)
    ap.add_argument("--qpa", default="offscreen")
    ap.add_argument("--only", default="all", choices=["deps", "build", "run", "all"])
    ap.add_argument("--gen-clip", action="store_true",
                    help="generate an H.264 clip on the board and run the core measurement via --source file (decode path)")
    ap.add_argument("--keep-remote", action="store_true")
    args = ap.parse_args()

    if not args.host or not args.user:
        sys.stderr.write("需要 --host 与 --user（连接参数来自项目环境配置，不写死）\n")
        return 2
    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("凭据环境变量 %s 未加载；不猜测凭据。\n" % args.credential_env)
        return 2

    local = Path(args.local_dir)
    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    tag = "spike-%s-%s" % (args.host.replace(".", "_"), args.stamp)
    transcript_path = outdir / (tag + ".transcript.json")

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    transcript: dict = {"invocation": vars(args), "host_key_fingerprint": None, "steps": []}

    def note(step: str, res: dict) -> None:
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
        note("mkdir", rrun(client, "mkdir -p %s && rm -rf %s/build" % (remote, remote), 20))
        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(args.connect_timeout * 6)
            for fn in SPIKE_FILES:
                src = local / fn
                if src.is_file():
                    sftp.put(str(src), "%s/%s" % (remote, fn))
            note("upload", {"exit": 0, "output": "uploaded %d files" % len(SPIKE_FILES)})
        finally:
            sftp.close()

        deps_cmd = ("for d in /usr/lib/aarch64-linux-gnu/cmake/Qt5Quick "
                    "/usr/lib/aarch64-linux-gnu/cmake/Qt5Qml /usr/lib/aarch64-linux-gnu/cmake/Qt5Gui; do "
                    "if [ -d $d ]; then echo \"$d yes\"; else echo \"$d no\"; fi; done; "
                    "pkg-config --exists gstreamer-1.0 && echo 'pc gstreamer-1.0 yes' || echo 'pc gstreamer-1.0 no'; "
                    "pkg-config --exists librga && echo 'pc librga yes' || echo 'pc librga no'; "
                    "ls /usr/include/rga/im2d.h 2>/dev/null || echo 'no /usr/include/rga/im2d.h'; "
                    "ls /usr/include/gstreamer-1.0/gst/gst.h 2>/dev/null || echo 'no gst headers'; "
                    "ls -d /usr/lib/aarch64-linux-gnu/qt5/qml/QtQuick 2>/dev/null || echo 'no QtQuick qml module'")
        note("deps", rrun(client, deps_cmd, 30))

        built = False
        if args.only in ("build", "run", "all"):
            cfg = rrun(client, "cd %s && cmake -S . -B build -DCMAKE_BUILD_TYPE=Release > cmake.log 2>&1; "
                               "echo CMAKE_EXIT=$?; tail -n 80 cmake.log" % remote, args.build_timeout)
            note("cmake-configure", cfg)
            if "CMAKE_EXIT=0" in (cfg.get("output") or ""):
                mk = rrun(client, "cd %s && cmake --build build -j4 > make.log 2>&1; "
                                  "echo MAKE_EXIT=$?; tail -n 80 make.log" % remote, args.build_timeout)
                note("cmake-build", mk)
                ls = rrun(client, "ls -l %s/build/render_spike 2>&1" % remote, 20)
                note("binary", ls)
                built = "MAKE_EXIT=0" in (mk.get("output") or "")

        if args.only in ("run", "all") and built:
            run_pre = ("cd %s && QT_QPA_PLATFORM=%s DISPLAY=:0 ./build/render_spike" % (remote, shlex.quote(args.qpa)))
            for path in ("gl", "cpu"):
                extra = " --url %s" % shlex.quote(args.url) if args.source == "rtsp" and args.url else ""
                if args.source == "file" and args.file:
                    extra = " --file %s" % shlex.quote(args.file)
                cmd = ("%s --path %s --source %s%s --duration %d --out %s/%s_%s.json > %s/%s.log 2>&1; "
                       "echo RUN_EXIT=$?; tail -n 50 %s/%s.log"
                       % (run_pre, path, args.source, extra, args.duration, remote, path, args.stamp,
                          remote, path, remote, path))
                note("run-" + path, rrun(client, cmd, args.run_timeout))
        elif args.only in ("run", "all") and not built:
            note("run-skipped", {"exit": None, "output": "Qt spike build not successful; QML runs not attempted"})

        run_extra = ""
        if args.source == "file" and args.file:
            run_extra = " --file %s" % shlex.quote(args.file)
        elif args.source == "rtsp" and args.url:
            run_extra = " --url %s" % shlex.quote(args.url)

        # 无 Qt 的核心管线替代测量（当 Qt Quick/QML 开发包缺失时仍可得到解码+RGA 真实数据）
        core_built = False
        if args.only in ("build", "run", "all"):
            cc = rrun(client, "cd %s && gcc -O2 -o spike_core spike_core.c "
                              "$(pkg-config --cflags --libs gstreamer-1.0 gstreamer-app-1.0 "
                              "gstreamer-video-1.0) -lrga > core_build.log 2>&1; echo CC_EXIT=$?; tail -n 40 core_build.log"
                              % remote, args.build_timeout)
            note("core-build", cc)
            core_built = "CC_EXIT=0" in (cc.get("output") or "")
        if args.only in ("run", "all") and core_built:
            core_source, core_extra = args.source, run_extra
            if args.gen_clip:
                gen = rrun(client, "cd %s && (gst-launch-1.0 -q videotestsrc num-buffers=250 "
                                  "pattern=solid-color foreground-color=0x808080 ! videoconvert "
                                  "! video/x-raw,format=NV12,width=1280,height=720,framerate=25/1 "
                                  "! mpph264enc ! h264parse ! mp4mux ! filesink location=clip.mp4 || "
                                  "gst-launch-1.0 -q videotestsrc num-buffers=250 pattern=solid-color "
                                  "foreground-color=0x808080 ! videoconvert ! video/x-raw,format=NV12,width=1280,height=720,framerate=25/1 "
                                  "! x264enc speed-preset=ultrafast ! h264parse ! mp4mux ! filesink location=clip.mp4) "
                                  "> genclip.log 2>&1; echo GEN_EXIT=$?; ls -l clip.mp4 2>&1 | tail -n1" % remote,
                                  args.run_timeout)
                note("gen-clip", gen)
                if "GEN_EXIT=0" in (gen.get("output") or ""):
                    core_source, core_extra = "file", " --file %s/clip.mp4" % remote
            cr = ("cd %s && ./spike_core --source %s%s --duration %d --out core_%s.json "
                  "--snapshot core_%s.bmp > core.log 2>&1; echo RUN_EXIT=$?; tail -n 60 core.log"
                  % (remote, core_source, core_extra, args.duration, args.stamp, args.stamp))
            note("core-run", rrun(client, cr, args.run_timeout))
        elif args.only in ("run", "all") and not core_built:
            note("core-run-skipped", {"exit": None, "output": "spike_core build not successful"})

        # GL 路径构造探测（qmlglsink 缺失时应为 parse error）
        if args.only in ("run", "all"):
            glp = ("cd %s && gst-launch-1.0 -q videotestsrc num-buffers=20 "
                   "! video/x-raw,format=NV12,width=1280,height=720,framerate=25/1 "
                   "! glupload ! glcolorconvert ! qmlglsink > gl_construct.log 2>&1; "
                   "echo GL_EXIT=$?; tail -n 30 gl_construct.log" % remote)
            note("gl-construct", rrun(client, glp, args.run_timeout))

        # 取回日志与 JSON
        sftp = client.open_sftp()
        try:
            sftp.get_channel().settimeout(args.connect_timeout * 6)
            for name in ("cmake.log", "make.log", "gl.log", "cpu.log",
                         "gl_%s.json" % args.stamp, "cpu_%s.json" % args.stamp,
                         "core_build.log", "core.log", "core_%s.json" % args.stamp,
                         "core_%s.bmp" % args.stamp, "gl_construct.log", "genclip.log"):
                local_out = outdir / ("%s.%s" % (tag, name))
                try:
                    sftp.get("%s/%s" % (remote, name), str(local_out))
                    if local_out.stat().st_size == 0:
                        local_out.unlink()
                        note("download", {"exit": None, "output": "%s: empty/missing" % name})
                    else:
                        note("download", {"exit": 0, "output": local_out.name})
                except OSError as exc:
                    if local_out.exists() and local_out.stat().st_size == 0:
                        local_out.unlink()
                    note("download", {"exit": None, "output": "%s: %r" % (name, exc)})
        finally:
            sftp.close()

        if not args.keep_remote:
            note("cleanup", rrun(client, "rm -rf %s && echo cleaned" % remote, 20))
    finally:
        client.close()

    transcript_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")

    # 本地判据（仅当取到 JSON）
    gl_json = outdir / ("%s.gl_%s.json" % (tag, args.stamp))
    cpu_json = outdir / ("%s.cpu_%s.json" % (tag, args.stamp))
    core_json = outdir / ("%s.core_%s.json" % (tag, args.stamp))
    analyze = local / "analyze.py"
    if analyze.is_file():
        sel_json = outdir / ("selection-%s.json" % tag)
        sel_md = outdir / ("selection-%s.md" % tag)
        cmd = [sys.executable, str(analyze), "--min-fps", str(args.min_fps),
               "--out", str(sel_json), "--md-out", str(sel_md)]
        if gl_json.is_file():
            cmd += ["--gl", str(gl_json)]
        if cpu_json.is_file():
            cmd += ["--cpu", str(cpu_json)]
        elif core_json.is_file():
            cmd += ["--cpu", str(core_json)]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        note("analyze", {"exit": proc.returncode, "output": (proc.stdout + proc.stderr)})
        transcript_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")

    print("host_key_fingerprint:", transcript["host_key_fingerprint"])
    for s in transcript["steps"]:
        print("[%s] exit=%s %s" % (s.get("step"), s.get("exit"),
                                   (s.get("output") or "").strip().splitlines()[-1] if (s.get("output") or "").strip() else ""))
    print("transcript:", transcript_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
