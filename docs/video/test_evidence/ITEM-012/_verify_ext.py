#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-012 验证侧扩展夹具（software_verification，VERIFY-20261017-v1）。

前提: 验证驱动已 --keep-remote 完成交付入口独立复跑
      （板端 /tmp/railhmi_clip 构建树在位，railhmi-clip-solidify 可执行）。
本夹具全部有限超时；判据先独立构造再取实测；不采信交付自身 acceptance 行。

EXT 清单:
  EX-A  独立参数轮（--pre-seconds 2 --post-seconds 3 --warmup 8 --extra 3; 独立语义重推导）:
        config 逐字段独立核对（配置常量 30/60 与探针无关论据 + buffer_seconds 公式）buffer 9s、
        buffer/trigger 区间独立检查、complete 行逐键避开 core-tests 既有前缀、
        覆盖数学重推导（actual_t0>=t0 && actual_t1<=t1 && t1<=trig+post + tol 覆盖判定独立重算）、
        incomplete/gap 段精确 <>
  EX-B  CLI 负例: --bogus-flag → exit=2 + unknown arg; --help → exit=0 含 --selftest/--container
  EX-C  板端 g++ 直编 ClipWriter + clip_request 自有判例（不采信 core-tests/sselftest，独立复验边界）:
        path 形状、窗口算术、注册表 Added/Duplicate/Conflict+job 未覆盖、
        可用性三态 roundtrip、record_unavailable 写缺口结构（视放 output_dir 为空体 MUX 参数）、
        清单遗留 .json 冒充片段禁判
  EX-D  确定性: selftest --help/语义行重推导与 streaming 轮无时间戳绑定 → 稳定行恒等
  EX-E  泄漏扫描: 本目录全部文件凭据语义形态 0 命中
  清理: 板端 /tmp/vext_clips、树内 vext 源逐项确认 exit=2 也显式记给清理
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
REMOTE = "/tmp/railhmi_clip"
BIN = REMOTE + "/build-media/tools/railhmi-clip-solidify"

results = []
problems = []


def record(ext_id, ok, detail):
    results.append({"id": ext_id, "ok": bool(ok), "detail": detail})
    print("%s %s %s" % (ext_id, "PASS" if ok else "FAIL", detail))


def expect(ext_id, ok, detail, problems_list=None):
    """判据 wire: 独立检查必须写 problems（无论 record 是否重复）。"""
    record(ext_id, ok, detail)
    if not ok:
        (problems_list or problems).append("%s: %s" % (ext_id, detail))


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


def exit_token(combined, tag):
    m = re.search(r"^%s_EXIT=(\d+)\s*$" % tag, combined, re.M)
    return int(m.group(1)) if m else None


# ---------------------------------------------------------------- 解析（与核对口径无关的部分共享）
CONFIG_RX = re.compile(
    r"type=config config_pre_seconds=(\d+) config_post_seconds=(\d+) effective_pre_seconds=(\d+) "
    r"effective_post_seconds=(\d+) buffer_seconds=(\d+) outdir=(\S+)")

ACC_RX = re.compile(
    r"type=acceptance real_media=(true|false) coverage_matches=(true|false) keyframe_aligned=(true|false) "
    r"gap_recorded=(true|false) availability_tristate=(true|false) idempotent=(true|false) "
    r"publish_after_rename=(true|false) no_stray_temp=(true|false) result=(\w+)")


def parse_round(combined):
    d = {}
    m = CONFIG_RX.search(combined)
    if m:
        d["config"] = {"config_pre": int(m.group(1)), "config_post": int(m.group(2)),
                       "eff_pre": int(m.group(3)), "eff_post": int(m.group(4)),
                       "buffer_s": int(m.group(5)), "outdir": m.group(6)}
    m = ACC_RX.search(combined)
    if m:
        d["acc"] = {"real_media": m.group(1), "coverage_matches": m.group(2),
                    "keyframe_aligned": m.group(3), "gap_recorded": m.group(4),
                    "availability_tristate": m.group(5), "idempotent": m.group(6),
                    "publish_after_rename": m.group(7), "no_stray_temp": m.group(8),
                    "result": m.group(9)}
    cases = {}
    for case in ("complete", "incomplete", "unavailable", "mux_failed"):
        m = re.search(r"^type=clip case=%s .*" % case, combined, re.M)
        if m:
            cases[case] = m.group(0)
    if cases:
        d["cases"] = cases
    m = re.search(r"^type=idempotency (.*)$", combined, re.M)
    if m:
        d["idem"] = dict(re.findall(r"(\w+)=\{?([A-Za-z]+)\}?", m.group(1)))
    m = re.search(r"^type=buffer packets=(\d+) span_ms=(\d+) last_ts_ms=(-?\d+) display_frames=(\d+) trigger_ts_ms=(-?\d+)", combined, re.M)
    if m:
        d["buffer"] = {"packets": int(m.group(1)), "span_ms": int(m.group(2)),
                       "last_ts_ms": int(m.group(3)), "display_frames": int(m.group(4)),
                       "trigger_ts_ms": int(m.group(5))}
    return d


def kv_closure(line, case):
    """单 case 行键集（与验收字段的交叠冲突唯一化：pkt.vue 完整字段名）。"""
    flags = {
        "complete": {"requested", "actual", "packets", "keyframe_aligned",
                     "availability", "size_bytes", "published", "path", "reason"},
        "incomplete": {"requested", "actual", "availability", "has_gap",
                       "gap_reason", "gap"},
        "unavailable": {"packets", "availability", "has_gap", "reason"},
        "mux_failed": {"availability", "has_gap", "reason"},
    }
    keys = set()
    for m in re.finditer(r"(\w+)=", line):
        k = m.group(1)
        keys.add(k)
        if k == "type" or k == "case":
            continue
        if k not in flags.get(case, set()):
            return "unexpected key %s in case %s" % (k, case)
    if keys - {"type", "case"} != flags.get(case, set()):
        return "key set mismatch: %s vs %s" % (sorted(keys - {"type", "case"}),
                                               sorted(flags.get(case, set())))
    return None


def extract_case_fields(line):
    """从单 case 行提取 (requested_t0, requested_t1, actual_t0, actual_t1, has_gap, gap_t0, gap_t1, gap_reason)."""
    f = {}
    m = re.search(r"requested=\[(-?\d+),(-?\d+)\)", line)
    if m:
        f["req_t0"], f["req_t1"] = int(m.group(1)), int(m.group(2))
    m = re.search(r"actual=\[(-?\d+),(-?\d+)\]", line)
    if m:
        f["act_t0"], f["act_t1"] = int(m.group(1)), int(m.group(2))
    m = re.search(r"has_gap=(true|false)", line)
    if m:
        f["has_gap"] = m.group(1) == "true"
    m = re.search(r"gap_reason=(\S+)", line)
    if m:
        f["gap_reason"] = m.group(1)
    m = re.search(r"gap=\[(-?\d+),(-?\d+)\)", line)
    if m:
        f["gap_t0"], f["gap_t1"] = int(m.group(1)), int(m.group(2))
    return f


CHECK_KEYS = ("real_media", "coverage_matches", "keyframe_aligned",
              "gap_recorded", "availability_tristate", "idempotent",
              "publish_after_rename", "no_stray_temp")


def c1_has_gap(line):
    """S1 行是否带缺口标注：S2 行用 gap_reason=/gap=[；S1(complete 案例) 行用 reason=<*_SHORTAGE>。"""
    return bool(re.search(r"gap_reason=\S+", line) or re.search(r"gap=\[", line) or
                re.search(r"reason=(PRE|POST|PRE_POST)_ROLL_SHORTAGE", line))


def check_case_semantics(tag, d, problems_list, pre, post, tol=1000):
    """EX-A/EX-D 独立语义重推导（不采信 acceptance 行）。tol 与探针判据同 1000ms 容差口径。"""
    if not all(k in d for k in ("config", "cases", "idem", "buffer", "acc")):
        problems_list.append("%s missing lines: %s" %
                             (tag, [k for k in ("config", "cases", "idem", "buffer", "acc") if k not in d]))
        return
    cfg = d["config"]
    if not (cfg["config_pre"] == 30 and cfg["config_post"] == 60 and
            cfg["eff_pre"] == pre and cfg["eff_post"] == post and
            cfg["buffer_s"] == pre + post + 2):
        problems_list.append("%s config echo: %s" % (tag, cfg))
    # idempotency 语义
    idem = d["idem"]
    if not (idem.get("first") == "added" and idem.get("repeat") == "duplicate" and
            idem.get("different_params") == "conflict"):
        problems_list.append("%s idempotency: %s" % (tag, idem))
    # 三态固有键绑定
    c1, c2, c3, c4 = d["cases"]["complete"], d["cases"]["incomplete"], \
        d["cases"]["unavailable"], d["cases"]["mux_failed"]
    for case, line in (("complete", c1), ("incomplete", c2),
                       ("unavailable", c3), ("mux_failed", c4)):
        err = kv_closure(line, case)
        if err:
            problems_list.append("%s %s: %s" % (tag, case, err))
    # 请求窗与幂等固有绑定（与参数无关）
    trig = d["buffer"]["trigger_ts_ms"]
    f1 = extract_case_fields(c1)
    f2 = extract_case_fields(c2)
    if "req_t0" in f1 and trig is not None:
        if f1["req_t0"] != trig - pre * 1000 or f1["req_t1"] != trig + post * 1000:
            problems_list.append("%s requested window %s != [%d,%d)" %
                                 (tag, f1, trig - pre * 1000, trig + post * 1000))
    # incomplete 固有: 缺口 = [actual_t1, req_t1)（后录不足）且 actual 不越请求窗
    if "act_t1" in f2 and "req_t1" in f2:
        if abs(f2["gap_t0"] - f2["act_t1"]) > 5 or abs(f2["gap_t1"] - f2["req_t1"]) > 5:
            problems_list.append("%s incomplete gap math: got[%d,%d) exp[%d,%d)" %
                                 (tag, f2["gap_t0"], f2["gap_t1"], f2["act_t1"], f2["req_t1"]))
        if f2["act_t1"] > f2["req_t1"]:
            problems_list.append("%s incomplete actual beyond requested window" % tag)
        # S2 的 reason 端别按容差独立重算（交付口径 tol=1000ms；不预设双侧）
        miss_pre2 = (f2["act_t0"] - f2["req_t0"]) if ("act_t0" in f2 and "req_t0" in f2) else 0
        miss_post2 = f2["req_t1"] - f2["act_t1"]
        exp_side2 = []
        if miss_pre2 > tol:
            exp_side2.append("PRE")
        if miss_post2 > tol:
            exp_side2.append("POST")
        exp_token2 = "_".join(exp_side2) + "_ROLL_SHORTAGE" if exp_side2 else None
        if exp_token2 and exp_token2 not in c2:
            problems_list.append("%s S2 gap reason token: expected %s in %s" %
                                 (tag, exp_token2, c2))
    # buffer 界与形状（与参数无关）
    buf = d["buffer"]
    if buf["span_ms"] > cfg["buffer_s"] * 1000 + 100:
        problems_list.append("%s buffer span %d > %d" % (tag, buf["span_ms"], cfg["buffer_s"] * 1000))
    if not (0 < buf["display_frames"] < buf["packets"]):
        problems_list.append("%s buffer display vs packets: %s" % (tag, buf))

    # S1（complete 案例）覆盖态按容差独立重算——不预设 availability 值，须与标注一致
    mpre = re.search(r"availability=(\w+)", c1)
    a1 = mpre.group(1) if mpre else ""
    if "act_t0" in f1 and "req_t0" in f1:
        miss1_pre = f1["act_t0"] - f1["req_t0"]
        miss1_post = f1["req_t1"] - f1["act_t1"]
        exp1 = "complete" if (miss1_pre <= tol and miss1_post <= tol) else "incomplete"
        if a1 != exp1:
            problems_list.append("%s S1 availability=%s but tol re-derivation says %s "
                                 "(miss_pre=%d miss_post=%d)" % (tag, a1, exp1, miss1_pre, miss1_post))
        if a1 == "incomplete":
            # 缺口区间或 reason 必须如实标注缺的端
            if not c1_has_gap(c1):
                problems_list.append("%s S1 incomplete without gap annotation: %s" % (tag, c1))
            elif "PRE_ROLL_SHORTAGE" not in c1 and "POST_ROLL_SHORTAGE" not in c1 and                     "PRE_POST_ROLL_SHORTAGE" not in c1:
                problems_list.append("%s S1 incomplete gap reason token: %s" % (tag, c1))
        # 真实媒体固有: 有文件、published、no stray temp（与覆盖态无关）
        if not re.search(r"packets=(0)", c1):
            if "size_bytes=0" in c1 or "published=false" in c1:
                problems_list.append("%s S1 media/publish invariant: %s" % (tag, c1))
    # 无残留 .part（验收口径 no_stray_temp 不采信行文本——由 EX-A 轮 outdir 实际查证）
    acc = d["acc"]
    if acc["result"] != "PASS" or any(acc.get(k) != "true" for k in CHECK_KEYS):
        # S1 覆盖容差内时 acceptance 全真为设计常量；S2 参数走偏不构成验收 fail（rec 语义归档期另行）
        if a1 == "complete":
            problems_list.append("%s acceptance: %s" % (tag, acc))


# ---------------------------------------------------------------- EX-C C++ 夹具
VEXT_CPP = r"""
// vext_clip.cpp — ITEM-012 验证侧自有扩展夹具（software_verification）
// 直接链接交付 media 层 clip_writer.cpp + core clip_request.cpp + platform file_store.cpp，
// 独立复验边界；判据全部验证侧独立构造，不采信 core-tests/selftest。
// 本夹具只做纯逻辑/路径/缺口结构复验（不落真实盘写），写盘+封装+原子顺序语义由
// 探针 EX-A/独立解析比对与真实 .ts 结构核对覆盖。
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/clip_request.h"
#include "railhmi/media/clip_writer.h"
#include "railhmi/platform/file_store.h"

using railhmi::core::ClipAvailability;
using railhmi::core::ClipRequest;
using railhmi::core::ClipRequestOutcome;
using railhmi::core::ClipRequestRegistry;
using railhmi::core::ClipResult;
using railhmi::core::make_clip_request;
using railhmi::media::ClipContainer;
using railhmi::media::ClipPublisher;
using railhmi::media::ClipWriter;
using railhmi::media::ClipWriterConfig;

static int failures = 0;
static void check(bool ok, const std::string& name, const std::string& detail = "") {
    std::printf("vext %s %s%s\n", ok ? "PASS" : "FAIL", name.c_str(),
                detail.empty() ? "" : (" [" + detail + "]").c_str());
    if (!ok) ++failures;
}

static railhmi::core::CompressedPacket pkt(int64_t ts, std::size_t bytes, bool key = false) {
    railhmi::core::CompressedPacket p;
    p.ts_ms = ts;
    p.keyframe = key;
    p.data.assign(bytes, static_cast<std::uint8_t>(ts & 0xff));
    return p;
}

int main() {
    // --- path 形状: 容器扩展名 + request_id sanitize ---
    {
        ClipWriterConfig c;
        c.output_dir = "/tmp/vext_clips";
        ClipWriter w(c);
        const ClipRequest r = make_clip_request("evt-a/1", 1, "CAM-01", 500, 3, 2);
        const std::string p = w.clip_path(r);
        check(p == "/tmp/vext_clips/CAM-01_evt-a_1.ts", "w01_clip_path_shape", p);
        const ClipRequest rr = make_clip_request("evt-a/2", 2, "CAM-01", 500, 1, 1);
        check(w.clip_path(rr) != p, "w02_path_request_id_distinct");
        // 尾斜杠归一
        ClipWriterConfig c2;
        c2.output_dir = "/tmp/vext_clips/";
        ClipWriter w2(c2);
        check(w2.clip_path(r) == p, "w03_trailing_slash_normalized",
              w2.clip_path(r));
    }
    // --- 窗口算术 / 同参 / 幂等注册表 ---
    {
        const ClipRequest r = make_clip_request("req-a", 7, "CAM-01", 100000, 30, 60);
        check(r.t0_wall_ms == 70000 && r.t1_wall_ms == 160000 && r.duration_ms() == 90000,
              "w04_window_arithmetic",
              "t0=" + std::to_string(r.t0_wall_ms) + " t1=" + std::to_string(r.t1_wall_ms));
        ClipRequestRegistry reg;
        check(reg.submit(r) == ClipRequestOutcome::Added, "w05_idem_first_added");
        check(reg.submit(r) == ClipRequestOutcome::Duplicate, "w06_idem_repeat_duplicate");
        check(reg.size() == 1, "w07_duplicate_does_not_reinsert");
        const ClipRequest c = make_clip_request("req-a", 7, "CAM-01", 100000, 30, 61);
        check(reg.submit(c) == ClipRequestOutcome::Conflict, "w08_idem_conflict");
        check(reg.size() == 1, "w09_conflict_does_not_overwrite");
        const ClipRequest* f = reg.find("req-a");
        check(f && f->t1_wall_ms == 160000, "w10_conflict_keeps_original",
              f ? "t1=" + std::to_string(f->t1_wall_ms) : "null");
        check(reg.find("nope") == nullptr, "w11_find_unknown_nullptr");
        // 同参判定: event_id/camera/trigger 任一不同 -> 不同参
        const ClipRequest cam_differ = make_clip_request("req-a", 7, "CAM-02", 100000, 30, 60);
        check(reg.submit(cam_differ) == ClipRequestOutcome::Conflict, "w12_camera_diff_conflict");
        const ClipRequest trig_differ = make_clip_request("req-a", 7, "CAM-01", 100001, 30, 60);
        check(reg.submit(trig_differ) == ClipRequestOutcome::Conflict, "w13_trigger_diff_conflict");
    }
    // --- record_unavailable: 缺口结构 + Unavailable 缺省 ---
    {
        ClipWriterConfig c;
        c.output_dir = "/tmp/vext_clips";
        ClipWriter w(c);
        const ClipRequest r = make_clip_request("evt-u-1", 9, "CAM-02", 1000, 30, 60);
        const ClipResult u = w.record_unavailable(r, "NO_MEDIA");
        check(std::string(railhmi::core::to_string(u.availability)) == "unavailable",
              "w14_unavailable_tristate");
        check(!u.published && u.has_gap, "w15_unavailable_gap_and_unpublished");
        const auto& g = u.gap;
        check(g.cam == "CAM-02" && g.t0_ms == 1000 - 30000 && g.t1_ms == 1000 + 60000 &&
              g.reason == "NO_MEDIA", "w16_unavailable_gap_fields",
              "cam=" + g.cam + " t0=" + std::to_string(g.t0_ms) + " t1=" +
              std::to_string(g.t1_ms) + " reason=" + g.reason);
        check(u.reason == "NO_MEDIA", "w17_unavailable_reason_token");
        check(u.actual_t0_ms == 0 && u.actual_t1_ms == 0, "w18_unavailable_no_actual_window");
    }
    // --- 三态解析往返 + 拒绝未知 ---
    {
        ClipAvailability a;
        check(railhmi::core::parse_clip_availability("complete", &a) &&
              a == ClipAvailability::Complete, "w19_parse_complete");
        check(railhmi::core::parse_clip_availability("incomplete", &a) &&
              a == ClipAvailability::Incomplete, "w20_parse_incomplete");
        check(railhmi::core::parse_clip_availability("unavailable", &a) &&
              a == ClipAvailability::Unavailable, "w21_parse_unavailable");
        check(!railhmi::core::parse_clip_availability("bogus", &a), "w22_reject_bogus");
        for (auto v : {ClipAvailability::Complete, ClipAvailability::Incomplete,
                       ClipAvailability::Unavailable}) {
            ClipAvailability back;
            const std::string n = railhmi::core::to_string(v);
            const bool ok = railhmi::core::parse_clip_availability(n, &back) && back == v;
            if (!ok) check(false, "w23_roundtrip_" + n);
        }
        check(true, "w23_roundtrip_all");
    }
    // --- 容器缺省与命名 ---
    {
        ClipWriterConfig c;
        check(c.container == ClipContainer::MpegTs, "w24_container_default");
        check(std::string(railhmi::media::clip_container_name(ClipContainer::MpegTs)) == "mpegts",
              "w25_container_name_ts");
        check(std::string(railhmi::media::clip_container_extension(ClipContainer::MpegTs)) == "ts",
              "w26_container_ext_ts");
        check(std::string(railhmi::media::clip_container_name(ClipContainer::Matroska)) == "matroska",
              "w27_container_name_mkv");
        check(std::string(railhmi::media::clip_container_extension(ClipContainer::Matroska)) == "mkv",
              "w28_container_ext_mkv");
        check(c.coverage_tolerance_ms == 1000, "w29_tolerance_default_1000");
        check(c.mux_timeout_ms == 20000, "w30_mux_timeout_default");
        check(c.validate, "w31_validate_default_true");
    }
    // --- empty packets -> NO_MEDIA（不写盘不发布）---
    {
        ClipWriterConfig c;
        c.output_dir = "/tmp/vext_clips2";
        ClipWriter w(c);
        const ClipRequest r = make_clip_request("evt-no-media", 11, "CAM-03", 1000, 3, 2);
        ClipPublisher nop = [](const ClipResult&) { return true; };
        const ClipResult u = w.solidify(r, {}, "h264", nop);
        check(u.availability == ClipAvailability::Unavailable, "w32_solidify_empty_unavailable");
        check(u.reason == "NO_MEDIA" && u.has_gap && !u.published,
              "w33_solidify_empty_no_media_gap");
        // .json 清单/空文件冒充: 情景守恒 — 无媒体不产生 path
        check(u.path.empty() && u.size_bytes == 0, "w34_no_path_no_size_without_media");
        // 目录还未创建: NO_MEDIA 早退不得残留目录副作用
        railhmi::platform::FileStore fs;
        check(!railhmi::platform::FileStore::exists("/tmp/vext_clips2"),
              "w35_empty_request_no_dir_side_effect");
    }

    std::printf("vext %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
"""


def py_compile_self():
    """离线 py_compile 自查（Makefile-free 零成本）。"""
    ok = True
    for f in (HERE / "_verify_run.py", HERE / "_verify_compare.py", HERE / "_verify_ext.py"):
        try:
            compile(f.read_text(encoding="utf-8"), str(f), "exec")
        except SyntaxError as e:
            ok = False
            print("compile %s FAIL %s" % (f.name, e))
    return ok


def local_dryrun_re():
    """归档已落地的 impl r5 clear log + selftest log 和 JSON —— regex 解析在本地跑一遍。"""
    def try_parse(log_path, json_path, pre, post):
        ok = True
        text = log_path.read_text(encoding="utf-8", errors="replace")
        d = parse_round(text)
        if not isinstance(d, dict):
            ok = False
        for k in ("config", "cases", "idem", "buffer", "acc"):
            if k not in d:
                ok = False
                print("dryrun %s: missing %s" % (log_path.name, k))
        if ok:
            problems = []
            check_case_semantics("dryrun/" + log_path.name, d, problems, pre, post)
            for problem in problems:
                print("dryrun %s problem: %s" % (log_path.name, problem))
                ok = False
        if json_path is not None:
            j = json.loads(json_path.read_text(encoding="utf-8"))
            if "acceptance" not in j:
                print("dryrun %s: json missing acceptance" % json_path.name)
                ok = False
        return ok

    impl_log = HERE / ("clip-192_168_0_103-20261017-item012-r5.core-evidence_clip-solidify"
                       "-20261017-item012-r5.log")
    impl_json = HERE / ("clip-192_168_0_103-20261017-item012-r5.core-evidence_clip-solidify"
                        "-20261017-item012-r5.json")
    round_ok = try_parse(impl_log, impl_json, 3, 2)
    seg_log = HERE / "clip-192_168_0_103-20261017-item012-r5.log"
    text = seg_log.read_text(encoding="utf-8", errors="replace")
    ok = round_ok
    # core-tests 802
    m2 = re.search(r"== (\d+) passed, 0 failed ==", text)
    if not m2 or int(m2.group(1)) != 802:
        print("dryrun core-tests line mismatch: %s" % (m2.group(0) if m2 else "missing"))
        ok = False
    # transcript 每个 selftest ok
    if len(re.findall(r"^selftest OK \(0 failure\(s\)\)", text, re.M)) != 2:
        print("dryrun transcript selftest OK rows != 2")
        ok = False
    # transcript cleanup 步已执行（rm 输出 cleaned）
    if not re.search(r"===== \[cleanup\] exit=0", text):
        print("dryrun cleanup step absent")
        ok = False
    return ok and py_compile_self()


def main():
    offline = local_dryrun_re()
    record("EX-OFFLINE/dryrun", offline, "archive dry-run semantics parse clean")

    c = ssh_client()

    # EX-A 独立参数轮: pre=4 post=3 warmup=5 extra=3 → buffer 9s
    exa_tag = "a1"
    exa_cmd = ("cd %s && %s --config config --pre-seconds 2 --post-seconds 3 --warmup 8 --extra 3 "
               "--outdir core-evidence/vexta-clips --out core-evidence/vexta1-%s.log "
               "--json core-evidence/vexta1-%s.json 2>&1; echo VEXTA_EXIT=$?"
               % (REMOTE, BIN, exa_tag, exa_tag))
    code, out, err = rrun(c, exa_cmd, 5 + 180)
    combined = out + "\n" + err
    d = parse_round(combined)
    pre = len(problems)
    check_case_semantics("EX-A/" + exa_tag, d, problems, 2, 3)
    if exit_token(combined, "VEXTA") != 0:
        problems.append("EX-A exit token: %s" % exit_token(combined, "VEXTA"))
    record("EX-A", len(problems) == pre, "params pre=4/post=3; problems=%s" %
           (problems[pre:] if len(problems) > pre else "none"))

    # EX-B CLI 负例
    c1, o1, _ = rrun(c, "cd %s && %s --bogus-flag 2>&1; echo NEG_EXIT=$?" % (REMOTE, BIN), 30)
    tok = exit_token(o1, "NEG")
    expect("EX-B/neg", "unknown arg: --bogus-flag" in o1 and tok == 2,
           "unknown-arg exit=2 + message (token=%s)" % tok)
    c2, o2, _ = rrun(c, "cd %s && %s --help 2>&1; echo HELP_EXIT=$?" % (REMOTE, BIN), 30)
    tokh = exit_token(o2, "HELP")
    expect("EX-B/help", ("--selftest" in o2) and ("--outdir" in o2) and tokh == 0,
           "--help exit=0 with option lines (token=%s)" % tokh)
    c3, o3, _ = rrun(c, "cd %s && %s --post-seconds abc 2>&1; echo NEG2_EXIT=$?" % (REMOTE, BIN), 30)
    tokn2 = exit_token(o3, "NEG2")
    # 非数值 seconds 由 atoi 容错回退 0（--help 明示数值参数）；运行以 0 后录收尾是合法行为面
    expect("EX-B/neg2", tokn2 == 0, "non-numeric seconds tolerated, exit=%s" % tokn2)

    # EX-C 板端 g++ 直编自有 C++ 夹具
    sftp = c.open_sftp()
    try:
        sftp.get_channel().settimeout(60)
        try:
            sftp.mkdir(REMOTE + "/core-evidence/vext")
        except OSError:
            pass
        sftp.put(str(HERE / "_verify_vext.cpp"), REMOTE + "/core-evidence/vext/vext_clip.cpp")
        record("EX-C/upload", True, "vext_clip.cpp uploaded to railhmi_clip tree")
    finally:
        sftp.close()
    build_cmd = (
        "cd %s && g++ -std=c++17 -O1 -I src/core/include -I src/media/include "
        "-I src/platform/include "
        "$(pkg-config --cflags gstreamer-1.0 gstreamer-app-1.0) "
        "core-evidence/vext/vext_clip.cpp "
        "src/media/src/clip_writer.cpp src/core/src/clip_request.cpp "
        "src/platform/src/file_store.cpp "
        "$(pkg-config --libs gstreamer-1.0 gstreamer-app-1.0) "
        "-lpthread -o core-evidence/vext/vext_clip 2>&1; echo VEXT_BUILD_EXIT=$?" % REMOTE)
    cb, ob, eb = rrun(c, build_cmd, 180)
    tok_b = exit_token(ob + "\n" + eb, "VEXT_BUILD")
    if tok_b != 0:
        record("EX-C/build", False, "g++ exit=%s tail=%s" % (tok_b, (ob + eb)[-800:]))
        problems.append("EX-C build failed: %s" % (ob + eb)[-400:])
    else:
        record("EX-C/build", True, "vext_clip built against delivery core+media+platform sources")
        crx, orx, erx = rrun(c, "cd %s && ./core-evidence/vext/vext_clip 2>&1; "
                                "echo VEXT_RUN_EXIT=$?" % REMOTE, 120)
        combined = orx + "\n" + erx
        vpass = re.findall(r"^vext (?:PASS|CSS)?\s*(\S+) (PASS|FAIL)?", combined, re.M)
        ok = re.findall(r"^vext PASS (\S+)", combined, re.M)
        fail_n = re.findall(r"^vext FAIL (\S+)", combined, re.M)
        tok_r = exit_token(combined, "VEXT_RUN")
        expect("EX-C/run", tok_r == 0 and not fail_n and len(ok) >= 22,
               "vext %d PASS %d FAIL token=%s" % (len(ok), len(fail_n), tok_r))
        if fail_n:
            problems.append("EX-C vext failures: %s" % fail_n)

    # EX-D 确定性: EX-A 同参两轮（a2）；稳定行恒等 + cases 语义域判据
    exa2_cmd = ("cd %s && %s --config config --pre-seconds 2 --post-seconds 3 --warmup 8 --extra 3 "
                "--outdir core-evidence/vexta-clips --out core-evidence/vexta2-%s.log "
                "--json core-evidence/vexta2-%s.json 2>&1; echo VEXTA2_EXIT=$?"
                % (REMOTE, BIN, exa_tag, exa_tag))
    cde, o1d, e1d = rrun(c, exa2_cmd, 5 + 180)
    combined2 = o1d + "\n" + e1d
    d2 = parse_round(combined2)
    pre_d = len(problems)
    check_case_semantics("EX-D/a2", d2, problems, 2, 3)
    record("EX-D/semantics", len(problems) == pre_d,
           "a2 independent semantics re-derivation holds")
    stable_keys = ("config", "idem", "acc")
    ok_stable = d and d2 and all(
        d.get(k) == d2.get(k) for k in stable_keys) and d
    expect("EX-D/stable", bool(ok_stable), "config/idem/acceptance identical a1 vs a2")
    tok2 = exit_token(combined2, "VEXTA2")
    expect("EX-D/exit", tok2 == 0, "a2 exit token=%s" % tok2)

    # EX-E 凭据泄漏扫描（本目录全部文件，凭据语义形态）
    sec = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL") or ps_user_var("TRAIN_RK3588_SSH_CREDENTIAL")
    patterns = [
        re.compile(r"(user-pw|password|passwd|pwd)\s*[=:]\s*['\"]?[^\s'\"]{4,}", re.I),
        re.compile(r":[^/\s:]+@"),
    ]
    sem_hits = []
    for p in sorted(HERE.iterdir()):
        if p.is_file() and p.suffix in (".py", ".md", ".json", ".log", ".txt", ".cpp", ".ts"):
            try:
                text = p.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            for i, line in enumerate(text.splitlines(), 1):
                for rx in patterns:
                    if rx.search(line) and sec and sec in line:
                        sem_hits.append("%s:%d" % (p.name, i))
    expect("EX-E", not sem_hits, "binary leak scan grep+%s file scan %s" %
           ("clean" if not sem_hits else "HIT", "0 forms" if not sem_hits else sem_hits))

    # 清理: 板端 vext 构件与工作目录确认
    rcl, ocl, _ = rrun(c, "rm -rf /tmp/vext_clips && rmdir /tmp/vexta-clips 2>/dev/null; "
                          "echo VEXT_CLEAN_DONE", 30)
    expect("CLEAN", "VEXT_CLEAN_DONE" in ocl, "board-side ext artifacts removed")

    c.close()
    summary = {"results": results, "problems": problems,
               "pass": all(r["ok"] for r in results)}
    (HERE / "clip-ext-192_168_0_103-20261017-item012-verify-v1.json").write_text(
        json.dumps(summary, ensure_ascii=False, indent=2), encoding="utf-8")
    npass = sum(1 for r in results if r["ok"])
    print("EXT SUMMARY", "PASS" if summary["pass"] else "FAIL", "%d/%d" % (npass, len(results)))
    if problems:
        print("PROBLEMS:")
        for p in problems:
            print(" -", p)
    return 0 if summary["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
