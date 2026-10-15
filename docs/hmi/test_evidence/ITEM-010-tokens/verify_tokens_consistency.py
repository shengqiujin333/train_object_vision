#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-010 验证夹具：tokens.json / Theme.qml 与 design_tokens.yaml 内容级一致性。"""
import json
import re
import sys

import yaml

sys.stdout.reconfigure(encoding="utf-8")

src = yaml.safe_load(open("docs/hmi/design_tokens.yaml", encoding="utf-8"))
tok = json.load(open("app/resources/tokens.json", encoding="utf-8"))
theme = open("app/src/ui/qml/Theme.qml", encoding="utf-8").read()

# 1) JSON 内容与源 YAML 一致
diffs = []

def walk(a, b, path=""):
    if isinstance(a, dict) and isinstance(b, dict):
        for k in set(a) | set(b):
            if k not in a:
                diffs.append(path + "/" + k + " only-in-src")
            elif k not in b:
                diffs.append(path + "/" + k + " only-in-json")
            else:
                walk(a[k], b[k], path + "/" + k)
    else:
        if a != b:
            diffs.append(path + ": src=%r json=%r" % (a, b))

walk(src, tok)
print("content-mismatch count:", len(diffs))
for d in diffs[:20]:
    print("  MISMATCH", d)

# 2) 关键验收令牌存在于 Theme.qml
checks = [
    ("bg_base #0E1116", '#0E1116' in theme),
    ("alarm L1 accent #FF6B6B", '#FF6B6B' in theme),
    ("alarm L2 accent #F0883E", '#F0883E' in theme),
    ("alarm L3 accent #E3B341", '#E3B341' in theme),
    ("typography display size 32",
     re.search(r"property QtObject display: QtObject \{[^\n]*size: 32", theme) is not None
     or re.search(r"property QtObject display: QtObject \{(?:.*?size: 32)", theme, re.S) is not None),
    ("typography body_lg size 18",
     re.search(r"body_lg: QtObject \{[^\n]*size: 18", theme) is not None
     or re.search(r"body_lg: QtObject \{(?:.*?size: 18)", theme, re.S) is not None),
    ("spacing s4 = 16", re.search(r"property int s4: 16\b", theme) is not None),
    ("radius modal = 12", re.search(r"property int modal: 12\b", theme) is not None),
    ("z_index alarm_banner = 400", re.search(r"property int alarm_banner: 400\b", theme) is not None),
    ("stream_state group", "property QtObject stream_state: QtObject {" in theme),
    ("mode group", "property QtObject mode: QtObject {" in theme),
    ("integration_state group", "property QtObject integration_state: QtObject {" in theme),
    ("detection_badge group", "property QtObject detection_badge: QtObject {" in theme),
    ("text_primary #E6EDF3", '#E6EDF3' in theme),
    ("accent #3B82F6", '#3B82F6' in theme),
]
fails = 0
for name, ok in checks:
    print(("  OK  " if ok else "  BAD "), name)
    fails += 0 if ok else 1

# 3) overlay_scrim rgba 转换数值验证
m = re.search(r"overlay_scrim: Qt\.rgba\(([^)]*)\)", theme)
print("overlay_scrim literal:", m.group(0) if m else "NOT FOUND")
ok = False
if m:
    nums = [float(x) for x in m.group(1).split(",")]
    r, g, b, a = nums
    ok = (abs(r * 255.0 - 6.0) < 0.02 and abs(g * 255.0 - 9.0) < 0.02
          and abs(b * 255.0 - 12.0) < 0.02 and abs(a - 0.72) < 0.002)
    print("  rgba decode: r=%r g=%r b=%r a=%r -> %s" % (r, g, b, a, "OK" if ok else "BAD"))
fails += 0 if ok else 1
print("json overlay_scrim:", repr(tok["color"]["overlay_scrim"]))

# 4) qmldir 单例注册
qd = open("app/src/ui/qml/qmldir", encoding="utf-8").read()
ok = ("singleton Theme 1.0 Theme.qml" in qd) and ("singleton Tokens 1.0 Tokens.qml" in qd)
print("qmldir singletons:", "OK" if ok else "BAD")
fails += 0 if ok else 1

# 5) 消费方 main.qml 引用 Theme，且无硬编码
main_qml = open("app/src/ui/qml/main.qml", encoding="utf-8").read()
uses_theme = "Theme.color.bg_base" in main_qml and "Theme.typography.scale.display.size" in main_qml
has_hard = re.search(r"#[0-9a-fA-F]{6}\b", main_qml) is not None
print("main.qml uses Theme:", "OK" if uses_theme else "BAD", "| hardcoded hex:", "BAD" if has_hard else "none")
fails += (0 if uses_theme else 1) + (1 if has_hard else 0)

print("FIXTURE SUMMARY: fails=%d" % fails)
sys.exit(1 if fails else 0)
