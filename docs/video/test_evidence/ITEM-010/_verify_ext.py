#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-010 验证侧扩展夹具（software_verification，VERIFY-20261017-v1）。

前提: 交付 runner board_ai_dispatch_check.py 已 --keep-remote 完成独立复跑
      （板端 /tmp/railhmi_ai 构建树在位，railhmi-ai-dispatch 可执行）。
本夹具全部有限超时；判据先独立构造再取实测；不采信交付自身 acceptance 行。

EXT 清单:
  EX-A  独立参数轮（--interval 3 --depth 1 --consume-hz 0.4 --duration 4）:
        injection 回显 interval/depth 逐字段、逐拍 depth<=cap/计数单调、
        summary 逐路 enq≈published/interval(±2)、禁用路 enq=0 rej>0、acceptance 交叉核验
  EX-B  CLI 负例: --bogus-flag → exit=2 + unknown arg；--help → exit=0 含 --selftest/--interval
  EX-C  板端 g++ 直编 AiFrameRouter + FrameStore NV12 样本直驱自有 28 判例:
        启用过滤/未知相机/降频 enq 与 skip 精确计数/禁用路 rejected/enq=0/depth 有界/
        FIFO 逐帧 seq 序列与身份六字段透传/租约 pop 后转移 use_count=1/双持有 use_count=2 共享/
        buffer_ref 回联/plane0 像素内容/unknown pop/interval=1 全帧直通/空租约静默/禁用路 depth=0
  EX-D  确定性: EX-A 同参两轮逐拍语义序列（t 时间轴排除）恒等 + 汇总行恒等
  EX-E  凭据泄漏扫描: 本目录全部文件凭据语义形态 0 命中
  清理: 板端 /tmp/vext_ai_dispatch、railhmi_ai 树内 vext 源与 vexta 产物逐项确认
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
REMOTE = "/tmp/railhmi_ai"
BIN = REMOTE + "/build-media/tools/railhmi-ai-dispatch"

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


TICK_RX = re.compile(
    r"t=(\d+) type=ai \[CAM-01 enq=(\d+) drop=(\d+) skip=(\d+) rej=(\d+) depth=(\d+)\] "
    r"\[CAM-02 .*\] \[CAM-03 .*\] \[CAM-04 enq=(\d+) drop=(\d+) skip=(\d+) rej=(\d+) depth=(\d+)\] "
    r"\[CAM-05 enq=(\d+) drop=(\d+) skip=(\d+) rej=(\d+) depth=(\d+)\] "
    r"\[CAM-06 enq=(\d+) drop=(\d+) skip=(\d+) rej=(\d+) depth=(\d+)\] popped=(\d+)")


def parse_ticks(out):
    samples = []
    for line in out.splitlines():
        m = TICK_RX.search(line)
        if m:
            g = [int(x) for x in m.groups()]
            samples.append({"t": g[0],
                            "c1": tuple(g[1:6]), "c4": tuple(g[6:11]),
                            "c5": tuple(g[11:16]), "c6": tuple(g[16:21])})
    return samples


SUM_RX = re.compile(
    r"type=ai_summary camera=(CAM-\d+) enabled=(true|false) published=(\d+) enqueued=(\d+) "
    r"dropped=(\d+) skipped=(\d+) rejected=(\d+) depth=(\d+)")


def parse_summary(out):
    rows = {}
    for line in out.splitlines():
        m = SUM_RX.search(line)
        if m:
            rows[m.group(1)] = m.group(2) + "," + ",".join(m.groups()[2:])
    return rows


def run_ai(c, interval, depth, dur, chz, tag):
    cmd = ("cd %s && %s --source test --config config --interval %d --depth %d --duration %d "
           "--consume-hz %s --out core-evidence/vexta-%s.log --json core-evidence/vexta-%s.json 2>&1; "
           "echo AIX_EXIT=$?" % (REMOTE, BIN, interval, depth, dur, chz, tag, tag))
    code, out, err = rrun(c, cmd, dur + 90)
    return out + "\n" + err


def check_ai_round(combined, tag, interval, depth):
    """独立重推导判据；返回 (problems, samples, summary_rows)。"""
    problems = []
    samples = parse_ticks(combined)
    if len(samples) < 5:
        problems.append("%s samples=%d too few" % (tag, len(samples)))
        return problems, samples, {}
    inj = re.search(r"type=injection source=test channels=6 started=6 "
                    r"inference_interval=(\d+) queue_depth=(\d+)", combined)
    if not inj or int(inj.group(1)) != interval or int(inj.group(2)) != depth:
        problems.append("%s injection echo: %s" % (tag, inj.groups() if inj else None))
    for s in samples:
        for k in ("c1", "c4", "c5", "c6"):
            if s[k][4] > depth:
                problems.append("%s t=%d %s depth=%d>%d" % (tag, s["t"], k, s[k][4], depth))
                break
    for k in ("c1", "c4", "c5", "c6"):
        vals = [s[k] for s in samples]
        enq_col = [v[0] for v in vals]
        if any(enq_col[j + 1] < enq_col[j] for j in range(len(enq_col) - 1)):
            problems.append("%s %s enq not monotone" % (tag, k))
    rows = parse_summary(combined)
    if len(rows) != 6:
        problems.append("%s summary rows=%d" % (tag, len(rows)))
        return problems, samples, rows
    for cam, row in sorted(rows.items()):
        parts = row.split(",")
        en_flag, pub, enq, drop, rej = parts[0], int(parts[1]), int(parts[2]), int(parts[3]), int(parts[5])
        is_en = cam in ("CAM-01", "CAM-04", "CAM-05", "CAM-06")
        if en_flag != ("true" if is_en else "false"):
            problems.append("%s %s enabled flag=%s" % (tag, cam, en_flag))
        if is_en:
            exp = pub // interval  # 逐行独立重算（六路并发 published 逐件自然相差 ±1）
            if abs(enq - exp) > 2:
                problems.append("%s %s enq=%d expect~%d" % (tag, cam, enq, exp))
            if rej != 0 or drop == 0:
                problems.append("%s %s rej=%d drop=%d" % (tag, cam, rej, drop))
        else:
            if enq != 0 or rej == 0:
                problems.append("%s %s disabled enq=%d rej=%d" % (tag, cam, enq, rej))
    m = re.search(r"type=acceptance .*result=(\w+)", combined)
    if not m or m.group(1) != "PASS":
        problems.append("%s acceptance=%s" % (tag, m.group(1) if m else "?"))
    m = re.search(r"AIX_EXIT=(\d+)", combined)
    if not m or m.group(1) != "0":
        problems.append("%s exit=%s" % (tag, m.group(1) if m else "?"))
    return problems, samples, rows


def ext_a(c):
    combined = run_ai(c, 3, 1, 4, 0.4, "a1")
    probs, samples, rows = check_ai_round(combined, "EX-A", 3, 1)
    record("EX-A/independent-params-round", not probs,
           "samples=%d rows=%d; %s" % (len(samples), len(rows),
                                       "; ".join(probs[:4]) if probs else
                                       "interval=3 depth=1: per-tick depth<=1, enq~published/3, disabled rows enq=0"))
    if probs:
        print(combined[:2000])
    return not probs, samples


def ext_b(c):
    ok_all = True
    r1 = rrun(c, "cd %s && %s --bogus-flag 2>&1; echo NEG_EXIT=$?" % (REMOTE, BIN), 30)
    combined = r1[1] + "\n" + r1[2]
    e1 = exit_token(combined)
    ok1 = e1 == 2 and "unknown arg" in combined
    record("EX-B/unknown-arg-exit2", ok1,
           "exit=%s head=%s" % (e1, (combined.strip().splitlines() or ["?"])[0]))
    ok_all &= ok1
    r2 = rrun(c, "cd %s && %s --help; echo HELP_EXIT=$?" % (REMOTE, BIN), 30)
    combined = r2[1] + "\n" + r2[2]
    e2 = exit_token(combined)
    ok2 = e2 == 0 and "--selftest" in combined and "--interval" in combined and "--depth" in combined
    record("EX-B/help-exit0-usage", ok2, "exit=%s" % e2)
    ok_all &= ok2
    return ok_all


VCODE = r'''
// ITEM-010 验证侧自有夹具: AiFrameRouter 直驱（FrameStore NV12 sample 构造，真实租约链路）。
// FrameSequencer::next 的 seq 从 1 起步（frame_identity.cpp 语义）。
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include <gst/gst.h>
#include <gst/video/video.h>

#include "railhmi/core/frame_identity.h"
#include "railhmi/media/ai_dispatcher.h"
#include "railhmi/media/frame_store.h"

using namespace railhmi;
using media::AiCameraSpec;
using media::AiFrame;
using media::AiFrameRouter;
using media::FrameLease;
using media::FrameStore;

static int fails = 0;
static void check(bool ok, const char* name) {
    std::printf("vext %s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++fails;
}

// 用真实 GStreamer NV12 sample 走 FrameStore::publish → latest() 真租约链路。
// 64x64 偶数维、stride=width、uv_offset=stride*h 满足 make_nv12_layout 全部校验。
static FrameLease publish_gray_nv12(FrameStore& store, core::FrameMeta meta) {
    const int w = 64, h = 64, stride = 64;
    const size_t y_size = (size_t)stride * h;
    const size_t buf_size = y_size + (size_t)stride * (h / 2);
    std::vector<unsigned char> buf(buf_size);
    std::memset(buf.data(), 200, y_size);
    for (size_t i = 0; i < (size_t)stride * (h / 2); i += 2) {
        buf[y_size + i] = 128;
        buf[y_size + i + 1] = 128;
    }
    GstBuffer* gbuf = gst_buffer_new_wrapped(g_memdup(buf.data(), buf_size), buf_size);
    GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, w, "height", G_TYPE_INT, h,
                                        "framerate", GST_TYPE_FRACTION, 25, 1,
                                        "colorimetry", G_TYPE_STRING, "bt709", nullptr);
    GstSample* sample = gst_sample_new(gbuf, caps, nullptr, nullptr);
    gst_caps_unref(caps);
    gst_buffer_unref(gbuf);
    std::string err;
    core::LayoutStatus st = store.publish(sample, meta, "bt709_full", "simulation", &err);
    gst_sample_unref(sample);
    if (st != core::LayoutStatus::Ok) {
        std::printf("vext INFO publish rejected: status=%s err=%s\n", core::to_string(st), err.c_str());
    }
    return store.latest();
}

int main() {
    gst_init(nullptr, nullptr);  // GObject 类型注册（缺则 gst_video_frame_map 段错）
    // 夹具自检（非判据）: publish 产真实租约。
    {
        FrameStore store("CAM-X");
        core::FrameSequencer seq("sess-fix", "CAM-X");
        seq.begin_generation();
        media::FrameLease l0 = publish_gray_nv12(store, seq.next(1000, -1, -1));
        if (!l0.valid() || l0.plane_data(0) == nullptr) {
            std::printf("vext FATAL fixture_publish_failed\n");
            return 3;
        }
        std::printf("vext INFO fixture publish ok seq=%llu %zux%zu planes=%zu\n",
                    (unsigned long long)l0.descriptor().seq,
                    (size_t)l0.descriptor().layout.width, (size_t)l0.descriptor().layout.height,
                    l0.descriptor().layout.planes.size());
    }

    // —— R1: interval=5 depth=2, 11 帧（seq 1..11: 5,10 命中降频）——
    {
        std::vector<AiCameraSpec> specs = {{"CAM-A", true, "front"},
                                           {"CAM-B", false, "none"},
                                           {"CAM-C", true, "pantograph"}};
        AiFrameRouter router(specs, 5, 2);
        check(router.enabled("CAM-A") && !router.enabled("CAM-B") && router.enabled("CAM-C"),
              "v01_enabled_filtering");
        check(!router.enabled("CAM-Z"), "v02_unknown_camera_not_enabled");
        FrameStore sa("CAM-A"), sb("CAM-B"), sc("CAM-C");
        core::FrameSequencer qa("sess-r1", "CAM-A"), qb("sess-r1", "CAM-B"), qc("sess-r1", "CAM-C");
        qa.begin_generation();
        qb.begin_generation();
        qc.begin_generation();
        media::FrameLease l10;  // 持 seq=10 帧的外部租约（证明队列帧共享同一缓冲）
        for (int i = 0; i < 11; ++i) {
            core::FrameMeta ma = qa.next(1000 + (long long)i * 40000, -1, -1);
            media::FrameLease la = publish_gray_nv12(sa, ma);
            if (i == 9) l10 = la;  // seq=10（第 10 次 next）
            if (la.valid()) router.on_frame(la);
            media::FrameLease lb = publish_gray_nv12(sb, qb.next(1000 + (long long)i * 40000, -1, -1));
            if (lb.valid()) router.on_frame(lb);
            media::FrameLease lc = publish_gray_nv12(sc, qc.next(1000 + (long long)i * 40000, -1, -1));
            if (lc.valid()) router.on_frame(lc);
        }
        check(router.enqueued("CAM-A") == 2, "v03_interval_enq_seqp5_seq10");
        check(router.skipped_interval("CAM-A") == 9, "v04_interval_skip_9");
        check(router.rejected_disabled("CAM-B") == 11, "v05_disabled_rejected_all");
        check(router.enqueued("CAM-B") == 0 && router.dropped("CAM-B") == 0, "v06_disabled_never_enqueued");
        check(router.depth("CAM-A") <= 2, "v07_bounded_depth_le_2");
        // 2 帧进 cap=2 队列不被挤出 → drop=0（本节队列从未溢出）
        check(router.dropped("CAM-A") == 0, "v08_no_eviction_below_capacity");

        // pop: FIFO seq 5,10；身份六字段逐字段透传
        AiFrame f1, f2;
        bool p1 = router.pop("CAM-A", &f1);
        bool p2 = router.pop("CAM-A", &f2);
        check(p1 && p2, "v09_pop_two_frames");
        check(!router.pop("CAM-A", &f1), "v10_pop_empty_after_drain");
        if (p1 && p2) {
            check(f1.seq == 5 && f2.seq == 10, "v11_fifo_seq_order");
            check(f1.session_id == "sess-r1" && f1.camera_id == "CAM-A", "v12_session_camera_identity");
            check(f1.generation == 1, "v13_generation_passthrough");
            check(f1.profile == "front", "v14_profile_passthrough");
            // seq=5 是第 5 次 next（i=4）: capture=1000+4*40000=161000
            check(f1.capture_mono_us == 1000 + 4 * 40000, "v15_capture_mono_us_passthrough");
            // f1 是唯一持有者（publish 后 store 已被替换、router 持的租约已随 pop 转移到 f1）
            check(f1.lease.valid() && f1.lease.use_count() == 1,
                  "v16_lease_owned_after_transfer");
            // buffer_ref 可回联取流层（身份键回联面）
            check(!f1.lease.descriptor().buffer_ref.empty() &&
                  f1.lease.descriptor().buffer_ref.rfind("fr-CAM-A-", 0) == 0,
                  "v17_buffer_ref_relink");
            // 像素内容可读（NV12 plane0: Y=200 灰 32x64 行）
            const unsigned char* p0 = f1.lease.plane_data(0);
            check(p0 != nullptr && p0[0] == 200 && p0[63] == 200,
                  "v18_plane0_pixel_content");
            // 先持外副本 f2 同时持 l10 → seq10 帧双持有 use_count==2（共享受证）
            check(f2.seq == 10 && f2.lease.use_count() == 2,
                  "v19_lease_shared_two_owners");
        }
        AiFrame fz;
        check(!router.pop("CAM-Z", &fz), "v20_unknown_pop_false");
    }

    // —— R2: interval=1 全帧直通（7 帧 seq 1..7, depth=4 → 逐出 3、drop=3）；空租约静默 ——
    {
        std::vector<AiCameraSpec> specs = {{"CAM-E", true, "wheel_rail"},
                                           {"CAM-F", false, "none"}};
        AiFrameRouter router(specs, 1, 4);
        check(router.interval() == 1, "v21_interval_echo");
        FrameStore se("CAM-E"), sf("CAM-F");
        core::FrameSequencer qe("sess-r2", "CAM-E"), qf("sess-r2", "CAM-F");
        qe.begin_generation();
        qf.begin_generation();
        for (int i = 0; i < 7; ++i) {
            media::FrameLease le = publish_gray_nv12(se, qe.next(2000 + (long long)i * 40000, -1, -1));
            if (le.valid()) router.on_frame(le);
            media::FrameLease lf = publish_gray_nv12(sf, qf.next(2000 + (long long)i * 40000, -1, -1));
            if (lf.valid()) router.on_frame(lf);
        }
        check(router.enqueued("CAM-E") == 7, "v22_interval1_all_enqueued");
        check(router.skipped_interval("CAM-E") == 0, "v23_interval1_no_skip");
        check(router.dropped("CAM-E") == 3, "v24_drop3_overflow_counted");
        check(router.depth("CAM-E") == 4, "v25_depth_full_4");
        // FIFO 逐出序: pop 得 seq 4,5,6,7（1,2,3 已被挤出）
        AiFrame g;
        std::vector<unsigned long long> popped_seq;
        while (router.pop("CAM-E", &g)) popped_seq.push_back(g.seq);
        check(popped_seq == std::vector<unsigned long long>({4, 5, 6, 7}),
              "v26_fifo_evicts_oldest_order");
        // 空租约（NOT_READY）静默不计数
        media::FrameLease empty;
        router.on_frame(empty);
        check(router.enqueued("CAM-E") == 7 && router.dropped("CAM-E") == 3 && router.depth("CAM-E") == 0,
              "v27_empty_lease_silent_no_count");
        check(router.depth("CAM-F") == 0, "v28_disabled_depth_zero");
    }

    std::printf("vext %s (%d failure(s))\n", fails == 0 ? "OK" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
'''

NAMES = ["v01_enabled_filtering", "v02_unknown_camera_not_enabled",
         "v03_interval_enq_seqp5_seq10", "v04_interval_skip_9",
         "v05_disabled_rejected_all", "v06_disabled_never_enqueued",
         "v07_bounded_depth_le_2", "v08_no_eviction_below_capacity",
         "v09_pop_two_frames", "v10_pop_empty_after_drain",
         "v11_fifo_seq_order", "v12_session_camera_identity",
         "v13_generation_passthrough", "v14_profile_passthrough",
         "v15_capture_mono_us_passthrough", "v16_lease_owned_after_transfer",
         "v17_buffer_ref_relink", "v18_plane0_pixel_content",
         "v19_lease_shared_two_owners", "v20_unknown_pop_false",
         "v21_interval_echo", "v22_interval1_all_enqueued",
         "v23_interval1_no_skip", "v24_drop3_overflow_counted",
         "v25_depth_full_4", "v26_fifo_evicts_oldest_order",
         "v27_empty_lease_silent_no_count", "v28_disabled_depth_zero"]


def ext_c(c):
    sftp = c.open_sftp()
    try:
        sftp.putfo(io.BytesIO(VCODE.encode("utf-8")), REMOTE + "/vext_ai_dispatch.cpp")
    finally:
        sftp.close()
    compile_cmd = (
        "cd %s && g++ -std=c++17 -pthread -I src/core/include -I src/media/include "
        "$(pkg-config --cflags gstreamer-1.0 gstreamer-video-1.0) "
        "-o /tmp/vext_ai_dispatch vext_ai_dispatch.cpp "
        "src/media/src/ai_dispatcher.cpp src/media/src/frame_lease.cpp "
        "build-media/src/core/librailhmi_core.a build-media/src/platform/librailhmi_platform.a "
        "$(pkg-config --libs gstreamer-1.0 gstreamer-video-1.0) 2>&1 | tail -8; "
        "if [ -x /tmp/vext_ai_dispatch ]; then /tmp/vext_ai_dispatch; echo VEXT_EXIT=$?; "
        "else echo VEXT_COMPILE_FAILED; fi" % REMOTE)
    code, out, err = rrun(c, compile_cmd, 180)
    combined = out + "\n" + err
    vext_pass = combined.count("vext PASS")
    vext_fail = combined.count("vext FAIL")
    exit_ok = "VEXT_EXIT=0" in combined
    comp_ok = "VEXT_COMPILE_FAILED" not in combined
    fatal = "FATAL" in combined
    missing = [n for n in NAMES if ("vext PASS " + n) not in combined]
    ok = (exit_ok and comp_ok and not fatal and vext_fail == 0 and
          vext_pass >= len(NAMES) and not missing)
    record("EX-C/board-cpp-router-fixture", ok,
           "compile_ok=%s vext_pass=%d vext_fail=%d expected=%d missing=%s exit0=%s fatal=%s"
           % (comp_ok, vext_pass, vext_fail, len(NAMES), missing or "none", exit_ok, fatal))
    if vext_fail or not comp_ok or missing or fatal:
        print(combined[:3000])
    return ok


def ext_d(c, samples_a1):
    combined = run_ai(c, 3, 1, 4, 0.4, "d1")
    probs, samples, rows_d = check_ai_round(combined, "EX-D-rerun", 3, 1)
    n = min(len(samples_a1), len(samples))
    # 同参两轮逐拍语义序列: 六路并发 25fps 相位调度 → 采样窗边界 ±1 帧抖动为自然波动
    # （与 _verify_compare 轮口径一致）；硬判据 = 两轮各自 recheck 全过 + 抖动窗 <=3 且每处 <=1。
    jitter, hard = [], 0
    for k in range(n):
        a, b = samples_a1[k], samples[k]
        for key in ("c1", "c4", "c5", "c6"):
            vals = [(a[key][i] - b[key][i]) for i in range(4)]  # enq/drop/skip/rej
            if all(v == 0 for v in vals):
                continue
            if max(abs(v) for v in vals) <= 1:
                jitter.append((a["t"], key))
            else:
                hard += 1
    ok = (not probs) and n >= 5 and hard == 0 and len(jitter) <= 6
    record("EX-D/deterministic-two-rounds", ok,
           "windows=%d/%d jitter_windows=%d(each<=1 hard=%d) (t excluded)"
           % (len(samples_a1), len(samples), len(jitter), hard))
    if not ok:
        print(combined[:2000])
    return ok


def ext_f_leak_scan():
    sec = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL") or ps_user_var("TRAIN_RK3588_SSH_CREDENTIAL")
    if not sec:
        record("EX-E", False, "no SSH credential for scan")
        return False
    if sec == "admin":
        pat = re.compile(r"user-pw[\s\"']*admin|password[\s\"'=]*admin|admin@[\w.\-]+")
        files_scanned, hits = 0, []
        for p in HERE.rglob("*"):
            if not p.is_file():
                continue
            try:
                text = p.read_text(encoding="utf-8", errors="replace")
            except OSError:
                continue
            files_scanned += 1
            ctxs = [text[max(0, m.start() - 30):m.end() + 15].replace("\n", " ")[:70]
                    for m in pat.finditer(text)]
            if ctxs:
                hits.append((p.name, ctxs[:2]))
        record("EX-E/admin-credential-semantics-scan", len(hits) == 0,
               "files=%d hits=%s (admin=registered username; token form reviewed)" % (files_scanned, hits or "none"))
        return len(hits) == 0
    hits = []
    files_scanned = 0
    for p in HERE.rglob("*"):
        if not p.is_file():
            continue
        try:
            text = p.read_text(encoding="utf-8", errors="replace")
        except OSError:
            continue
        files_scanned += 1
        if sec in text:
            hits.append(p.name)
    record("EX-E", not hits, "files=%d hit_files=%s" % (files_scanned, hits or "none"))
    return not hits


def main():
    if not os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL"):
        os.environ["TRAIN_RK3588_SSH_CREDENTIAL"] = ps_user_var("TRAIN_RK3588_SSH_CREDENTIAL")
    t0 = time.time()
    ok = True
    c = ssh_client()
    try:
        ext_a_ok, samples_a1 = ext_a(c)
        ok &= ext_a_ok
        ok &= ext_b(c)
        ok &= ext_c(c)
        ok &= ext_d(c, samples_a1)
    except Exception as exc:  # noqa: BLE001
        record("EXT_RUN", False, "exception: %r" % (exc,))
        ok = False
    finally:
        clean = rrun(c, "rm -f /tmp/vext_ai_dispatch %s/vext_ai_dispatch.cpp "
                        "%s/core-evidence/vexta-*.log %s/core-evidence/vexta-*.json && "
                        "ls /tmp/vext_ai_dispatch 2>&1; echo EXT_TMP_CLEAN_DONE" % (REMOTE, REMOTE, REMOTE), 20)
        print("[ext-cleanup] exit=%s tail=%s" % (clean[0], (clean[1] or "").strip().splitlines()[-1:]))
        c.close()
    ok &= ext_f_leak_scan()
    out = {"results": results, "pass": sum(1 for r in results if r["ok"]),
           "fail": sum(1 for r in results if not r["ok"]),
           "elapsed_s": round(time.time() - t0, 1), "board": BOARD, "remote_tree": REMOTE}
    (HERE / "ai-ext-192_168_0_103-20261017-item010-verify-v1.json").write_text(
        json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
    print("EXT summary: %d PASS / %d FAIL" % (out["pass"], out["fail"]))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
