#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 设备维护页静态核对（ITEM-022，2026-11-17 现场定稿）

源码级核对（无 Qt 环境可运行）：
  - P6 只保留两块并铺满整屏：日志显示 + 功能操作；
  - 原来的分区卡片（摄像头/AI/录像与存储/网络与温度/云台/维护日志/NVR/BLE 门磁/
    权限矩阵/结构化日志/启动恢复进度/故障自检）**必须已移除**；
  - 五个一键操作 + 日志保存/导出/清空控件齐全；
  - 桥接使用 MaintenanceOps（ping / NVR 登录 / 系统信息 / WiFi 自检 / 导出）；
  - UI 源码无硬编码十六进制色值（复用 check_no_hardcoded_ui 口径）。

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
    ap = argparse.ArgumentParser(description="railhmi maintenance wiring check")
    ap.add_argument("--root", default="app/src/ui")
    args = ap.parse_args()
    if not os.path.isdir(args.root):
        sys.stderr.write("root not found: %s\n" % args.root)
        return 2

    p6 = read(os.path.join(args.root, "qml", "pages", "P6Maintenance.qml"))
    fails = []

    # 1) 必须存在的两块 + 五个操作 + 日志控件
    for token, what in {
        "maintLogPanel": "log display block",
        "maintOpsLog": "log list",
        "maintLogExport": "log save/export control",
        "maintLogClear": "log clear control",
        "maintOpsBar": "operation block",
        "maintOpsCamera": "camera ping operation",
        "maintOpsNvr": "NVR online/login check",
        "maintOpsBle": "BLE scan operation",
        "maintOpsSelfCheck": "fault self-check operation",
        "maintOpsSystem": "system info operation",
    }.items():
        if token not in p6:
            fails.append("P6 missing %s (%s)" % (what, token))

    # 2) 铺满整屏：两块放在一个 Column 里，日志高度 = 剩余高度
    if "Column" not in p6 or "height: parent.height - opsBar.height" not in p6:
        fails.append("P6 does not fill the screen (log block must expand, ops bar fixed)")

    # 3) 业主要求删除的分区卡片不得回潮
    for token in ("maintCameraCard", "maintAiCard", "maintRecordingCard", "maintHealthCard",
                  "maintPtzCard", "maintLogCard", "maintNvrCard", "maintDoorCard",
                  "maintPermMatrixCard", "maintLogQueryCard", "maintRecoveryCard",
                  "maintSelfCheckCard", "ptzConfirmModal", "logsByDay", "structuredLogRow",
                  "recoveryStepRow-"):
        if token in p6:
            fails.append("P6 still contains removed card/control (%s)" % token)

    # 4) NVR 只读：不得出现自动修改通道映射的入口
    code = "\n".join(line.split("//", 1)[0] for line in p6.splitlines())
    if "修改" in code or "setChannel" in code or "editNvr" in code:
        fails.append("P6 appears to expose an NVR auto-modify entry (not allowed)")
    if any(HEX.search(line.split("//", 1)[0]) for line in p6.splitlines()):
        fails.append("P6 contains hardcoded hex color")

    # 5) 桥接实现（MaintenanceOps）
    ops = os.path.join(args.root, "src", "maintenance_ops.cpp")
    if os.path.exists(ops):
        ops_src = read(ops)
        for token, what in {
            "pingCameras": "camera ping implementation",
            "checkNvr": "NVR login check implementation",
            "systemInfo": "system info implementation",
            "wifiCheckSummary": "WiFi driver self-check",
            "exportLog": "log export implementation",
        }.items():
            if token not in ops_src:
                fails.append("maintenance_ops.cpp missing %s (%s)" % (what, token))
    else:
        fails.append("maintenance_ops.cpp not found (operations not implemented)")

    print("[check_maintenance_wiring] P6 two blocks fill the screen; legacy cards removed; "
          "ops + WiFi self-check checked")
    if fails:
        for f in fails:
            print("[FAIL] %s" % f)
        print("[check_maintenance_wiring] FAIL (%d)" % len(fails))
        return 1
    print("[check_maintenance_wiring] PASS (ITEM-022 device maintenance page wired)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
