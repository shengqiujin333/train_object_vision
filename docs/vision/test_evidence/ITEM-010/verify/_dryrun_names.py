#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""离线自查 v3：check 名提取（深度感知：只在 check 开括号深度 1 找首个逗号）。"""
import os
import re


def extract_names(src):
    names = []
    for mm in re.finditer(r"\bcheck\(", src):
        head = src[max(0, mm.start() - 12):mm.start()]
        if head.endswith("static void "):
            continue
        i = mm.end()   # 开括号之后
        depth = 1
        name = None
        while i < len(src) and depth > 0:
            ch = src[i]
            if ch == '"':   # 字符串字面量
                i += 1
                while i < len(src) and src[i] != '"':
                    if src[i] == "\\":
                        i += 1
                    i += 1
                i += 1
                continue
            if ch in "([{":
                depth += 1
            elif ch in ")]}":
                depth -= 1
            elif ch == "," and depth == 1 and name is None:
                m2 = re.match(r'\s*"((?:[^"\\]|\\.)*)"', src[i + 1:i + 200])
                if m2:
                    name = m2.group(1)
                    break
                # 首个顶层逗号后不是名字（如多行断言名在后面）——继续不合法，break
            i += 1
        if name:
            names.append(name)
    return names


if __name__ == "__main__":
    HERE = os.path.dirname(os.path.abspath(__file__))
    src = open(os.path.join(HERE, "vext_item010.cpp"), encoding="utf-8").read()
    names = extract_names(src)
    print("extracted %d check names" % len(names))
    for n in names:
        print(" ", n)
