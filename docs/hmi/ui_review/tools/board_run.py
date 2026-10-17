#!/usr/bin/env python3
"""UI 评审工具 — 目标板受限远程执行（评审用途，只运行给定命令；不修改产品源码）。

用法:
  python docs/hmi/ui_review/tools/board_run.py --host H --user U --cmd "..."
"""
import argparse
import base64
import hashlib
import os
import sys

import paramiko


def fp(key) -> str:
    return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")


class Policy(paramiko.MissingHostKeyPolicy):
    def __init__(self, want): self.want = want
    def missing_host_key(self, client, hostname, key):
        got = fp(key)
        if got != self.want:
            raise paramiko.SSHException("fingerprint mismatch: %s" % got)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--cmd", required=True)
    ap.add_argument("--timeout", type=int, default=60)
    args = ap.parse_args()
    pw = os.environ.get(args.credential_env)
    if not pw:
        print("credential missing", file=sys.stderr); return 2
    c = paramiko.SSHClient(); c.load_system_host_keys(); c.set_missing_host_key_policy(Policy(args.expected_fingerprint))
    c.connect(args.host, port=args.port, username=args.user, password=pw, timeout=10, banner_timeout=10, auth_timeout=10)
    _, o, e = c.exec_command(args.cmd, timeout=args.timeout)
    out = o.read().decode("utf-8", "replace"); err = e.read().decode("utf-8", "replace")
    rc = o.channel.recv_exit_status()
    print("EXIT=%s" % rc)
    if out.strip(): print(out.rstrip())
    if err.strip(): print("[stderr]\n" + err.rstrip())
    c.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
