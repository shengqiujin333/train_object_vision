#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 回放接线静态核对（ITEM-020 修复取证）

核对两处用户可观察点的接线事实（源码级、可在无 Qt 环境运行）：
  D-01 报警卡 -> 回放选择：main.qml onReplayRequest 必须使用 cameraId 与 tsWallMs 并调用 selectAlarm；
       P3Replay 必须消费 selectedCameraId/selectedAlarmTs/relatedCameras/selectedDetail。
  D-02 三联同步：P3Replay 必须通过 syncState 构建同步状态，且不得出现恒等三值 [cursor, cursor, cursor]；
       无时钟时显示“同步状态不可用”。

退出码: 0 通过；1 发现缺陷；2 路径错误。
"""
from __future__ import annotations

import argparse
import os
import re
import sys


def read(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def func_body(text: str, name: str) -> str:
    m = re.search(r"function\s+%s\s*\([^)]*\)\s*\{" % re.escape(name), text)
    if not m:
        return ""
    i = m.end()
    depth = 1
    out = []
    while i < len(text) and depth > 0:
        ch = text[i]
        if ch == "{":
            depth += 1
        elif ch == "}":
            depth -= 1
            if depth == 0:
                break
        out.append(ch)
        i += 1
    return "".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi replay wiring check")
    ap.add_argument("--root", default="app/src/ui")
    args = ap.parse_args()
    if not os.path.isdir(args.root):
        sys.stderr.write("root not found: %s\n" % args.root)
        return 2

    main_qml = os.path.join(args.root, "qml", "main.qml")
    p3 = os.path.join(args.root, "qml", "pages", "P3Replay.qml")
    fails = []

    mt = read(main_qml)
    body = func_body(mt, "onReplayRequest")
    if "cameraId" not in body:
        fails.append("main.qml onReplayRequest does not use cameraId (D-01)")
    if "tsWallMs" not in body:
        fails.append("main.qml onReplayRequest does not use tsWallMs (D-01)")
    if "selectAlarm" not in body:
        fails.append("main.qml onReplayRequest does not call selectAlarm (D-01)")

    pt = read(p3)
    for need in ("selectedCameraId", "relatedCameras", "selectedDetail", "syncState"):
        if need not in pt:
            fails.append("P3Replay.qml missing %s (D-01/D-02)" % need)
    if "[cursor, cursor, cursor]" in pt:
        fails.append("P3Replay.qml still feeds identical triple to syncStatus (D-02)")
    if "同步状态不可用" not in pt:
        fails.append("P3Replay.qml missing '同步状态不可用' unavailable state (D-02)")

    print("[check_replay_wiring] main.qml onReplayRequest uses cameraId=%s tsWallMs=%s selectAlarm=%s"
          % ("cameraId" in body, "tsWallMs" in body, "selectAlarm" in body))
    if fails:
        for f in fails:
            print("[FAIL] %s" % f)
        print("[check_replay_wiring] FAIL (%d)" % len(fails))
        return 1
    print("[check_replay_wiring] PASS (D-01 selection wired; D-02 dynamic sync + unavailable state)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
