#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""N012 整体交付验证轮独立复跑驱动（software_verification，VERIFY-20261021-n012-v1）。

对 n012 video_systems_engineer.video_system_implementation（VideoSystemImplementation,
ITEM-017 预算测量装置）做交付级整体复验：实现文件（metrics.* / measure_probe.cpp /
board_measure_check.py）自 commit 195b370 起变更 δ=0，但其间共享 core/media 由视觉线
+19.6k 行（CMakeLists 注册 rknn/preprocess/thermal/session/worker/detector 六新源），
整体轮在新树上重跑完整交付链（build.sh CoreTests + MediaCheck + railhmi-measure）证明
组合层仍绿。stamp=20261021-n012-overall-v1；--duration 8（独立窗口口径）；--keep-remote
保留板端树供 verify_n012_ext.py EXT 夹具轮。
"""
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
RUNNER = ROOT / "app" / "tools" / "board_measure_check.py"

for n in ("TRAIN_RK3588_SSH_CREDENTIAL",):
    v = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                        '[Environment]::GetEnvironmentVariable("%s","User")' % n],
                       capture_output=True, text=True, timeout=30).stdout.strip()
    if v:
        os.environ[n] = v

missing = [n for n in ("TRAIN_RK3588_SSH_CREDENTIAL",) if not os.environ.get(n)]
if missing:
    sys.stderr.write("凭据缺失，不猜测: %s\n" % missing)
    raise SystemExit(2)

cmd = [sys.executable, str(RUNNER),
       "--host", "192.168.0.103",
       "--user", "admin",
       "--outdir", str(HERE),
       "--stamp", "20261021-n012-overall-v1",
       "--duration", "8",
       "--keep-remote"]
print("run:", cmd[:1])
res = subprocess.run(cmd)
raise SystemExit(res.returncode)
