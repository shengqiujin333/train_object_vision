#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-025 fix-round verification driver (verifier-owned), stamp VERIFY-20261017-v4.

Subject: implementer fix round stamp 20261017-ITEM025-fix-r1 (commit ee0a741),
which claims D-01/D-02 resolved: ExecStartPre railhmi-version-check removed
(UPGRADE.md updated), main.cpp --config parsed via core parse_config_dir,
delivered check_packaging.py gained D-01/D-02 assertions.

Steps (all finite timeouts):
  1. Fingerprint-checked SSH connect (Paramiko RejectPolicy).
  2. Deliverable board_build.py --mode TestRun with verifier stamp (keep remote).
  3. Line-by-line [PASS] comparison: fresh core-tests (670) vs implementer
     fix-r1 archived log; independent JUnit parse 670/0 + 31 ITEM025 cases
     + 7 new parse_config_dir cases.
  4. Fault-probe oracle (16 lines) + byte identity vs implementer fix-r1
     probe log (fix-r2 did not touch fault catalog) + on-board rerun diff
     (determinism).
  5. Sim-inject oracle + JSON + byte identity vs fix-r1 (injection semantics
     untouched by fix-r2).
  6. Verifier ext fixture compiled on-board against THIS round's static
     libs, now including the fix-r2 D-02 arg-contract section (G).
  7. Verifier packaging/deployment scan on the fresh board copy:
     positives True, D-01/D-01b/D-02 expected True (hints fixed), D02 parse
     path actually present (not just compile-time prefix fallback).
  8. Delivered-checker negative controls on-board: healthy copy PASS;
     fresh D-01 re-injection (ExecStartPre railhmi-version-check) FAIL with
     the provenance message; fresh D-02 re-injection (main.cpp stops
     consuming --config) FAIL with the parse/consume message.
  9. Regression probes listing + cleanup by name.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
import io
import json
import os
import re
import socket
import subprocess
import sys
import time
from pathlib import Path

try:
    import paramiko
except Exception as exc:  # noqa: BLE001
    sys.stderr.write("paramiko unavailable: %r\n" % (exc,))
    raise SystemExit(4)

ITEM025_CASES = [
    "FI-01 id", "FI-10 id", "fault to_string", "parse '01'", "parse 'fi-10'",
    "parse 'freeze'", "parse invalid fault rejected",
    "injection params logged", "injection camera logged",
    "format_observation non-empty",
    "FI-01 -> reconnecting", "FI-01 -> offline escalation", "FI-01 recovers to live",
    "FI-01 final state live",
    "FI-02 -> frozen", "FI-02 recovers to live",
    "FI-03 -> ai unavailable", "FI-03 no detections -> no alarms",
    "FI-03 summary alarms=0",
    "FI-04 -> storage low", "FI-04 record_available=false",
    "FI-04 recovers (low=false after)",
    "FI-05 -> ptz tolerance fail", "FI-05 recovers tolerance",
    "FI-10 -> recording gap logged", "FI-10 summary has gaps",
    "no fault -> front alarm produced", "no fault summary alarms=1",
    "simulation is deterministic/resettable",
    "pantograph injection -> L2 alarm", "none profile -> no alarms",
]

FIXR2_ARG_CASES = [
    "--config <dir> is parsed",
    "--config=<dir> form is parsed",
    "absent --config falls back to compile-time default",
    "--config without value falls back (never empty path)",
    "--config with empty value falls back",
    "deployment unit argument is honoured",
    "no argv falls back safely",
]

FAULT_ORACLE = [
    r'^type=fault-catalog count=6$',
    r'^fault id=FI-01 label=断流 prompt="画面占位：重连中（显示已重连次数与最近帧时间），旧帧不作为实时" '
    r'recovery="等待自动重连（退避）或维护页手动重连；达阈值升级为设备报警"$',
    r'^fault id=FI-02 label=画面冻结 prompt="画面冻结：叠加占位层，最近帧时间停止推进" '
    r'recovery="确认摄像头/网络后重连；恢复出帧自动转在线"$',
    r'^fault id=FI-03 label=AI 退出 prompt="状态区 AI 项转“AI 不可用”，相关检测类报警标注“AI 不可用”（不静默）" '
    r'recovery="重启推理线程/服务；恢复后 AI 项转“运行”，前方 AI READY"$',
    r'^fault id=FI-04 label=磁盘不足 prompt="存储项提前告警；录像写入标注容量不足" '
    r'recovery="按保留策略淘汰普通片段（保护片段优先）；扩容或清理后恢复"$',
    r'^fault id=FI-05 label=云台超差 prompt="云台卡显示目标值 vs 实际值 \+ 容差校验失败（超差），前方 AI 进入 NOT_READY" '
    r'recovery="一键归位 / 重调前方预置位（二次确认）后复校；成功转 READY"$',
    r'^fault id=FI-10 label=录像中断 prompt="时间轴出现录像缺口标记，事件标注“录像不完整/不可用”（不静默）" '
    r'recovery="恢复录像进程/存储；缺口记录保留可检索"$',
    r'^type=self-check count=7$',
    r'^step id=SC-01 inject="railhmi-sim-inject --fi 01（断流）" '
    r'prompt="该路画面占位 \+ 重连中 \+ 重连次数/最近帧时间" recovery="自动重连或手动重连；恢复出帧转在线"$',
    r'^step id=SC-02 inject="railhmi-sim-inject --fi 02（冻结）" '
    r'prompt="画面冻结占位，最近帧时间停更" recovery="重连恢复；出帧自动转在线"$',
    r'^step id=SC-03 inject="railhmi-sim-inject --fi 03（AI 退出）" '
    r'prompt="状态区 AI 项“AI 不可用”，关联报警标注" recovery="重启推理；恢复后 AI 运行/READY"$',
    r'^step id=SC-04 inject="railhmi-sim-inject --fi 04（磁盘不足）" '
    r'prompt="存储项告警 \+ 录像容量不足标注" recovery="保留淘汰/清理后恢复"$',
    r'^step id=SC-05 inject="railhmi-sim-inject --fi 05（云台超差）" '
    r'prompt="云台卡超差 \+ 前方 AI NOT_READY" recovery="归位/预置位二次确认后复校"$',
    r'^step id=SC-10 inject="railhmi-sim-inject --fi 10（录像中断）" '
    r'prompt="时间轴缺口标记 \+ 事件“录像不可用”" recovery="恢复录像进程/存储"$',
    r'^step id=SC-BOOT inject="systemctl restart railhmi（或断电重启）" '
    r'prompt="显示启动恢复进度（8 步），完成后进入可用状态" recovery="systemd Restart=always 自动拉起；失败查日志"$',
    r'^type=acceptance fi_covered=true boot_step=true prompts_nonempty=true result=PASS$',
]

SIM_ORACLE = [
    (r'^t=0 type=injection camera=CAM-01 profile=front '
     r'faults=FI-01,FI-02,FI-03,FI-04,FI-05,FI-10 onset_ms=1500 recovery_ms=5000 '
     r'duration_ms=6000 step_ms=500$', "injection params line"),
    (r'^t=1500 type=stream_state state=reconnecting reconnect_count=1 presentable=false cam=CAM-01$',
     "FI-01 reconnect at onset"),
    (r'^t=1500 type=ai status=unavailable cam=CAM-01$', "FI-03 ai unavailable at onset"),
    (r'^t=1500 type=storage free_bytes=2147483647 .* low=true record_available=false$',
     "FI-04 storage low at onset"),
    (r'^t=1500 type=ptz tolerance_ok=false target_zoom=10\.000000 actual_zoom=10\.000000$',
     "FI-05 ptz out of tolerance at onset"),
    (r'^t=1500 type=gap cam=CAM-01 t0_ms=1500 t1_ms=2000 gap_count=1$', "FI-10 gap at onset"),
    (r'^t=2500 type=stream_state state=offline reconnect_count=3 presentable=false cam=CAM-01$',
     "FI-01 offline escalation at threshold=3"),
    (r'^t=5000 type=stream_state state=live reconnect_count=0 presentable=true cam=CAM-01$',
     "recovery at 5000 -> live, count reset"),
    (r'^t=6000 type=summary final_state=live reconnect_count=0 alarms=2 gaps=1 events_recorded=2$',
     "summary line"),
]

# Board-side negative control (fix-round): healthy PASS; D-01/D-02 re-injection FAIL.
NEG_FIX_PY = r'''
import os, shutil, subprocess, sys
checker = "/tmp/railhmi_build/tools/check_packaging.py"
base_src = "/tmp/railhmi_build/src"
base_pkg = "/tmp/railhmi_build/packaging"
work = "/tmp/railhmi_verify025f/neg"
if os.path.exists(work):
    shutil.rmtree(work)
os.makedirs(work)

def run(args):
    return subprocess.run([sys.executable, checker] + args,
                          capture_output=True, text=True, timeout=120)

def staging(dst, mutate_pkg=None, mutate_src=False):
    src = base_src
    if mutate_src:
        shutil.copytree(base_src, os.path.join(dst, "src"))
        mp = os.path.join(dst, "src", "ui", "src", "main.cpp")
        with open(mp, encoding="utf-8") as fh:
            s = fh.read()
        assert "railhmi::core::parse_config_dir(argc, argv, RAILHMI_CONFIG_DIR)" in s
        assert "load_config_dir(configDir)" in s
        s = s.replace("railhmi::core::parse_config_dir(argc, argv, RAILHMI_CONFIG_DIR)",
                      "std::string(RAILHMI_CONFIG_DIR)")
        s = s.replace("load_config_dir(configDir)", "load_config_dir(RAILHMI_CONFIG_DIR)")
        with open(mp, "w", encoding="utf-8") as fh:
            fh.write(s)
        src = os.path.join(dst, "src")
    return src

fails = []

h = os.path.join(work, "healthy"); os.makedirs(h)
for kind in ("bin", "config", "models", "resources", "data", "logs"):
    os.makedirs(os.path.join(h, kind))
r0 = run(["--root", h, "--app-src", base_src, "--packaging", base_pkg])
print("NEG_HEALTHY_EXIT=%d" % r0.returncode)
if r0.returncode != 0:
    fails.append("healthy copy must pass")

d1 = os.path.join(work, "d01")
src = staging(d1)
shutil.copytree(base_pkg, os.path.join(d1, "packaging"))
u = os.path.join(d1, "packaging", "railhmi.service")
with open(u, encoding="utf-8") as fh:
    s = fh.read()
s = s.replace("ExecStart=/opt/railhmi/bin/railhmi",
              "ExecStartPre=/opt/railhmi/bin/railhmi-version-check\n"
              "ExecStart=/opt/railhmi/bin/railhmi")
with open(u, "w", encoding="utf-8") as fh:
    fh.write(s)
r1 = run(["--root", d1, "--app-src", src, "--packaging", os.path.join(d1, "packaging")])
print("NEG_D01_EXIT=%d" % r1.returncode)
hit1 = any("railhmi-version-check" in l for l in (r1.stdout + r1.stderr).splitlines()
           if l.startswith("[FAIL]"))
for l in (r1.stdout + r1.stderr).splitlines():
    if l.startswith("[FAIL]"):
        print("neg-d01:", l)
if r1.returncode != 1 or not hit1:
    fails.append("D-01 re-injection must fail with the provenance message")

d2 = os.path.join(work, "d02")
src2 = staging(d2, mutate_src=True)
r2 = run(["--root", d2, "--app-src", src2, "--packaging", base_pkg])
print("NEG_D02_EXIT=%d" % r2.returncode)
hit2 = any("does not parse/consume" in l for l in (r2.stdout + r2.stderr).splitlines()
           if l.startswith("[FAIL]"))
for l in (r2.stdout + r2.stderr).splitlines():
    if l.startswith("[FAIL]"):
        print("neg-d02:", l)
if r2.returncode != 1 or not hit2:
    fails.append("D-02 re-injection must fail with the parse/consume message")

print("NEG_DRIVER_FAILS=%s" % fails, file=sys.stderr)
sys.exit(0 if not fails else 1)
'''


def fingerprint(key) -> str:
    try:
        return "SHA256:" + base64.b64encode(
            hashlib.sha256(key.asbytes()).digest()).decode().rstrip("=")
    except Exception:  # noqa: BLE001
        return "(unavailable)"


def rrun(client, cmd: str, timeout: int) -> dict:
    started = time.time()
    try:
        _in, out, err = client.exec_command(cmd, timeout=timeout)
        so = out.read().decode("utf-8", "replace")
        se = err.read().decode("utf-8", "replace")
        rc = out.channel.recv_exit_status()
        return {"cmd": cmd, "exit": rc, "elapsed_s": round(time.time() - started, 2),
                "output": so + se}
    except socket.timeout:
        return {"cmd": cmd, "exit": None, "elapsed_s": round(time.time() - started, 2),
                "output": "(timeout after %ss)" % timeout}
    except Exception as exc:  # noqa: BLE001
        return {"cmd": cmd, "exit": None, "elapsed_s": round(time.time() - started, 2),
                "output": "(error: %r)" % (exc,)}


def main() -> int:
    ap = argparse.ArgumentParser(description="ITEM-025 fix-round verification driver")
    ap.add_argument("--host", required=True)
    ap.add_argument("--port", type=int, default=22)
    ap.add_argument("--user", required=True)
    ap.add_argument("--credential-env", default="TRAIN_RK3588_SSH_CREDENTIAL")
    ap.add_argument("--expected-fingerprint", required=True)
    ap.add_argument("--workdir", default="/tmp/railhmi_verify025f")
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--stamp", required=True)
    ap.add_argument("--connect-timeout", type=int, default=10)
    ap.add_argument("--timeout-sec", type=int, default=1800)
    ap.add_argument("--app-root", default=str(Path(__file__).resolve().parents[4] / "app"))
    args = ap.parse_args()

    secret = os.environ.get(args.credential_env)
    if not secret:
        sys.stderr.write("credential env %s not loaded; refusing to guess\n"
                         % args.credential_env)
        return 2

    outdir = Path(args.outdir)
    outdir.mkdir(parents=True, exist_ok=True)
    tag = "VERIFY-FIXTURES-" + args.stamp
    wd = args.workdir
    remote_dir = "/tmp/railhmi_build"
    transcript: dict = {"invocation": {k: str(v) for k, v in vars(args).items()},
                        "host_key_fingerprint": None,
                        "notes": ["ITEM-025 fix-round (D-01/D-02) independent "
                                  "verification; fingerprint-pinned; no credentials "
                                  "logged"],
                        "steps": []}
    ok = True

    def note(step, res):
        transcript["steps"].append({"step": step, **res})

    def save():
        (outdir / (tag + ".transcript.json")).write_text(
            json.dumps(transcript, ensure_ascii=False, indent=2), encoding="utf-8")
        with (outdir / (tag + ".log")).open("w", encoding="utf-8") as fh:
            fh.write("host_key_fingerprint: %s\n" % transcript["host_key_fingerprint"])
            for s in transcript["steps"]:
                fh.write("\n===== [%s] exit=%s =====\n" % (s.get("step"), s.get("exit")))
                fh.write((s.get("output") or "") + "\n")

    client = paramiko.SSHClient()
    client.load_system_host_keys()
    client.set_missing_host_key_policy(paramiko.RejectPolicy())
    try:
        client.connect(hostname=args.host, port=args.port, username=args.user,
                       password=secret, timeout=args.connect_timeout,
                       auth_timeout=args.connect_timeout,
                       banner_timeout=args.connect_timeout,
                       look_for_keys=False, allow_agent=False)
    except Exception as exc:  # noqa: BLE001
        note("connect", {"exit": None, "output": "connect error: %r" % (exc,)})
        save()
        sys.stderr.write("connect failed (not asserting offline): %r\n" % (exc,))
        return 2

    try:
        tr = client.get_transport()
        fp = fingerprint(tr.get_remote_server_key()) if tr is not None else "(transport None)"
        transcript["host_key_fingerprint"] = fp
        note("fingerprint", {"exit": 0 if fp == args.expected_fingerprint else 1,
                             "output": "observed=%s expected=%s"
                                       % (fp, args.expected_fingerprint)})
        if fp != args.expected_fingerprint:
            save()
            sys.stderr.write("host fingerprint mismatch; aborting\n")
            return 2

        stamp = args.stamp

        # 1. deliverable TestRun entry with verifier stamp (keep remote)
        cmd = [sys.executable, str(Path(args.app_root) / "tools" / "board_build.py"),
               "--host", args.host, "--user", args.user,
               "--credential-env", args.credential_env,
               "--mode", "TestRun",
               "--outdir", str(outdir),
               "--stamp", stamp,
               "--keep-remote"]
        started = time.time()
        try:
            proc = subprocess.run(cmd, capture_output=True, text=True,
                                  encoding="utf-8", errors="replace",
                                  timeout=args.timeout_sec)
            entry_exit = proc.returncode
            entry_out = (proc.stdout or "") + (proc.stderr or "")
        except subprocess.TimeoutExpired:
            entry_exit = None
            entry_out = "(timeout after %ss)" % args.timeout_sec
        note("run-testrun-entry", {"cmd": " ".join(cmd), "exit": entry_exit,
                                   "elapsed_s": round(time.time() - started, 2),
                                   "output": entry_out})
        if entry_exit != 0:
            ok = False

        host_u = args.host.replace(".", "_")

        # 1b. fresh artifact locations + implementer fix-r1 archive for compare
        fresh = {
            "core_tests_log": outdir / ("build-TestRun-%s-%s.core-evidence_core-tests-%s.log"
                                        % (host_u, stamp, stamp)),
            "core_tests_xml": outdir / ("build-TestRun-%s-%s.core-evidence_core-tests-%s.xml"
                                        % (host_u, stamp, stamp)),
            "probe_log": outdir / ("build-TestRun-%s-%s.core-evidence_fault-probe-%s.log"
                                   % (host_u, stamp, stamp)),
            "sim_log": outdir / ("build-TestRun-%s-%s.core-evidence_sim-inject-%s.log"
                                 % (host_u, stamp, stamp)),
            "sim_json": outdir / ("build-TestRun-%s-%s.core-evidence_sim-inject-%s.json"
                                  % (host_u, stamp, stamp)),
        }
        impl = {
            "core_tests_log": outdir / ("build-TestRun-192_168_0_103-20261017-ITEM025-fix-r1"
                                        ".core-evidence_core-tests-20261017-ITEM025-fix-r1.log"),
            "core_tests_xml": outdir / ("build-TestRun-192_168_0_103-20261017-ITEM025-fix-r1"
                                        ".core-evidence_core-tests-20261017-ITEM025-fix-r1.xml"),
            "probe_log": outdir / ("build-TestRun-192_168_0_103-20261017-ITEM025-fix-r1"
                                   ".core-evidence_fault-probe-20261017-ITEM025-fix-r1.log"),
            "sim_log": outdir / ("build-TestRun-192_168_0_103-20261017-ITEM025-fix-r1"
                                 ".core-evidence_sim-inject-20261017-ITEM025-fix-r1.log"),
        }
        missing = [k for k, p in fresh.items() if not p.is_file()]
        missing += [k for k, p in impl.items() if not p.is_file()]
        if missing:
            note("fresh-artifacts", {"exit": None,
                 "output": "missing artifacts: %s" % ", ".join(missing)})
            ok = False

        def rd(p):
            return Path(p).read_text(encoding="utf-8", errors="replace")

        # 2. line-by-line [PASS] comparison vs implementer fix-r1 + JUnit parse
        expected_pass = 670
        if fresh["core_tests_log"].is_file() and impl["core_tests_log"].is_file():
            def pass_list(p):
                return [l.strip() for l in rd(p).splitlines() if l.startswith("[PASS]")]
            same = pass_list(impl["core_tests_log"]) == pass_list(fresh["core_tests_log"])
            ver_fail = [l for l in rd(fresh["core_tests_log"]).splitlines()
                        if l.startswith("[FAIL]")]
            lcm = {"exit": 0 if same and not ver_fail
                   and len(pass_list(fresh["core_tests_log"])) == expected_pass else 1,
                   "output": "identical=%s verify_pass=%d verify_fails=%d expect=%d"
                             % (same, len(pass_list(fresh["core_tests_log"])),
                                len(ver_fail), expected_pass)}
            note("log-compare", lcm)
            if lcm["exit"] != 0:
                ok = False
        if fresh["core_tests_xml"].is_file():
            xtext = rd(fresh["core_tests_xml"])
            m = re.search(r'tests="(\d+)"[^>]*failures="(\d+)"', xtext, re.S)
            n_tests, n_fail = (m.group(1), m.group(2)) if m else (None, None)
            names_all = [n2.replace("&gt;", ">").replace("&lt;", "<")
                         .replace("&amp;", "&").replace("&apos;", "'")
                         .replace("&quot;", '"')
                         for n2 in re.findall(r'name="([^"]+)"', xtext)]
            want = ITEM025_CASES + FIXR2_ARG_CASES
            missing_cases = [c for c in want if c not in names_all]
            junit_ok = (n_tests == str(expected_pass) and n_fail == "0"
                        and not missing_cases)
            note("junit-parse", {"exit": 0 if junit_ok else 1,
                 "output": "junit tests=%s failures=%s cases=%d/%d missing=%s"
                           % (n_tests, n_fail, len(want) - len(missing_cases),
                              len(want), missing_cases)})
            if not junit_ok:
                ok = False

        # 3. fault-probe oracle + cross-round byte identity vs fix-r1
        if fresh["probe_log"].is_file() and impl["probe_log"].is_file():
            f_probe = rd(fresh["probe_log"])
            fails_oracle = ["missing line: %s" % rx[:70]
                            for rx in FAULT_ORACLE if not re.search(rx, f_probe, re.M)]
            note("fault-probe-assert", {"exit": 0 if not fails_oracle else 1,
                 "output": json.dumps({"fails": fails_oracle}, ensure_ascii=False)})
            if fails_oracle:
                ok = False
            identical = rd(impl["probe_log"]).splitlines() == f_probe.splitlines()
            note("fault-probe-compare", {"exit": 0 if identical else 1,
                 "output": "byte identity vs implementer fix-r1: identical=%s" % identical})
            if not identical:
                ok = False

        # 4. sim-inject oracle + json + byte identity vs fix-r1
        if fresh["sim_log"].is_file() and impl["sim_log"].is_file():
            f_sim = rd(fresh["sim_log"])
            fails_sim = ["missing: %s" % name for rx, name in SIM_ORACLE
                         if not re.search(rx, f_sim, re.M)]
            note("sim-inject-assert", {"exit": 0 if not fails_sim else 1,
                 "output": json.dumps({"fails": fails_sim}, ensure_ascii=False)})
            if fails_sim:
                ok = False
            identical = rd(impl["sim_log"]).splitlines() == f_sim.splitlines()
            note("sim-inject-compare", {"exit": 0 if identical else 1,
                 "output": "byte identity vs implementer fix-r1: identical=%s" % identical})
            if not identical:
                ok = False
        if fresh["sim_json"].is_file():
            try:
                arr = json.loads(rd(fresh["sim_json"]))
                inj = [x for x in arr if x.get("type") == "injection"]
                json_ok = (isinstance(arr, list) and len(inj) == 1
                           and "faults=FI-01,FI-02,FI-03,FI-04,FI-05,FI-10"
                           in inj[0].get("detail", ""))
            except Exception as exc:  # noqa: BLE001
                json_ok = False
                note("sim-json-assert", {"exit": 1, "output": "json error %r" % (exc,)})
            else:
                note("sim-json-assert", {"exit": 0 if json_ok else 1,
                     "output": "json_ok=%s inj=1" % json_ok})
            if not json_ok:
                ok = False

        # 5. ext fixture (with fix-r2 arg-contract section G) on fresh libs
        rrun(client, "mkdir -p %s" % wd, 30)
        note("mkdir-workdir", {"exit": 0, "output": wd})
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(120)
        try:
            sftp.put(str(Path(__file__).resolve().parent / "verify_packaging25_ext.cpp"),
                     "%s/verify_packaging25_ext.cpp" % wd)
            note("upload-ext", {"exit": 0, "output": "verify_packaging25_ext.cpp"})
        finally:
            sftp.close()
        cext = rrun(client, "cd %s && g++ -std=c++17 -O1 -Wall -Wextra "
                            "-Isrc/core/include -Isrc/platform/include "
                            "%s/verify_packaging25_ext.cpp "
                            "build-core/src/core/librailhmi_core.a "
                            "build-core/src/platform/librailhmi_platform.a "
                            "-o %s/verify_packaging25_ext 2>&1; echo GXX_EXIT=$?"
                    % (remote_dir, wd, wd), 240)
        note("compile-ext", cext)
        if "GXX_EXIT=0" not in cext.get("output", ""):
            ok = False
        else:
            rext = rrun(client, "cd %s && %s/verify_packaging25_ext; echo EXT_EXIT=$?"
                        % (remote_dir, wd), 180)
            note("run-ext", rext)
            m2 = re.search(r"RESULT: (\w+) passed=(\d+) failed=(\d+)",
                           rext.get("output", ""))
            if rext.get("exit") != 0 or not (m2 and m2.group(1) == "PASS"
                                             and int(m2.group(3)) == 0):
                ok = False
            else:
                note("ext-summary", {"exit": 0,
                     "output": "passed=%s failed=%s" % (m2.group(2), m2.group(3))})

        # 6. verifier deployment scan on the fresh copy (D keys expected True now)
        scan_body = (Path(__file__).resolve().parent
                     / "verify_packaging25_scan.py").read_text(encoding="utf-8")
        sftp = client.open_sftp()
        sftp.get_channel().settimeout(60)
        try:
            sftp.putfo(io.BytesIO(scan_body.encode("utf-8")),
                       "%s/verify_scan25.py" % wd)
            sftp.putfo(io.BytesIO(NEG_FIX_PY.encode("utf-8")),
                       "%s/verify_negfix25.py" % wd)
            note("upload-scan-neg", {"exit": 0, "output": "verify_scan25.py verify_negfix25.py"})
        finally:
            sftp.close()
        scan = rrun(client, "cd %s && python3 %s/verify_scan25.py --app src; echo SCAN25_EXIT=$?"
                    % (remote_dir, wd), 60)
        note("packaging-scan", scan)
        so = scan.get("output", "")

        def svar(name):
            m3 = re.search(r"^%s=(\S+)" % re.escape(name), so, re.M)
            return m3.group(1) if m3 else None

        POSITIVE = ["P1_unit_restart_always", "P2_unit_display", "P3_no_cpu_performance",
                    "P4_install_runtime_dirs", "P5_install_enables_unit",
                    "P6_version_upgrade_resources", "P7_fault_catalog_six_kinds",
                    "P8_p6_selfcheck_card", "P9_main_injects_faults",
                    "P13_unit_named_railhmi", "P14_execstart_binary_installed",
                    "P15_sim_inject_cli_built", "P16_fault_probe_built"]
        DEFECTS = ["D01_execstartpre_provenance", "D01b_upgrade_doc_bin_tools",
                   "D02_config_arg_consumed"]
        pos_bad = [k for k in POSITIVE if svar(k) != "True"]
        defect_bad = [k for k in DEFECTS if svar(k) != "True"]  # fix round: must be True
        d02_parse = svar("D02_parses_arg")
        scan_ok = (scan.get("exit") == 0 and not pos_bad and not defect_bad
                   and d02_parse == "True")
        note("packaging-scan-findings", {"exit": 0 if scan_ok else 1,
             "output": "positive_failing=%s defect_keys_not_true=%s d02_parses_arg=%s"
                       % (pos_bad, defect_bad, d02_parse)})
        if not scan_ok:
            ok = False

        # 7. delivered-checker negative controls on-board (fix-round bite proof)
        neg = rrun(client, "cd %s && python3 %s/verify_negfix25.py; echo NEG_DRIVER_EXIT=$?"
                   % (remote_dir, wd), 180)
        note("negative-control", neg)
        no = neg.get("output", "")
        neg_ok = (neg.get("exit") == 0 and "NEG_DRIVER_EXIT=0" in no
                  and "NEG_HEALTHY_EXIT=0" in no and "NEG_D01_EXIT=1" in no
                  and "NEG_D02_EXIT=1" in no)
        note("negative-control-findings", {"exit": 0 if neg_ok else 1,
             "output": "neg_ok=%s" % neg_ok})
        if not neg_ok:
            ok = False

        # 8. determinism: rerun fault-probe and diff
        n2 = rrun(client, "cd %s && mkdir -p verify && ./build-core/tools/railhmi-fault-probe "
                          "--out verify/fault-probe-rep.log; echo FAULTREP_EXIT=$?"
                  % remote_dir, 60)
        note("fault-probe-rep-run", n2)
        if "FAULTREP_EXIT=0" not in n2.get("output", ""):
            ok = False
        n2b = rrun(client, "cd %s && diff verify/fault-probe-rep.log "
                           "core-evidence/fault-probe-%s.log && echo IDENTICAL"
                   % (remote_dir, stamp), 30)
        note("fault-probe-determinism", n2b)
        if "IDENTICAL" not in n2b.get("output", ""):
            ok = False

        # 9. regression artifacts listing
        reg = rrun(client, "cd %s && ls core-evidence/" % remote_dir, 30)
        note("regression-artifacts", reg)
        if reg.get("exit") != 0:
            ok = False

        # 10. cleanup by name; trailing ls confirms absence
        note("cleanup", rrun(client,
             "rm -rf %s /tmp/railhmi_build && echo fixture_cleanup_done && "
             "echo LEFT_LIST && ls -d %s /tmp/railhmi_build 2>&1" % (wd, wd), 30))
        cleanup_res = transcript["steps"][-1]
        cleanup_ok = ("fixture_cleanup_done" in cleanup_res.get("output", "")
                      and cleanup_res.get("output", "").count("cannot access") >= 1)
        note("cleanup-verify", {"exit": 0 if cleanup_ok else 1,
                                "output": "cleanup_ok=%s" % cleanup_ok})
        if not cleanup_ok:
            ok = False

        findings = [
            {"fix_round_verification": ("fix-r2 D-01/D-02 resolved: ExecStartPre "
                                        "railhmi-version-check removed (no dangling ref, "
                                        "UPGRADE.md updated), --config parsed+consumed in "
                                        "main.cpp via core parse_config_dir (ext fixture G "
                                        "green on fresh libs), delivered checker now bites "
                                        "on D-01/D-02 re-injection; core 670/0 line-identical "
                                        "to implementer fix-r1, fault-probe/sim-inject "
                                        "byte-identical (untouched), positives all True"),
             "confirmed": ok},
        ]
        note("findings", {"exit": 0, "output": json.dumps(findings, ensure_ascii=False)})
    finally:
        client.close()
        save()

    print("host_key_fingerprint:", transcript["host_key_fingerprint"])
    for s in transcript["steps"]:
        tail = (s.get("output") or "").strip().splitlines()
        print("[%s] exit=%s %s" % (s.get("step"), s.get("exit"), tail[-1] if tail else ""))
    if not ok:
        print("NOTE: one or more steps returned nonzero or failed assertions; see transcript")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
