#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 设计令牌生成器（ITEM-010）

构建期由 docs/hmi/design_tokens.yaml 生成：
  - app/resources/tokens.json        机器可读令牌（C++/工具消费）
  - app/src/ui/qml/Tokens.qml        QML 单例（令牌数据）
  - app/src/ui/qml/Theme.qml         QML 单例（运行时主题外观，消费方使用 Theme.*）
  - app/src/ui/qml/qmldir            QML 模块清单（注册单例）

要求：所有颜色/字号/间距/圆角/z-index 均来自本生成物；QML/C++ UI 不硬编码。
依赖：Python 3 + PyYAML（主机构建与板端构建均可运行；板端 python3 已带 PyYAML）。

用法:
  python gen_tokens.py --yaml <design_tokens.yaml> --json <tokens.json> --qml-dir <dir>
  python gen_tokens.py --selftest          # 校验必需令牌组、确定性、变更传播
退出码: 0 成功；2 参数/输入错误；3 生成或校验失败。
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
import tempfile

try:
    import yaml
except Exception as exc:  # pragma: no cover
    sys.stderr.write("PyYAML 不可用: %r\n" % (exc,))
    raise SystemExit(2)

REQUIRED_GROUPS = ["color", "alarm", "stream_state", "mode", "integration_state",
                   "detection_badge", "typography", "spacing", "radius", "z_index"]

QML_KEYWORDS = {"property", "signal", "function", "import", "as", "on", "id", "true",
                "false", "null", "var", "int", "real", "string", "bool"}

_HEX = re.compile(r"^#(?:[0-9a-fA-F]{3}|[0-9a-fA-F]{4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$")
_RGBA = re.compile(r"^rgba?\(\s*([\d.]+)\s*,\s*([\d.]+)\s*,\s*([\d.]+)\s*(?:,\s*([\d.]+)\s*)?\)$")


def ident(key: str) -> str:
    s = re.sub(r"[^0-9A-Za-z_]", "_", str(key))
    if not s:
        s = "_"
    if s[0].isdigit():
        s = "_" + s
    if s in QML_KEYWORDS:
        s += "_"
    return s


def qml_scalar(v):
    """返回 (qml_type, qml_value_literal)。"""
    if isinstance(v, bool):
        return "bool", "true" if v else "false"
    if isinstance(v, int):
        return "int", str(v)
    if isinstance(v, float):
        return "real", repr(v)
    s = str(v)
    st = s.strip()
    if _HEX.match(st):
        return "color", '"%s"' % st
    m = _RGBA.match(st)
    if m:
        r, g, b = (float(m.group(i)) / 255.0 for i in (1, 2, 3))
        a = float(m.group(4)) if m.group(4) is not None else 1.0
        return "color", "Qt.rgba(%s, %s, %s, %s)" % (repr(r), repr(g), repr(b), repr(a))
    esc = s.replace("\\", "\\\\").replace('"', '\\"').replace("\n", "\\n")
    return "string", '"%s"' % esc


def qml_value(name: str, v, indent: int) -> list:
    pad = " " * indent
    out = []
    if isinstance(v, dict):
        out.append("%sproperty QtObject %s: QtObject {" % (pad, ident(name)))
        for k, sub in v.items():
            out.extend(qml_value(str(k), sub, indent + 4))
        out.append("%s}" % pad)
    elif isinstance(v, list):
        parts = []
        for item in v:
            if isinstance(item, dict):
                inner = ", ".join("%s: %s" % (ident(str(kk)), qml_scalar(vv)[1]) for kk, vv in item.items())
                parts.append("{ %s }" % inner)
            else:
                parts.append(qml_scalar(item)[1])
        out.append("%sproperty var %s: [%s]" % (pad, ident(name), ", ".join(parts)))
    else:
        t, lit = qml_scalar(v)
        out.append("%sproperty %s %s: %s" % (pad, t, ident(name), lit))
    return out


def generate_qml(tokens: dict, singleton_name: str) -> str:
    lines = [
        "// 由 app/tools/gen_tokens.py 从 docs/hmi/design_tokens.yaml 生成；请勿手工编辑。",
        "pragma Singleton",
        "import QtQuick 2.15",
        "",
        "QtObject {",
        "    id: %s" % singleton_name.lower(),
    ]
    for k, v in tokens.items():
        lines.extend(qml_value(str(k), v, 4))
    lines.append("}")
    return "\n".join(lines) + "\n"


def load_tokens(path: str) -> dict:
    with open(path, "r", encoding="utf-8") as fh:
        data = yaml.safe_load(fh)
    if not isinstance(data, dict):
        raise ValueError("design_tokens.yaml 顶层必须是映射")
    return data


def write_outputs(tokens: dict, json_path: str, qml_dir: str) -> None:
    if json_path:
        os.makedirs(os.path.dirname(os.path.abspath(json_path)), exist_ok=True)
        with open(json_path, "w", encoding="utf-8") as fh:
            json.dump(tokens, fh, ensure_ascii=False, indent=2)
            fh.write("\n")
    if qml_dir:
        os.makedirs(qml_dir, exist_ok=True)
        theme = generate_qml(tokens, "Theme")
        tokens_qml = generate_qml(tokens, "Tokens")
        with open(os.path.join(qml_dir, "Theme.qml"), "w", encoding="utf-8") as fh:
            fh.write(theme)
        with open(os.path.join(qml_dir, "Tokens.qml"), "w", encoding="utf-8") as fh:
            fh.write(tokens_qml)
        qmldir = "singleton Theme 1.0 Theme.qml\nsingleton Tokens 1.0 Tokens.qml\n"
        with open(os.path.join(qml_dir, "qmldir"), "w", encoding="utf-8") as fh:
            fh.write(qmldir)


def check_required(tokens: dict) -> list:
    errs = []
    for g in REQUIRED_GROUPS:
        if g not in tokens:
            errs.append("missing token group: %s" % g)
    alarm = tokens.get("alarm") or {}
    for lv in ("L1", "L2", "L3"):
        if lv not in alarm:
            errs.append("missing alarm.%s" % lv)
    return errs


def selftest(yaml_path: str) -> int:
    base = load_tokens(yaml_path)
    errs = check_required(base)
    if errs:
        for e in errs:
            print("[FAIL] %s" % e)
        return 3
    print("[PASS] required token groups present (%d)" % len(REQUIRED_GROUPS))

    with tempfile.TemporaryDirectory() as d1, tempfile.TemporaryDirectory() as d2:
        j1 = os.path.join(d1, "tokens.json")
        write_outputs(base, j1, d1)
        # 确定性
        write_outputs(base, os.path.join(d2, "tokens.json"), d2)
        same = open(j1, "rb").read() == open(os.path.join(d2, "tokens.json"), "rb").read()
        same = same and open(os.path.join(d1, "Theme.qml"), "rb").read() == open(os.path.join(d2, "Theme.qml"), "rb").read()
        print("[%s] deterministic generation" % ("PASS" if same else "FAIL"))
        if not same:
            return 3
        # JSON 可解析 + 组存在
        loaded = json.load(open(j1, encoding="utf-8"))
        print("[%s] tokens.json parseable" % ("PASS" if isinstance(loaded, dict) else "FAIL"))
        theme = open(os.path.join(d1, "Theme.qml"), encoding="utf-8").read()
        for probe in ("property color bg_base", "QtObject L1", "property QtObject stream_state",
                      "property QtObject integration_state", "property QtObject detection_badge"):
            ok = probe in theme
            print("[%s] Theme.qml contains %r" % ("PASS" if ok else "FAIL", probe))
            if not ok:
                return 3
        # 变更传播：改一个颜色后输出不同
        import copy
        changed = copy.deepcopy(base)
        changed["color"]["bg_base"] = "#010203"
        with tempfile.TemporaryDirectory() as d3:
            write_outputs(changed, os.path.join(d3, "tokens.json"), d3)
            diff = open(os.path.join(d3, "tokens.json"), "rb").read() != open(j1, "rb").read()
            diff = diff and open(os.path.join(d3, "Theme.qml"), "rb").read() != open(os.path.join(d1, "Theme.qml"), "rb").read()
            print("[%s] token change propagates to outputs" % ("PASS" if diff else "FAIL"))
            if not diff:
                return 3
    print("selftest: PASS")
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi design-token generator")
    ap.add_argument("--yaml", default="docs/hmi/design_tokens.yaml")
    ap.add_argument("--json", default="app/resources/tokens.json")
    ap.add_argument("--qml-dir", default="app/src/ui/qml")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()

    if args.selftest:
        return selftest(args.yaml)

    try:
        tokens = load_tokens(args.yaml)
    except Exception as exc:  # noqa: BLE001
        sys.stderr.write("cannot load %s: %r\n" % (args.yaml, exc))
        return 2
    errs = check_required(tokens)
    if errs:
        for e in errs:
            sys.stderr.write("ERROR: %s\n" % e)
        return 3
    write_outputs(tokens, args.json, args.qml_dir)
    print("[gen_tokens] wrote %s and %s/{Theme,Tokens}.qml,qmldir" % (args.json, args.qml_dir))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
