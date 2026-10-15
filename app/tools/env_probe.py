#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 依赖 / 构建环境探针 (ITEM-001, ITEM-002 复用)

按 docs/hmi/DESKTOP_APP_DESIGN.md §3.4 逐条核验桌面应用的目标构建/运行依赖，
把每条命令的原始输出记录下来，生成可归档的 Markdown 报告。

编号对齐 (D-02 修复): 与 DESKTOP_APP_DESIGN §3.4 一致
  E-01 Qt / E-02 qmlglsink / E-03 GL-EGL / E-04 MPP+GL 元素 / E-05 RGA /
  E-06 NPU / E-07 SQLite / E-08 显示会话 / E-09 磁盘 / E-10 相机可达 /
  E-11 NVR 接口 / E-12 governor+UDP(扩展) / E-13 UI 渲染前提(扩展, D-04)

设计约束:
  * 只读探测: 不安装、不修改系统、不写产品目录以外位置。
  * 跨平台: 在 RK3588 目标板 (Ubuntu 22.04 aarch64) 与主机 (任意 OS) 均可运行;
    同一脚本在两边跑出的结果必须分开表述 —— 主机结果不能当作目标板结论。
  * 不硬编码开发机路径; 不使用第三方依赖 (仅 Python 标准库)。
  * 相机/NVR 网络探测默认关闭 (需要项目授权), 用 --probe-cameras 显式开启。

用法:
  python3 env_probe.py                       # 打印报告到 stdout
  python3 env_probe.py --out report.md       # 写 Markdown 报告
  python3 env_probe.py --raw-out raw.txt     # 同时保留原始日志
  python3 env_probe.py --json-out probe.json # 机器可读结果
  python3 env_probe.py --probe-cameras       # 额外探测相机/NVR 端口 (需授权)
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import socket
import subprocess
import sys
import time
from pathlib import Path

DEFAULT_TIMEOUT = 20

BOARD_QT_PLUGIN_DIRS = [
    "/usr/lib/aarch64-linux-gnu/qt5/plugins/platforms",
    "/usr/lib/aarch64-linux-gnu/qt6/plugins/platforms",
    "/usr/lib/qt5/plugins/platforms",
]
BOARD_QT_QML_DIRS = [
    "/usr/lib/aarch64-linux-gnu/qt5/qml",
    "/usr/lib/qt5/qml",
    "/usr/lib/aarch64-linux-gnu/qt6/qml",
]
BOARD_LIB_DIRS = [
    "/usr/lib/aarch64-linux-gnu",
    "/usr/lib",
    "/usr/local/lib",
]
BOARD_PATHS = {
    "rknpu_version": "/sys/kernel/debug/rknpu/version",
    "rknpu_load": "/sys/kernel/debug/rknpu/load",
    "x11_socket": "/tmp/.X11-unix",
    "governor": "/sys/devices/system/cpu/cpu0/cpufreq/scaling_governor",
    "udp_sysctl": "/etc/sysctl.d/99-udp-buffers.conf",
}

# 相机配置基线 (docs/hmi/INTERACTION_DESIGN.md §7.3); 仅用于 --probe-cameras
CAMERA_BASELINE = [
    ("CAM-01", "车头前方", "192.168.1.65"),
    ("CAM-02", "车辆左侧", "192.168.1.160"),
    ("CAM-03", "车辆右侧", "192.168.1.162"),
    ("CAM-04", "车顶受电弓", "192.168.1.163"),
    ("CAM-05", "车底轮轨", "192.168.1.165"),
    ("CAM-06", "司机室车门", "192.168.1.167"),
]
NVR_BASELINE = ("NVR", "192.168.1.4")


def run(cmd, timeout: int = DEFAULT_TIMEOUT) -> dict:
    exe = cmd[0]
    if shutil.which(exe) is None:
        return {"cmd": cmd, "available": False, "exit": None,
                "output": "(command not found: %s)" % exe}
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        out = ((p.stdout or "") + (p.stderr or "")).strip()
        return {"cmd": cmd, "available": True, "exit": p.returncode, "output": out}
    except subprocess.TimeoutExpired:
        return {"cmd": cmd, "available": True, "exit": None,
                "output": "(timeout after %ss)" % timeout}
    except Exception as e:  # noqa: BLE001
        return {"cmd": cmd, "available": True, "exit": None, "output": "(error: %r)" % e}


def first_existing(paths):
    for p in paths:
        if os.path.isdir(p):
            return p
    return None


def list_dir(path: str) -> list:
    try:
        return sorted(os.listdir(path))
    except OSError:
        return []


def lib_present(name_fragment: str) -> list:
    hits = []
    for d in BOARD_LIB_DIRS:
        if not os.path.isdir(d):
            continue
        try:
            for fn in os.listdir(d):
                if name_fragment in fn:
                    hits.append(os.path.join(d, fn))
        except OSError:
            continue
    return sorted(hits)


def port_open(host: str, port: int, timeout: float = 2.0) -> bool:
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def tcp_entry(label: str, host: str, port: int) -> tuple:
    return (label, {"cmd": ["[tcp-connect]", host, str(port)], "available": True,
                    "exit": None,
                    "output": "open" if port_open(host, port) else "closed/unreachable"})


# --------------------------------------------------------------------------
# 检查项
# --------------------------------------------------------------------------

def check_env_header() -> list:
    return [
        ("uname -a", run(["uname", "-a"])),
        ("python3 --version", run([sys.executable, "--version"])),
    ]


def check_build_toolchain() -> list:
    return [
        ("gcc --version", run(["gcc", "--version"])),
        ("g++ --version", run(["g++", "--version"])),
        ("cmake --version", run(["cmake", "--version"])),
        ("pkg-config --version", run(["pkg-config", "--version"])),
        ("dpkg --print-architecture", run(["dpkg", "--print-architecture"])),
    ]


def check_qt() -> dict:
    cmds = [
        ("qmake -v", run(["qmake", "-v"])),
        ("qmake6 -v", run(["qmake6", "-v"])),
        ("dpkg -l | grep -i qt5 (filtered)", run(["bash", "-lc",
            "dpkg -l 2>/dev/null | grep -iE 'qtbase5|qtdeclarative5|qml-module-qtquick' | head -40"])),
        ("dpkg -l | grep QML/Quick libs (filtered)", run(["bash", "-lc",
            "dpkg -l 2>/dev/null | grep -iE 'libqt5qml|libqt5quick|qtdeclarative|qml-module' | head -40"])),
        ("ls qt5/qml modules", run(["bash", "-lc",
            "ls %s 2>&1 | head -40" % " ".join(BOARD_QT_QML_DIRS)])),
    ]
    plugin_dir = first_existing(BOARD_QT_PLUGIN_DIRS)
    return {"commands": cmds, "plugin_dir": plugin_dir,
            "plugins": list_dir(plugin_dir) if plugin_dir else []}


def check_gst_qt_sink() -> list:
    return [
        ("gst-inspect-1.0 --version", run(["gst-inspect-1.0", "--version"])),
        ("gst-inspect-1.0 qmlglsink", run(["gst-inspect-1.0", "qmlglsink"])),
        ("dpkg -l | grep gstreamer1.0-qt (filtered)", run(["bash", "-lc",
            "dpkg -l 2>/dev/null | grep -iE 'gstreamer1.0-(qt5|qt6|plugins)' | head -20"])),
    ]


def check_graphics() -> dict:
    cmds = [
        ("eglinfo", run(["eglinfo"])),
        ("glxinfo -B", run(["glxinfo", "-B"])),
        ("ls /proc/driver/mali", run(["bash", "-lc", "ls -la /proc/driver/mali 2>&1 | head -20"])),
    ]
    libs = (lib_present("libEGL") + lib_present("libGLESv2") + lib_present("libmali")
            + lib_present("libgbm"))
    return {"commands": cmds, "libs": libs}


def check_mpp_gl_elements() -> list:
    elems = ["mppvideodec", "glupload", "glcolorconvert", "rgavideoconvert",
             "rtspsrc", "h265parse", "h264parse", "appsink", "glimagesink"]
    return [("gst-inspect-1.0 %s" % e, run(["gst-inspect-1.0", e])) for e in elems]


def check_rga() -> dict:
    cmds = [
        ("gst-inspect-1.0 rgavideoconvert", run(["gst-inspect-1.0", "rgavideoconvert"])),
        ("ldconfig -p | grep -i rga (filtered)", run(["bash", "-lc",
            "ldconfig -p 2>/dev/null | grep -i rga | head -20"])),
    ]
    return {"commands": cmds, "libs": lib_present("librga")}


def check_npu() -> dict:
    cmds = [
        ("cat /sys/kernel/debug/rknpu/version", run(["cat", BOARD_PATHS["rknpu_version"]])),
        ("cat /sys/kernel/debug/rknpu/load", run(["cat", BOARD_PATHS["rknpu_load"]])),
        ("ldconfig -p | grep -i rknnrt (filtered)", run(["bash", "-lc",
            "ldconfig -p 2>/dev/null | grep -i rknnrt | head -20"])),
    ]
    return {"commands": cmds, "libs": lib_present("librknnrt")}


def check_sqlite() -> dict:
    cmds = [
        ("sqlite3 --version", run(["sqlite3", "--version"])),
        ("python3 -c 'import sqlite3; print(sqlite3.sqlite_version)'",
         run([sys.executable, "-c", "import sqlite3; print(sqlite3.sqlite_version)"])),
    ]
    return {"commands": cmds, "libs": lib_present("libsqlite3")}


def check_display_session() -> dict:
    cmds = [
        ("printenv DISPLAY", run(["bash", "-lc", "printenv DISPLAY || echo '(DISPLAY unset)'"])),
        ("printenv XAUTHORITY", run(["bash", "-lc", "printenv XAUTHORITY || echo '(XAUTHORITY unset)'"])),
        ("ls /tmp/.X11-unix", run(["bash", "-lc", "ls -la /tmp/.X11-unix 2>&1 | head -10"])),
        ("systemctl get-default", run(["systemctl", "get-default"])),
        ("loginctl list-sessions", run(["loginctl", "list-sessions"])),
    ]
    return {"commands": cmds}


def check_disk(skip_slow: bool = False) -> dict:
    rows = []
    for t in ("/", "/home/admin"):
        try:
            u = shutil.disk_usage(t)
            rows.append((t, u.total, u.used, u.free))
        except OSError:
            rows.append((t, None, None, None))
    cmds = [
        ("df -h /", run(["df", "-h", "/"])),
        ("df -h /home/admin", run(["df", "-h", "/home/admin"])),
    ]
    if skip_slow:
        cmds.append(("du -sh /home/admin (skipped: --skip-slow)", {
            "cmd": ["du", "-sh", "/home/admin"], "available": True, "exit": None,
            "output": "skipped by --skip-slow (bounded board probe)"}))
    else:
        cmds.append(("du -sh /home/admin (may be slow)", run(["bash", "-lc",
            "du -sh /home/admin 2>/dev/null | head -5"], 60)))
    return {"commands": cmds, "usage": rows}


def check_cpu_governor_udp() -> dict:
    cmds = [
        ("cat scaling_governor", run(["cat", BOARD_PATHS["governor"]])),
        ("systemctl is-enabled cpu-performance.service",
         run(["systemctl", "is-enabled", "cpu-performance.service"])),
        ("cat /etc/sysctl.d/99-udp-buffers.conf", run(["cat", BOARD_PATHS["udp_sysctl"]])),
        ("sysctl net.core.rmem_max", run(["sysctl", "net.core.rmem_max"])),
    ]
    return {"commands": cmds}


def check_ui_prerequisites() -> dict:
    """D-04: VISUAL_DESIGN §4/§9 的渲染前提（字体/触控/分辨率/QML 模块）。"""
    qml_dir = first_existing(BOARD_QT_QML_DIRS)
    controls2 = None
    if qml_dir:
        for cand in ("QtQuick/Controls.2", "QtQuick/Controls"):
            if os.path.isdir(os.path.join(qml_dir, cand)):
                controls2 = os.path.join(qml_dir, cand)
                break
    cmds = [
        ("fc-list | grep -iE 'Noto Sans CJK|Noto Sans Mono' (filtered)", run(["bash", "-lc",
            "fc-list 2>/dev/null | grep -iE 'Noto Sans CJK|Noto Sans Mono' | head -10"])),
        ("fc-list | wc -l", run(["bash", "-lc", "fc-list 2>/dev/null | wc -l"])),
        ("xinput list", run(["xinput", "list"])),
        ("libinput list-devices (needs root)", run(["libinput", "list-devices"])),
        ("xrandr", run(["xrandr"])),
        ("dpkg -l | grep qml-module-qtquick (filtered)", run(["bash", "-lc",
            "dpkg -l 2>/dev/null | grep -E 'qml-module-qtquick' | head -20"])),
        ("ls QtQuick Controls 2 qml dir", run(["bash", "-lc",
            "ls -d /usr/lib/aarch64-linux-gnu/qt5/qml/QtQuick/Controls.2 2>&1 | head -5"])),
    ]
    return {"commands": cmds, "qml_dir": qml_dir, "controls2": controls2}


def check_cameras() -> list:
    out = []
    for cid, name, ip in CAMERA_BASELINE:
        out.append(tcp_entry("RTSP TCP 554 %s %s (%s)" % (cid, name, ip), ip, 554))
    return out


def check_nvr() -> list:
    cid, ip = NVR_BASELINE
    return [
        tcp_entry("NVR HTTP 80 %s" % ip, ip, 80),
        tcp_entry("NVR RTSP 554 %s" % ip, ip, 554),
    ]


# --------------------------------------------------------------------------

def fmt_cmd(entry) -> list:
    cmd, res = entry
    lines = ["```", "$ " + cmd]
    if res.get("exit") is not None:
        lines.append("# exit=%s" % res["exit"])
    out = res.get("output", "")
    lines.append(out if out else "(no output)")
    lines.append("```")
    return lines


def build_report(probe_cameras: bool, skip_slow: bool = False) -> tuple:
    now = time.strftime("%Y-%m-%d %H:%M:%S")
    env = {
        "hostname": platform.node(),
        "system": platform.system(),
        "release": platform.release(),
        "machine": platform.machine(),
        "python": sys.version.split()[0],
    }
    md, data = [], {"generated_at": now, "environment": env, "checks": {}}
    md.append("# railhmi 依赖 / 构建环境核验原始报告")
    md.append("")
    md.append("- 探针: `app/tools/env_probe.py` (只读, 标准库); 编号对齐 `DESKTOP_APP_DESIGN` §3.4")
    md.append("- 生成时间: %s" % now)
    md.append("- 执行环境: hostname=`%s` system=`%s %s` machine=`%s` python=`%s`"
              % (env["hostname"], env["system"], env["release"], env["machine"], env["python"]))
    md.append("")
    md.append("> 环境判定: 若 machine 含 `aarch64`/`arm64` 且存在 `/sys/kernel/debug/rknpu`，"
              "则本报告为目标板 (RK3588) 结果；否则为**主机**结果，不能作为目标板结论。")
    md.append("")

    def section(cid, title, purpose, entries, extra=None):
        md.append("## %s %s" % (cid, title))
        md.append("")
        md.append("- 目的: %s" % purpose)
        md.append("")
        for e in entries:
            md.extend(fmt_cmd(e))
            md.append("")
        for line in (extra or []):
            md.append(line)
        if extra:
            md.append("")
        data["checks"][cid] = {
            "title": title,
            "commands": [{"cmd": c, "exit": r.get("exit"), "available": r.get("available"),
                          "output": r.get("output")} for c, r in entries],
        }

    section("E-00", "执行环境与工具链（扩展项）", "确认 ABI/架构与编译工具链",
            check_env_header() + check_build_toolchain())

    q = check_qt()
    extra = ["- Qt 平台插件目录: `%s`" % (q["plugin_dir"] or "(not found)")]
    if q["plugins"]:
        extra.append("- 插件列表: %s" % ", ".join(q["plugins"]))
        extra.append("- xcb 可用: **%s**" % ("yes" if any("xcb" in p for p in q["plugins"]) else "no"))
        extra.append("- eglfs 可用: **%s**" % ("yes" if any("eglfs" in p for p in q["plugins"]) else "no"))
    section("E-01", "Qt 版本与平台插件", "确认 Qt5.15 与 xcb/eglfs 平台插件", q["commands"], extra)

    section("E-02", "GStreamer Qt sink (qmlglsink)", "确认 gstreamer1.0-qt5 与 qmlglsink",
            check_gst_qt_sink())

    g = check_graphics()
    extra = ["- GL/EGL 库: " + ", ".join(g["libs"][:12])] if g["libs"] else []
    section("E-03", "GL/EGL (Mali)", "确认 EGL/GLES 驱动与 renderer", g["commands"], extra)

    section("E-04", "MPP 解码与 GL 元素",
            "确认 mppvideodec/glupload/glcolorconvert/rgavideoconvert/glimagesink 存在",
            check_mpp_gl_elements())

    r = check_rga()
    extra = ["- librga: " + (", ".join(r["libs"]) if r["libs"] else "(not found)")]
    section("E-05", "RGA (librga)", "确认 RGA 库与转换元素", r["commands"], extra)

    n = check_npu()
    extra = ["- librknnrt: " + (", ".join(n["libs"]) if n["libs"] else "(not found)")]
    section("E-06", "NPU 运行时 (librknnrt)", "确认 2.3.2 与 rknpu 驱动版本可读", n["commands"], extra)

    s = check_sqlite()
    extra = ["- libsqlite3: " + (", ".join(s["libs"]) if s["libs"] else "(not found)")]
    section("E-07", "SQLite", "确认 sqlite3 与库", s["commands"], extra)

    section("E-08", "显示会话与图形目标", "确认 X11 DISPLAY/XAUTHORITY 与 systemd 默认目标",
            check_display_session()["commands"])

    d = check_disk(skip_slow=skip_slow)
    extra = []
    for t, total, used, free in d["usage"]:
        if total:
            extra.append("- `%s`: total=%.1f GiB used=%.1f GiB free=%.1f GiB (%.0f%% used)"
                         % (t, total / 2**30, used / 2**30, free / 2**30, 100.0 * used / total))
        else:
            extra.append("- `%s`: (unavailable)" % t)
    section("E-09", "磁盘容量", "确认根分区与运行目录可用空间", d["commands"], extra)

    if probe_cameras:
        section("E-10", "相机可达性 (需授权)", "六路 RTSP 端口可达性；不发送 RTSP 鉴权",
                check_cameras())
        section("E-11", "NVR 接口 (需授权)",
                "NVR 端口可达性；**回放/导出接口能力需厂商文档或现场核验，本项不覆盖**",
                check_nvr())
    else:
        md.append("## E-10 相机可达性")
        md.append("")
        md.append("- 状态: **未执行**（默认关闭；网络探测需项目授权，`--probe-cameras` 开启）。")
        md.append("")
        md.append("## E-11 NVR 接口")
        md.append("")
        md.append("- 状态: **未执行**（需授权）。注: 本项仅能测端口可达性，"
                  "NVR 回放/导出接口能力需厂商文档/现场核验，属 E2E/现场档。")
        md.append("")
        data["checks"]["E-10"] = {"title": "camera reachability", "commands": [],
                                  "status": "not_run_needs_authorization"}
        data["checks"]["E-11"] = {"title": "NVR interface", "commands": [],
                                  "status": "not_run_needs_authorization"}

    section("E-12", "温度/调频/UDP 基线（扩展项）",
            "确认 governor 与 UDP 缓冲配置（现有平台事实）",
            check_cpu_governor_udp()["commands"])

    u = check_ui_prerequisites()
    extra = ["- Qt QML 目录: `%s`" % (u["qml_dir"] or "(not found)"),
             "- QtQuick Controls 目录: `%s`" % (u["controls2"] or "(not found)")]
    section("E-13", "UI 渲染前提（扩展项, D-04）",
            "字体（CJK/等宽）、触控输入、显示分辨率、QML 控件模块",
            u["commands"], extra)

    md.append("---")
    md.append("")
    md.append("## 结论口径")
    md.append("")
    md.append("- 本文件只陈述**实际执行的命令与原始输出**；命令不存在/超时/未执行均如实记录。")
    md.append("- 主机结果仅证明主机环境；目标板结论必须以 machine=aarch64 且 rknpu 可读的同一脚本输出为准。")
    md.append("- 元素/库“存在”不等于“协商/运行成功”，运行时协商属 ITEM-002 渲染 spike。")
    md.append("")
    return "\n".join(md), data


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi dependency / build-env probe")
    ap.add_argument("--out", help="write Markdown report to this path")
    ap.add_argument("--raw-out", help="write raw stdout to this path")
    ap.add_argument("--json-out", help="write structured JSON to this path")
    ap.add_argument("--probe-cameras", action="store_true",
                    help="enable network reachability probe (requires project authorization)")
    ap.add_argument("--skip-slow", action="store_true",
                    help="skip potentially slow commands (e.g. du) for a bounded board run")
    args = ap.parse_args()

    md, data = build_report(probe_cameras=args.probe_cameras, skip_slow=args.skip_slow)
    sys.stdout.write(md)
    for path, content in ((args.raw_out, md),
                          (args.out, md),
                          (args.json_out, json.dumps(data, ensure_ascii=False, indent=2))):
        if path:
            Path(path).parent.mkdir(parents=True, exist_ok=True)
            Path(path).write_text(content, encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
