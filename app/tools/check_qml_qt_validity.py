#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi QML/Qt 合法性静态核对（发布缺陷 B-01/B-03 的通用回归守卫）

背景（见 docs/hmi/release/RELEASE-PACKAGE.md）：
  * B-01：core 头文件里的成员/方法名与 Qt 关键字同名（如 `emit`）。core 单测不包含 Qt
    头文件，故编译通过；但一旦被 Qt/QML 应用包含，Qt 宏会把 `emit` 展开为空 ->
    目标 Qt 构建失败。
  * B-03：QML 属性名以大写字母开头（含生成令牌 Theme.qml/Tokens.qml 与手写页面）在装载期
    报 "Property names cannot begin with an upper case letter"，整个界面无法加载。
本检查在**源码级**捕获这两类问题，无需 Qt 构建（可与板端 QML 装载冒烟互补）。

退出码: 0 通过；1 发现缺陷；2 路径错误。
"""
from __future__ import annotations

import argparse
import os
import re
import sys

QT_KEYWORDS = ("emit", "signals", "slots", "foreach", "Q_SIGNALS", "Q_SLOTS", "Q_EMIT", "Q_FOREACH")
PROP_RE = re.compile(r"(?:^|\s)(?:readonly\s+)?property\s+[A-Za-z_][\w.]*\s+([A-Za-z_]\w*)")
ID_RE = re.compile(r"(?:^|\s)id\s*:\s*([A-Za-z_]\w*)")
FUNC_RE = re.compile(r"\bfunction\s+([A-Za-z_]\w*)")
SIGNAL_RE = re.compile(r"\bsignal\s+([A-Za-z_]\w*)")


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", "", text, flags=re.S)
    text = re.sub(r"//[^\n]*", "", text)
    return text


def iter_files(root: str, suffix: str):
    for dirpath, _dirnames, filenames in os.walk(root):
        for fn in sorted(filenames):
            if fn.endswith(suffix):
                yield os.path.join(dirpath, fn)


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi QML/Qt validity static check")
    ap.add_argument("--root", required=True, help="UI 源码根（含 *.qml）")
    ap.add_argument("--headers", nargs="*", default=[], help="参与 Qt 编译的 C++ 头文件目录")
    args = ap.parse_args()

    if not os.path.isdir(args.root):
        sys.stderr.write("root not found: %s\n" % args.root)
        return 2

    fails = []
    qml_count = 0
    for path in iter_files(args.root, ".qml"):
        qml_count += 1
        rel = os.path.relpath(path, os.path.dirname(args.root) or ".")
        with open(path, "r", encoding="utf-8", errors="replace") as fh:
            src = strip_comments(fh.read())
        for m in PROP_RE.finditer(src):
            name = m.group(1)
            if name[0].isupper():
                fails.append("%s: property '%s' starts upper-case (QML forbids it)" % (rel, name))
        for m in ID_RE.finditer(src):
            name = m.group(1)
            if name[0].isupper():
                fails.append("%s: id '%s' starts upper-case (QML forbids it)" % (rel, name))
        # 根元素 id 上的成员调用必须在本文件内有定义（function/property/signal 或 id 本身）。
        # 捕获 F-01 类：`page.chipName(...)` 而 chipName 未定义 -> 运行期 TypeError。
        id_matches = list(ID_RE.finditer(src))
        if id_matches:
            root_id = id_matches[0].group(1)
            defined = set(FUNC_RE.findall(src)) | set(PROP_RE.findall(src)) \
                | set(SIGNAL_RE.findall(src)) | set(m.group(1) for m in id_matches)
            for m in re.finditer(r"\b" + re.escape(root_id) + r"\.([A-Za-z_]\w*)\s*\(", src):
                member = m.group(1)
                if member not in defined:
                    fails.append("%s: call %s.%s() but '%s' is not defined in this file "
                                 "(runtime TypeError)" % (rel, root_id, member, member))

    hdr_count = 0
    for hroot in args.headers:
        if not os.path.isdir(hroot):
            fails.append("headers dir not found: %s" % hroot)
            continue
        for path in list(iter_files(hroot, ".h")) + list(iter_files(hroot, ".hpp")):
            hdr_count += 1
            rel = os.path.relpath(path, os.path.dirname(hroot) or ".")
            with open(path, "r", encoding="utf-8", errors="replace") as fh:
                src = strip_comments(fh.read())
            for kw in QT_KEYWORDS:
                # 作为标识符使用（成员/方法/变量）而非注释；词边界匹配
                if re.search(r"(?<![\w:])" + re.escape(kw) + r"\s*[\(;=<,)]", src) or \
                   re.search(r"[\w>]\s+" + re.escape(kw) + r"\s*[\(;=]", src):
                    fails.append("%s: identifier '%s' collides with Qt keyword "
                                 "(fails when included by Qt TUs)" % (rel, kw))

    print("[check_qml_qt_validity] scanned %d qml files, %d headers" % (qml_count, hdr_count))
    if fails:
        for f in fails:
            print("[FAIL] %s" % f)
        print("[check_qml_qt_validity] FAIL (%d)" % len(fails))
        return 1
    print("[check_qml_qt_validity] PASS (no upper-case QML property/id, no Qt-keyword identifier in included headers)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
