#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-010 验证扩展夹具：字号传播 / qmldir / rgba 往返 / 扫描器排除与根目录。

覆盖实现者自检未断言的验收边界（TC-TOKEN-01 要求颜色+字号都传播）。
全部在临时目录进行，不改产品源码；退出码 0=全部通过。
"""
import copy
import json
import os
import shutil
import subprocess
import sys
import tempfile

import yaml

sys.stdout.reconfigure(encoding="utf-8")

ROOT = "E:/work/rail_issue/rk3588_handover"
GEN = os.path.join(ROOT, "app", "tools", "gen_tokens.py")
SCAN = os.path.join(ROOT, "app", "tools", "check_no_hardcoded_ui.py")
YAML = os.path.join(ROOT, "docs", "hmi", "design_tokens.yaml")

fails = []


def run(cmd, timeout=30):
    return subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)


def check(name, ok, detail=""):
    print(("  OK  " if ok else "  BAD "), name, detail)
    if not ok:
        fails.append(name)


base = yaml.safe_load(open(YAML, encoding="utf-8"))

tmp = tempfile.mkdtemp(prefix="item010_ext_")
try:
    # ---- 1) 字号传播：typography.scale.h2.size 20 -> 21 ----
    changed = copy.deepcopy(base)
    changed["typography"]["scale"]["h2"]["size"] = 21
    d1, d2 = os.path.join(tmp, "p1"), os.path.join(tmp, "p2")
    os.makedirs(d1)
    os.makedirs(d2)
    yaml.safe_dump(base, open(os.path.join(tmp, "y1.yaml"), "w", encoding="utf-8"), allow_unicode=True)
    yaml.safe_dump(changed, open(os.path.join(tmp, "y2.yaml"), "w", encoding="utf-8"), allow_unicode=True)
    r1 = run([sys.executable, GEN, "--yaml", os.path.join(tmp, "y1.yaml"),
              "--json", os.path.join(d1, "tokens.json"), "--qml-dir", d1])
    r2 = run([sys.executable, GEN, "--yaml", os.path.join(tmp, "y2.yaml"),
              "--json", os.path.join(d2, "tokens.json"), "--qml-dir", d2])
    check("gen with h2=20 exit=0", r1.returncode == 0, r1.stderr.strip())
    check("gen with h2=21 exit=0", r2.returncode == 0, r2.stderr.strip())
    t1 = open(os.path.join(d1, "Theme.qml"), encoding="utf-8").read()
    t2 = open(os.path.join(d2, "Theme.qml"), encoding="utf-8").read()
    check("Theme.qml h2 size differs after font-size token change", t1 != t2)
    has20 = "size: 20" in t1 and "size: 20" not in t2
    has21 = "size: 21" in t2
    check("h2 size 20/21 present respectively", has20 and has21)
    j1 = json.load(open(os.path.join(d1, "tokens.json"), encoding="utf-8"))
    j2 = json.load(open(os.path.join(d2, "tokens.json"), encoding="utf-8"))
    check("tokens.json h2 size updated", j1["typography"]["scale"]["h2"]["size"] == 20
          and j2["typography"]["scale"]["h2"]["size"] == 21)

    # ---- 2) qmldir 每次生成都注册两个单例 ----
    qd = open(os.path.join(d2, "qmldir"), encoding="utf-8").read()
    check("qmldir singletons regenerated", "singleton Theme 1.0 Theme.qml" in qd
          and "singleton Tokens 1.0 Tokens.qml" in qd)

    # ---- 3) 非法输入报错 ----
    bad = os.path.join(tmp, "bad.yaml")
    open(bad, "w", encoding="utf-8").write("color:\n  bg_base: \"#0E1116\"\n")
    rb = run([sys.executable, GEN, "--yaml", bad, "--json", os.path.join(tmp, "b.json"),
              "--qml-dir", os.path.join(tmp, "bqml")])
    check("missing groups -> exit 3", rb.returncode == 3, "exit=%d" % rb.returncode)
    rb2 = run([sys.executable, GEN, "--yaml", os.path.join(tmp, "missing.yaml")])
    check("nonexistent yaml -> exit 2", rb2.returncode == 2, "exit=%d" % rb2.returncode)

    # ---- 4) rgba 往返：写一个 rgba 值，检查 Theme.qml 数值 ----
    rg = copy.deepcopy(base)
    rg["color"]["probe_rgba"] = "rgba(120, 200, 30, 0.5)"
    d3 = os.path.join(tmp, "p3")
    os.makedirs(d3)
    y3 = os.path.join(tmp, "y3.yaml")
    yaml.safe_dump(rg, open(y3, "w", encoding="utf-8"), allow_unicode=True)
    r3 = run([sys.executable, GEN, "--yaml", y3, "--json", os.path.join(d3, "tokens.json"), "--qml-dir", d3])
    check("rgba probe gen exit=0", r3.returncode == 0, r3.stderr.strip())
    t3 = open(os.path.join(d3, "Theme.qml"), encoding="utf-8").read()
    import re as _re
    m = _re.search(r"probe_rgba: Qt\.rgba\(([^)]*)\)", t3)
    ok = False
    detail = "literal not found"
    if m:
        nums = [float(x) for x in m.group(1).split(",")]
        r_, g_, b_, a_ = nums
        ok = abs(r_ * 255 - 120) < 0.05 and abs(g_ * 255 - 200) < 0.05 and abs(b_ * 255 - 30) < 0.05 and abs(a_ - 0.5) < 0.005
        detail = "r,g,b,a=%r" % (nums,)
    check("rgba(120,200,30,0.5) decode", ok, detail)

    # ---- 5) 扫描器：排除生成文件、扫描 cpp/h、--root 参数 ----
    d4 = os.path.join(tmp, "p4")
    os.makedirs(os.path.join(d4, "qml"))
    os.makedirs(os.path.join(d4, "src"))
    open(os.path.join(d4, "qml", "Theme.qml"), "w", encoding="utf-8").write(
        "Rectangle { color: \"#ABCDEF\"; font.pixelSize: 99 }\n")
    open(os.path.join(d4, "qml", "Tokens.qml"), "w", encoding="utf-8").write(
        "// tokens data\nproperty color c: \"#F0F0F0\"\n")
    open(os.path.join(d4, "qml", "view.qml"), "w", encoding="utf-8").write(
        "Rectangle { color: Theme.color.bg_base }\n// #123456 in comment only\n")
    open(os.path.join(d4, "src", "widget.h"), "w", encoding="utf-8").write(
        "// header\npaint.setColor(QColor(\"#4A90D9\"));\n")
    open(os.path.join(d4, "src", "widget.cpp"), "w", encoding="utf-8").write(
        "// note: font.setPixelSize(16) call form is outside declared scope\nQColor base(\"#AABBCC\");\n")
    rs = run([sys.executable, SCAN, "--root", d4])
    out = rs.stdout
    check("scanner excludes Theme/Tokens.qml", "Theme.qml" not in out and "Tokens.qml" not in out, "exit=%d" % rs.returncode)
    check("scanner catches view.qml hex in code (not comment)", "view.qml:1" in out or "#0E1116" not in out)
    # comment-only hex line must NOT be flagged (code after // split)
    lines = [l for l in out.splitlines() if "view.qml" in l]
    check("comment hex ignored", all("2:" not in l for l in lines), str(lines))
    check("scanner catches h hex literal #4A90D9", any("widget.h" in l for l in out.splitlines()), str([l for l in out.splitlines() if "widget" in l]))
    check("scanner catches cpp hex literal #AABBCC", any("widget.cpp" in l for l in out.splitlines()), "")
    check("scanner exit=1 with violations", rs.returncode == 1)
    rs2 = run([sys.executable, SCAN, "--root", os.path.join(tmp, "nonexistent")])
    check("scanner missing root -> exit 2", rs2.returncode == 2, "exit=%d" % rs2.returncode)
finally:
    shutil.rmtree(tmp, ignore_errors=True)

print("EXT FIXTURE SUMMARY: fails=%d %s" % (len(fails), fails if fails else "ALL OK"))
sys.exit(1 if fails else 0)
