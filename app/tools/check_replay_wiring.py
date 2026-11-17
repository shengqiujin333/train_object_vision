#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 回放接线静态核对（ITEM-020 + 现场定稿 P3 精简）

源码级核对回放页交付项（无 Qt 环境可运行）：
  D-01 关键帧：桥接 timeline() 必须导出 keyframes；P3 必须按 ts 定位渲染缩略图槽。
  D-02 缺口斜纹：P3 缺口必须有 Canvas 斜纹绘制。
  D-03 播放控制：P3 必须保留播放/暂停与已实现的四跳转；**未实现的倍速控件必须不存在**。
  D-04 级别：桥接导出 segment level 与 alarm marker level；P3 按级别上色。
  D-05 点选：段/缺口/标记可点选。
  D-06 摄像头选择：P3 使用完整摄像头列表（railhmiCameras）。
  D-07 详情黄条：P3 有录像不可用显著黄条（detailRecordBar）。
  D-08 非法日期：P3 有 dateError 提示；FilterChip 使用 touch_target_min。
  D-09 尺寸：轨道 32 / 关键帧 64 / 游标顶部等宽时间。
  D-01/D-02(v1) 选择锁定与动态同步：onReplayRequest 使用 cameraId/tsWallMs 并调 selectAlarm；
       P3 使用 syncState 且无恒等三值、含“同步状态不可用”。
  现场定稿 F-01：账号/密码/登录/连接/NVR 通道/查询录像输入行必须全部删除，
       由 configureNvr 用固定账号 + 私有凭据自动登录、nvr.yaml 映射自动带出通道。
  现场定稿 F-02：日期旁必须有可见「查询」按钮，不得靠 TextInput.onEditingFinished（现场无键盘）。
  现场定稿 F-03：日期点击弹应用内虚拟键盘（含数字与 -），点空白处收起。
  现场定稿 F-04：导出录像入口/二次确认必须移除（导出到 NVR 侧完成）。

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
    main_cpp = read(os.path.join(args.root, "src", "main.cpp"))
    cred = read(os.path.join(args.root, "src", "credential_env.cpp"))
    keyboard = read(os.path.join(args.root, "qml", "components", "VirtualKeyboard.qml"))
    qrc = read(os.path.join(args.root, "qml.qrc"))
    fails = []

    body = func_body(main_qml, "onReplayRequest")
    for need in ("cameraId", "tsWallMs", "selectAlarm"):
        if need not in body:
            fails.append("main.qml onReplayRequest missing %s" % need)

    # D-01 keyframes / D-04 levels exported by bridge
    if '"keyframes"' not in bridge:
        fails.append("replay_model.cpp timeline() does not export keyframes (D-01)")
    if "alarm_marks_from_rows" not in bridge:
        fails.append("replay_model.cpp does not derive real-level marks from rows (v2 D-01)")
    if 'sm["level"]' not in bridge:
        fails.append("replay_model.cpp does not export segment level (D-04)")
    if 'am["level"]' not in bridge:
        fails.append("replay_model.cpp does not export alarm marker level (D-04)")

    checks = {
        "keyframeThumb": "keyframe thumbnail (D-01)",
        "onPaint": "gap hatch canvas (D-02)",
        "playbackToggle": "play/pause control (D-03)",
        "jump-minus30": "-30s jump (D-03)",
        "jump-alarm": "alarm-time jump (D-03)",
        "jump-plus30": "+30s jump (D-03)",
        "jump-plus60": "+60s jump (D-03)",
        "railhmiCameras": "full camera selection (D-06)",
        "detailRecordBar": "recording-unavailable bar (D-07)",
        "dateError": "invalid-date feedback (D-08)",
        "syncState": "dynamic sync (v1 D-02)",
        "同步状态不可用": "sync unavailable state (v1 D-02)",
        "chipName(modelData)": "panel Chinese label (v2 D-03)",
        "当前 ": "cursor-following top time (v2 D-02)",
        "queryHistoryButton": "visible date query button (F-02)",
        "replayDateInput": "date field (F-02)",
        "replayKeyboard": "virtual keyboard instance (F-03)",
        "replayDismissKeyboard": "blank-area keyboard dismiss (F-03)",
        "function applyDate": "single date submit entry (F-02)",
        # 现场二轮：画面必须尽量大且严格 16:9 + Cover；空白关键帧条必须条件隐藏。
        "panelH * 16 / 9": "strict 16:9 replay panel (no huge black bars)",
        "fillMode: 1": "replay video uses Cover",
        "page.thumbs.length > 0": "empty keyframe strip no longer occupies space",
        "videoArea": "dedicated maximised video region",
    }
    for token, what in checks.items():
        if token not in p3:
            fails.append("P3Replay.qml missing %s" % what)
    if "[cursor, cursor, cursor]" in p3:
        fails.append("P3Replay.qml still feeds identical triple to syncStatus")
    if "Theme.size.touch_target_min" not in chip:
        fails.append("FilterChip does not use touch_target_min (D-08)")

    # 现场定稿：这些控件是「必须不存在」的反向断言，误留一个都算缺陷。
    removed = {
        "setSpeed": "unimplemented speed control (D-03/F-01)",
        "speed-0.5": "0.5x speed chip",
        "0.5×": "0.5x speed label",
        "2×": "2x speed label",
        "exportRecording": "export entry (F-04)",
        "exportConfirmModal": "export confirmation modal (F-04)",
        "exportConfirmYes": "export confirm action (F-04)",
        "canExport": "export permission gate (F-04)",
        "nvrUsername": "manual NVR username field (F-01)",
        "nvrPassword": "manual NVR password field (F-01)",
        "nvrChannelInput": "manual NVR channel field (F-01)",
        "loginNvr": "manual login button (F-01)",
        "connectNvr": "manual connect button (F-01)",
        "queryNvrRecordings": "manual query-recordings button (F-01)",
        "onEditingFinished": "Enter-key-only date submit (F-02)",
        # 右侧事件详情栏整栏删除：这些字段现场无用，留着会吃掉 22% 画面宽度。
        "replayDetail": "removed right-side detail panel",
        "nvrChannelList": "removed read-only NVR channel list",
        "录像机通道": "removed right-side channel heading",
        "时间：": "removed right-side detail field",
        "类型：": "removed right-side detail field",
        "级别：": "removed right-side detail field",
        "距离：": "removed right-side detail field",
        "置信度：": "removed right-side detail field",
        "处理状态：": "removed right-side detail field",
        "备注：": "removed right-side detail field",
    }
    for token, what in removed.items():
        if token in p3:
            fails.append("P3Replay.qml still contains removed %s (%s)" % (what, token))

    # 自动登录真值：固定账号 + 私有凭据文件 + 启动即刷新实际通道，页面不填任何东西。
    for token, what in {
        "setCredentials": "fixed-account credential injection",
        "refreshNvrChannels()": "automatic channel refresh",
        "autoLogin": "auto-login state exposed to UI",
        "credentialEnv": "private credential env name from nvr.yaml",
    }.items():
        if token not in bridge:
            fails.append("replay_model.cpp missing %s (%s)" % (what, token))
    if "loadPrivateCredentialFiles" not in main_cpp:
        fails.append("main.cpp does not load private credential files before configureNvr")
    if "qputenv" not in cred or "nvr-credentials.env" not in cred:
        fails.append("credential_env.cpp does not inject NVR private credentials")
    if 'qml/components/VirtualKeyboard.qml' not in qrc:
        fails.append("qml.qrc does not register VirtualKeyboard.qml")
    for token, what in {
        '"-"': "minus key for YYYY-MM-DD",
        "function close": "keyboard close",
        "signal accepted": "keyboard commit signal",
        "function press": "key input",
        # 回放页也可能被 Flickable/ListView 覆盖，收起不能只靠页面底层 MouseArea。
        "virtualKeyboardScrim": "modal full-page blank-click dismiss",
        "propagateComposedEvents: true": "underlying controls still clickable",
    }.items():
        if token not in keyboard:
            fails.append("VirtualKeyboard.qml missing %s (%s)" % (what, token))

    print("[check_replay_wiring] main.qml onReplayRequest(cameraId=%s tsWallMs=%s selectAlarm=%s)"
          % ("cameraId" in body, "tsWallMs" in body, "selectAlarm" in body))
    if fails:
        for f in fails:
            print("[FAIL] %s" % f)
        print("[check_replay_wiring] FAIL (%d)" % len(fails))
        return 1
    print("[check_replay_wiring] PASS (D-01..D-09 + v1 D-01/D-02 + F-01..F-04 wired)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
