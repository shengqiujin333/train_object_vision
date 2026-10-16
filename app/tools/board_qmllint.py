#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 发布包 - 板端 QML 静态检查（qmllint）运行器 (n008 desktop_release)

沿登记 Paramiko SSH 入口上传 `app/src/ui/qml` 到目标 /tmp，用 Qt 自带 `qmllint`
对每个 QML 文件做静态检查，取回完整输出（不依赖运行 X 会话）。只写目标 /tmp，按名清理。

用法:
  python board_qmllint.py --host 192.168.0.103 --user admin \
      --out-dir docs/hmi/release --stamp <tag>

退出码: 0 检查完成（不论是否发现 QML 问题）；2 连接失败；3 qmllint 不可用。
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import posixpath
import sys
import time
from pathlib import Path

try:
    import paramiko
except Exception as exc:  # pragma: no cover
    sys.stderr.write("paramiko 不可用: %r\n" % (exc,))
    raise SystemExit(4)

REPO = Path(__file__).resolve().parents[2]


def fingerprint(key) -> str:
    try:
        return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")
    except Exception:  # noqa: BLE001
        return "(unavailable)"


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi board qmllint runner")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--stamp", default=time.strftime("%Y%m%d-%H%M%S"))
    ap.add_argument("--remote-dir", default="/tmp/railhmi_qmllint")
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--command-timeout", type=int, default=120)
    args = ap.parse_args()

    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("凭据环境变量 %s 未加载。\n" % args.credential_env)
        return 2

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    log_path = out_dir / ("qmllint-%s-%s.json" % (args.host.replace(".", "_"), args.stamp))
    txt_path = out_dir / ("qmllint-%s-%s.log" % (args.host.replace(".", "_"), args.stamp))

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    transcript: dict = {"invocation": vars(args), "host_key_fingerprint": None, "steps": []}
    try:
        client.connect(hostname=args.host, port=args.port, username=args.user, password=secret,
                       timeout=args.connect_timeout, auth_timeout=args.connect_timeout,
                       banner_timeout=args.connect_timeout, look_for_keys=False, allow_agent=False)
    except Exception as exc:  # noqa: BLE001
        transcript["steps"].append({"step": "connect", "exit": None, "output": repr(exc)})
        log_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
        return 2
    tr = client.get_transport()
    if tr is not None and tr.get_remote_server_key() is not None:
        transcript["host_key_fingerprint"] = fingerprint(tr.get_remote_server_key())

    def run(cmd: str, timeout: int) -> dict:
        started = time.time()
        try:
            _in, out, err = client.exec_command(cmd, timeout=timeout)
            o = out.read().decode("utf-8", "replace")
            e = err.read().decode("utf-8", "replace")
            rc = out.channel.recv_exit_status()
            res = {"cmd": cmd, "exit": rc, "elapsed_s": round(time.time() - started, 2),
                   "output": (o + e).strip()}
        except Exception as exc:  # noqa: BLE001
            res = {"cmd": cmd, "exit": None, "output": "(error: %r)" % (exc,)}
        transcript["steps"].append(res)
        return res

    rd = args.remote_dir
    try:
        run("rm -rf %s && mkdir -p %s/qml" % (rd, rd), args.command_timeout)
        have = run("command -v qmllint && qmllint --version 2>&1 | head -1", args.command_timeout)
        if have.get("exit") != 0:
            transcript["blocked_reason"] = "qmllint 不可用"
            log_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
            return 3
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(args.command_timeout)
        try:
            qml_root = REPO / "app" / "src" / "ui" / "qml"
            for dirpath, dirnames, filenames in os.walk(qml_root):
                rel_dir = os.path.relpath(dirpath, qml_root)
                target_dir = posixpath.join(rd, "qml", rel_dir.replace("\\", "/")) if rel_dir != "." else posixpath.join(rd, "qml")
                try:
                    sftp.stat(target_dir)
                except IOError:
                    sftp.mkdir(target_dir)
                for fn in filenames:
                    sftp.put(os.path.join(dirpath, fn), posixpath.join(target_dir, fn))
        finally:
            sftp.close()
        lint = run("cd %s && for f in qml/*.qml qml/components/*.qml qml/pages/*.qml; do "
                   "echo \"===== $f\"; qmllint \"$f\" 2>&1; done" % rd, args.command_timeout)
    finally:
        try:
            run("rm -rf %s" % rd, args.command_timeout)
        except Exception:  # noqa: BLE001
            pass
        client.close()

    text = lint.get("output") or ""
    txt_path.write_text(text, encoding="utf-8")
    issues = [ln.strip() for ln in text.splitlines()
              if ln.strip() and not ln.startswith("=====") and "Warning" in ln or "Error" in ln]
    upper = [ln for ln in text.splitlines() if "upper case" in ln]
    transcript["summary"] = {"lines": len(text.splitlines()), "issues": issues,
                             "uppercase_property_findings": len(upper)}
    log_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
    print("host_key_fingerprint:", transcript["host_key_fingerprint"])
    print("qmllint lines:", len(text.splitlines()),
          "| 'upper case' findings:", len(upper), "| issue lines:", len(issues))
    print("log:", txt_path)
    print("json:", log_path)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
