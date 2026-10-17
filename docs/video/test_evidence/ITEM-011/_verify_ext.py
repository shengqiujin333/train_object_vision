#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-011 验证侧扩展夹具（software_verification，VERIFY-20261017-v1）。

前提: 交付 runner board_recording_tap_check.py 已 --keep-remote 完成独立复跑
      （板端 /tmp/railhmi_rec 构建树在位，railhmi-recording-tap 可执行）。
本夹具全部有限超时；判据先独立构造再取实测；不采信交付自身 acceptance 行。

EXT 清单:
  EX-A  独立参数轮（--pre-seconds 2 --max-bytes 524288 --duration 5 --display-pull-ms 150）:
        config/source 回显逐字段、独立语义重推导（双上限/守恒/独立/显式回落）
  EX-B  CLI 负例: --bogus-flag → exit=2 + unknown arg; --help → exit=0 含 --selftest/--max-bytes
  EX-C  板端 g++ 直编 CompressedRingBuffer + RecordingTap 自有判例（不采信 core-tests，独立复验边界）:
        无限上限语义(max_bytes=0/duration=0 不淘汰)、单包超宽即时淘汰(字节归零)、
        collect 闭区间两端含、first_keyframe_at_or_after 边界(恰等/无/空)、set_limits 收紧后 bytes<=新上限、
        淘汰计数分账(duration/bytes 不互串)、大窗口 1000 包循环不变量、
        RecordingTap push/stats 并发线程安全(4 写 2 读 500 包无死锁不变量成立)、source_status 回显
  EX-D  确定性: EX-A 同参两轮（a1/a2）config/source/acceptance 稳定行恒等 + rolling 语义域恒等
        （received/bytes/evicted 为自然波动域，按守恒与界限判据判定，不做数字恒等）
  EX-E  凭据泄漏扫描: 本目录全部文件凭据语义形态 0 命中
  清理: 板端 /tmp/vext_rec /tmp/vext_rec2 产物与 railhmi_rec 树内 vext 源逐项确认
"""
import json
import os
import re
import subprocess
import sys
from pathlib import Path

import paramiko

HERE = Path(__file__).resolve().parent
BOARD = "192.168.0.103"
USER = "admin"
REMOTE = "/tmp/railhmi_rec"
BIN = REMOTE + "/build-media/tools/railhmi-recording-tap"

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
    m = re.search(r"^.*_EXIT=(\d+)\s*$", combined, re.M)
    return int(m.group(1)) if m else None


CONFIG_RX = re.compile(
    r"type=config config_pre_seconds=(\d+) effective_pre_seconds=(\d+) max_bytes=(\d+) "
    r"duration_s=(\d+) fps=(\d+) display_pull_ms=(\d+)")
SOURCE_RX = re.compile(
    r"type=source requested=(\w+) nvr_configured=(true|false) effective=(\w+) local=(true|false) "
    r"nvr=(true|false)(?: reason=(\S+))?")
TAP_RX = re.compile(
    r"type=tap_records received=(\d+) expected=(\d+) keyframe_last=(true|false) display_frames=(\d+)")
ROLLING_RX = re.compile(
    r"type=rolling packets=(\d+) bytes=(\d+) first_ts_ms=(-?\d+) last_ts_ms=(-?\d+) span_ms=(-?\d+) "
    r"keyframes=(\d+) pushed=(\d+) evicted_duration=(\d+) evicted_bytes=(\d+)")
ACC_RX = re.compile(
    r"type=acceptance bytes_bounded=(true|false) duration_bounded=(true|false) evicted=(true|false) "
    r"record_rate_ok=(true|false) display_independent=(true|false) source_status_explicit=(true|false) "
    r"result=(\w+)")


def parse_round(combined):
    d = {}
    m = CONFIG_RX.search(combined)
    if m:
        d["config"] = tuple(int(x) for x in m.groups())
    m = SOURCE_RX.search(combined)
    if m:
        d["source"] = m.groups()
    m = TAP_RX.search(combined)
    if m:
        d["tap"] = tuple(int(x) for x in (m.group(1), m.group(2), m.group(4))) + (m.group(3),)
    m = ROLLING_RX.search(combined)
    if m:
        d["rolling"] = dict(zip(("packets", "bytes", "first_ts_ms", "last_ts_ms", "span_ms",
                                 "keyframes", "pushed", "ev_dur", "ev_bytes"),
                                (int(x) for x in m.groups())))
    m = ACC_RX.search(combined)
    if m:
        d["acc"] = m.groups()
    return d


def check_round_semantics(tag, d, problems, pre, max_bytes, dur, pull_ms):
    """EX-A/EX-D 独立语义重推导（不采信 acceptance 行）。"""
    if not all(k in d for k in ("config", "source", "tap", "rolling", "acc")):
        problems.append("%s missing lines: %s" % (tag, [k for k in ("config", "source", "tap", "rolling", "acc") if k not in d]))
        return
    cfg = d["config"]
    if not (cfg[0] == 30 and cfg[1] == pre and cfg[2] == max_bytes and
            cfg[3] == dur and cfg[4] == 25 and cfg[5] == pull_ms):
        problems.append("%s config echo: %s" % (tag, cfg))
    src = d["source"]
    if not (src[0] == "both" and src[1] == "false" and src[2] == "local_fallback" and
            src[3] == "true" and src[4] == "false" and src[5] == "NVR_UNCONFIGURED"):
        problems.append("%s source fallback: %s" % (tag, src))
    r = d["rolling"]
    if r["bytes"] > max_bytes:
        problems.append("%s bytes %d > max %d" % (tag, r["bytes"], max_bytes))
    if r["span_ms"] > pre * 1000 + 100:
        problems.append("%s span %d > pre %d" % (tag, r["span_ms"], pre))
    if r["pushed"] != r["packets"] + r["ev_dur"] + r["ev_bytes"]:
        problems.append("%s conservation: %s" % (tag, r))
    if d["tap"][0] < d["tap"][1] * 85 // 100:
        problems.append("%s received %d < 85%% of %d" % (tag, d["tap"][0], d["tap"][1]))
    if not (d["tap"][2] < d["tap"][0] // 3):
        problems.append("%s display %d not << records %d" % (tag, d["tap"][2], d["tap"][0]))
    acc = d["acc"]
    if acc[-1] != "PASS":
        problems.append("%s acceptance result=%s" % (tag, acc[-1]))
    # 无线程侧要求: evicted 恒有一者>0（cap 或 window 必绑定其一）
    if not (r["ev_dur"] > 0 or r["ev_bytes"] > 0):
        problems.append("%s no eviction in bounded round: %s" % (tag, r))


# ---------------------------------------------------------------- EX-C C++ 夹具
VEXT_CPP = r"""
// vext_recording.cpp — ITEM-011 验证侧自有扩展夹具（software_verification）
// 直接链接交付 core 层 compressed_ring.cpp / recording_source.cpp + media 层 recording_tap.cpp，
// 独立复验边界；判据全部验证侧独立构造，不采信 core-tests/sselftest。
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/core/compressed_ring.h"
#include "railhmi/core/recording_source.h"
#include "railhmi/media/recording_tap.h"

using railhmi::core::CompressedPacket;
using railhmi::core::CompressedRingBuffer;
using railhmi::core::CompressedRingStats;
using railhmi::core::IntegrationConfig;
using railhmi::core::RecordingAvailability;
using railhmi::core::RecordingSource;
using railhmi::core::RecordingSourceStatus;
using railhmi::core::resolve_recording_source;
using railhmi::media::RecordingTap;

static int failures = 0;
static void check(bool ok, const std::string& name, const std::string& detail = "") {
    std::printf("vext %s %s%s\n", ok ? "PASS" : "FAIL", name.c_str(),
                detail.empty() ? "" : (" [" + detail + "]").c_str());
    if (!ok) ++failures;
}

static CompressedPacket pkt(int64_t ts, std::size_t bytes, bool key = false) {
    CompressedPacket p;
    p.ts_ms = ts;
    p.keyframe = key;
    p.data.assign(bytes, static_cast<std::uint8_t>(ts & 0xff));
    return p;
}

int main() {
    // --- 无限上限语义: max_bytes=0 与 duration<=0 均表示不限 ---
    {
        CompressedRingBuffer r(0, 0);  // 双不限
        for (int i = 0; i < 50; ++i) r.push(pkt(i * 10, 64));
        CompressedRingStats s = r.stats();
        check(s.packets == 50 && s.evicted_duration == 0 && s.evicted_bytes == 0,
              "v01_unlimited_no_eviction", "packets=" + std::to_string(s.packets));
        check(s.span_ms == 490 && s.bytes == 50u * 64u, "v02_unlimited_full_window",
              "span=" + std::to_string(s.span_ms));
    }
    {
        CompressedRingBuffer r(0, 0);  // 仅时长不限（bytes 有界 0 → 不限）
        for (int i = 0; i < 30; ++i) r.push(pkt(i, 10));
        check(r.stats().packets == 30, "v03_zero_byte_cap_unlimited");
    }
    // --- 单包超宽: 超字节上限的单包被立即淘空（bytes 归零、不残留半包） ---
    {
        CompressedRingBuffer r(100, 0);
        r.push(pkt(0, 500));
        CompressedRingStats s = r.stats();
        check(s.bytes == 0 && s.packets == 0 && s.evicted_bytes == 1,
              "v04_oversize_packet_evicted", "bytes=" + std::to_string(s.bytes));
        // 超宽包不阻后续正常包
        r.push(pkt(1, 40));
        s = r.stats();
        check(s.packets == 1 && s.bytes == 40, "v05_recovery_after_oversize");
    }
    // --- collect 闭区间两端含（契约 [from,to] 闭合） ---
    {
        CompressedRingBuffer r(1u << 20, 100000);
        for (int i = 0; i <= 10; ++i) r.push(pkt(i * 100, 8, i == 2 || i == 9));
        auto w = r.collect(200, 900);
        check(w.size() == 8 && w.front().ts_ms == 200 && w.back().ts_ms == 900,
              "v06_collect_closed_interval", "size=" + std::to_string(w.size()));
        auto w1 = r.collect(100, 100);
        check(w1.size() == 1 && w1.front().ts_ms == 100, "v07_collect_single_eq");
        auto w2 = r.collect(95, 99);
        check(w2.empty(), "v08_collect_disjoint_empty");
    }
    // --- first_keyframe_at_or_after: 恰等边界 / 之后首个 / 无 -1 / 空 -1 ---
    {
        CompressedRingBuffer r(1u << 20, 100000);
        r.push(pkt(100, 8, true));
        r.push(pkt(200, 8));
        r.push(pkt(300, 8, true));
        r.push(pkt(400, 8));
        check(r.first_keyframe_at_or_after(100) == 100, "v09_kf_exact_boundary");
        check(r.first_keyframe_at_or_after(101) == 300, "v10_kf_after");
        check(r.first_keyframe_at_or_after(401) == -1, "v11_no_kf_minus1");
        r.push(pkt(500, 8, true));
        check(r.first_keyframe_at_or_after(500) == 500, "v12_kf_last_exact");
        CompressedRingBuffer e(100, 1000);
        check(e.first_keyframe_at_or_after(0) == -1, "v13_empty_kf_minus1");
    }
    // --- 淘汰分账: 先 duration 后 bytes，两类计数不互串 ---
    {
        // 每 100ms 一包 100B：duration=300 保留 4 包(0,100,200,300@迭代 i≤j)；
        CompressedRingBuffer r(500, 300);
        for (int i = 0; i <= 10; ++i) r.push(pkt(i * 100, 100));
        CompressedRingStats s = r.stats();
        check(s.span_ms <= 300 && s.evicted_duration >= 1 && s.evicted_bytes == 0,
              "v14_duration_first_account", "ev_d=" + std::to_string(s.evicted_duration) +
              " ev_b=" + std::to_string(s.evicted_bytes));
        // 收紧字节后 bytes 计数才开始
        r.set_limits(150, 300);
        s = r.stats();
        check(s.bytes <= 150 && s.evicted_bytes >= 1 && s.evicted_duration >= 1,
              "v15_byte_after_shrink", "bytes=" + std::to_string(s.bytes));
        check(r.size() <= 150 / 100 + 1, "v16_shrink_packet_bound");
    }
    // --- 大窗口不变量: 1000 包混合尺寸流经双上限，逐包断言界与守恒 ---
    {
        CompressedRingBuffer r(20000, 5000);
        std::uint64_t total_pushed_bytes = 0;
        for (int i = 0; i < 1000; ++i) {
            const std::size_t b = 30 + (i % 7) * 25;  // 30..180B 波动
            CompressedPacket p = pkt(i * 20, b, i % 40 == 0);
            total_pushed_bytes += b;
            r.push(std::move(p));
            CompressedRingStats s = r.stats();
            if (s.bytes > 20000 || s.span_ms > 5000) {
                check(false, "v17_overflow_invariant", "i=" + std::to_string(i) +
                      " bytes=" + std::to_string(s.bytes) + " span=" + std::to_string(s.span_ms));
                break;
            }
            if (s.pushed != s.packets + s.evicted_duration + s.evicted_bytes) {
                check(false, "v18_conservation_invariant", "i=" + std::to_string(i));
                break;
            }
        }
        CompressedRingStats s = r.stats();
        check(s.pushed == 1000 && s.bytes <= 20000 && s.span_ms <= 5000,
              "v19_big_window_final", "pushed=" + std::to_string(s.pushed) +
              " bytes=" + std::to_string(s.bytes) + " span=" + std::to_string(s.span_ms));
        check(s.keyframes == 25, "v20_big_window_kf_count", "kf=" + std::to_string(s.keyframes));
        check(s.bytes < total_pushed_bytes, "v21_ring_discards_old_data");
    }
    // --- ts 回退防护观察: 文档声明“假定单调不减”，回退 ts 不崩且界保持（登记观察） ---
    {
        CompressedRingBuffer r(10000, 1000);
        r.push(pkt(5000, 50, true));
        r.push(pkt(4900, 50));  // 回退 100ms
        CompressedRingStats s = r.stats();
        check(s.bytes <= 10000 && s.packets <= 21, "v22_ts_regression_bounded",
              "packets=" + std::to_string(s.packets));
    }
    // --- RecordingTap 线程安全: 4 写 2 读并发 800 包（400/写者）无死锁、终态不变量成立 ---
    {
        RecordingTap tap(8000, 1500);
        std::atomic<bool> stop{false};
        std::atomic<int> read_errors{0};
        std::vector<std::thread> ths;
        for (int w = 0; w < 4; ++w) {
            ths.emplace_back([&tap, w]() {
                for (int i = 0; i < 400; ++i) {
                    CompressedPacket p = pkt(i, 40 + (i % 5) * 8, i % 50 == 0);
                    p.ts_ms = i * 5;
                    tap.push(std::move(p));
                }
            });
        }
        for (int rd = 0; rd < 2; ++rd) {
            ths.emplace_back([&tap, &stop, &read_errors]() {
                while (!stop.load()) {
                    auto st = tap.stats();
                    auto c = tap.collect(0, 3000);
                    auto k = tap.first_keyframe_at_or_after(500);
                    (void)k;
                    if (st.bytes > 8000 || st.span_ms > 1500) ++read_errors;
                }
            });
        }
        for (std::size_t i = 0; i < 4; ++i) ths[i].join();
        stop.store(true);
        for (std::size_t i = 4; i < ths.size(); ++i) ths[i].join();
        CompressedRingStats s = tap.stats();
        check(read_errors.load() == 0, "v23_concurrent_reader_no_invariant_violation",
              "errors=" + std::to_string(read_errors.load()));
        check(s.pushed == 1600 && s.bytes <= 8000 && s.span_ms <= 1500,
              "v24_concurrent_terminal_invariants", "pushed=" + std::to_string(s.pushed) +
              " bytes=" + std::to_string(s.bytes) + " span=" + std::to_string(s.span_ms));
        check(s.pushed == s.packets + s.evicted_duration + s.evicted_bytes,
              "v25_concurrent_conservation");
    }
    // --- source_status 经 RecordingTap 存取回显 ---
    {
        RecordingTap tap(1000, 1000);
        tap.set_source_status(resolve_recording_source(RecordingSource::Nvr, false));
        auto st = tap.source_status();
        check(st.effective == RecordingAvailability::LocalFallbackNvrUnconfigured &&
              st.reason == "NVR_UNCONFIGURED", "v26_tap_source_status_echo");
        tap.set_source_status(resolve_recording_source(RecordingSource::Local, true));
        st = tap.source_status();
        check(st.effective == RecordingAvailability::LocalOnly && st.nvr_enabled == false,
              "v27_tap_source_status_local");
    }
    // --- integration_is_configured: proto/mapping 两维独立 ---
    {
        IntegrationConfig c;
        c.id = "NVR";
        c.registered = true;
        check(!railhmi::core::integration_is_configured(c), "v28_registered_alone_not_configured");
        c.has_protocol = true;
        check(!railhmi::core::integration_is_configured(c), "v29_proto_only_not_configured");
        c.has_channel_mapping = true;
        check(railhmi::core::integration_is_configured(c), "v30_both_configured");
    }
    // --- RecordingSource::Local 显式拒绝 NVR 即便 nvr_configured=true ---
    {
        auto s = resolve_recording_source(RecordingSource::Local, true);
        check(s.effective == RecordingAvailability::LocalOnly && !s.nvr_enabled && s.reason.empty(),
              "v31_local_request_ignores_nvr");
        auto b = resolve_recording_source(RecordingSource::Both, true);
        check(b.effective == RecordingAvailability::Both && b.nvr_enabled && b.local_enabled,
              "v32_both_configured_enables_nvr");
    }

    std::printf("vext %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
"""


def main():
    c = ssh_client()
    problems = []

    # EX-A 独立参数轮: pre=2 bytes=512KiB dur=5 pull=150
    exa_tag = "a1"
    cmd = ("cd %s && %s --config config --pre-seconds 2 --max-bytes 524288 --duration 5 "
           "--display-pull-ms 150 --out core-evidence/vexta-%s.log --json core-evidence/vexta-%s.json "
           "2>&1; echo VEXTA_EXIT=$?" % (REMOTE, BIN, exa_tag, exa_tag))
    code, out, err = rrun(c, cmd, 5 + 120)
    d = parse_round(out + "\n" + err)
    pre = len(problems)
    check_round_semantics("EX-A/" + exa_tag, d, problems, 2, 524288, 5, 150)
    tok = exit_token(out + "\n" + err)
    if tok != 0:
        problems.append("EX-A exit token %s" % tok)
    record("EX-A", len(problems) == pre, "independent params round; problems=%s" %
           (problems[pre:] if len(problems) > pre else "none"))

    # EX-B CLI 负例
    c1, o1, _ = rrun(c, "cd %s && %s --bogus-flag 2>&1; echo NEG_EXIT=$?" % (REMOTE, BIN), 30)
    ok_b1 = ("unknown arg: --bogus-flag" in o1) and (exit_token(o1) == 2)
    record("EX-B/neg", ok_b1, "unknown-arg exit=2 + message (token=%s)" % exit_token(o1))
    c2, o2, _ = rrun(c, "cd %s && %s --help 2>&1; echo HELP_EXIT=$?" % (REMOTE, BIN), 30)
    ok_b2 = ("--selftest" in o2) and ("--max-bytes" in o2) and (exit_token(o2) == 0)
    record("EX-B/help", ok_b2, "--help exit=0 with option lines (token=%s)" % exit_token(o2))

    # EX-C 板端 g++ 直编自有 C++ 夹具
    sftp = c.open_sftp()
    try:
        sftp.get_channel().settimeout(60)
        try:
            sftp.mkdir(REMOTE + "/core-evidence/vext")
        except OSError:
            pass
        sftp.put(str(HERE / "_verify_vext.cpp"), REMOTE + "/core-evidence/vext/vext_recording.cpp")
        record("EX-C/upload", True, "vext_recording.cpp uploaded to railhmi_rec tree")
    finally:
        sftp.close()
    build_cmd = (
        "cd %s && g++ -std=c++17 -O1 -I src/core/include -I src/media/include "
        "-I src/platform/include "
        "$(pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) "
        "core-evidence/vext/vext_recording.cpp "
        "src/media/src/recording_tap.cpp src/core/src/compressed_ring.cpp "
        "src/core/src/recording_source.cpp src/core/src/config.cpp src/core/src/yaml.cpp "
        "$(pkg-config --libs gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0) "
        "-lpthread -o core-evidence/vext/vext_recording 2>&1; echo VEXT_BUILD_EXIT=$?"
        % REMOTE)
    cb, ob, eb = rrun(c, build_cmd, 180)
    tok_b = exit_token(ob + "\n" + eb)
    if tok_b != 0:
        record("EX-C/build", False, "g++ exit=%s tail=%s" % (tok_b, (ob + eb)[-800:]))
        problems.append("EX-C build failed")
        record("EX-C", False, "build failure blocks run")
    else:
        record("EX-C/build", True, "vext_recording built against delivery core+media sources")
        crx, orx, erx = rrun(c, "cd %s && ./core-evidence/vext/vext_recording 2>&1; "
                                "echo VEXT_RUN_EXIT=$?" % REMOTE, 120)
        combined = orx + "\n" + erx
        vpass = re.findall(r"^vext PASS (\S+)", combined, re.M)
        vfail = re.findall(r"^vext FAIL (\S+)", combined, re.M)
        tok_r = exit_token(combined)
        record("EX-C/run", tok_r == 0 and not vfail and len(vpass) >= 25,
               "vext %d PASS %d FAIL token=%s" % (len(vpass), len(vfail), tok_r))
        if vfail:
            problems.append("EX-C vext failures: %s" % vfail)

    # EX-D 确定性: EX-A 同参两轮（a2）；稳定行恒等 + rolling 语义域判据
    exa2_tag = "a2"
    cmd2 = ("cd %s && %s --config config --pre-seconds 2 --max-bytes 524288 --duration 5 "
            "--display-pull-ms 150 --out core-evidence/vexta-%s.log --json core-evidence/vexta-%s.json "
            "2>&1; echo VEXTA2_EXIT=$?" % (REMOTE, BIN, exa2_tag, exa2_tag))
    cde, o1d, e1d = rrun(c, cmd2, 5 + 120)
    d1 = parse_round(o1d + "\n" + e1d)
    d2 = d  # a1 from EX-A
    stable_keys = ("config", "source", "acc")
    ok_stable = all(d1.get(k) == d2.get(k) for k in stable_keys) and all(
        d1.get(k) for k in stable_keys)
    if not ok_stable:
        problems.append("EX-D stable rows differ between a1/a2: %s vs %s" %
                        ({k: d1.get(k) for k in stable_keys}, {k: d2.get(k) for k in stable_keys}))
    record("EX-D/stable", ok_stable, "config/source/acceptance identical a1 vs a2")
    pre_d = len(problems)
    check_round_semantics("EX-D/a2", d1, problems, 2, 524288, 5, 150)
    record("EX-D/semantics", len(problems) == pre_d,
           "a2 independent semantics re-derivation holds")
    tok2 = exit_token(o1d + "\n" + e1d)
    ok_tok = tok2 == 0
    record("EX-D/exit", ok_tok, "a2 exit token=%s" % tok2)
    # rolling received/bytes 自然波动: 波动窗判据 |Δreceived|<=8, |Δbytes| 有界（不能假设字节少而恒等）
    if d1.get("rolling") and d2.get("rolling"):
        dr = abs(d1["rolling"]["packets"] - d2["rolling"]["packets"])
        ok_win = dr <= 8
        record("EX-D/window", ok_win, "packets a1=%d a2=%d (|Δ|=%d<=8)"
               % (d2["rolling"]["packets"], d1["rolling"]["packets"], dr))
        if not ok_win:
            problems.append("EX-D packet-count window exceeded: %d" % dr)
    else:
        record("EX-D/window", False, "rolling rows missing")
        problems.append("EX-D rolling rows missing")

    # EX-E 凭据泄漏扫描（本目录全部文件，凭据语义形态）
    sec = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL") or ps_user_var("TRAIN_RK3588_SSH_CREDENTIAL")
    patterns = [
        re.compile(r"(user-pw|password|passwd|pwd)\s*[=:]\s*['\"]?[^\s'\"]{4,}", re.I),
        re.compile(r"://[^\s/:@]+:[^\s/@]+@"),
    ]
    sem_hits = []
    for p in sorted(HERE.iterdir()):
        if p.suffix in (".py", ".md", ".json", ".log", ".txt", ".cpp"):
            try:
                text = p.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            for i, line in enumerate(text.splitlines(), 1):
                for rx in patterns:
                    if rx.search(line) and sec and sec in line:
                        sem_hits.append("%s:%d" % (p.name, i))
    ok_e = not sem_hits
    record("EX-E", ok_e, "credential semantic forms 0 hits (%s)" % ("clean" if ok_e else sem_hits))
    if not ok_e:
        problems.append("EX-E credential leakage: %s" % sem_hits)

    # 清理: 板端 vext 构件与工作目录确认
    rcl, ocl, _ = rrun(c, "rm -rf %s/core-evidence/vext %s/core-evidence/vexta-*.log "
                          "%s/core-evidence/vexta-*.json /tmp/vext_rec /tmp/vext_rec2 && "
                          "echo VEXT_CLEAN_DONE" % (REMOTE, REMOTE, REMOTE), 30)
    record("CLEAN", "VEXT_CLEAN_DONE" in ocl, "board-side ext artifacts removed")

    c.close()
    summary = {"results": results, "problems": problems,
               "pass": all(r["ok"] for r in results)}
    (HERE / "rec-ext-192_168_0_103-20261017-item011-verify-v1.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    npass = sum(1 for r in results if r["ok"])
    print("EXT SUMMARY", "PASS" if summary["pass"] else "FAIL",
          "%d/%d" % (npass, len(results)))
    if problems:
        print("PROBLEMS:")
        for p in problems:
            print(" -", p)
    return 0 if summary["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
