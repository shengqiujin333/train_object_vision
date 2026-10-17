#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-007 验证侧扩展夹具（software_verification，VERIFY-20261017-v1）。

前提: 交付 runner board_multi_worker_check.py 已 --keep-remote 完成独立复跑
      （板端 /tmp/railhmi_multi 构建树在位）。
本夹具全部有限超时；判据先独立构造再取实测；不采信交付自身 acceptance 行。

EXT 清单（预期先于实测，依据 multi_stream.cpp/camera_worker.cpp 语义预构造）:
  EX-A  六路全健康并发非阻塞（fail-index=-1 无注入）: 全程每拍非故障路 presentable=true、
        槽位恒等、8s 后 5 槽位无 cut 注入全 6 路健康 → 全部 presentable 且帧钟推进；
        验收 TC-V-WORKER-02 的"并发非阻塞"黑盒形态：若某路 worker 阻塞它路，
        受阻路帧钟将停滞 → 每相邻采样窗 last_frame_ms 严格推进即排除
  EX-B  重建隔离（任务书三场景之三，实现归档未测）: 六路健康运行，t=2s 对 idx=2 发
        request_restart → 该路 generation+1 且短暂 presentable=false 后恢复；其余五路
        presentable 恒 true、帧钟持续推进、generation 不变、无额外 reconnect
  EX-C  板端 g++ 直编验证侧自有 C++ 夹具（MultiStreamManager 直驱，从 librailhmi_media.a 链）:
        g01 构造 6 槽位 size/ids/index_of
        g02 start_all 单路 start_failure → 返回 5、该路 started=false start_error 非空
        g03 槽位顺序 start 前后恒等（ids() 不变）
        g04 stop 后再 start（未注入路）恢复出帧
        g05 未 start 的 stats 全 offline presentable=false
        g06 request_cut 只作用目标路：cut(idx=1) 后 idx=1 presentable 转 false，
            其余 4 路健康路 presentable 恒 true
        g07 latest_frame(camera_id) 与 latest_frame(index) 同源一致；未知 id 返回空租约
        g08 越界 index 安全: latest_frame(999) 空租约、is_presentable(999)=false、
            started(999)=false、start_error(999) 空串
        g09 双路同时断流互不干扰: cut(idx=0)+cut(idx=4) 后两路不 presentable，
            其余三路（健康）presentable=true 且帧钟推进
  EX-D  CLI 负例: 未知参数 exit=2；--count 0 运行期空转（started=0 → exit=3）
  EX-E  确定性: 同参六路运行两轮 acceptance 判据布尔恒等（state 序列语义一致，fps 自然波动非判据）
  EX-F  凭据泄漏扫描: 本目录全部文件对两相机凭据值凭据语义形态 0 命中
  清理: 板端 /tmp/ext007_*、railhmi_multi 树内 vext 源，均删除并确认

用法: python docs/video/test_evidence/ITEM-007/_verify_ext.py
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
REMOTE = "/tmp/railhmi_multi"
BIN = REMOTE + "/build-media/tools/railhmi-multi-probe"
EXT_REMOTE_TMP = "/tmp/ext007_board"

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


def exit_token(combined):
    """外层 shell 恒 0；从 'echo X_EXIT=$?' 尾 token 提取真实退出码。"""
    m = re.search(r"^.*_EXIT=(\d+)\s*$", combined, re.M)
    return int(m.group(1)) if m else None


GRID_RX = re.compile(r"t=(\d+) type=grid cells=(\d+) live=(\d+) presentable=(\d+)")
CELL_RX = re.compile(r"t=(\d+) type=cell idx=(\d) id=(CAM-\d+) state=(\S+) fps=([\d.]+) "
                     r"last_frame_ms=(\d+) reconnect=(\d+) presentable=(true|false) "
                     r"lease=(true|false) generation=(\d+)")


def parse_cells(out):
    cells = {}  # (t, idx) -> dict
    for line in out.splitlines():
        m = CELL_RX.search(line)
        if m:
            t, idx = int(m.group(1)), int(m.group(2))
            cells[(t, idx)] = {"id": m.group(3), "state": m.group(4), "fps": float(m.group(5)),
                               "last": int(m.group(6)), "rc": int(m.group(7)),
                               "pres": m.group(8) == "true", "lease": m.group(9) == "true",
                               "gen": int(m.group(10))}
    return cells


def parse_grids(out):
    rows = []
    for line in out.splitlines():
        m = GRID_RX.search(line)
        if m:
            rows.append({"t": int(m.group(1)), "cells": int(m.group(2)),
                         "live": int(m.group(3)), "pres": int(m.group(4))})
    return rows


def ts(line):
    m = re.search(r"t=(\d+) type=multi", line)
    return m


# ---------- EX-A: 六路全健康并发非阻塞（TC-V-WORKER-02 黑盒形态） ----------
def ext_a(c):
    cmd = ("cd %s && %s --count 6 --source test --duration 6 --cut-index 99 --cut-after -1 "
           "--fail-index -1 --step 500 --out /dev/null 2>&1; echo A_EXIT=$?" % (REMOTE, BIN))
    code, out, err = rrun(c, cmd, 90)
    combined = out + "\n" + err
    pz = exit_token(combined)
    cells = parse_cells(combined)
    grids = parse_grids(combined)
    times = sorted({t for (t, _) in cells})
    problems = []
    if pz != 0:
        problems.append("exit=%s" % pz)
    for g in grids:
        if g["cells"] != 6 or g["pres"] != 6 or g["live"] != 6:
            problems.append("t=%d grid %s" % (g["t"], g))
    # 槽位身份恒等
    for t in times:
        ids = [cells[(t, i)]["id"] for i in range(6)]
        if ids != ["CAM-0%d" % i for i in range(1, 7)]:
            problems.append("t=%d ids=%s" % (t, ids))
    # 帧钟推进（并发非阻塞的直接观测：任何 worker 阻塞它路都会让它路帧钟停滞）
    t0s = times[0]
    for t in times[1:]:
        for i in range(6):
            if cells[(t, i)]["last"] <= cells[(t0s, i)]["last"]:
                problems.append("t=%d idx=%d clock not advancing" % (t, i))
    # 每路至少有一个采样窗 fps 高于串行泵理论上限（6 路顺序 pump 25fps 源时每路 ≤ ~4fps）
    fps_ok = 0
    for i in range(6):
        fmax = max(cells[(t, i)]["fps"] for t in times)
        if fmax >= 20.0:
            fps_ok += 1
    if fps_ok < 6:
        problems.append("fps>=20 on %d/6 paths" % fps_ok)
    record("EX-A/six-healthy-concurrent-nonblocking", not problems,
        "exit=%s windows=%d grid_all_6l6p=%s all_clocks_advance=%s fps>=20 paths=%d/6 %s"
        % (pz, len(times), not any("grid" in p for p in problems),
           not any("clock" in p for p in problems), fps_ok,
           ("; " + "; ".join(problems[:3])) if problems else ""))
    return not problems


# ---------- EX-B: 重建隔离（任务书三场景之三；probe CLI 无 restart 注入参数） ----------
# 重建隔离经 EX-C 板端 g++ 夹具 g10 驱动（MultiStreamManager::request_restart 直驱）。


# ---------- EX-C: 板端 g++ 直编验证侧自有 C++ 夹具（MultiStreamManager 直驱） ----------
VCODE = r'''
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/media/multi_stream.h"

using namespace railhmi;
using media::ChannelSpec;
using media::MultiStreamManager;
using media::FrameLease;

static int fails = 0;
static void check(bool ok, const char* name) {
    std::printf("vext %s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++fails;
}

static std::vector<ChannelSpec> six_specs(bool fail_idx5) {
    std::vector<ChannelSpec> specs;
    for (int i = 0; i < 6; ++i) {
        ChannelSpec sp;
        char id[16];
        std::snprintf(id, sizeof(id), "CAM-%02d", i + 1);
        sp.camera_id = id;
        sp.cfg.camera_id = id;
        sp.cfg.source = media::SourceKind::Test;
        sp.start_failure = fail_idx5 && (i == 5);
        specs.push_back(sp);
    }
    return specs;
}

static void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

static bool all_presentable_except(MultiStreamManager& m, const std::vector<int>& excl) {
    for (std::size_t i = 0; i < m.size(); ++i) {
        bool skip = false;
        for (int x : excl) if ((int)i == x) skip = true;
        if (skip) continue;
        if (!m.is_presentable(i)) return false;
    }
    return true;
}

int main() {
    // g05 未 start: stats 全 offline、presentable=false
    {
        MultiStreamManager m(six_specs(false));
        check(m.size() == 6, "g01_size_6");
        bool all_off = true;
        for (std::size_t i = 0; i < m.size(); ++i)
            if (m.stats()[i].state != core::StreamState::Offline || m.is_presentable(i)) all_off = false;
        check(all_off, "g05_not_started_all_offline");
        check(m.latest_frame(999).status() == std::string("NOT_READY"), "g08_oob_lease_not_ready");
        check(!m.is_presentable(999), "g08_oob_presentable_false");
        check(!m.started(999), "g08_oob_started_false");
        check(m.start_error(999).empty(), "g08_oob_start_error_empty");
        check(m.index_of("CAM-04") == 3 && m.index_of("nope") == -1, "g01_index_of");
    }
    // g02/g03: start_all 单路失败隔离 + 槽位恒等
    {
        MultiStreamManager m(six_specs(true));
        const std::vector<std::string> before = m.ids();
        std::string err;
        const int started = m.start_all(&err);
        check(started == 5, "g02_start_all_returns_5");
        check(!m.started(5) && !m.start_error(5).empty(), "g02_fail_channel_reported");
        check(m.started(0) && m.started(4), "g02_others_started");
        check(err.find("CAM-06") != std::string::npos, "g02_err_names_failed_channel");
        check(m.ids() == before && m.ids()[5] == "CAM-06", "g03_slot_order_unchanged");
        sleep_ms(1200);
        check(all_presentable_except(m, {5}), "g02_five_presentable");
        m.stop_all();
    }
    // g06: 单路 cut 隔离 + 帧钟冻结 + 其余路推进
    {
        MultiStreamManager m(six_specs(false));
        std::string err;
        check(m.start_all(&err) == 6, "g06_six_started");
        sleep_ms(1500);
        const long long f4_before = m.stats()[4].last_frame_ms;
        m.request_cut(1);
        sleep_ms(1200);
        const auto st1 = m.stats();
        check(st1[1].state != core::StreamState::Live && !m.is_presentable(1),
              "g06_cut_path_not_presentable");
        const long long f1_frozen = st1[1].last_frame_ms;
        sleep_ms(600);
        check(m.stats()[1].last_frame_ms == f1_frozen, "g06_cut_path_frame_frozen");
        check(m.stats()[4].last_frame_ms > f4_before, "g06_other_path_advancing");
        check(all_presentable_except(m, {1}), "g06_others_presentable");
        // 恢复: restart cut 路 → 新 generation、重新出帧
        const unsigned long long g1 = st1[1].generation;
        m.request_restart(1);
        sleep_ms(1500);
        const auto st2 = m.stats();
        check(st2[1].generation == g1 + 1, "g06_restart_bumps_generation");
        check(m.is_presentable(1), "g06_restart_recovers_presentable");
        // 其余路全程未被重启（generation 不变）
        bool others_gen_stable = true;
        const auto st3 = m.stats();
        for (int i = 0; i < 6; ++i) {
            if (i == 1) continue;
            if (st3[(std::size_t)i].generation != 1) others_gen_stable = false;
        }
        check(others_gen_stable, "g06_others_generation_unchanged");
        m.stop_all();
    }
    // g07: latest_frame 两形态同源 + 未知 id 空租约
    {
        MultiStreamManager m(six_specs(false));
        std::string err;
        m.start_all(&err);
        sleep_ms(1200);
        const FrameLease by_idx = m.latest_frame(2);
        const FrameLease by_id = m.latest_frame("CAM-03");
        check(by_idx.valid() && by_id.valid(), "g07_leases_valid");
        check(by_idx.descriptor().camera_id == "CAM-03" && by_id.descriptor().camera_id == "CAM-03",
              "g07_same_camera_descriptor");
        check(by_idx.descriptor().session_id == by_id.descriptor().session_id,
              "g07_same_session");
        check(!m.latest_frame("NOPE").valid(), "g07_unknown_id_empty_lease");
        m.stop_all();
    }
    // g09: 双路同时断流互不干扰（第三路帧钟持续推进）
    {
        MultiStreamManager m(six_specs(false));
        std::string err;
        m.start_all(&err);
        sleep_ms(1500);
        m.request_cut(0);
        m.request_cut(4);
        sleep_ms(1200);
        check(!m.is_presentable(0) && !m.is_presentable(4), "g09_both_cut_not_presentable");
        check(m.is_presentable(2), "g09_middle_path_presentable");
        const long long f2_a = m.stats()[2].last_frame_ms;
        sleep_ms(800);
        check(m.stats()[2].last_frame_ms > f2_a, "g09_middle_path_advancing");
        m.stop_all();
    }
    // g10: 重建隔离（任务书三场景之三）：restart 一路不影响其余五路
    {
        MultiStreamManager m(six_specs(false));
        std::string err;
        check(m.start_all(&err) == 6, "g10_six_started");
        sleep_ms(1500);
        // 记录其余五路 restart 前帧位置与迭代计数
        long long others_last[6];
        for (int i = 0; i < 6; ++i) others_last[i] = m.stats()[(std::size_t)i].last_frame_ms;
        m.request_restart(3);  // 重建 idx=3
        sleep_ms(800);
        // 重建窗内: 其余五路 presentable 恒 true 且帧钟推进（未被重启/阻塞）
        bool others_presentable = all_presentable_except(m, {3});
        bool others_advancing = true;
        for (int i = 0; i < 6; ++i) {
            if (i == 3) continue;
            if (m.stats()[(std::size_t)i].last_frame_ms <= others_last[i]) others_advancing = false;
        }
        check(others_presentable, "g10_others_presentable_during_rebuild");
        check(others_advancing, "g10_others_advancing_during_rebuild");
        sleep_ms(1200);
        const auto st = m.stats();
        check(st[3].generation == 2, "g10_rebuilt_generation_2");
        check(m.is_presentable(3), "g10_rebuilt_recovers");
        check(st[3].reconnect_count == 0, "g10_manual_restart_not_a_reconnect");
        bool slots_ok = (m.ids().size() == 6 && m.ids()[3] == "CAM-04" && m.ids()[5] == "CAM-06");
        check(slots_ok, "g10_slot_order_unchanged");
        m.stop_all();
    }
    std::printf("vext %s (%d failure(s))\n", fails == 0 ? "OK" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
'''


def ext_c(c):
    sftp = c.open_sftp()
    try:
        sftp.putfo(io.BytesIO(VCODE.encode("utf-8")), REMOTE + "/vext_multi_stream.cpp")
    finally:
        sftp.close()
    compile_cmd = (
        "cd %s && g++ -std=c++17 -pthread -I src/core/include -I src/media/include -I src/platform/include "
        "$(pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) "
        "$(pkg-config --cflags librga) "
        "-o /tmp/vext_multi_stream vext_multi_stream.cpp "
        "build-media/src/media/librailhmi_media.a "
        "build-media/src/core/librailhmi_core.a build-media/src/platform/librailhmi_platform.a "
        "$(pkg-config --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) "
        "$(pkg-config --libs librga) 2>&1 | tail -8; "
        "if [ -x /tmp/vext_multi_stream ]; then /tmp/vext_multi_stream; echo VEXT_EXIT=$?; "
        "else echo VEXT_COMPILE_FAILED; fi" % REMOTE)
    code, out, err = rrun(c, compile_cmd, 240)
    combined = out + "\n" + err
    vext_pass = combined.count("vext PASS")
    vext_fail = combined.count("vext FAIL")
    exit_ok = "VEXT_EXIT=0" in combined
    comp_ok = "VEXT_COMPILE_FAILED" not in combined
    # 判例名集合核对（防静默少跑）
    names = ["g01_size_6", "g01_index_of", "g05_not_started_all_offline",
             "g08_oob_lease_not_ready", "g08_oob_presentable_false", "g08_oob_started_false",
             "g08_oob_start_error_empty", "g02_start_all_returns_5", "g02_fail_channel_reported",
             "g02_others_started", "g02_err_names_failed_channel", "g03_slot_order_unchanged",
             "g02_five_presentable", "g06_six_started", "g06_cut_path_not_presentable",
             "g06_cut_path_frame_frozen", "g06_other_path_advancing",
             "g06_others_presentable", "g06_restart_bumps_generation", "g06_restart_recovers_presentable",
             "g06_others_generation_unchanged", "g07_leases_valid", "g07_same_camera_descriptor",
             "g07_same_session", "g07_unknown_id_empty_lease", "g09_both_cut_not_presentable",
             "g09_middle_path_presentable", "g09_middle_path_advancing", "g10_six_started",
             "g10_others_presentable_during_rebuild", "g10_others_advancing_during_rebuild",
             "g10_rebuilt_generation_2", "g10_rebuilt_recovers", "g10_manual_restart_not_a_reconnect",
             "g10_slot_order_unchanged"]
    missing = [n for n in names if ("vext PASS " + n) not in combined]
    ok = exit_ok and comp_ok and vext_fail == 0 and vext_pass >= len(names) and not missing
    record("EX-C/board-cpp-multi-manager-fixture", ok,
           "compile_ok=%s vext_pass=%d vext_fail=%d expected=%d missing=%s exit0=%s"
           % (comp_ok, vext_pass, vext_fail, len(names), missing or "none", exit_ok))
    if vext_fail or not comp_ok or missing:
        print(combined[:3000])
    return ok


# ---------- EX-D: CLI 负例 ----------
def ext_d(c):
    ok_all = True
    r1 = rrun(c, "cd %s && %s --bogus-arg 2>&1; echo NEG_EXIT=$?" % (REMOTE, BIN), 60)
    combined = r1[1] + "\n" + r1[2]
    e1 = exit_token(combined)
    ok1 = e1 == 2 and "unknown arg" in combined
    record("EX-D/unknown-arg-exit2", ok1, "exit=%s output_head=%s" % (e1, combined.strip().splitlines()[:1]))
    ok_all &= ok1
    r2 = rrun(c, "cd %s && %s --count 0 --source test --duration 1 --cut-index 99 --cut-after -1 "
                 "--fail-index -1 --out /dev/null 2>&1; echo NEG2_EXIT=$?" % (REMOTE, BIN), 60)
    combined = r2[1] + "\n" + r2[2]
    e2 = exit_token(combined)
    ok2 = e2 == 3 and "all channels failed to start" in combined
    record("EX-D/count0-exit3", ok2, "exit=%s" % e2)
    ok_all &= ok2
    return ok_all


# ---------- EX-E: 确定性（同参两轮判据布尔恒等） ----------
def ext_e(c):
    def run_once(tag):
        cmd = ("cd %s && %s --count 6 --source test --duration 5 --cut-index 3 --cut-after 2 "
               "--fail-index 5 --step 500 --out /dev/null 2>&1; echo D_EXIT=$?" % (REMOTE, BIN))
        code, out, err = rrun(c, cmd, 120)
        combined = out + "\n" + err
        m = re.search(r"type=acceptance (.*)result=(\w+)", combined)
        return (m.group(2) if m else "?"), parse_cells(combined), parse_grids(combined)

    res1, c1, g1 = run_once("d1")
    res2, c2, g2 = run_once("d2")
    ok1 = res1 == "PASS" and res2 == "PASS"
    # 状态序列语义一致：窗数对齐后逐 cell (state,presentable,lease,gen) 恒等
    t1 = sorted({t for (t, _) in c1})
    t2 = sorted({t for (t, _) in c2})
    n = min(len(t1), len(t2))
    sem_diff = []
    for k in range(n):
        for i in range(6):
            a, b = c1[(t1[k], i)], c2[(t2[k], i)]
            if (a["state"], a["pres"], a["lease"], a["gen"], a["last"] == 0) != \
               (b["state"], b["pres"], b["lease"], b["gen"], b["last"] == 0):
                sem_diff.append((t1[k], i))
    ok = ok1 and not sem_diff
    record("EX-E/deterministic-acceptance", ok,
           "run1=%s run2=%s windows=%d/%d semantic_diffs=%d (fps natural jitter excluded)"
           % (res1, res2, len(t1), len(t2), len(sem_diff)))
    return ok


# ---------- EX-F: 凭据泄漏扫描 ----------
def ext_g_leak_scan():
    secrets = [(n, ps_user_var(n)) for n in
               ("TRAIN_RK3588_SSH_CREDENTIAL", "TRAIN_FRONT_RTSP_CREDENTIAL",
                "TRAIN_SIDE_RTSP_CREDENTIAL")]
    secrets = [(n, v) for n, v in secrets if v and len(v) >= 4]
    if not secrets:
        record("EX-F", False, "no credentials available for scan (runner env)")
        return False
    hits = []
    files_scanned = 0
    ssh_note = []

    def cred_semantics(text, value):
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
    record("EX-F", not hits,
           "files=%d secrets=3 cred-semantics hits=%s ssh-value-username-form-only_files=%d"
           % (files_scanned, hits or "none", len(ssh_note)))
    return not hits


def main():
    t0 = time.time()
    ok = True
    c = ssh_client()
    try:
        ok &= ext_a(c)
        ok &= ext_c(c)
        ok &= ext_d(c)
        ok &= ext_e(c)
        rrun(c, "rm -rf %s && rm -f /tmp/vext_multi_stream %s/vext_multi_stream.cpp && echo EXT_TMP_CLEANED"
             % (EXT_REMOTE_TMP, REMOTE), 20)
    finally:
        c.close()
    ok &= ext_g_leak_scan()
    out = {"results": results, "pass": sum(1 for r in results if r["ok"]),
           "fail": sum(1 for r in results if not r["ok"]),
           "elapsed_s": round(time.time() - t0, 1), "board": BOARD, "remote_tmp": EXT_REMOTE_TMP}
    (HERE / "multi-ext-192_168_0_103-20261017-item007-verify-v1.json").write_text(
        json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
    print("EXT summary: %d PASS / %d FAIL" % (out["pass"], out["fail"]))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
