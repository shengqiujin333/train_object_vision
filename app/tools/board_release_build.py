#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""railhmi 发布包 - 板端目标构建/打包运行器 (n008 desktop_release)

沿项目登记的 Paramiko SSH 入口（access.rk3588_inspect/build/deploy）执行**有限步骤**：
  1) 探测目标构建依赖（Qt5 Quick/Qml 开发包等）；
  2) 若缺失且在 environment_repair(development) 授权内 → 非交互补装（仅所需依赖，
     `apt-get install -y --no-install-recommends`），记录安装前后版本；
  3) 上传 app 源码（排除 build-*/）与 packaging/；
  4) 板端本机构建 `bash build.sh Release`（aarch64，无交叉编译）；
  5) 产出真实程序产物：`build-target/railhmi`（记录 size/file/ldd/sha256）；
  6) 有限等待冒烟启动（offscreen 平台，不占用用户 X 会话）；
  7) 组装并下载发布包 tar.gz（bin + src + config + packaging + VERSION + RELEASE-INFO）；
  8) 按名清理目标临时目录。

安全边界: 不重启、不清数据、不重装系统、不改网络/驱动/服务/相机；不改 cpu-performance.service；
凭据仅从环境变量读取，不打日志；所有命令/SFTP 有显式 finite timeout。

用法:
  python board_release_build.py --host 192.168.0.103 --user admin \
      --out-dir docs/hmi/release --stamp <tag> [--no-install-deps]

退出码: 0 成功；2 连接/认证失败；3 构建失败；4 打包/下载失败。
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import json
import os
import posixpath
import socket
import sys
import time
from pathlib import Path

try:
    import paramiko
except Exception as exc:  # pragma: no cover
    sys.stderr.write("paramiko 不可用: %r\n" % (exc,))
    raise SystemExit(4)

REPO = Path(__file__).resolve().parents[2]  # .../rk3588_handover
SKIP_DIRS = {"build-target", "build-preview", "build-core", "build-media", ".git", "__pycache__"}
QT_PKGS = ["qtdeclarative5-dev", "qml-module-qtquick2", "qml-module-qtquick-controls2",
           "qml-module-qtquick-window2"]


def fingerprint(key) -> str:
    try:
        return "SHA256:" + base64.b64encode(hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")
    except Exception:  # noqa: BLE001
        return "(unavailable)"


def main() -> int:
    ap = argparse.ArgumentParser(description="railhmi board release build/pack runner")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--out-dir", required=True)
    ap.add_argument("--stamp", default=time.strftime("%Y%m%d-%H%M%S"))
    ap.add_argument("--remote-dir", default="/tmp/railhmi_release")
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--command-timeout", type=int, default=120)
    ap.add_argument("--apt-timeout", type=int, default=900)
    ap.add_argument("--build-timeout", type=int, default=1500)
    ap.add_argument("--no-install-deps", action="store_true",
                    help="只探测/构建，不补装系统依赖（用于无授权场景）")
    ap.add_argument("--no-bundle", action="store_true",
                    help="只构建并冒烟，不组装/下载 tar.gz（用于实现/修复轮的构建运行证据）")
    ap.add_argument("--diag-rename-emit", action="store_true",
                    help="诊断专用：仅在目标临时副本把 core 的 Qt 关键字同名方法 emit 改名为 emit_event，"
                         "用于确认该碰撞是否为唯一板端 Qt 编译阻碍（不修改项目源码、不产出正式发布包）")
    ap.add_argument("--diag-add-pages-import", action="store_true",
                    help="诊断专用（需同时 --diag-rename-emit）：仅在目标临时副本给 qml/main.qml 补 `import \"pages\"`，"
                         "用于确认 DeniedView 未导入是否为唯一 QML 装载阻碍")
    args = ap.parse_args()

    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("凭据环境变量 %s 未加载；不猜测凭据。\n" % args.credential_env)
        return 2

    out_dir = Path(args.out_dir)
    out_dir.mkdir(parents=True, exist_ok=True)
    log_path = out_dir / ("release-build-%s-%s.json" % (args.host.replace(".", "_"), args.stamp))
    transcript: dict = {"invocation": vars(args), "host_key_fingerprint": None,
                        "connected": False, "steps": [], "repairs": []}

    def log(msg: str) -> None:
        print(msg, flush=True)

    def flush() -> None:
        log_path.write_text(json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    try:
        client.connect(hostname=args.host, port=args.port, username=args.user, password=secret,
                       timeout=args.connect_timeout, auth_timeout=args.connect_timeout,
                       banner_timeout=args.connect_timeout, look_for_keys=False, allow_agent=False)
    except Exception as exc:  # noqa: BLE001
        transcript["steps"].append({"step": "connect", "exit": None, "output": repr(exc)})
        flush()
        sys.stderr.write("连接失败（未核验在线）: %r；详见 %s\n" % (exc, log_path))
        return 2
    transcript["connected"] = True
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
            res = {"cmd": cmd, "exit": None, "elapsed_s": round(time.time() - started, 2),
                   "output": "(error: %r)" % (exc,)}
        transcript["steps"].append(res)
        log("[%s] exit=%s %.1fs %s" % (cmd.split()[0], res["exit"], res["elapsed_s"],
                                       res["output"][:200].replace("\n", " | ")))
        flush()
        return res

    rd = args.remote_dir
    try:
        run("rm -rf %s && mkdir -p %s" % (rd, rd), args.command_timeout)

        # 1) 依赖探测
        det = run("ls /usr/lib/aarch64-linux-gnu/cmake/Qt5Quick/Qt5QuickConfig.cmake 2>/dev/null "
                  "&& echo QT_QML=yes || echo QT_QML=no", args.command_timeout)
        have_qml = "QT_QML=yes" in (det.get("output") or "")

        # 2) 缺失则在授权内非交互补装（development repair）
        if not have_qml and not args.no_install_deps:
            before = run("dpkg-query -W -f='${Package} ${Version}\\n' %s 2>&1" % " ".join(QT_PKGS),
                         args.command_timeout)
            inst = run("sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y "
                       "--no-install-recommends %s" % " ".join(QT_PKGS), args.apt_timeout)
            if inst.get("exit") != 0:
                run("sudo -n env DEBIAN_FRONTEND=noninteractive apt-get update", args.apt_timeout)
                inst = run("sudo -n env DEBIAN_FRONTEND=noninteractive apt-get install -y "
                           "--no-install-recommends %s" % " ".join(QT_PKGS), args.apt_timeout)
            after = run("dpkg-query -W -f='${Package} ${Version}\\n' %s 2>&1" % " ".join(QT_PKGS),
                        args.command_timeout)
            transcript["repairs"].append({"issue": "missing Qt5 Quick/Qml dev packages",
                                          "scope": "development", "action": "apt-get install (non-interactive)",
                                          "packages": QT_PKGS, "before": before.get("output"),
                                          "install_exit": inst.get("exit"), "after": after.get("output")})
            flush()
            det2 = run("ls /usr/lib/aarch64-linux-gnu/cmake/Qt5Quick/Qt5QuickConfig.cmake 2>/dev/null "
                       "&& echo QT_QML=yes || echo QT_QML=no", args.command_timeout)
            have_qml = "QT_QML=yes" in (det2.get("output") or "")
        if not have_qml:
            run("bash -lc 'true'", args.command_timeout)
            transcript["blocked_reason"] = "Qt5 Quick/Qml dev 不可用：无法进行目标本机构建（记录为依赖缺口，不伪造产物）"
            flush()
            log("QT_QML 仍不可用：目标构建无法进行（依赖缺口，已记录）。")
            return 3

        # 3) 上传源码（排除 build-*/ 与 .git）
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(args.command_timeout)
        uploaded = 0
        try:
            for base_rel in ("app", "packaging"):
                root = REPO / base_rel
                for dirpath, dirnames, filenames in os.walk(root):
                    dirnames[:] = [d for d in dirnames if d not in SKIP_DIRS]
                    rel_dir = os.path.relpath(dirpath, REPO)
                    if rel_dir != ".":
                        try:
                            sftp.stat(posixpath.join(rd, rel_dir.replace("\\", "/")))
                        except IOError:
                            sftp.mkdir(posixpath.join(rd, rel_dir.replace("\\", "/")))
                    for fn in filenames:
                        local = os.path.join(dirpath, fn)
                        rel = os.path.relpath(local, REPO).replace("\\", "/")
                        remote = posixpath.join(rd, rel)
                        try:
                            sftp.stat(posixpath.dirname(remote))
                        except IOError:
                            sftp.mkdir(posixpath.dirname(remote))
                        sftp.put(local, remote)
                        uploaded += 1
        finally:
            sftp.close()
        transcript["steps"].append({"step": "upload", "exit": 0, "output": "uploaded %d files" % uploaded})
        log("[upload] %d files" % uploaded)

        if args.diag_rename_emit:
            transcript["diag"] = {"rename_emit": True,
                                  "note": "仅目标 /tmp 副本，诊断用；项目源码未修改"}
            run("cd %s/app && sed -i 's/\\bemit\\b/emit_event/g' "
                "src/core/include/railhmi/core/stream_state.h src/core/src/stream_state.cpp "
                "&& echo diag-patched-emit" % rd, args.command_timeout)
        if args.diag_add_pages_import:
            transcript.setdefault("diag", {"note": "仅目标 /tmp 副本，诊断用；项目源码未修改"})
            transcript["diag"]["add_pages_import"] = True
            run("cd %s/app/src/ui/qml && sed -i 's|^import \"components\"$|import \"components\"\\nimport \"pages\"|' "
                "main.qml && head -4 main.qml" % rd, args.command_timeout)

        # 4) 板端本机构建（pipefail 保留真实退出码，不用末尾 tail 掩盖失败）
        build = run("cd %s/app && bash -o pipefail -c 'bash build.sh Release 2>&1 | tail -60'" % rd,
                    args.build_timeout)
        art = run("cd %s/app && for p in build-target/railhmi build-target/src/ui/railhmi; do "
                  "[ -x \"$p\" ] && { echo BIN:$p; ls -l \"$p\"; file \"$p\" 2>/dev/null || true; "
                  "sha256sum \"$p\"; }; done" % rd, args.command_timeout)
        bin_rel = ""
        for line in (art.get("output") or "").splitlines():
            if line.startswith("BIN:"):
                bin_rel = line[4:].strip()
                break
        if build.get("exit") != 0 or not bin_rel:
            transcript["blocked_reason"] = ("板端 Release 构建失败（build_exit=%s）或未找到 railhmi 可执行文件"
                                           % build.get("exit"))
            flush()
            log("构建失败：build_exit=%s bin=%r；见 %s" % (build.get("exit"), bin_rel, log_path))
            return 3
        log("[artifact] %s" % bin_rel)

        # 5) 有限等待冒烟启动（offscreen，不占用用户 X 会话）
        smoke = run("cd %s/app && QT_QPA_PLATFORM=offscreen timeout 12 ./%s "
                    "--config %s/app/config > %s/smoke.log 2>&1; echo smoke_exit=$?; tail -25 %s/smoke.log"
                    % (rd, bin_rel, rd, rd, rd), args.command_timeout)

        if args.diag_rename_emit or args.diag_add_pages_import:
            transcript["diag"]["build_exit"] = build.get("exit")
            transcript["diag"]["smoke"] = smoke.get("output")
            transcript["diag"]["conclusion"] = ("diagnostic build OK"
                                               if build.get("exit") == 0 else "more blockers remain")
            flush()
            log("[diag] conclusion: %s" % transcript["diag"]["conclusion"])
            return 0 if build.get("exit") == 0 else 3

        if args.no_bundle:
            # 只保留构建/运行证据：回取 smoke 日志并结束（不产出发布包）
            sftp = client.open_sftp()
            sftp.get_channel().settimeout(args.command_timeout)
            try:
                sftp.get("%s/smoke.log" % rd, str(out_dir / ("smoke-%s.log" % args.stamp)))
            except IOError:
                pass
            finally:
                sftp.close()
            transcript["bundle"] = None
            transcript["artifact"] = {"path": bin_rel, "detail": art.get("output")}
            transcript["smoke_exit_parsed"] = next(
                (int(l.split("=", 1)[1]) for l in (smoke.get("output") or "").splitlines()
                 if l.startswith("smoke_exit=")), None)
            flush()
            log("[no-bundle] conducted build + smoke only; bin=%s smoke_exit=%s"
                % (bin_rel, transcript["smoke_exit_parsed"]))
            return 0 if build.get("exit") == 0 else 3

        # 6) 组装发布包
        stage = "rm -rf %s/stage && mkdir -p %s/stage/{bin,src,config,packaging,resources,models,data,logs} && " \
                "cp %s/app/%s %s/stage/bin/railhmi && " \
                "cp %s/app/config/*.yaml %s/stage/config/ && " \
                "cp -r %s/packaging/. %s/stage/packaging/ && " \
                "(cd %s && tar czf %s/stage/src/railhmi-src.tar.gz --exclude=build-target --exclude=build-preview app) && " \
                "cp %s/packaging/VERSION %s/stage/VERSION" % (rd, rd, rd, bin_rel, rd, rd, rd, rd, rd, rd, rd, rd)
        run(stage, args.command_timeout)
        info = ("(cd %s/stage && { echo 'railhmi target release'; echo \"build_stamp=%s\"; "
                "uname -a; gcc --version | head -1; cmake --version | head -1; "
                "echo -n 'qt5_quick='; ls /usr/lib/aarch64-linux-gnu/cmake/Qt5Quick/Qt5QuickConfig.cmake >/dev/null 2>&1 && echo present || echo absent; "
                "gst-inspect-1.0 --version | head -1; pkg-config --modversion librga 2>/dev/null | sed 's/^/librga=/'; "
                "sha256sum bin/railhmi; } > RELEASE-INFO.txt) && tail -12 %s/stage/RELEASE-INFO.txt" % (rd, args.stamp, rd))
        run(info, args.command_timeout)
        bundle_name = "railhmi-0.1.0-rk3588-aarch64.tar.gz"
        pack = run("cd %s/stage && tar czf %s/%s . && sha256sum %s/%s && ls -l %s/%s"
                   % (rd, rd, bundle_name, rd, bundle_name, rd, bundle_name), args.build_timeout)
        if pack.get("exit") != 0:
            transcript["blocked_reason"] = "发布包 tar.gz 组装失败"
            flush()
            return 4

        sftp = client.open_sftp()
        sftp.get_channel().settimeout(args.command_timeout)
        try:
            local_bundle = out_dir / bundle_name
            sftp.get("%s/%s" % (rd, bundle_name), str(local_bundle))
            try:
                sftp.get("%s/smoke.log" % rd, str(out_dir / ("release-smoke-%s.log" % args.stamp)))
            except IOError:
                pass
        finally:
            sftp.close()
        local_sha = hashlib.sha256(local_bundle.read_bytes()).hexdigest()
        (out_dir / (bundle_name + ".sha256")).write_text("%s  %s\n" % (local_sha, bundle_name), encoding="utf-8")
        transcript["bundle"] = {"name": bundle_name, "local_size": local_bundle.stat().st_size,
                                "local_sha256": local_sha}
        log("[bundle] %s size=%d sha256=%s" % (bundle_name, local_bundle.stat().st_size, local_sha))
        flush()
        return 0
    finally:
        try:
            run("rm -rf %s" % rd, args.command_timeout)
        except Exception:  # noqa: BLE001
            pass
        client.close()
        flush()


if __name__ == "__main__":
    raise SystemExit(main())
