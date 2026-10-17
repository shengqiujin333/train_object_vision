#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-015 验证轮板端最终清理（VERIFY-20261017-v1，ext 完成后）。"""
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[3]
sys.path.insert(0, str(ROOT / "app" / "tools"))
import paramiko  # noqa: E402
from board_build import rrun  # noqa: E402

for n in ("TRAIN_RK3588_SSH_CREDENTIAL",):
    v = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                        '[Environment]::GetEnvironmentVariable("%s","User")' % n],
                       capture_output=True, text=True).stdout.strip()
    if v:
        os.environ[n] = v

client = paramiko.SSHClient()
client.load_system_host_keys()
client.set_missing_host_key_policy(paramiko.RejectPolicy())
client.connect("192.168.0.103", port=22, username="admin",
               password=os.environ["TRAIN_RK3588_SSH_CREDENTIAL"],
               timeout=10, auth_timeout=10, banner_timeout=10,
               look_for_keys=False, allow_agent=False)
res = rrun(client, "rm -rf /tmp/railhmi_timebase && ls /tmp/railhmi_timebase 2>/dev/null; "
                   "echo BOARD_CLEAN_DONE", 30)
print(res["output"])
client.close()
raise SystemExit(0 if "BOARD_CLEAN_DONE" in res["output"] else 1)
