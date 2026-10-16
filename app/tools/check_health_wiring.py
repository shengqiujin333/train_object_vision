#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 健康/结构化日志/自恢复静态核对（ITEM-024）

源码级核对（无 Qt 环境可运行）：
  - 启动恢复浮层 RecoveryOverlay（顺序步骤/进度/关键失败提示）并接入 main.qml。
  - P6 结构化日志卡（按级别查询）与恢复进度卡。
  - 桥接 health_model 使用 core（evaluate_health / query_logs / log_record_json / recovery）。
  - main.cpp 注入 railhmiHealth。
  - UI 源码无硬编码十六进制色值。

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
    ap = argparse.ArgumentParser(description="railhmi health/recovery wiring check")
    ap.add_argument("--root", default="app/src/ui")
    args = ap.parse_args()
    if not os.path.isdir(args.root):
        sys.stderr.write("root not found: %s\n" % args.root)
        return 2

    overlay = read(os.path.join(args.root, "qml", "components", "RecoveryOverlay.qml"))
    main_qml = read(os.path.join(args.root, "qml", "main.qml"))
    p6 = read(os.path.join(args.root, "qml", "pages", "P6Maintenance.qml"))
    bridge = read(os.path.join(args.root, "src", "health_model.cpp"))
    main_cpp = read(os.path.join(args.root, "src", "main.cpp"))
    fails = []

    for token, what in {
        "recoveryOverlay": "recovery overlay root", "recoveryStep-": "per-step rows",
        "recoveryText": "recovery progress text", "recoveryAlive": "critical-failure notice",
        "railhmiHealth": "bridge usage", "Connections": "changed() wiring (D-02)",
        "advanceRecovery": "progress demo (D-04)",
    }.items():
        if token not in overlay:
            fails.append("RecoveryOverlay missing %s (%s)" % (what, token))
    if "RecoveryOverlay" not in main_qml:
        fails.append("main.qml does not include RecoveryOverlay")
    for token, what in {
        "maintLogQueryCard": "structured log card", "structuredLogRow": "structured log rows",
        "maintRecoveryCard": "recovery progress card", "recoveryStepRow-": "recovery step rows",
        "Flickable": "scrollable card area (D-01)",
        "logSourceInput": "source query input (D-03)", "logKeywordInput": "keyword query input (D-03)",
        "healthSt": "reactive recovery state (D-02)", "logRows": "reactive log rows (D-02)",
        "recoveryDemo": "recovery progress demo (D-04)",
    }.items():
        if token not in p6:
            fails.append("P6 missing %s (%s)" % (what, token))
    if "onChanged" not in p6 or "page.reloadHealth" not in p6:
        fails.append("P6 does not connect railhmiHealth changed() (D-02)")
    for token, what in {
        "evaluate_health": "health eval", "query_logs": "log query", "log_record_json": "log json",
        "recovery": "recovery state", "resetRecovery": "recovery reset (D-04)", "advanceRecovery": "recovery advance (D-04)",
    }.items():
        if token not in bridge:
            fails.append("health bridge missing core call %s (%s)" % (what, token))
    if "railhmiHealth" not in main_cpp:
        fails.append("main.cpp does not inject railhmiHealth")
    for name, src in (("RecoveryOverlay.qml", overlay), ("P6Maintenance.qml", p6)):
        if any(HEX.search(line.split("//", 1)[0]) for line in src.splitlines()):
            fails.append("%s contains hardcoded hex color" % name)

    print("[check_health_wiring] recovery overlay + P6 log/recovery cards + bridge core checked")
    if fails:
        for f in fails:
            print("[FAIL] %s" % f)
        print("[check_health_wiring] FAIL (%d)" % len(fails))
        return 1
    print("[check_health_wiring] PASS (ITEM-024 health/log/recovery wired)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
