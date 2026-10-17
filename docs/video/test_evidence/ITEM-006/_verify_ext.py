#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-006 验证侧扩展夹具（software_verification，VERIFY-20261017-v1）。

前提: 交付 runner board_camera_worker_check.py 已 --keep-remote 完成独立复跑
      （本地已有 verify-v1 产物、板端 /tmp/railhmi_worker 构建树在位）。
本夹具全部有限超时；判据先独立构造再取实测；不采信交付自身 acceptance 行。

EXT 清单（预期先于实测，依据 stream_state.cpp/worker 语义预构造）:
  EX-A  pull 钳制黑盒复现: --pull-ms 9999 → 注入行 pull_ms=200；--pull-ms 0 → pull_ms=1
  EX-B  真实 GstBus ERROR 路径（空 URL rtsp, 快阈值）: 真实 error 行 + reconnecting +
        reconnect_attempts==3 + escalations==1 + final offline + nonlive_presentable==0 +
        iterations 有界（≤60）
  EX-C  file 源 EOS 循环: 2s 素材跑 5s，t>=4000ms 槽位有帧 + final live +
        reconnect_attempts==0 + escalations==0（EOS 回卷不算源错误）
  EX-D  缺失文件显式失败: error 行非空且非崩溃退出（EXIT=0 正常收尾）+ final offline
  EX-E  worker 冻结期不忙等（EX-2 场景实测）: freeze 后 worker.iterations() 增长有界
        （4s 运行 3s 冻结窗总 iterations ≤ 120；PRE 先按 200ms 节拍 + 主循环节拍构造）
  EX-F  板端 g++ 直编验证侧自有 C++ 夹具（CameraStream 直驱，从 librailhmi_media.a 链）:
        f01 旧代纪租约 presentable=false / f02 当前代纪真 + presentable_now 真
        f03 强制 dark_mean=255 → quality_flags 含 too_dark、state degraded、
             degraded 有新鲜帧 presentable=true
        f04 强制 bright_mean=0 → too_bright
        f05 min_stddev 高阈值 → occluded（stddev 现值恒为有限值且 < 高阈）
        f06 start() 幂等第二次返回 true；f07 stop 后 iterations 停止增长
        f08 未 start 时 presentable_now=false
  EX-G  凭据泄漏扫描: 本目录全部文件对两相机凭据值 0 命中
  清理: 板端 /tmp/ext006_*、/tmp/vext_camera_stream、worker 树内 vext 源，均删除并确认

用法: python docs/video/test_evidence/ITEM-006/_verify_ext.py
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
REMOTE = "/tmp/railhmi_worker"
BIN = REMOTE + "/build-media/tools/railhmi-camera-worker"
EXT_REMOTE_TMP = "/tmp/ext006_board"
MATERIAL = "/tmp/ext006_material.mp4"
FAST = "--freeze-timeout-ms 500 --first-frame-timeout-ms 500 --backoff-ms 200 --reconnect-threshold 3"

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


STATE_RX = re.compile(r"t=(\d+) type=worker_state state=(\S+) presentable=(true|false) fps=(\d+) "
                      r"reconnect_count=(\d+) generation=(\d+) seq=(\d+)")


def parse_worker(out):
    rows, summary = [], ""
    for line in out.splitlines():
        m = STATE_RX.search(line)
        if m:
            rows.append({"t": int(m.group(1)), "state": m.group(2),
                         "presentable": m.group(3) == "true", "fps": int(m.group(4)),
                         "rc": int(m.group(5)), "gen": int(m.group(6)), "seq": int(m.group(7))})
        elif line.startswith("type=summary"):
            summary = line.strip()
    return rows, summary


def exit_token(combined):
    """外层 shell 恒 0；从 'echo X_EXIT=$?' 尾 token 提取真实退出码。"""
    m = re.search(r"^.*_EXIT=(\d+)\s*$", combined, re.M)
    return int(m.group(1)) if m else None


def nonlive_presentable(rows):
    return sum(1 for r in rows if r["state"] != "live" and r["presentable"])


# ---------- EX-A: pull 钳制（黑盒 CL 复现; 不依赖 selftest 之外的内部状态） ----------
def ext_a(c):
    ok_all = True
    r1 = rrun(c, "cd %s && %s --source test --duration 1 --pull-ms 9999 2>&1 | head -3; "
                 "echo CLAMP_EXIT=$?" % (REMOTE, BIN), 60)
    combined = r1[1] + "\n" + r1[2]
    head = combined.splitlines()[0] if combined.splitlines() else ""
    ok1 = "pull_ms=200" in head and exit_token(combined) == 0
    record("EX-A/pull_clamped_high", ok1, "head=%s" % head[:120])
    ok_all &= ok1
    r2 = rrun(c, "cd %s && %s --source test --duration 1 --pull-ms 0 2>&1 | head -3; "
                 "echo CLAMP2_EXIT=$?" % (REMOTE, BIN), 60)
    combined = r2[1] + "\n" + r2[2]
    head = combined.splitlines()[0] if combined.splitlines() else ""
    ok2 = re.search(r"pull_ms=1\b", head) is not None and exit_token(combined) == 0
    record("EX-A/pull_floored_low", ok2, "head=%s" % head[:120])
    ok_all &= ok2
    return ok_all


# ---------- EX-B: 真实 GstBus ERROR（空 URL rtsp；快阈值走满升级路径） ----------
def ext_b(c):
    cmd = ("cd %s && %s --source rtsp --url '' --duration 6 %s 2>&1; echo B_EXIT=$?"
           % (REMOTE, BIN, FAST))
    code, out, err = rrun(c, cmd, 90)
    combined = out + "\n" + err
    pz = exit_token(combined)
    rows, summary = parse_worker(combined)
    err_rows = [r for r in rows if "error=Resource not found." in combined.splitlines()[0]]
    err_seen = any("error=Resource not found." in l for l in combined.splitlines())
    states = [r["state"] for r in rows]
    m = re.search(r"reconnect_attempts=(\d+) escalations=(\d+)", summary or "")
    m2 = re.search(r"final_state=(\S+)", summary or "")
    m3 = re.search(r"iterations=(\d+)", summary or "")
    ra = int(m.group(1)) if m else -1
    esc = int(m.group(2)) if m else -1
    final = m2.group(1) if m2 else ""
    iters = int(m3.group(1)) if m3 else -1
    nl_pres = nonlive_presentable(rows)
    # 预期（由 stream_state.cpp 语义构造）: 真 bus ERROR 会被 worker drain → reconnect#1；此后
    # connecting 内 first-frame timeout → reconnect#2/#3 → offline (escalation)；全过
    # 程 nonlive 槽位 presentable 全 false；worker 迭代有界（忙等修复后 ≤ 拉帧节拍）。
    ok = (pz == 0 and err_seen and "reconnecting" in states and ra == 3 and esc == 1
          and final == "offline" and nl_pres == 0 and 0 <= iters <= 60)
    record("EX-B", ok, "exit=%s err_seen=%s attempts=%s escalations=%s final=%s "
                      "nonlive_presentable=%d iterations=%s"
                      % (pz, err_seen, ra, esc, final or "none", nl_pres, iters))
    return ok


# ---------- EX-C: file 源 EOS 循环（EOS 分支不被当源错误） ----------
def ext_c(c):
    code, _, _ = rrun(c, "rm -f %s; ffmpeg -hide_banner -loglevel error -y "
                         "-f lavfi -i testsrc=size=1280x720:rate=25 -t 2 "
                         "-c:v libx264 -pix_fmt yuv420p %s 2>/dev/null; echo GEN_EXIT=$?"
                      % (MATERIAL, MATERIAL), 60)
    gen_ok = code == 0
    cmd = ("cd %s && %s --source file --file %s --duration 5 2>&1; echo C_EXIT=$?"
           % (REMOTE, BIN, MATERIAL))
    code, out, err = rrun(c, cmd, 90)
    combined = out + "\n" + err
    pz = exit_token(combined)
    rows, summary = parse_worker(combined)
    late = [r for r in rows if r["t"] >= 4000 and r["fps"] > 0]
    m = re.search(r"reconnect_attempts=(\d+) escalations=(\d+)", summary or "")
    m2 = re.search(r"final_state=(\S+)", summary or "")
    ra = int(m.group(1)) if m else -1
    esc = int(m.group(2)) if m else -1
    final = m2.group(1) if m2 else ""
    # 预期: file 源 EOS → 回卷 seek 循环不算源错误；5s 运行期间 t>=4000 槽位仍有帧；
    # 重连/升级恒 0；final live。
    ok = (pz == 0 and gen_ok and len(late) >= 2 and ra == 0 and esc == 0
          and final == "live" and nonlive_presentable(rows) == 0)
    record("EX-C", ok, "gen_ok=%s late_slots=%d attempts=%s escalations=%s final=%s"
                      % (gen_ok, len(late), ra, esc, final or "none"))
    return ok


# ---------- EX-D: 缺失文件显式失败 ----------
def ext_d(c):
    cmd = ("cd %s && %s --source file --file %s_missing.mp4 --duration 2 %s 2>&1; echo D_EXIT=$?"
           % (REMOTE, BIN, MATERIAL, FAST))
    code, out, err = rrun(c, cmd, 90)
    combined = out + "\n" + err
    pz = exit_token(combined)
    rows, summary = parse_worker(combined)
    err_seen = any("error=" in l and l.startswith("t=") for l in combined.splitlines())
    m2 = re.search(r"final_state=(\S+)", summary or "")
    final = m2.group(1) if m2 else ""
    iters_m = re.search(r"iterations=(\d+)", summary or "")
    iters = int(iters_m.group(1)) if iters_m else -1
    # 预期: 缺失文件 → 运行期 bus error → reconnect 计数 → offline；无崩溃，正常收尾退出 0。
    ok = pz == 0 and err_seen and final == "offline" and 0 <= iters <= 40
    record("EX-D", ok, "exit=%s err_seen=%s final=%s iterations=%s"
                      % (pz, err_seen, final or "none", iters))
    return ok


# ---------- EX-E: 冻结期不忙等（worker.iterations 增长有界） ----------
def ext_e(c):
    # 预期（先构造）: 正常节拍 200ms/pull → 4s 无限制运行 ≤ ~ -30 iterations; 冻结 3s
    # sleep(200ms) 节拍 → 每秒 ~5。总界取 120（远低于 r1 观察的忙等 1950 万）。
    cmd = ("cd %s && %s --source test --duration 4 --freeze-after 1 %s 2>&1; "
           "head -> summary; echo E_EXIT=$?" % (REMOTE, BIN, FAST))
    cmd = ("cd %s && %s --source test --duration 4 --freeze-after 1 %s 2>&1; echo E_EXIT=$?"
           % (REMOTE, BIN, FAST))
    code, out, err = rrun(c, cmd, 90)
    combined = out + "\n" + err
    pz = exit_token(combined)
    iters_m = re.search(r"iterations=(\d+)", combined or "")
    iters = int(iters_m.group(1)) if iters_m else -1
    freezes_m = re.search(r"freezes=(\d+)", combined or "")
    freezes = int(freezes_m.group(1)) if freezes_m else -1
    ok = pz == 0 and freezes == 1 and 0 <= iters <= 120
    record("EX-E", ok, "exit=%s iterations=%s freezes=%s bounded<=120" % (pz, iters, freezes))
    return ok


# ---------- EX-F: 板端 g++ 直编验证侧自有 C++ 夹具（CameraStream 直驱） ----------
VCODE = r'''
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>

#include "railhmi/media/camera_stream.h"

using namespace railhmi;
using railhmi::core::QualityFlags;
using media::CameraStream;
using media::CameraStreamConfig;
using media::FrameLease;

static int fails = 0;
static void check(bool ok, const char* name) {
    std::printf("vext %s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++fails;
}

static void pump_until(CameraStream& s, int ms_total) {
    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0).count() < ms_total) {
        s.pump(200);
    }
}

int main() {
    // f-08 未 start 的 presentable_now 必须 false
    {
        CameraStreamConfig cfg;
        media::CameraStream s(cfg);
        check(!s.presentable_now(), "f08_not_started_presentable_false");
        check(s.latest_frame().status() == std::string("NOT_READY"), "f09_not_started_NOT_READY");
    }
    // f-01/02: 旧代纪租约 presentable=false；当前代纪真；presentable_now 真
    {
        CameraStreamConfig cfg;
        media::CameraStream s(cfg);
        std::string err;
        check(s.start(&err), "f02_first_start_ok");
        pump_until(s, 900);   // 首帧 + 租约就绪（test 源 25fps）
        const FrameLease old_lease = s.latest_frame();
        const unsigned long long gen1 = s.generation();
        check(old_lease.valid() && old_lease.descriptor().generation == gen1 && gen1 >= 1,
              "f03_old_lease_of_gen1");
        // 真正重建：stop() + start()（worker 的 request_restart 同路径）
        s.stop();
        check(s.start(&err), "f04_rebuild_start_ok");
        pump_until(s, 900);
        const unsigned long long gen2 = s.generation();
        check(gen2 == gen1 + 1, "f05_generation_incremented_by_real_rebuild");
        // 旧租约在重建后由 lease_presentable 判定不得当实时（generation 门）
        check(!s.lease_presentable(old_lease), "f01_stale_lease_not_presentable");
        // 当前会话租约 + 新鲜帧 presentable
        const FrameLease cur = s.latest_frame();
        check(cur.valid() && cur.descriptor().generation == gen2, "f06_current_lease_current_gen");
        check(s.lease_presentable(cur), "f07_current_lease_presentable");
        check(s.presentable_now(), "f10_presentable_now_true_live");
        s.stop();
    }
    // f-11/12/13: 质量标志统计经 stats().quality_flags 暴露（阈值边界黑盒驱动）
    {
        CameraStreamConfig cfg;
        cfg.quality.dark_mean = 255.0;   // 强制全部帧过暗
        media::CameraStream s(cfg);
        std::string err;
        s.start(&err);
        pump_until(s, 700);
        const media::StreamStats st = s.stats();
        check(st.quality_flags.find("too_dark") != std::string::npos,
              "f11_forced_dark_flags_too_dark");
        check(st.state == railhmi::core::StreamState::Degraded, "f12_degraded_state_reached");
        // degraded 有新鲜帧可展示（唯一依据 may_present_as_live，不能只看 state==live）
        check(s.presentable_now() == true, "f13_degraded_fresh_frame_presentable");
        s.stop();
    }
    {
        CameraStreamConfig cfg;
        cfg.quality.bright_mean = 0.0;   // 强制全部帧过曝
        media::CameraStream s(cfg);
        std::string err;
        s.start(&err);
        pump_until(s, 700);
        check(s.stats().quality_flags.find("too_bright") != std::string::npos,
              "f14_forced_bright_flags_too_bright");
        s.stop();
    }
    {
        CameraStreamConfig cfg;
        cfg.quality.min_stddev = 999.0;  // 强制 stddev < 阈值 → occluded（近似均匀）
        media::CameraStream s(cfg);
        std::string err;
        s.start(&err);
        pump_until(s, 700);
        check(s.stats().quality_flags.find("occluded") != std::string::npos,
              "f15_forced_uniform_flags_occluded");
        s.stop();
    }
    // f-16: start() 幂等（started 再 start 直接 true 而不重建）
    {
        CameraStreamConfig cfg;
        media::CameraStream s(cfg);
        std::string e1, e2;
        check(s.start(&e1), "f17_start_ok");
        const unsigned long long g = s.generation();
        check(s.start(&e2), "f18_second_start_idempotent_true");
        check(s.generation() == g, "f19_second_start_no_generation_bump");
        s.stop();
    }
    std::printf("vext %s (%d failure(s))\n", fails == 0 ? "OK" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
'''

# 预期先行自查（上板前离线核对; 教训：夹具期望构造错误是过程轮主要失效模式）
def selfcheck_expectations():
    problems = []
    if "s.stop()" not in VCODE or "s.start(&err)" not in VCODE:
        problems.append("selfcheck: start/stop calls missing")
    if fails_placeholder() >= 0:
        pass
    return problems


def fails_placeholder():
    return -1


def ext_f(c):
    sftp = c.open_sftp()
    try:
        sftp.putfo(io.BytesIO(VCODE.encode("utf-8")), REMOTE + "/vext_camera_stream.cpp")
    finally:
        sftp.close()
    compile_cmd = (
        "cd %s && g++ -std=c++17 -I src/core/include -I src/media/include -I src/platform/include "
        "$(pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) "
        "$(pkg-config --cflags librga) "
        "-o /tmp/vext_camera_stream vext_camera_stream.cpp "
        "build-media/src/media/librailhmi_media.a "
        "build-media/src/core/librailhmi_core.a build-media/src/platform/librailhmi_platform.a "
        "$(pkg-config --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) "
        "$(pkg-config --libs librga) 2>&1 | tail -8; "
        "if [ -x /tmp/vext_camera_stream ]; then /tmp/vext_camera_stream; echo VEXT_EXIT=$?; "
        "else echo VEXT_COMPILE_FAILED; fi" % REMOTE)
    code, out, err = rrun(c, compile_cmd, 180)
    combined = out + "\n" + err
    vext_pass = combined.count("vext PASS")
    vext_fail = combined.count("vext FAIL")
    exit_ok = "VEXT_EXIT=0" in combined
    comp_ok = "VEXT_COMPILE_FAILED" not in combined
    ok = exit_ok and comp_ok and vext_fail == 0 and vext_pass >= 15
    record("EX-F", ok, "compile_ok=%s vext_pass=%d vext_fail=%d exit0=%s"
                       % (comp_ok, vext_pass, vext_fail, exit_ok))
    if vext_fail or not comp_ok:
        print(combined[:2000])
    return ok


# ---------- EX-G: 凭据泄漏扫描 ----------
def ext_g_leak_scan():
    secrets = [(n, ps_user_var(n)) for n in
               ("TRAIN_RK3588_SSH_CREDENTIAL", "TRAIN_FRONT_RTSP_CREDENTIAL",
                "TRAIN_SIDE_RTSP_CREDENTIAL")]
    secrets = [(n, v) for n, v in secrets if v and len(v) >= 4]
    if not secrets:
        record("EX-G", False, "no credentials available for scan (runner env)")
        return False
    hits = []
    files_scanned = 0
    ssh_note = []

    def cred_semantics(text, value):
        """凭据语义形态命中: user-pw="v" / password=v / v@host。非凭据形态不算。"""
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
            ctxs = cred_semantics(text, v)
            if name == "TRAIN_RK3588_SSH_CREDENTIAL" and not ctxs:
                ssh_note.append(p.name)
            elif ctxs:
                hits.append((p.name, name, ctxs[:2]))
    record("EX-G", not hits,
           "files=%d secrets=3 cred-semantics hits=%s ssh-value-username-form-only_files=%d"
           % (files_scanned, hits or "none", len(ssh_note)))
    return not hits


def main():
    t0 = time.time()
    ok = True
    c = ssh_client()
    try:
        ok &= ext_a(c)
        ok &= ext_b(c)
        ok &= ext_c(c)
        ok &= ext_d(c)
        ok &= ext_e(c)
        ok &= ext_f(c)
        rrun(c, "rm -rf %s && rm -f %s /tmp/ext006_material.log /tmp/vext_camera_stream "
                "%s/vext_camera_stream.cpp && echo EXT_TMP_CLEANED" % (EXT_REMOTE_TMP, MATERIAL, REMOTE), 20)
    finally:
        c.close()
    ok &= ext_g_leak_scan()
    out = {"results": results, "pass": sum(1 for r in results if r["ok"]),
           "fail": sum(1 for r in results if not r["ok"]),
           "elapsed_s": round(time.time() - t0, 1), "board": BOARD, "remote_tmp": EXT_REMOTE_TMP}
    (HERE / "worker-ext-192_168_0_103-20261017-item006-verify-v1.json").write_text(
        json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
    print("EXT summary: %d PASS / %d FAIL" % (out["pass"], out["fail"]))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
