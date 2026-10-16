#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 发布包 - 板端构建资源/支持范围只读探针 (n008 desktop_release)

沿项目登记的 Paramiko SSH 入口（runtime environment.yaml: access.rk3588_inspect）
采集发布包所需的**只读**事实：板型/OS/内核/编译器/CMake、Qt5 Quick/Qml 开发包、
GStreamer 插件、librga/librknnrt/libsqlite3、显示、目标构建依赖是否需要补装（apt 可达性）。
不安装、不改系统、不重启；只写目标 /tmp 与本机 out-dir。凭据仅从环境变量读取，不打日志。

用法:
  python board_release_probe.py --host 192.168.0.103 --user admin \
      --out-dir docs/hmi/release --stamp <tag>

退出码: 0 完成；2 连接/认证失败；3 远程命令失败。
"""
from __future__ import annotations

import argparse
import base64
import hashlib
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

# 只读命令集合；全部带有限等待。不使用 shell grep / 递归遍历。
COMMANDS = [
    ("os", "cat /etc/os-release 2>/dev/null | head -4; uname -a"),
    ("arch", "uname -m; nproc; getconf LONG_BIT"),
    ("toolchain", "gcc --version 2>/dev/null | head -1; cmake --version 2>/dev/null | head -1"),
    ("qt_cmake", "ls -d /usr/lib/*/cmake/Qt5Quick 2>/dev/null; ls -d /usr/lib/*/cmake/Qt5Qml 2>/dev/null; ls -d /usr/lib/*/cmake/Qt5Core 2>/dev/null"),
    ("qt_quick_cfg", "cat /usr/lib/aarch64-linux-gnu/cmake/Qt5Quick/Qt5QuickConfig.cmake 2>/dev/null | head -3"),
    ("qt_pkgs", "dpkg-query -W -f='${Package} ${Version} ${Status}\\n' qtbase5-dev qtdeclarative5-dev qml-module-qtquick2 qml-module-qtquick-controls2 libqt5svg5-dev 2>&1"),
    ("gst", "gst-inspect-1.0 --version 2>/dev/null | head -1; pkg-config --modversion gstreamer-1.0 2>/dev/null"),
    ("gst_plugins", "gst-inspect-1.0 2>/dev/null | head -5"),
    ("gst_elems", "for e in rtspsrc mppvideodec glupload appsink; do printf '%s=' $e; gst-inspect-1.0 $e >/dev/null 2>&1 && echo yes || echo no; done"),
    ("rga", "pkg-config --modversion librga 2>/dev/null; ls /usr/include/rga/im2d.h /usr/local/include/rga/im2d.h 2>/dev/null"),
    ("rknn", "ls /usr/lib/librknnrt.so /usr/lib/aarch64-linux-gnu/librknnrt.so 2>/dev/null; cat /sys/kernel/debug/rknpu/version 2>/dev/null"),
    ("sqlite", "ldconfig -p 2>/dev/null | fgrep libsqlite3 || true; ls /usr/include/sqlite3.h 2>/dev/null"),
    ("display", "printenv DISPLAY; ls /tmp/.X11-unix 2>/dev/null; systemctl is-active graphical.target 2>/dev/null"),
    ("systemd", "systemctl --version 2>/dev/null | head -1; ls /etc/systemd/system/railhmi.service 2>/dev/null || echo 'railhmi.service: not-installed'"),
    ("cpu_service", "systemctl is-enabled cpu-performance.service 2>/dev/null; systemctl is-active cpu-performance.service 2>/dev/null"),
    ("apt_reach", "python3 -c \"import socket;socket.create_connection(('archive.ubuntu.com',80),5);print('apt-reachable')\" 2>&1"),
    ("apt_policy", "apt-cache policy qtdeclarative5-dev 2>&1 | head -6"),
    ("sudo", "sudo -n true 2>&1 && echo 'sudo-nopass-ok' || echo 'sudo-nopass-fail'"),
    ("disk", "df -h /tmp / 2>/dev/null"),
    ("models_dir", "ls /home/admin/rknn_model_zoo/install/rk3588_linux_aarch64/rknn_yolov8_seg_demo/model 2>/dev/null || echo 'models: not-found'"),
    ("repo_dir", "ls -d /opt/railhmi 2>/dev/null || echo '/opt/railhmi: absent'"),
]


def fingerprint(key) -> str:
    try:
        digest = hashlib.sha256(key.asbytes()).digest()
        return "SHA256:" + base64.b64encode(digest).decode("ascii").rstrip("=")
    except Exception:  # noqa: BLE001
        return "(unavailable)"


def run_remote(client, cmd: str, timeout: int) -> dict:
    started = time.time()
    try:
        _in, out, err = client.exec_command(cmd, timeout=timeout)
        o = out.read().decode("utf-8", "replace")
        e = err.read().decode("utf-8", "replace")
        rc = out.channel.recv_exit_status()
        return {"exit": rc, "elapsed_s": round(time.time() - started, 2),
                "output": (o + e).strip()}
    except Exception as exc:  # noqa: BLE001
        return {"exit": None, "elapsed_s": round(time.time() - started, 2),
                "output": "(error: %r)" % (exc,)}


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi board release resource probe")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--stamp", default=time.strftime("%Y%m%d-%H%M%S"))
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--command-timeout", type=int, default=60)
    args = ap.parse_args()

    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("凭据环境变量 %s 未加载；不猜测凭据，标记未核验。\n" % args.credential_env)
        return 2

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    base = out_dir / ("board-release-probe-%s-%s" % (args.host.replace(".", "_"), args.stamp))

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    transcript: dict = {"invocation": {"host": args.host, "port": args.port, "user": args.user,
                                       "credential_env": args.credential_env,
                                       "connect_timeout_s": args.connect_timeout,
                                       "command_timeout_s": args.command_timeout},
                        "host_key_fingerprint": None, "connected": False, "facts": {}}
    log_path = base.with_suffix(".json")
    try:
        client.connect(hostname=args.host, port=args.port, username=args.user, password=secret,
                       timeout=args.connect_timeout, auth_timeout=args.connect_timeout,
                       banner_timeout=args.connect_timeout, look_for_keys=False, allow_agent=False)
    except Exception as exc:  # noqa: BLE001
        transcript["facts"]["connect_error"] = repr(exc)
        log_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
        sys.stderr.write("连接失败（未核验在线，不写成不可达）: %r；详见 %s\n" % (exc, log_path))
        return 2

    transcript["connected"] = True
    tr = client.get_transport()
    if tr is not None and tr.get_remote_server_key() is not None:
        transcript["host_key_fingerprint"] = fingerprint(tr.get_remote_server_key())
    try:
        for name, cmd in COMMANDS:
            res = run_remote(client, cmd, args.command_timeout)
            transcript["facts"][name] = {"cmd": cmd, **res}
    finally:
        client.close()

    log_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")

    md = ["# railhmi 板端构建资源/支持范围探针（只读）", "",
          "- host: `%s`  user: `%s`  port: %d" % (args.host, args.user, args.port),
          "- host_key_fingerprint: `%s`" % transcript["host_key_fingerprint"],
          "- generated: %s" % args.stamp, ""]
    for name, _ in COMMANDS:
        f = transcript["facts"].get(name, {})
        md.append("## %s" % name)
        md.append("```")
        md.append((f.get("output") or "").strip())
        md.append("```")
        md.append("")
    base.with_suffix(".md").write_text("\n".join(md), encoding="utf-8")

    print("host_key_fingerprint:", transcript["host_key_fingerprint"])
    for name, _ in COMMANDS:
        f = transcript["facts"].get(name, {})
        first = (f.get("output") or "").replace("\n", " ")[:140]
        print("[%s] exit=%s %s" % (name, f.get("exit"), first))
    print("json:", log_path)
    print("md:", base.with_suffix(".md"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
