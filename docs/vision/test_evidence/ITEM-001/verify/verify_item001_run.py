#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-001 独立验证驱动（software_tester.software_verification）。

职责：导入用户级凭据后调用交付入口 board_vision_model_probe.py 做独立板端复跑；
板端保留构建树（--keep-remote）供验证自有扩展夹具 vext 复用；本驱动自身不做判定，
判定由 verify 的独立解析脚本完成。

用法（Git-Bash）:
  MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL='*' python verify_item001_run.py --stamp verify-20261018-item001-v1
"""

import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", ".."))
PROBE = os.path.join(REPO, "app", "tools", "board_vision_model_probe.py")


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

    outdir = os.path.join(REPO, "docs", "vision", "test_evidence", "ITEM-001")
    stamp = sys.argv[sys.argv.index("--stamp") + 1] if "--stamp" in sys.argv else "verify-manual"
    cmd = [sys.executable, PROBE,
           "--host", "192.168.0.103", "--user", "admin",
           "--credential-env", "TRAIN_RK3588_SSH_CREDENTIAL",
           "--outdir", os.path.abspath(outdir), "--stamp", stamp, "--keep-remote"]
    print("RUN:", " ".join(cmd), flush=True)
    rc = subprocess.call(cmd, timeout=1500)
    print("PROBE_EXIT=%d" % rc)
    return rc


if __name__ == "__main__":
    raise SystemExit(main())
