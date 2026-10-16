#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""反例自检：证明 app/tools/check_qml_qt_validity.py 能检出 B-01 / B-03 两类缺陷。

在临时副本上重新注入：
  NEG-01（B-01）：core 头文件出现与 Qt 关键字同名的成员方法 `void emit(...)`
  NEG-02（B-03）：QML 出现大写开头属性名 `property var L:`
健康副本期望 0，注入副本期望 1。
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))


def run(root: str, headers: list, cwd: str) -> int:
    cmd = [sys.executable, os.path.join(REPO, "app", "tools", "check_qml_qt_validity.py"),
           "--root", root] + (["--headers"] + headers if headers else [])
    return subprocess.run(cmd, capture_output=True, text=True, cwd=cwd).returncode


def stage(tmp: str) -> None:
    shutil.copytree(os.path.join(REPO, "app", "src", "ui", "qml"), os.path.join(tmp, "src", "ui", "qml"))
    os.makedirs(os.path.join(tmp, "src", "core", "include"))
    os.makedirs(os.path.join(tmp, "src", "platform", "include"))
    # 健康头文件：无 Qt 关键字同名标识符
    with open(os.path.join(tmp, "src", "core", "include", "ok.h"), "w", encoding="utf-8") as fh:
        fh.write("class Ok { public: void emit_event(int x); };\n")


def main() -> int:
    rc = 0
    with tempfile.TemporaryDirectory() as tmp:
        stage(tmp)
        e = run("src/ui", ["src/core/include", "src/platform/include"], tmp)
        print("NEG_HEALTHY_EXIT=%d (expect 0)" % e)
        if e != 0:
            rc = 1
    with tempfile.TemporaryDirectory() as tmp:
        stage(tmp)
        with open(os.path.join(tmp, "src", "core", "include", "bad.h"), "w", encoding="utf-8") as fh:
            fh.write("class Bad { private: void emit(int now_ms); };\n")
        e = run("src/ui", ["src/core/include", "src/platform/include"], tmp)
        print("NEG_B01_EXIT=%d (expect 1)" % e)
        if e != 1:
            rc = 1
    with tempfile.TemporaryDirectory() as tmp:
        stage(tmp)
        p = os.path.join(tmp, "src", "ui", "qml", "pages", "P1Realtime.qml")
        with open(p, "a", encoding="utf-8") as fh:
            fh.write("\n// injected\nreadonly property var L: ({})\n")
        e = run("src/ui", ["src/core/include", "src/platform/include"], tmp)
        print("NEG_B03_EXIT=%d (expect 1)" % e)
        if e != 1:
            rc = 1
    print("NEGCHECK_RESULT=%s" % ("PASS" if rc == 0 else "FAIL"))
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
