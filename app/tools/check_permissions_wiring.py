#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 权限与二次确认静态核对（ITEM-023 + 现场定稿 P3 精简）

源码级核对（无 Qt 环境可运行）：
  - 现场定稿：录像导出由 NVR 侧完成，P3 **不得**再保留导出入口、权限门或二次确认模态；
    误留任何一个都算缺陷（HMI 不提供该动作）。
  - P6 权限矩阵卡（railhmiPermissions.matrix()）保留。
  - 桥接 permissions_model 仍使用 core（role_has_permission / permission_matrix / export_recording /
    modify_config / build_action_log）；core 门禁不因 UI 移除入口而删除，其它受控动作继续复用。
  - main.cpp 注入 railhmiPermissions 并以 config.app.permissions 初始化。
  - 权限矩阵文档存在且含角色列。
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
    ap = argparse.ArgumentParser(description="railhmi permissions wiring check")
    ap.add_argument("--root", default="app/src/ui")
    ap.add_argument("--matrix", default="docs/hmi/PERMISSION_MATRIX.md")
    args = ap.parse_args()
    if not os.path.isdir(args.root):
        sys.stderr.write("root not found: %s\n" % args.root)
        return 2

    p3 = read(os.path.join(args.root, "qml", "pages", "P3Replay.qml"))
    p6 = read(os.path.join(args.root, "qml", "pages", "P6Maintenance.qml"))
    bridge = read(os.path.join(args.root, "src", "permissions_model.cpp"))
    main_cpp = read(os.path.join(args.root, "src", "main.cpp"))
    fails = []

    # 导出动作已移交 NVR 侧：P3 里出现任何导出控件/权限门/确认模态都是回归。
    for token, what in {
        "canExportRecording": "export permission check",
        "exportRecording": "export control",
        "exportConfirmModal": "export secondary confirmation modal",
        "exportConfirmYes": "export confirm action",
        "canExport": "export gate function",
        "function confirmExport": "export bridge call",
    }.items():
        if token in p3:
            fails.append("P3 still contains removed %s (%s); export belongs to NVR" % (what, token))
    if "maintPermMatrixCard" not in p6 or "railhmiPermissions.matrix()" not in p6:
        fails.append("P6 missing permission matrix card / matrix() (ITEM-023)")
    for token, what in {
        "role_has_permission": "role check", "permission_matrix": "matrix", "export_recording": "export action",
        "modify_config": "config action", "build_action_log": "action log",
    }.items():
        if token not in bridge:
            fails.append("permissions bridge missing core call %s (%s)" % (what, token))
    if "railhmiPermissions" not in main_cpp or "app.permissions" not in main_cpp:
        fails.append("main.cpp does not inject railhmiPermissions / seed app.permissions")
    if not os.path.exists(args.matrix):
        fails.append("permission matrix doc not found: %s" % args.matrix)
    else:
        doc = read(args.matrix)
        if "司机" not in doc or "管理员" not in doc or "export_recording" not in doc:
            fails.append("permission matrix doc missing role columns / export row")
    for name, src in (("P3Replay.qml", p3), ("P6Maintenance.qml", p6)):
        if any(HEX.search(line.split("//", 1)[0]) for line in src.splitlines()):
            fails.append("%s contains hardcoded hex color" % name)

    print("[check_permissions_wiring] P3 export removed / P6 matrix + bridge core + doc checked")
    if fails:
        for f in fails:
            print("[FAIL] %s" % f)
        print("[check_permissions_wiring] FAIL (%d)" % len(fails))
        return 1
    print("[check_permissions_wiring] PASS (ITEM-023 core gate kept, P3 export UI removed)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
