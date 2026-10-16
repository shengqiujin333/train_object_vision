#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-025 修复反例自检：确认交付的 check_packaging.py 能检出 D-01 / D-02。

在临时副本上重新注入两种部署缺陷，期望检查器返回 1：
  NEG-01 D-01：systemd 单元加入 ExecStartPre=/opt/railhmi/bin/railhmi-version-check
  NEG-02 D-02：main.cpp 不解析 --config（回退为直接使用编译期 RAILHMI_CONFIG_DIR）
另加健康副本期望返回 0。
"""
from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", "..", "..", ".."))


def run(root: str) -> int:
    cmd = [sys.executable, os.path.join(REPO, "app", "tools", "check_packaging.py"),
           "--root", root, "--app-src", "app/src", "--packaging", "packaging"]
    return subprocess.run(cmd, capture_output=True, text=True).returncode


def stage(tmp: str) -> None:
    shutil.copytree(os.path.join(REPO, "packaging"), os.path.join(tmp, "packaging"))
    ap = os.path.join(tmp, "app", "src", "ui", "src")
    os.makedirs(ap)
    for rel in (os.path.join("app", "src", "ui", "src", "main.cpp"),
                os.path.join("app", "src", "core", "src", "fault_catalog.cpp")):
        dst = os.path.join(tmp, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy(os.path.join(REPO, rel), dst)
    for rel in (os.path.join("app", "src", "ui", "qml", "pages", "P6Maintenance.qml"),
                os.path.join("app", "src", "core", "include", "railhmi", "core", "fault_catalog.h")):
        dst = os.path.join(tmp, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy(os.path.join(REPO, rel), dst)


def mutate(path: str, old: str, new: str) -> None:
    with open(path, "r", encoding="utf-8") as fh:
        txt = fh.read()
    assert old in txt, "anchor not found: %s" % old
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(txt.replace(old, new, 1))


def main() -> int:
    rc = 0
    with tempfile.TemporaryDirectory() as tmp:
        healthy = os.path.join(tmp, "healthy")
        os.makedirs(healthy)
        stage(healthy)
        e = run(healthy)
        print("NEG_HEALTHY_EXIT=%d (expect 0)" % e)
        if e != 0:
            rc = 1
    with tempfile.TemporaryDirectory() as tmp:
        d1 = os.path.join(tmp, "d01")
        os.makedirs(d1)
        stage(d1)
        unit = os.path.join(d1, "packaging", "railhmi.service")
        mutate(unit, "ExecStart=/opt/railhmi/bin/railhmi",
               "ExecStartPre=/opt/railhmi/bin/railhmi-version-check\nExecStart=/opt/railhmi/bin/railhmi")
        e = run(d1)
        print("NEG_D01_EXIT=%d (expect 1)" % e)
        if e != 1:
            rc = 1
    with tempfile.TemporaryDirectory() as tmp:
        d2 = os.path.join(tmp, "d02")
        os.makedirs(d2)
        stage(d2)
        # 单元仍传 --config，但 main.cpp 不再消费它 → 检查器应报 D-02
        mutate(os.path.join(d2, "app", "src", "ui", "src", "main.cpp"),
               "load_config_dir(configDir)", "load_config_dir(RAILHMI_CONFIG_DIR)")
        mutate(os.path.join(d2, "app", "src", "ui", "src", "main.cpp"),
               "railhmi::core::parse_config_dir(argc, argv, RAILHMI_CONFIG_DIR)",
               "std::string(RAILHMI_CONFIG_DIR)")
        e = run(d2)
        print("NEG_D02_EXIT=%d (expect 1)" % e)
        if e != 1:
            rc = 1
    print("NEGCHECK_RESULT=%s" % ("PASS" if rc == 0 else "FAIL"))
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
