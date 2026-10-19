#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""VERIFY-N018-OVERALL — R4b (adapted)：F-01/F-02 触碰域 (P3Replay 数据域) 板端 ext 夹具运行器。

无参数直跑。复用 TestRun --keep-remote 保留的板端构建树 /tmp/railhmi_build：
g++ 直接连 librailhmi_core.a 编译运行 verify_p3_ext.cpp（与 ITEM-005/021 相同通道）。
有限等待、凭据仅经环境变量、退出码显式打印；清理仅触碰夹具目录。
"""
from __future__ import annotations

import base64
import hashlib
import os
import sys
import time
from pathlib import Path

import paramiko

FP = "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU"
HOST, USER = "192.168.0.103", "admin"
BUILD_DIR = "/tmp/railhmi_build"
WORK = "/tmp/railhmi_verify_p3_n018"
HERE = Path(__file__).resolve().parent
CPP = HERE / "verify_p3_ext.cpp"


def fingerprint(key) -> str:
    return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")


class Want(paramiko.MissingHostKeyPolicy):
    def __init__(self) -> None:
        self.want = FP

    def missing_host_key(self, client, hostname, key) -> None:
        got = fingerprint(key)
        if got != self.want:
            raise paramiko.SSHException("fingerprint mismatch: %s" % got)


def main() -> int:
    pw = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL")
    if not pw:
        print("credential env missing", file=sys.stderr)
        return 2
    if not CPP.exists():
        print("fixture cpp missing: %s" % CPP, file=sys.stderr)
        return 2
    c = paramiko.SSHClient()
    c.load_system_host_keys()
    c.set_missing_host_key_policy(Want())
    c.connect(HOST, port=22, username=USER, password=pw,
              timeout=10, banner_timeout=10, auth_timeout=10)
    steps = []

    def run(cmd, timeout=60):
        _, o, e = c.exec_command(cmd, timeout=timeout)
        out = o.read().decode("utf-8", "replace")
        err = e.read().decode("utf-8", "replace")
        rc = o.channel.recv_exit_status()
        steps.append((cmd, rc, out + err))
        print("[exit=%d] %s" % (rc, cmd[:90]))
        return rc, (out + err)

    try:
        run("rm -rf %s && mkdir -p %s" % (WORK, WORK), 20)
        sftp = c.open_sftp()
        try:
            sftp.get_channel().settimeout(60)
            sftp.put(str(CPP), "%s/verify_p3_ext.cpp" % WORK)
        finally:
            sftp.close()
        print("uploaded %s" % CPP.name)
        # 链接交付入口产出的 librailhmi_core.a（TestRun --keep-remote 保留的树）
        rc, out = run("cd %s && ls build-core/src/core/librailhmi_core.a 2>/dev/null || "
                      "ls build-core/src/core/*.a 2>/dev/null || find build-core -name "
                      "librailhmi_core.a -maxdepth 4 2>/dev/null | head -3" % BUILD_DIR, 30)
        lib = "build-core/src/core/librailhmi_core.a"
        cmd = ("cd %s && g++ -std=c++17 -O0 -Wall -Isrc/core/include %s/verify_p3_ext.cpp "
               "%s -o %s/verify_p3_ext 2>&1; echo GXX_EXIT=$?"
               % (BUILD_DIR, WORK, lib, WORK))
        rc, out = run(cmd, 120)
        if "GXX_EXIT=0" not in out:
            print("COMPILE FAIL")
            return 3
        rc, out = run("%s/verify_p3_ext; echo P3EXT_EXIT=$?" % WORK, 60)
        rc, out = run("rm -rf %s && echo p3_fixtures_cleaned" % WORK, 20)
    finally:
        c.close()
        log_path = HERE / "VERIFY-FIXTURES-20261020-n018-p3ext.log"
        with open(log_path, "w", encoding="utf-8") as fh:
            for cmd, rc, out in steps:
                fh.write("\n===== [exit=%d] %s =====\n%s\n" % (rc, cmd[:140], out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
