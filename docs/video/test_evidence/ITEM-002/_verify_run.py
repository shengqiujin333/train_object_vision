#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""验证轮独立运行器包装：从 Windows 用户级环境变量导入三组凭据后调用交付 runner。

用途（software_verification 独立复现）：
  python docs/video/test_evidence/ITEM-002/_verify_run.py [-- args forwarded to board_pipeline_check.py]

凭据仅进本进程环境，不写盘、不打日志值；转发自身后的全部参数。
"""
import os
import subprocess
import sys

NAMES = ("TRAIN_RK3588_SSH_CREDENTIAL", "TRAIN_FRONT_RTSP_CREDENTIAL", "TRAIN_SIDE_RTSP_CREDENTIAL")


def load_user_env() -> None:
    for name in NAMES:
        r = subprocess.run(
            ["powershell.exe", "-NoProfile", "-Command",
             '[Environment]::GetEnvironmentVariable("%s","User")' % name],
            capture_output=True, text=True, timeout=30,
        )
        v = (r.stdout or "").strip()
        if v:
            os.environ[name] = v
    print("credentials_loaded=%d/3" % sum(1 for n in NAMES if os.environ.get(n)))


def main() -> int:
    load_user_env()
    cmd = [sys.executable, "app/tools/board_pipeline_check.py"] + sys.argv[1:]
    return subprocess.run(cmd).returncode


if __name__ == "__main__":
    raise SystemExit(main())
