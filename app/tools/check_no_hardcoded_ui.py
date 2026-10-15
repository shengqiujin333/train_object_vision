#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi UI 硬编码静态扫描（ITEM-010）

扫描产品 UI 源码，禁止出现硬编码十六进制色值/字号；颜色/字号必须来自生成的 Theme 单例。
范围: app/src/ui/qml/*.qml、app/src/ui/src/*.{cpp,h}
排除: 生成的 Theme.qml / Tokens.qml（令牌数据本身含色值）。
用法: python check_no_hardcoded_ui.py [--root app/src/ui]
退出码: 0 干净；1 发现硬编码；2 路径错误。
"""

from __future__ import annotations

import argparse
import os
import re
import sys

HEX = re.compile(r"#[0-9a-fA-F]{3,8}\b")
# 字号硬编码形状：pixelSize/pointSize/fontSize 后跟数字
SIZE = re.compile(r"(pixelSize|pointSize|fontSize)\s*[:=]\s*[0-9]")
GENERATED = {"Theme.qml", "Tokens.qml"}


def scan_file(path: str) -> list:
    hits = []
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        for n, line in enumerate(fh, 1):
            stripped = line.strip()
            if stripped.startswith("//") or stripped.startswith("#"):
                # 注释不参与判据（但仍会检查行内代码部分）
                pass
            code = line.split("//", 1)[0]
            if HEX.search(code):
                hits.append((n, "hex color", line.strip()))
            if SIZE.search(code):
                hits.append((n, "hardcoded font size", line.strip()))
    return hits


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi hardcoded-UI scan")
    ap.add_argument("--root", default="app/src/ui")
    args = ap.parse_args()
    if not os.path.isdir(args.root):
        sys.stderr.write("root not found: %s\n" % args.root)
        return 2
    violations = []
    scanned = 0
    for dp, dn, fn in os.walk(args.root):
        for f in fn:
            if f in GENERATED:
                continue
            if f.endswith(".qml") or f.endswith(".cpp") or f.endswith(".h"):
                scanned += 1
                p = os.path.join(dp, f)
                for n, kind, text in scan_file(p):
                    violations.append("%s:%d: %s: %s" % (p.replace(os.sep, "/"), n, kind, text))
    print("[check_no_hardcoded_ui] scanned %d files under %s" % (scanned, args.root))
    if violations:
        for v in violations:
            print("[FAIL] %s" % v)
        print("[check_no_hardcoded_ui] FAIL (%d violation(s))" % len(violations))
        return 1
    print("[check_no_hardcoded_ui] PASS (no hardcoded hex color / font size in UI sources)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
