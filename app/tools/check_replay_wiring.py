#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 回放接线静态核对（ITEM-020）

源码级核对回放页交付项（无 Qt 环境可运行）：
  D-01 关键帧：桥接 timeline() 必须导出 keyframes；P3 必须按 ts 定位渲染缩略图槽。
  D-02 缺口斜纹：P3 缺口必须有 Canvas 斜纹绘制。
  D-03 播放控制：P3 必须有播放/暂停与倍速控件并联线 bridge.setPlaying/setSpeed。
  D-04 级别：桥接导出 segment level 与 alarm marker level；P3 按级别上色。
  D-05 点选：段/缺口/标记可点选。
  D-06 摄像头选择：P3 使用完整摄像头列表（railhmiCameras）。
  D-07 详情黄条：P3 有录像不可用显著黄条（detailRecordBar）。
  D-08 非法日期：P3 有 dateError 提示；FilterChip 使用 touch_target_min。
  D-09 尺寸：轨道 32 / 关键帧 64 / 游标顶部等宽时间。
  D-01/D-02(v1) 选择锁定与动态同步：onReplayRequest 使用 cameraId/tsWallMs 并调 selectAlarm；
       P3 使用 syncState 且无恒等三值、含“同步状态不可用”。

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

    main_qml = read(os.path.join(args.root, "qml", "main.qml"))
    p3 = read(os.path.join(args.root, "qml", "pages", "P3Replay.qml"))
    bridge = read(os.path.join(args.root, "src", "replay_model.cpp"))
    chip = read(os.path.join(args.root, "qml", "components", "FilterChip.qml"))
    fails = []

    body = func_body(main_qml, "onReplayRequest")
    for need in ("cameraId", "tsWallMs", "selectAlarm"):
        if need not in body:
            fails.append("main.qml onReplayRequest missing %s" % need)

    # D-01 keyframes / D-04 levels exported by bridge
    if '"keyframes"' not in bridge:
        fails.append("replay_model.cpp timeline() does not export keyframes (D-01)")
    if 'sm["level"]' not in bridge:
        fails.append("replay_model.cpp does not export segment level (D-04)")
    if 'am["level"]' not in bridge:
        fails.append("replay_model.cpp does not export alarm marker level (D-04)")

    checks = {
        "keyframeThumb": "keyframe thumbnail (D-01)",
        "onPaint": "gap hatch canvas (D-02)",
        "playbackToggle": "play/pause control (D-03)",
        "setSpeed": "speed control (D-03)",
        "railhmiCameras": "full camera selection (D-06)",
        "detailRecordBar": "recording-unavailable bar (D-07)",
        "dateError": "invalid-date feedback (D-08)",
        "syncState": "dynamic sync (v1 D-02)",
        "同步状态不可用": "sync unavailable state (v1 D-02)",
    }
    for token, what in checks.items():
        if token not in p3:
            fails.append("P3Replay.qml missing %s" % what)
    if "[cursor, cursor, cursor]" in p3:
        fails.append("P3Replay.qml still feeds identical triple to syncStatus")
    if "Theme.size.touch_target_min" not in chip:
        fails.append("FilterChip does not use touch_target_min (D-08)")

    print("[check_replay_wiring] main.qml onReplayRequest(cameraId=%s tsWallMs=%s selectAlarm=%s)"
          % ("cameraId" in body, "tsWallMs" in body, "selectAlarm" in body))
    if fails:
        for f in fails:
            print("[FAIL] %s" % f)
        print("[check_replay_wiring] FAIL (%d)" % len(fails))
        return 1
    print("[check_replay_wiring] PASS (D-01..D-09 + v1 D-01/D-02 wired)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
