#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-003 验证轮独立运行器包装（software_verification 自有复现入口）。

职责:
  1) 从 Windows 用户级环境变量导入 SSH 凭据（不落盘、不打值）；
  2) 板端先清场并预置 fresh-round 标记文件（含本轮随机 token）——保证
     /tmp/railhmi_identity 构建树来自本轮真实 upload，而非历史缓存树；
  3) 调用 **交付运行器** app/tools/board_frame_identity_check.py 全量独立复跑
     （build-core → core-tests → build-media → selftest → --rebuild-after 重建运行），
     --keep-remote 保留构建树供验证侧 ext 夹具使用；
  4) 记录运行结束后的 marker 读回文件供 _verify_ext.py 二次核验。

用法:
  python docs/video/test_evidence/ITEM-003/_verify_run.py --stamp 20261017-item003-verify-v1

退出码: 转发交付 runner 退出码；包装层失败为 5。
"""
import json
import os
import secrets
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
BOARD = "192.168.0.103"
USER = "admin"
REMOTE = "/tmp/railhmi_identity"
CRED_NAMES = ("TRAIN_RK3588_SSH_CREDENTIAL",)
MARKER_REL = "core-evidence/verify-marker.txt"
STATE_FILE = HERE / "verify_state.json"

import paramiko


def ps_user_var(name):
    r = subprocess.run(
        ["powershell.exe", "-NoProfile", "-Command",
         '[Environment]::GetEnvironmentVariable("%s","User")' % name],
        capture_output=True, text=True, timeout=30,
    )
    return (r.stdout or "").strip()


def rrun(c, cmd, timeout_s=30):
    _, o, e = c.exec_command(cmd, timeout=timeout_s)
    out = o.read().decode("utf-8", "replace")
    err = e.read().decode("utf-8", "replace")
    code = o.channel.recv_exit_status()
    return code, out + err


def main() -> int:
    stamp = "20261017-item003-verify-v1"
    for a in sys.argv[1:]:
        if a == "--stamp" and sys.argv.index(a) + 1 < len(sys.argv):
            stamp = sys.argv[sys.argv.index(a) + 1]

    loaded = 0
    for name in CRED_NAMES:
        v = ps_user_var(name)
        if v:
            os.environ[name] = v
            loaded += 1
    print("credentials_loaded=%d/%d" % (loaded, len(CRED_NAMES)))

    token = secrets.token_hex(8)
    c = paramiko.SSHClient()
    c.load_system_host_keys()
    c.set_missing_host_key_policy(paramiko.RejectPolicy())
    secret = os.environ[CRED_NAMES[0]]
    if not secret:
        print("TOKEN=%s none:none" % token)
        return 5
    c.connect(BOARD, port=22, username=USER, password=secret, timeout=10,
              auth_timeout=10, banner_timeout=10, look_for_keys=False, allow_agent=False)
    code, out = rrun(c, "rm -rf %s && mkdir -p %s/core-evidence && echo cleanup_done" % (REMOTE, REMOTE), 30)
    print("[cleanup-pre] exit=%d out=%s" % (code, out.strip()))
    ok = code == 0 and "cleanup_done" in out
    if not ok:
        c.close()
        print("TOKEN=%s none:none" % token)
        return 5

    sftp = c.open_sftp()
    try:
        marker = "item003-verify fresh-round token=%s stamp=%s t=%.0f\n" % (
            token, stamp, time.time())
        sftp.putfo(__import__("io").BytesIO(marker.encode("utf-8")),
                   "%s/%s" % (REMOTE, MARKER_REL))
        code, out = rrun(c, "cat %s/%s" % (REMOTE, MARKER_REL), 20)
        marker_ok = token in out
    finally:
        sftp.close()
    c.close()
    print("[marker] exit=%d token_ok=%s" % (code, marker_ok))
    if not marker_ok:
        print("TOKEN=", token)
        return 5

    print("TOKEN=%s runner_start" % token)
    repo = Path(__file__).resolve().parents[4]      # …/app → repo root? parents[4] = E:/work/rail_issue/rk3588_handover
    runner = repo / "app" / "tools" / "board_frame_identity_check.py"
    cmd = [sys.executable, str(runner),
           "--host", BOARD, "--user", USER,
           "--outdir", "docs/video/test_evidence/ITEM-003",
           "--stamp", stamp, "--keep-remote"]
    t0 = time.time()
    try:
        rc = subprocess.run(cmd, timeout=640).returncode
    except subprocess.TimeoutExpired:
        print("runner TIMEOUT after 640s")
        rc = None
    print("[runner] exit=%s elapsed=%.1fs" % (rc, time.time() - t0))

    readback = None
    try:
        c.connect(BOARD, port=22, username=USER, password=secret, timeout=10,
                  auth_timeout=10, banner_timeout=10, look_for_keys=False, allow_agent=False)
        code, readback = rrun(c, "cat %s/%s 2>&1" % (REMOTE, MARKER_REL), 20)
        c.close()
    except Exception as exc:  # noqa: BLE001
        print("[marker-readback] error %r" % (exc,))
    STATE_FILE.write_text(json.dumps({
        "stamp": stamp, "token": token,
        "runner_exit": rc,
        "marker_present_after": bool(readback and token in readback),
    }, ensure_ascii=False, indent=2), encoding="utf-8")
    print("[marker-readback] token_ok=%s" % (bool(readback and token in readback)))
    return 0 if rc == 0 else (5 if rc is None else rc)


if __name__ == "__main__":
    raise SystemExit(main())
