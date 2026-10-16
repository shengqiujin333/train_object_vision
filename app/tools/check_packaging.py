#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 打包/systemd/自检静态核对（ITEM-025）

源码级核对（无 Qt 环境可运行）：
  - systemd 单元：Restart=always、Environment=DISPLAY、ExecStart；**不修改/不引用 cpu-performance.service**。
  - install.sh 创建板端运行目录 bin/config/models/resources/data/logs 并安装单元。
  - 版本信息 VERSION、升级/回滚 UPGRADE.md、资源/图标来源许可 RESOURCES.md。
  - 故障自检目录 core fault_catalog + 维护页自检卡 + main.cpp 注入。

退出码: 0 通过；1 发现缺陷；2 路径错误。
"""
from __future__ import annotations

import argparse
import os
import sys


def read(path: str) -> str:
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        return fh.read()


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi packaging/self-check verification")
    ap.add_argument("--root", default=".")
    ap.add_argument("--app-src", default="app/src",
                    help="project app source root (contains ui/, core/)")
    ap.add_argument("--packaging", default="packaging")
    args = ap.parse_args()
    root = args.root
    if not os.path.isdir(root):
        sys.stderr.write("root not found: %s\n" % root)
        return 2
    app = args.app_src
    pk = args.packaging

    def p(*parts):
        return os.path.join(root, *parts)

    fails = []
    for f in (f"{pk}/railhmi.service", f"{pk}/install.sh", f"{pk}/VERSION",
              f"{pk}/UPGRADE.md", f"{pk}/RESOURCES.md",
              f"{app}/core/include/railhmi/core/fault_catalog.h", f"{app}/core/src/fault_catalog.cpp"):
        if not os.path.exists(p(f)):
            fails.append("missing %s" % f)

    if os.path.exists(p(f"{pk}/railhmi.service")):
        unit = read(p(f"{pk}/railhmi.service"))
        for token, what in {"Restart=always": "auto-restart", "Environment=DISPLAY=": "DISPLAY",
                            "ExecStart=": "ExecStart"}.items():
            if token not in unit:
                fails.append("systemd unit missing %s (%s)" % (what, token))
        code = "\n".join(l.split("#", 1)[0] for l in unit.splitlines())
        if "cpu-performance" in code:
            fails.append("systemd unit references/modifies cpu-performance.service (not allowed)")
        # D-01: 单元引用的每个 /opt/railhmi/bin/<name> 必须真的会被构建并安装，
        # 否则 ExecStartPre/ExecStart 非零退出 → Restart=always 无限 Crash-Loop。
        import re as _re
        ref_bins = sorted(set(_re.findall(r"/opt/railhmi/bin/([A-Za-z0-9_.\-]+)", code)))
        installer = read(p(f"{pk}/install.sh")) if os.path.exists(p(f"{pk}/install.sh")) else ""
        for b in ref_bins:
            if b not in installer:
                fails.append("systemd unit executes /opt/railhmi/bin/%s but install.sh never installs it" % b)

    if os.path.exists(p(f"{pk}/install.sh")):
        sh = read(p(f"{pk}/install.sh"))
        for d in ("bin", "config", "models", "resources", "data", "logs"):
            if d not in sh:
                fails.append("install.sh does not create runtime dir '%s'" % d)
        if "railhmi.service" not in sh or "systemctl" not in sh:
            fails.append("install.sh does not install/enable the systemd unit")

    if os.path.exists(p(f"{pk}/UPGRADE.md")):
        up = read(p(f"{pk}/UPGRADE.md"))
        if "升级" not in up or "回滚" not in up:
            fails.append("UPGRADE.md missing upgrade/rollback sections")
    if os.path.exists(p(f"{pk}/RESOURCES.md")):
        rs = read(p(f"{pk}/RESOURCES.md"))
        if "许可" not in rs or "来源" not in rs:
            fails.append("RESOURCES.md missing source/license registry")

    if os.path.exists(p(f"{app}/core/src/fault_catalog.cpp")):
        fc = read(p(f"{app}/core/src/fault_catalog.cpp"))
        for kind in ("StreamCut", "Freeze", "AiExit", "DiskLow", "PtzOutOfTolerance", "RecordingInterruption"):
            if kind not in fc:
                fails.append("fault catalog missing %s (FI mapping)" % kind)

    p6 = read(p(f"{app}/ui/qml/pages/P6Maintenance.qml"))
    if "maintSelfCheckCard" not in p6 or "faultRow-" not in p6:
        fails.append("P6 missing fault self-check card / rows")
    main_cpp = read(p(f"{app}/ui/src/main.cpp"))
    if "railhmiFaults" not in main_cpp:
        fails.append("main.cpp does not inject railhmiFaults")
    # D-02: 单元传 `--config <dir>`，二进制必须真正解析该参数（否则部署到 /opt/railhmi 后
    # 会读到编译期源码树路径并 exit 4 → Crash-Loop）。
    unit_cfg = os.path.exists(p(f"{pk}/railhmi.service")) and "--config" in read(p(f"{pk}/railhmi.service"))
    if unit_cfg and ("parse_config_dir" not in main_cpp or "load_config_dir(configDir)" not in main_cpp):
        fails.append("unit passes --config but main.cpp does not parse/consume it")

    print("[check_packaging] systemd unit + install layout + version/upgrade/resources + fault self-check checked")
    if fails:
        for f in fails:
            print("[FAIL] %s" % f)
        print("[check_packaging] FAIL (%d)" % len(fails))
        return 1
    print("[check_packaging] PASS (ITEM-025 packaging + systemd + fault self-check)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
