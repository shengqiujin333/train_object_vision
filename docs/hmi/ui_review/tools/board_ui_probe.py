#!/usr/bin/env python3
"""UI 评审工具 — 目标板只读探针（ITEM-025/REPAIR-n008 完整交付 UI 评审）。

用途：在登记的 Paramiko SSH 入口上，只读探测“修复版本实际装载/渲染 UI”所需的前提
（Qt 运行时、虚拟显示 Xvfb、截图工具、已构建 Release 二进制、DISPLAY/X11 socket），
为 ui_implementation_review 的 complete_delivery 观察提供事实；不修改产品源码/界面。

用法:
  python docs/hmi/ui_review/tools/board_ui_probe.py --out-dir docs/hmi/test_evidence/VERIFY-REPAIR-n008-ui --stamp <tag>
"""
import argparse
import base64
import hashlib
import json
import os
import sys
from pathlib import Path

import paramiko


def fingerprint(key) -> str:
    return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")


class VerifyPolicy(paramiko.MissingHostKeyPolicy):
    def __init__(self, want: str) -> None:
        self.want = want

    def missing_host_key(self, client, hostname, key) -> None:
        got = fingerprint(key)
        if got != self.want:
            raise paramiko.SSHException("host key fingerprint mismatch: %s" % got)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--stamp", default="probe")
    args = ap.parse_args()

    pw = os.environ.get(args.credential_env)
    if not pw:
        print("credential env %s missing" % args.credential_env, file=sys.stderr)
        return 2

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)

    c = paramiko.SSHClient()
    c.load_system_host_keys()
    c.set_missing_host_key_policy(VerifyPolicy(args.expected_fingerprint))
    c.connect(args.host, port=args.port, username=args.user, password=pw,
              timeout=10, banner_timeout=10, auth_timeout=10)

    def run(cmd: str, timeout: int = 60) -> str:
        _, o, e = c.exec_command(cmd, timeout=timeout)
        out = o.read().decode("utf-8", "replace")
        err = e.read().decode("utf-8", "replace")
        return (out + ("\n[stderr]\n" + err if err.strip() else "")).strip()

    report = {}
    report["os"] = run("cat /etc/os-release | sed -n '1,3p'")
    report["qt"] = run("dpkg -l 'qtbase5-dev*' 'qtdeclarative5-dev*' 2>/dev/null | tail -n +6 | awk '{print $2, $3}'")
    report["tools"] = run(
        "for x in Xvfb xvfb-run import convert magick xwd scrot xdotool ffmpeg; do "
        "command -v $x >/dev/null 2>&1 && echo HAVE $x || echo MISS $x; done")
    report["display"] = run("echo DISPLAY=[$DISPLAY]; ls -la /tmp/.X11-unix 2>/dev/null; "
                            "systemctl is-active graphical.target 2>/dev/null")
    report["bins"] = run(
        "ls -la /tmp/railhmi_release/app/build-target/src/ui/railhmi 2>/dev/null || echo NO_RELEASE_BIN; "
        "ls -la /tmp/railhmi_build/app/build-core/tests/railhmi-core-tests 2>/dev/null || echo NO_BUILD_DIR; "
        "ls -d /tmp/railhmi_release /tmp/railhmi_build 2>/dev/null")
    report["sudo"] = run("sudo -n true 2>&1 && echo SUDO_OK || echo SUDO_NO")

    (out_dir / ("board-ui-probe-%s.json" % args.stamp)).write_text(
        json.dumps(report, ensure_ascii=False, indent=2), encoding="utf-8")
    (out_dir / ("board-ui-probe-%s.md" % args.stamp)).write_text(
        "# 板上 UI 观察前提探针（只读）\n\n- host: %s\n- stamp: %s\n\n```\n%s\n```\n" % (
            args.host, args.stamp,
            "\n".join("%s: %s" % (k, v) for k, v in report.items())),
        encoding="utf-8")
    for k, v in report.items():
        print("==", k)
        print(v)
    c.close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
