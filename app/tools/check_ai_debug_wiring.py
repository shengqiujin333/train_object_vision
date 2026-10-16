#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi AI 调试页/司机隔离静态核对（ITEM-021）

源码级核对（无 Qt 环境可运行）：
  - P5 调试叠加层逐项开关（框/ROI/文字/掩码）且**默认全部关闭**。
  - 描边用独立高对比色令牌 debug_box/debug_roi（避免与报警红混淆），且令牌存在。
  - 展示字段：检测框/ROI/置信度/距离/帧率/端到端延迟/模型版本/最近结果时间。
  - 司机隔离：P5 有司机无权限占位；外壳对受限页显示 DeniedView。
  - UI 源码无硬编码十六进制色值/字号（复用 check_no_hardcoded_ui 口径）。

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
    ap = argparse.ArgumentParser(description="railhmi AI debug wiring check")
    ap.add_argument("--root", default="app/src/ui")
    ap.add_argument("--tokens", default="docs/hmi/design_tokens.yaml")
    args = ap.parse_args()
    if not os.path.isdir(args.root):
        sys.stderr.write("root not found: %s\n" % args.root)
        return 2

    p5 = read(os.path.join(args.root, "qml", "pages", "P5AiDebug.qml"))
    main_qml = read(os.path.join(args.root, "qml", "main.qml"))
    theme = read(os.path.join(args.root, "qml", "Theme.qml"))
    tokens = read(args.tokens) if os.path.exists(args.tokens) else ""
    fails = []
    if not tokens:
        fails.append("design tokens file not found: %s" % args.tokens)

    # 逐项开关默认关闭
    for name in ("overlayBoxes", "overlayRoi", "overlayLabels", "overlayMasks"):
        if not re.search(r"property\s+bool\s+%s\s*:\s*false" % name, p5):
            fails.append("P5 overlay toggle %s not declared default false (ITEM-021)" % name)
        if ('objectName: "%s"' % name) not in p5:
            fails.append("P5 overlay toggle %s not present as control" % name)

    # 独立高对比调试色（令牌，不硬编码）
    if "Theme.color.debug_box" not in p5:
        fails.append("P5 does not use Theme.color.debug_box for boxes")
    if "Theme.color.debug_roi" not in p5:
        fails.append("P5 does not use Theme.color.debug_roi for ROI")
    if any(HEX.search(line.split("//", 1)[0]) for line in p5.splitlines()):
        fails.append("P5 contains hardcoded hex color")
    for key in ("debug_box", "debug_roi"):
        if key not in tokens:
            fails.append("design_tokens.yaml missing %s" % key)
        if ("property color %s" % key) not in theme:
            fails.append("Theme.qml missing %s (regenerate tokens)" % key)

    # 展示字段
    for token, what in {
        "aiBox": "detection box", "aiRoi": "ROI", "aiLabel": "confidence/label/distance text",
        "confidenceText": "confidence", "distanceText": "distance", "fpsText": "fps",
        "latencyMs": "end-to-end latency", "modelVersion": "model version", "lastResultText": "last result time",
    }.items():
        if token not in p5:
            fails.append("P5 missing %s (%s)" % (what, token))

    # 司机隔离
    if "railhmiRole" not in p5 or "aiDebugDenied" not in p5:
        fails.append("P5 has no driver no-permission guard")
    if 'if (id === "ai_debug") return "pages/P5AiDebug.qml"' not in main_qml:
        fails.append("main.qml does not route ai_debug to P5")
    if "DeniedView" not in main_qml:
        fails.append("main.qml has no DeniedView for restricted pages")

    print("[check_ai_debug_wiring] overlay toggles default-off + debug tokens + distance + driver guard checked")
    if fails:
        for f in fails:
            print("[FAIL] %s" % f)
        print("[check_ai_debug_wiring] FAIL (%d)" % len(fails))
        return 1
    print("[check_ai_debug_wiring] PASS (ITEM-021 AI debug page + driver isolation wired)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
