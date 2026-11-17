#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 报警中心筛选/日期/虚拟键盘接线静态核对（现场定稿 P2）

源码级核对（无 Qt 环境可运行）：
  F-01 单选互斥：第二行固定写死「全部类型 / 摄像头 / 门磁 / NVR / 主机」五个 chip，
       全部走 selectCategory(key) 并共用唯一 fCategory；选中一个必然取消其它项。
       chip 不得再从当前报警行动态生成（无报警时也必须可见）。
  F-02 默认值 + 查询：日期从/至必须给可见默认值（最近 30 天窗口 → 当天，避免默认只查当天
       把已有历史事件滤成空列表），并有可见「查询」按钮；
       日期不得依赖 TextInput.onEditingFinished（现场没有物理键盘/回车键）。
  F-03 虚拟键盘：点日期弹应用内键盘（含数字与 -），点空白处收起，「完成」等价于查询。
  F-04 主题：UI 源码无硬编码十六进制色值。

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
    ap = argparse.ArgumentParser(description="railhmi alarm-center wiring check")
    ap.add_argument("--root", default="app/src/ui")
    args = ap.parse_args()
    if not os.path.isdir(args.root):
        sys.stderr.write("root not found: %s\n" % args.root)
        return 2

    p2 = read(os.path.join(args.root, "qml", "pages", "P2AlarmCenter.qml"))
    keyboard = read(os.path.join(args.root, "qml", "components", "VirtualKeyboard.qml"))
    qrc = read(os.path.join(args.root, "qml.qrc"))
    fails = []

    # F-01 单选互斥
    for token, what in {
        "alarmCategoryRow": "single-select category row",
        "property string fCategory": "single selection state",
        "function selectCategory": "central selection function",
        "page.fCategory === ": "chips derive highlight from single state",
    }.items():
        if token not in p2:
            fails.append("P2AlarmCenter.qml missing %s (%s)" % (what, token))
    body = func_body(p2, "selectCategory")
    for token, what in {
        "page.fCategory = key": "selection writes single state",
        'page.fType = ""': "type dimension reset before selection",
        'page.fCamera = ""': "camera dimension reset before selection",
        "page.refresh()": "selection still re-queries",
    }.items():
        if token not in body:
            fails.append("selectCategory() missing %s (%s)" % (what, token))
    calls = p2.count("page.selectCategory(")
    if calls < 4:
        fails.append("only %d selectCategory() call sites; all chips must route through it" % calls)
    for stale, what in {
        "selected: page.fType ===": "type chip highlights from independent state",
        "selected: page.fCamera ===": "camera chip highlights from independent state",
        "page.fType = modelData.token": "direct type assignment bypasses single-select",
        "page.fCamera = modelData.id": "direct camera assignment bypasses single-select",
        "model.options()": "chips derived from current alarm rows",
        "page.opts.types": "dynamic type chip model",
        "page.opts.cameras": "dynamic camera chip model",
        "alarmFilter-type-": "dynamic type chip object name",
        "alarmFilter-camera-": "dynamic camera chip object name",
        "all_type": "obsolete dynamic category token",
        "all_camera": "obsolete dynamic category token",
    }.items():
        if stale in p2:
            fails.append("P2AlarmCenter.qml still has %s (%s)" % (what, stale))

    # F-05 五个固定类别 chip 必须真实写在页面上，且类别筛选要传到桥接层。
    for token, what in {
        "alarmFilter-allType": "all-types chip",
        "alarmFilter-camera": "fixed camera category chip",
        "alarmFilter-door": "fixed door category chip",
        "alarmFilter-nvr": "fixed NVR category chip",
        "alarmFilter-host": "fixed host category chip",
        'text: "摄像头"': "camera label",
        'text: "门磁"': "door label",
        'text: "NVR"': "NVR label",
        'text: "主机"': "host label",
        "page.categoryToken()": "category passed to bridge query",
    }.items():
        if token not in p2:
            fails.append("P2AlarmCenter.qml missing %s (%s)" % (what, token))
    bridge = read(os.path.join(args.root, "src", "alarm_model.cpp"))
    if "parse_alarm_category" not in bridge or "f.has_category" not in bridge:
        fails.append("alarm_model.cpp does not apply fixed category filter via core")
    core_header = os.path.join(args.root, "..", "core", "include", "railhmi", "core", "alarm_center.h")
    if os.path.isfile(core_header) and "alarm_category(" not in read(core_header):
        fails.append("core alarm_center.h missing alarm_category() mapping")

    # F-02 默认值 + 查询按钮
    for token, what in {
        "property string fFrom: Qt.formatDate(new Date(": "default from-date",
        "property string fTo: Qt.formatDate(new Date()": "default to-date",
        "fromInput.text = fFrom": "from-date seeded into field on load",
        "toInput.text = fTo": "to-date seeded into field on load",
        "alarmQueryButton": "visible query button",
        "function applyDates": "single date submit entry",
        "function today()": "today helper for reset",
        "30 * 86400000": "30-day default window on load",
        "daysAgo(30)": "30-day default window on filter reset",
        "默认最近 30 天": "default-window hint shown to operator",
    }.items():
        if token not in p2:
            fails.append("P2AlarmCenter.qml missing %s (%s)" % (what, token))
    for token, what in {
        "onEditingFinished": "Enter-key-only date submit",
        "text: page.fFrom": "date field bound to state instead of keyboard target",
        "text: page.fTo": "date field bound to state instead of keyboard target",
    }.items():
        if token in p2:
            fails.append("P2AlarmCenter.qml still contains %s (%s)" % (what, token))

    # F-03 虚拟键盘
    for token, what in {
        "VirtualKeyboard": "keyboard instance",
        "alarmKeyboard": "keyboard object name",
        "alarmDismissKeyboard": "blank-area dismiss",
        "keyboard.open(": "date field opens keyboard",
        "keyboard.close()": "keyboard closes on query/dismiss",
        "onAccepted: page.applyDates()": "keyboard Done equals query",
    }.items():
        if token not in p2:
            fails.append("P2AlarmCenter.qml missing %s (%s)" % (what, token))
    for token, what in {
        '"-"': "minus key for YYYY-MM-DD",
        "function press": "key input",
        "function close": "keyboard dismiss",
        "signal accepted": "commit signal",
        "anchors.fill: parent": "keyboard panel hit area",
        # 有报警行时 ListView(Flickable) 会吞掉页面底层点击；收起必须由
        # z=modal 的全页 scrim 负责，否则「点空白收起」只在空列表时有效。
        "virtualKeyboardScrim": "modal full-page blank-click dismiss",
        "propagateComposedEvents: true": "underlying controls still clickable",
    }.items():
        if token not in keyboard:
            fails.append("VirtualKeyboard.qml missing %s (%s)" % (what, token))
    if "qml/components/VirtualKeyboard.qml" not in qrc:
        fails.append("qml.qrc does not register VirtualKeyboard.qml")

    # F-04 主题
    if any(HEX.search(line.split("//", 1)[0]) for line in p2.splitlines()):
        fails.append("P2AlarmCenter.qml contains hardcoded hex color")

    print("[check_alarm_center_wiring] P2 fixed 5 category chips, single-select=%d call sites, "
          "default dates, query, keyboard" % calls)
    if fails:
        for f in fails:
            print("[FAIL] %s" % f)
        print("[check_alarm_center_wiring] FAIL (%d)" % len(fails))
        return 1
    print("[check_alarm_center_wiring] PASS (F-01..F-04 wired)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
