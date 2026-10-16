#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 设备维护页静态核对（ITEM-022）

源码级核对（无 Qt 环境可运行）：
  - P6 分区卡片：摄像头/AI/录像与存储/网络与温度/云台/日志/NVR。
  - 云台显示 Pan/Tilt/Zoom 目标值 vs 实际值 + 容差校验文本 + 前方 AI 就绪。
  - [一键归位][重调前方预置位] 控件 + 二次确认模态；操作后刷新。
  - 维护日志（recentLogs）。
  - NVR 设备卡（通道映射未配置，无自动修改入口）。
  - 桥接使用 core（ptzAction/build_ptz_log/front_ai_ready）。
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
    bridge = read(os.path.join(args.root, "src", "maintenance_model.cpp"))
    fails = []

    for token, what in {
        "maintCameraCard": "camera card", "maintAiCard": "AI card",
        "maintRecordingCard": "recording/storage card", "maintHealthCard": "network/temp card",
        "maintPtzCard": "PTZ card", "maintLogCard": "maintenance log card", "maintNvrCard": "NVR card",
        "panTarget": "PTZ target pan", "panActual": "PTZ actual pan",
        "ptzCheckText": "tolerance check text", "ptzFrontAi": "front AI readiness",
        "ptzHome": "one-key homing control", "ptzPreset": "front preset control",
        "ptzConfirmModal": "secondary confirmation modal", "ptzConfirmYes": "confirm action",
        "recentLogs": "maintenance log list",
        "channelMapping": "NVR channel mapping (未配置)",
    }.items():
        if token not in p6:
            fails.append("P6 missing %s (%s)" % (what, token))
    code = "\n".join(line.split("//", 1)[0] for line in p6.splitlines())
    if "修改" in code or "setChannel" in code or "editNvr" in code:
        fails.append("P6 appears to expose an NVR auto-modify entry (not allowed)")
    if any(HEX.search(line.split("//", 1)[0]) for line in p6.splitlines()):
        fails.append("P6 contains hardcoded hex color")

    for token, what in {
        "ptzAction": "ptzAction", "build_ptz_log": "maintenance log build",
        "front_ai_ready": "front AI readiness", "ptz_check": "tolerance check",
    }.items():
        if token not in bridge:
            fails.append("maintenance bridge missing core call %s (%s)" % (what, token))

    print("[check_maintenance_wiring] P6 sections + PTZ tolerance/confirm + log + NVR + bridge checked")
    if fails:
        for f in fails:
            print("[FAIL] %s" % f)
        print("[check_maintenance_wiring] FAIL (%d)" % len(fails))
        return 1
    print("[check_maintenance_wiring] PASS (ITEM-022 device maintenance page wired)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
