#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-006 验证侧独立复跑驱动（software_verification，VERIFY-20261017-v1）。

从 PowerShell 用户级环境导入凭据到当前进程，再以同进程环境独立复跑交付入口
app/tools/board_camera_worker_check.py（--keep-remote 保留板端构建树供验证侧 EXT 夹具使用）。
stamp=20261017-item006-verify-v1；不采信实现方 r3 归档，判据单独核对。
"""
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
RUNNER = ROOT / "app" / "tools" / "board_camera_worker_check.py"

NAMES = ["TRAIN_RK3588_SSH_CREDENTIAL", "TRAIN_FRONT_RTSP_CREDENTIAL", "TRAIN_SIDE_RTSP_CREDENTIAL"]
for n in NAMES:
    v = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                        '[Environment]::GetEnvironmentVariable("%s","User")' % n],
                       capture_output=True, text=True).stdout.strip()
    if v:
        os.environ[n] = v

missing = [n for n in ("TRAIN_RK3588_SSH_CREDENTIAL", "TRAIN_FRONT_RTSP_CREDENTIAL")
           if not os.environ.get(n)]
if missing:
    sys.stderr.write("凭据缺失，不猜测: %s\n" % missing)
    raise SystemExit(2)

cmd = [sys.executable, str(RUNNER),
       "--host", "192.168.0.103",
       "--user", "admin",
       "--outdir", str(HERE),
       "--stamp", "20261017-item006-verify-v1",
       "--keep-remote"]
print("run:", cmd[:1])
res = subprocess.run(cmd)
raise SystemExit(res.returncode)
