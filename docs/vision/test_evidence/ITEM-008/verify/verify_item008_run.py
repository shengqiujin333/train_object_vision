#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-008 独立验证驱动（software_tester.software_verification）。

导入用户级凭据后调用交付入口 board_vision_tracking.py 做独立板端复跑；
板端保留构建树（--keep-remote）供验证自有扩展夹具 vext 复用；本驱动自身不做判定，
判定由 verify_item008_compare.py 与 vext 驱动完成。

用法（Git-Bash）:
  MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' python verify_item008_run.py
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", ".."))
PROBE = os.path.join(REPO, "app", "tools", "board_vision_tracking.py")
STAMP = "verify-20261019-item008-v1"


def main() -> int:
    cred = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL")
    if not cred:
        ps = subprocess.run(
            ["powershell.exe", "-NoProfile", "-Command",
             "[Environment]::GetEnvironmentVariable('TRAIN_RK3588_SSH_CREDENTIAL','User')"],
            capture_output=True, text=True, timeout=30)
        val = (ps.stdout or "").strip()
        if not val:
            sys.stderr.write("TRAIN_RK3588_SSH_CREDENTIAL 不可用（User 级为空）\n")
            return 2
        os.environ["TRAIN_RK3588_SSH_CREDENTIAL"] = val

    outdir = os.path.join(REPO, "docs", "vision", "test_evidence", "ITEM-008")
    cmd = [sys.executable, PROBE,
           "--host", "192.168.0.103", "--user", "admin",
           "--credential-env", "TRAIN_RK3588_SSH_CREDENTIAL",
           "--outdir", os.path.abspath(outdir), "--stamp", STAMP, "--keep-remote"]
    print("RUN:", " ".join(cmd), flush=True)
    rc = subprocess.call(cmd, timeout=2400)
    print("PROBE_EXIT=%d" % rc)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
