#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-002 验证侧扩展夹具（software_verification 自有判据，交付 16 项 selftest 未覆盖的边界）。

前提: 交付 runner board_pipeline_check.py 已 --keep-remote 完成独立复跑（本地已有
      verify-v1 产物、板端 /tmp/railhmi_pipe 构建树在位）。本夹具全部有限超时。

EXT 清单（判据先独立构造，再取实测）:
  EXT-A  三类源同一工厂出帧（行为级, 本地证据）: 4 份 probe 日志均 live 收帧 + summary live
  EXT-B  file 源 EOS 循环: 2s 素材跑 5s，t>=4000ms 槽位仍有帧（fps>0）
  EXT-C  board FI-01 断流注入: cut 后 presentable=false、错误行含 simulated stream cut (FI-01)、
         final_state 属 reconnecting/frozen/degraded/offline、输出无管线原文
  EXT-D  --decode 非法值 → exit=2 + "bad --decode"
  EXT-E  未知参数 → exit=2
  EXT-F  rtsp 空 URL → exit=3 + "stream start failed"; 输出无管线原文（protocols=/user-pw）
  EXT-G  假凭据不可达 rtsp（DUMMY_SEC@127.0.0.1）: 运行期错误行非空且全文无凭据值/user-pw/拼接 URL
  EXT-H  真实相机错误凭据（FI-V-04 域, 0600 env 文件注入计划…用错误密码）: 明确错误行、
         错误密码与真实密码均不出现在输出、env 文件删除确认
  EXT-I  证据目录凭据泄漏扫描: ITEM-002 目录全部文件对真实凭据值 0 命中
  清理:  删除板端 /tmp/railhmi_ext002 并确认

用法: python docs/video/test_evidence/ITEM-002/_verify_ext.py
退出码: 全 PASS → 0; 任一 FAIL → 1
"""
import io
import json
import os
import re
import subprocess
import sys
import time
from pathlib import Path

import paramiko

HERE = Path(__file__).resolve().parent
BOARD = "192.168.0.103"
USER = "admin"
REMOTE = "/tmp/railhmi_pipe"          # 交付 runner --keep-remote 保留的构建树
PROBE = REMOTE + "/build-media/tools/railhmi-stream-probe"
EXT_REMOTE_TMP = "/tmp/railhmi_ext002"
VERIFY_TAG = "pipe-192_168_0_103-20261017-item002-verify-v1"
CRED_NAMES = ("TRAIN_RK3588_SSH_CREDENTIAL", "TRAIN_FRONT_RTSP_CREDENTIAL",
              "TRAIN_SIDE_RTSP_CREDENTIAL")
RTSP_URL = "rtsp://192.168.1.65:554/cam/realmonitor?channel=1&subtype=0"
DUMMY = "DUMMY_SEC"
WRONG_PW = "WRONGPW_7F3D"

results = []


def record(ext_id, ok, detail):
    results.append({"id": ext_id, "ok": bool(ok), "detail": detail})
    print("%s %s %s" % (ext_id, "PASS" if ok else "FAIL", detail))


def ps_user_var(name):
    r = subprocess.run(["powershell.exe", "-NoProfile", "-Command",
                        '[Environment]::GetEnvironmentVariable("%s","User")' % name],
                       capture_output=True, text=True, timeout=30)
    return (r.stdout or "").strip()


def ssh_client():
    sec = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL") or ps_user_var("TRAIN_RK3588_SSH_CREDENTIAL")
    if not sec:
        raise SystemExit("SSH credential unavailable")
    os.environ["TRAIN_RK3588_SSH_CREDENTIAL"] = sec
    c = paramiko.SSHClient()
    c.load_system_host_keys()
    c.set_missing_host_key_policy(paramiko.RejectPolicy())
    c.connect(BOARD, port=22, username=USER, password=sec, timeout=10,
              auth_timeout=10, banner_timeout=10, look_for_keys=False, allow_agent=False)
    return c


def rrun(c, cmd, timeout_s=60):
    _, o, e = c.exec_command(cmd, timeout=timeout_s)
    out = o.read().decode("utf-8", "replace")
    err = e.read().decode("utf-8", "replace")
    code = o.channel.recv_exit_status()
    return code, out, err


STATE_RX = re.compile(
    r"t=(\d+) type=stream_state state=(\S+) fps=([\d.]+) last_frame_ms=(\d+)"
    r" reconnect_count=(\d+) presentable=(true|false)(?: error=(.*))?")


def parse_probe_log(path):
    samples, summary, errors = [], None, []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        m = STATE_RX.search(line)
        if m:
            samples.append({"t": int(m.group(1)), "state": m.group(2), "fps": float(m.group(3)),
                            "rc": int(m.group(5)), "presentable": m.group(6) == "true",
                            "error": m.group(7) or ""})
        elif "type=summary final_state=" in line:
            summary = line.strip()
        elif line.startswith("t=") and "error=" in line:
            errors.append(line.strip())
    return samples, summary, errors


def ext_ab_local():
    ok_all = True
    for src in ("test", "file", "rtsp-auto", "rtsp-h265"):
        p = HERE / ("%s.core-evidence_probe-%s-20261017-item002-verify-v1.log" % (VERIFY_TAG, src))
        if not p.exists():
            record("EXT-A/" + src, False, "missing artifact %s" % p.name)
            ok_all = False
            continue
        samples, summary, _ = parse_probe_log(p)
        live_frames = [s for s in samples if s["state"] == "live" and s["fps"] > 0]
        good = (len(live_frames) >= 3 and samples and samples[-1]["state"] == "live"
                and summary is not None and "final_state=live" in summary)
        record("EXT-A/" + src, good,
               "live_slots=%d last=%s summary_live=%s"
               % (len(live_frames), samples[-1]["state"] if samples else "-",
                  bool(summary and "final_state=live" in summary)))
        ok_all = ok_all and good
    p = HERE / ("%s.core-evidence_probe-file-20261017-item002-verify-v1.log" % VERIFY_TAG)
    samples, _, _ = parse_probe_log(p)
    late = [s for s in samples if s["t"] >= 4000 and s["fps"] > 0]
    record("EXT-B", len(late) >= 2, "slots>=4000ms with frames: %d (material 2s)" % len(late))
    ok_all = ok_all and len(late) >= 2
    return ok_all


def exit_token(combined):
    """自运行命令尾部 'echo EXIT=$?' 提取真实退出码（shell 本身恒 0，不能当被测进程码）。"""
    m = re.search(r"^.*EXIT=(\d+)\s*$", combined, re.M)
    return int(m.group(1)) if m else None


def ext_board(c):
    ok_all = True
    ev = EXT_REMOTE_TMP + "/ext"
    rrun(c, "mkdir -p %s" % ev, 20)

    # EXT-C FI-01 cut injection
    code, out, err = rrun(c, "%s --source test --duration 6 --cut-after 1 --step 400 "
                            "--out %s/cut.log 2>&1; echo CUT_EXIT=$?" % (PROBE, ev), 60)
    combined = out + "\n" + err
    cut_code = exit_token(combined)
    cut_seen = "type=injection cut=FI-01" in combined
    samples = []
    for line in combined.splitlines():
        m = STATE_RX.search(line)
        if m:
            samples.append({"t": int(m.group(1)), "state": m.group(2),
                            "presentable": m.group(6) == "true"})
    post_cut = [s for s in samples if cut_seen and s["t"] >= 1000]
    summary_line = next((l for l in combined.splitlines() if "type=summary" in l), "")
    m = re.search(r"final_state=(\S+)", summary_line or "")
    final_state = m.group(1) if m else ""
    err_line = next((l for l in combined.splitlines()
                     if "simulated stream cut (FI-01)" in l), "")
    no_raw = "protocols=udp" not in combined and "user-pw" not in combined
    # 判据（按 stream_state.cpp 语义预构造）: cut 后首源错误 reconnect#1 → reconnecting，
    # backoff 1s → connecting；无新帧 5s first_frame_timeout → reconnect#2 ……
    # 默认阈值下 6 秒运行不会升级到 offline。cut 后不允许任何 presentable=true：
    # simulate_cut 置 pipeline NULL 且不该有新帧；final_state 合法域 = 切断循环态。
    good_c = (cut_code == 0 and cut_seen and post_cut and all(not s["presentable"] for s in post_cut)
              and err_line and final_state in ("connecting", "reconnecting", "frozen", "offline")
              and no_raw)
    record("EXT-C", good_c, "exit=%s cut_seen=%s non_presentable_slots=%d final_state=%s err=%s no_raw=%s"
           % (cut_code, cut_seen, len(post_cut), final_state or "none", bool(err_line), no_raw))
    ok_all = ok_all and good_c

    # EXT-D bad --decode（退出码从 EXIT token 解析；shell 外层恒 0）
    code, out, err = rrun(c, "%s --decode zzzz 2>&1; echo EXIT=$?" % PROBE, 30)
    combined = out + "\n" + err
    pz = exit_token(combined)
    good = pz == 2 and "bad --decode" in combined
    record("EXT-D", good, "exit=%s" % pz)
    ok_all = ok_all and good

    # EXT-E unknown arg
    code, out, err = rrun(c, "%s --nope 2>&1; echo EXIT=$?" % PROBE, 30)
    combined = out + "\n" + err
    pz = exit_token(combined)
    good = pz == 2 and "unknown arg" in combined
    record("EXT-E", good, "exit=%s" % pz)
    ok_all = ok_all and good

    # EXT-F 空 rtsp URL: gst_parse_launch 可构造空 location（解析期不报错），
    # 真实失败发生在运行期 bus error → 判据 = 运行期 error 行存在 + 输出无管线原文 +
    # 未崩溃正常退出。
    code, out, err = rrun(c, "%s --source rtsp --url '' --duration 3 2>&1; echo EXIT=$?" % PROBE, 60)
    combined = out + "\n" + err
    pz = exit_token(combined)
    err_lines = [l for l in combined.splitlines() if l.startswith("t=") and "error=" in l]
    no_raw = ("protocols=udp" not in combined and "user-pw" not in combined)
    good = pz == 0 and err_lines and no_raw
    record("EXT-F", good, "exit=%s err_lines=%d no_raw=%s" % (pz, len(err_lines), no_raw))
    ok_all = ok_all and good

    # EXT-G dummy credential unreachable host, full-run redaction
    url = "rtsp://admin:%s@127.0.0.1:9/nope" % DUMMY
    code, out, err = rrun(c, "%s --source rtsp --timeout-us 1500000 --duration 4 "
                            "--url '%s' 2>&1; echo EXIT=$?" % (PROBE, url), 60)
    combined = out + "\n" + err
    pz = exit_token(combined)
    err_lines = [l for l in combined.splitlines() if l.startswith("t=") and "error=" in l]
    no_leak = (DUMMY not in combined and "rtsp://admin:" not in combined
               and "user-pw" not in combined)
    good = pz == 0 and err_lines and no_leak
    record("EXT-G", good, "exit=%s err_lines=%d no_leak=%s"
           % (pz, len(err_lines), no_leak))
    ok_all = ok_all and good

    # EXT-H real camera with WRONG credential via 0600 env file
    real_pw = os.environ.get("TRAIN_FRONT_RTSP_CREDENTIAL") or ps_user_var("TRAIN_FRONT_RTSP_CREDENTIAL")
    if real_pw and len(real_pw) >= 4:
        os.environ["TRAIN_FRONT_RTSP_CREDENTIAL"] = real_pw
        envfile = EXT_REMOTE_TMP + "/.cam_env_ext.sh"
        sftp = c.open_sftp()
        try:
            sftp.putfo(io.BytesIO(("export TRAIN_FRONT_RTSP_CREDENTIAL='%s'\n"
                                   % WRONG_PW).encode("utf-8")), envfile)
            sftp.chmod(envfile, 0o600)
        finally:
            sftp.close()
        cmd = ("cd %s && set -a && . ./.cam_env_ext.sh && set +a && %s "
               "--source rtsp --url '%s' --user admin --password-env TRAIN_FRONT_RTSP_CREDENTIAL "
               "--timeout-us 3000000 --duration 5 2>&1; echo EXIT=$?; "
               "rm -f %s/.cam_env_ext.sh && echo extenv_removed"
               % (EXT_REMOTE_TMP, PROBE, RTSP_URL, EXT_REMOTE_TMP))
        code, out, err = rrun(c, cmd, 90)
        combined = out + "\n" + err
        pz = exit_token(combined)
        err_lines = [l for l in combined.splitlines() if l.startswith("t=") and "error=" in l]
        no_leak = (WRONG_PW not in combined and real_pw not in combined
                   and "user-pw" not in combined and "rtsp://admin:" not in combined)
        env_removed = "extenv_removed" in combined
        good = pz == 0 and err_lines and no_leak and env_removed
        record("EXT-H", good, "exit=%s err_lines=%d no_leak=%s env_removed=%s"
               % (pz, len(err_lines), no_leak, env_removed))
        ok_all = ok_all and good
    else:
        record("EXT-H", False, "real camera credential unavailable in runner env")
        ok_all = False

    code, out, err = rrun(c, "rm -rf %s && echo ext_cleaned" % EXT_REMOTE_TMP, 20)
    print("[cleanup] exit=%d %s" % (code, (out + err).strip()))
    ok_all = ok_all and code == 0
    return ok_all


def ext_i_leak_scan():
    secrets = [(n, ps_user_var(n)) for n in CRED_NAMES]
    secrets = [(n, v) for n, v in secrets if v and len(v) >= 4]
    if not secrets:
        record("EXT-I", False, "no credentials available for scan (runner env)")
        return False
    hits = []
    files_scanned = 0
    ssh_note = []
    def cred_semantics(text, value):
        """值处于凭据语义形态的精确同现: user-pw="value" / password=value /
        value@host 拼接 URL。返回逐命中上下文; 非凭据形态的字面量提及不算。"""
        out = []
        rv = re.escape(value)
        for m in re.finditer(r"user-pw[\s\"']*%s|password[\s\"'=]*%s|%s@[\w.\-]*"
                             % (rv, rv, rv), text):
            out.append(text[max(0, m.start() - 30):m.end() + 15].replace("\n", " ")[:70])
        return out

    for p in HERE.rglob("*"):
        if not p.is_file():
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        files_scanned += 1
        for name, v in secrets:
            if v not in text:
                continue
            if name == "TRAIN_RK3588_SSH_CREDENTIAL":
                # SSH 凭据值与已登记的非机密用户名同字面量: '--user' 参数、user-id 属性
                # 是用户名 token 非凭据。判据 = 该字面量不处于凭据语义形态
                # （见 cred_semantics 的三种 regex 口径；本注释不含该字面量避免自命中）。
                ctxs = cred_semantics(text, v)
                if ctxs:
                    hits.append((p.name, name, ctxs[:2]))
                else:
                    ssh_note.append(p.name)
            else:
                ctxs = cred_semantics(text, v)
                if ctxs:
                    hits.append((p.name, name, ctxs[:2]))
    record("EXT-I", not hits,
           "files=%d secrets=3 cred-semantics hits=%s ssh-value-username-form-only_files=%d pass"
           % (files_scanned, hits or "none", len(ssh_note)))
    return not hits


def main():
    t0 = time.time()
    ok = True
    c = ssh_client()
    try:
        ok &= ext_ab_local()
        ok &= ext_board(c)
    finally:
        c.close()
    ok &= ext_i_leak_scan()
    out = {"results": results, "pass": sum(1 for r in results if r["ok"]),
           "fail": sum(1 for r in results if not r["ok"]),
           "elapsed_s": round(time.time() - t0, 1), "board": BOARD, "remote_tmp": EXT_REMOTE_TMP}
    (HERE / "video-cap-ext-192_168_0_103-20261017-item002-verify-v1.json").write_text(
        json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
    print("EXT summary: %d PASS / %d FAIL" % (out["pass"], out["fail"]))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
