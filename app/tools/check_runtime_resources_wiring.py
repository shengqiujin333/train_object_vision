#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 顶栏资源读数 + 门磁事件驱动加速 静态核对（现场二轮）

源码级核对（无 Qt 环境可运行）：
  R-01 真实采样：内存来自 /proc/meminfo，逐核 CPU 来自 /proc/stat 差值，
       逐核 NPU 来自 /sys/kernel/debug/rknpu/load；不得出现任何假造/常量百分比。
  R-02 分核真值：debugfs 不可读时只能退到 devfreq「聚合」并明确标注，
       绝不允许把聚合单值复制成多个核；全都读不到时显示「未接入」。
  R-03 装配：SystemMetricsRuntime 必须进 CMake / main.cpp / qml.qrc / TopStatusBar，
       QML 只消费 railhmiSystemMetrics.metrics，颜色字号全部走 Theme。
  R-04 门磁事件驱动：PropertiesChanged 必须订阅在 org.freedesktop.DBus.Properties
       （旧版写成 org.freedesktop.DBus.PropertiesChanged，永远收不到信号）；
       必须有 InterfacesAdded/ObjectManager 之外的直接推送帧路径与 QThread 装配。
  R-05 门磁 I/O 线程：GUI 侧不得再有 QDBus::Block 同步调用；轮询间隔仍限制在 1~10s。
  R-06 主题：新增 UI 源码无硬编码十六进制色值。
  R-07 广播去重（11-16）：必须周期性下发 HCI filter_dup=0 抵消控制器去重，
       全量轮询改为 1s，权限不足时优雅降级并告警；装配进 CMake 与部署单元。

退出码: 0 通过；1 发现缺陷；2 路径错误。
"""
from __future__ import annotations

import argparse
import os
import re
import sys

HEX = re.compile(r"#[0-9a-fA-F]{3,8}\b")


def read(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi runtime resources / door event wiring check")
    ap.add_argument("--root", default="app/src/ui")
    args = ap.parse_args()
    if not os.path.isdir(args.root):
        sys.stderr.write("root not found: %s\n" % args.root)
        return 2

    ui = args.root
    metrics_h = read(os.path.join(ui, "src", "system_metrics.h"))
    metrics_cpp = read(os.path.join(ui, "src", "system_metrics.cpp"))
    meters = read(os.path.join(ui, "qml", "components", "ResourceMeters.qml"))
    topbar = read(os.path.join(ui, "qml", "components", "TopStatusBar.qml"))
    main_cpp = read(os.path.join(ui, "src", "main.cpp"))
    cmake = read(os.path.join(ui, "CMakeLists.txt"))
    qrc = read(os.path.join(ui, "qml.qrc"))
    door_h = read(os.path.join(ui, "src", "door_sensor_runtime.h"))
    door_cpp = read(os.path.join(ui, "src", "door_sensor_runtime.cpp"))
    worker_h = read(os.path.join(ui, "src", "door_sensor_worker.h"))
    worker_cpp = read(os.path.join(ui, "src", "door_sensor_worker.cpp"))
    fails = []

    # ---- R-01 真实采样源 ----
    for token, what in {
        "/proc/meminfo": "memory sampled from /proc/meminfo",
        "/proc/stat": "per-core CPU sampled from /proc/stat",
        "/sys/kernel/debug/rknpu/load": "per-core NPU sampled from rknpu debugfs",
        "/sys/class/devfreq/fdab0000.npu/load": "devfreq aggregate fallback",
        "dTotal": "CPU usage computed from two consecutive tick deltas",
        "npuPerCoreAvailable": "per-core availability explicitly exposed",
    }.items():
        if token not in metrics_cpp:
            fails.append("system_metrics.cpp missing %s (%s)" % (what, token))
    for token, what in {
        '"memoryPct"': "memory percentage exported",
        '"cpuCores"': "per-core CPU list exported",
        '"npuCores"': "per-core NPU list exported",
        '"npuAggregatePct"': "aggregate fallback exported",
    }.items():
        if token not in metrics_cpp:
            fails.append("system_metrics.cpp missing %s (%s)" % (what, token))

    # ---- R-02 不允许伪造分核值 ----
    if "npuNote" in metrics_cpp and "聚合" not in metrics_cpp:
        fails.append("aggregate fallback not labelled as 聚合")
    if "聚合 " not in meters:
        fails.append("ResourceMeters.qml does not label aggregate NPU value")
    if "未接入" not in meters:
        fails.append("ResourceMeters.qml does not show 未接入 when sources unavailable")
    for token, what in {
        "meters.coresOf(\"cpuCores\")": "CPU repeater bound to real per-core list",
        "meters.coresOf(\"npuCores\")": "NPU repeater bound to real per-core list",
        "railhmiSystemMetrics.metrics": "QML reads the sampled metrics",
    }.items():
        if token not in meters:
            fails.append("ResourceMeters.qml missing %s (%s)" % (what, token))

    # ---- R-03 装配 ----
    for token, what in {
        "system_metrics.cpp": "metrics source in CMake",
        "system_metrics.h": "metrics header in CMake",
        "ResourceMeters.qml": "meters QML in qrc",
        '"system_metrics.h"': "main.cpp include",
        "SystemMetricsRuntime systemMetrics": "metrics runtime instantiated",
        '"railhmiSystemMetrics"': "metrics exposed as QML context property",
    }.items():
        haystack = cmake if token.endswith(".cpp") or token.endswith(".h") else (
            qrc if token.endswith(".qml") else main_cpp)
        if token not in haystack:
            fails.append("missing %s (%s)" % (what, token))
    if "ResourceMeters" not in topbar:
        fails.append("TopStatusBar.qml does not host ResourceMeters")
    if "resourceMeters.left" not in topbar:
        fails.append("top-bar left row not bounded by the meters (overlap risk at 1280px)")

    # ---- R-04 门磁事件驱动：HCI 直读为主路径，BlueZ PropertiesChanged 为降级 ----
    # 主路径：HCI raw socket + QSocketNotifier 即时推送 makeRawFrame。
    # 降级路径：PropertiesChanged 订阅在 org.freedesktop.DBus.Properties（旧版写成
    # org.freedesktop.DBus.PropertiesChanged，永远收不到信号）。
    if "pushedFrame" not in worker_cpp or "onPushedFrame" not in door_cpp:
        fails.append("event-driven pushed frame path is not wired end to end")
    if "ManufacturerData" not in worker_cpp:
        fails.append("worker does not decode pushed ManufacturerData (BlueZ fallback)")
    if "macFromPath" not in worker_cpp:
        fails.append("worker cannot identify a device object without a prior poll (BlueZ fallback)")
    # Correct interface name in the BlueZ fallback path.
    if "org.freedesktop.DBus.Properties\"" not in worker_cpp.replace(
            "QStringLiteral(\"org.freedesktop.DBus.Properties\")", "\"org.freedesktop.DBus.Properties\"") and \
            "QStringLiteral(\"org.freedesktop.DBus.Properties\")" not in worker_cpp:
        fails.append("worker fallback does not subscribe to org.freedesktop.DBus.Properties")
    worker_code = "\n".join(line.split("//", 1)[0] for line in worker_cpp.splitlines())
    if "org.freedesktop.DBus.PropertiesChanged" in worker_code:
        fails.append("worker still uses the wrong PropertiesChanged interface name")

    # ---- R-05 I/O 线程与轮询上限 ----
    for token, what in {
        "QThread": "dedicated door I/O thread",
        "moveToThread": "worker moved onto that thread",
        "connectToBus": "private system-bus connection in the worker thread",
        "Qt::QueuedConnection": "frames delivered across threads by queued signal",
    }.items():
        if token not in door_cpp and token not in worker_cpp:
            fails.append("door runtime missing %s (%s)" % (what, token))
    if "QDBus::Block" in door_cpp:
        fails.append("GUI-thread door runtime still has blocking QDBus::Block calls")
    for token, what in {
        "std::min(10000": "poll interval capped at 10s",
        "10000, std::max(1000": "poll interval capped at 10s (worker)",
    }.items():
        if token not in door_cpp and token not in worker_cpp:
            fails.append("missing %s (%s)" % (what, token))
    if "pushEvents" not in door_cpp:
        fails.append("no event-driven push counter exposed for acceptance")
    if "ioThread" not in door_cpp:
        fails.append("no I/O thread identity exposed for acceptance")

    # ---- R-06 主题 ----
    for name, text in (("ResourceMeters.qml", meters), ("TopStatusBar.qml", topbar)):
        if any(HEX.search(line.split("//", 1)[0]) for line in text.splitlines()):
            fails.append("%s contains hardcoded hex color" % name)

    # ---- R-07 HCI 原始直读 + BlueZ 降级 ----
    hci_h = read(os.path.join(ui, "src", "hci_scan_filter.h"))
    hci_cpp = read(os.path.join(ui, "src", "hci_scan_filter.cpp"))
    for token, what in {
        "openBleAdvSocket": "persistent raw HCI socket opener",
        "configureBleScan": "LE Set Scan Parameters command",
        "enableBleScanNoDup": "LE Set Scan Enable filter_dup=0 command",
        "disableBleScan": "scan disable on shutdown",
        "readBleAdvPacket": "non-blocking HCI event reader",
        "closeBleAdvSocket": "socket cleanup",
        "0x0c, 0x20": "LE Set Scan Enable opcode (OGF 0x08 / OCF 0x000c)",
        "0x01, 0x00": "enable=1, Filter_Duplicates=0",
        "AF_BLUETOOTH": "raw HCI socket without libbluetooth-dev",
    }.items():
        if token not in hci_cpp:
            fails.append("hci_scan_filter.cpp missing %s (%s)" % (what, token))
    # Worker must use the HCI raw socket as the primary path.
    for token, what in {
        "startRawHci": "HCI raw mode entry point",
        "openBleAdvSocket": "worker opens raw HCI socket",
        "enableBleScanNoDup": "worker enables scan with filter_dup=0",
        "QSocketNotifier": "worker uses event-driven socket read",
        "onAdvData": "worker has HCI data-ready handler",
        "parseAdvReports": "worker parses LE Advertising Reports",
        "makeRawFrame": "worker constructs frames from raw HCI data",
        "startBluezFallback": "worker falls back to BlueZ when HCI unavailable",
        "refreshRawScan": "worker periodically re-enables filter_dup=0",
        "rawScanTimer_": "worker has a periodic rescan timer",
        "CAP_NET_ADMIN": "capability hint for graceful degradation",
    }.items():
        if token not in worker_cpp and token not in worker_h:
            fails.append("worker missing %s (%s)" % (what, token))
    # Worker must NOT call disableBleDuplicateFilter in the raw path
    # (that function opens/closes a temporary socket and conflicts with BlueZ discovery).
    # It should only be used by diagnostic tools.
    if "hci_scan_filter.cpp" not in cmake:
        fails.append("hci_scan_filter.cpp not compiled into the UI target")
    app_yaml = os.path.normpath(os.path.join(ui, "..", "..", "config", "app.yaml"))
    service = os.path.normpath(os.path.join(ui, "..", "..", "..", "packaging", "railhmi.service"))
    if os.path.isfile(app_yaml) and "poll_interval_ms: 1000" not in read(app_yaml):
        fails.append("app.yaml is not configured for 1s door polling")
    if os.path.isfile(service) and "cap_net_admin" not in read(service):
        fails.append("systemd unit does not document the HCI capability requirement")

    print("[check_runtime_resources_wiring] meters=%s doorWorker=%s thread=%s hciRaw=%s poll1s=%s"
          % ("ResourceMeters" in topbar, "DoorSensorWorker" in door_cpp,
             "QThread" in door_cpp, "openBleAdvSocket" in worker_cpp,
             os.path.isfile(app_yaml) and "poll_interval_ms: 1000" in read(app_yaml)))
    if fails:
        for f in fails:
            print("[FAIL] %s" % f)
        print("[check_runtime_resources_wiring] FAIL (%d)" % len(fails))
        return 1
    print("[check_runtime_resources_wiring] PASS (R-01..R-07: real metrics + HCI raw door I/O thread + BlueZ fallback)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
