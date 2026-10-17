#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-016 验证侧独立复跑驱动（software_verification，VERIFY-20261017-v1）。

从 PowerShell 用户级环境导入 SSH 凭据到当前进程，同进程环境独立复跑交付入口
app/tools/board_diagnostics_check.py（--keep-remote 保留板端构建树 /tmp/railhmi_diag
供验证侧 EXT 夹具使用）。
stamp=20261017-item016-verify-v1；不采信实现方 r3 归档，判据由 _verify16_compare.py
与 _verify16_ext.py 独立核对（判据不采信交付 type=acceptance 行为最终依据）。
"""
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
RUNNER = ROOT / "app" / "tools" / "board_diagnostics_check.py"

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
       "--stamp", "20261017-item016-verify-v1",
       "--duration", "2",
       "--keep-remote"]
print("run:", cmd[:1])
res = subprocess.run(cmd)
raise SystemExit(res.returncode)
