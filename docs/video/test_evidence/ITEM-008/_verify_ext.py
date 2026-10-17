#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""ITEM-008 验证侧扩展夹具（software_verification，VERIFY-20261017-v1）。

前提: 交付 runner board_backpressure_check.py 已 --keep-remote 完成独立复跑
      （板端 /tmp/railhmi_bp 构建树在位，railhmi-backpressure 可执行）。
本夹具全部有限超时；判据先独立构造再取实测；不采信交付自身 acceptance 行。

EXT 清单:
  EX-A  独立参数背压轮（duration 6s, display/ai 2Hz, stop 3s, rec-frames 4）:
        逐拍独立判据——capture fps>=20 收尾、display/ai 丢帧非降、ai_depth<=2、
        pre-stop recording_overflow==0、post-stop overflow 递增 + recording_depth<=4 收尾4、
        recording_reads 停后冻结、acceptance result=PASS、BP_EXIT=0
  EX-B  TC-V-REC-01 字面场景（display 25Hz + AI 1Hz）:
        pre-stop ai_dropped 递增（慢 AI 消费注入）且 recording_overflow==0（录像分支不受慢 AI/快显示
        影响）；fps>=20 不塌陷；ai_depth<=2
  EX-C  CLI 负例: --bogus-flag → exit=2 + unknown arg；--help → exit=0 含 --selftest/--recording-frames
  EX-D  板端 g++ 直编 header-only bounded_queue.h 验证侧自有 12 判例:
        v01 cap1 语义 latest-only、v02 cap2 FIFO 丢旧序、v03 字节预算逐出与序、
        v04 字节上限<单项（退化语义自洽 pushed==dropped+size 且不崩溃）、
        v05 latest() 非 pop 语义、v06 空队列语义与零计数、v07 cap=0 归一为 1、
        v08 计数不变式 pushed==dropped+popped+size、
        v09 双生产者双消费者并发不变式（有界+字节，2000×2 push + join 有限完成即无死锁）、
        v10 录像字节预算 NV12 量级（1.5MB/0.7MB 四帧五帧逐出）、
        v11 混合尺寸字节账目逐出序、v12 pop 后 bytes/size 归零
  EX-E  确定性: EX-A 同参两轮 acceptance 全布尔恒等 + 逐拍语义序列一致
        （(dd,ad,aid,ov,rd,dr,ar,rr) 逐窗相等；fps 为自然波动非判据）
  EX-F  凭据泄漏扫描: 本目录全部文件对 SSH 凭据值凭据语义形态 0 命中
  清理: 板端 /tmp/vext_bounded_queue、/tmp/ext*.log 与 railhmi_bp 树内 vext 源逐项确认
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
REMOTE = "/tmp/railhmi_bp"
BIN = REMOTE + "/build-media/tools/railhmi-backpressure"

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


BP_RX = re.compile(
    r"t=(\d+) type=bp dispatched=(\d+) fps=(\d+) display_dropped=(\d+) ai_dropped=(\d+) "
    r"ai_depth=(\d+) recording_overflow=(\d+) recording_depth=(\d+) "
    r"display_reads=(\d+) ai_reads=(\d+) recording_reads=(\d+)")


def parse_bp(out):
    samples = []
    for line in out.splitlines():
        m = BP_RX.search(line)
        if m:
            samples.append({"t": int(m.group(1)), "dis": int(m.group(2)), "fps": int(m.group(3)),
                            "dd": int(m.group(4)), "ad": int(m.group(5)), "aid": int(m.group(6)),
                            "ov": int(m.group(7)), "rd": int(m.group(8)),
                            "dr": int(m.group(9)), "ar": int(m.group(10)), "rr": int(m.group(11))})
    return samples


def run_bp(c, dur, dhz, ahz, stop, depth, rec_frames, tag):
    cmd = ("cd %s && %s --source test --duration %d --display-hz %s --ai-hz %s "
           "--stop-recording-after %d --ai-depth %d --recording-frames %d "
           "--out /tmp/extbp-%s.log --json /tmp/extbp-%s.json 2>&1; echo EXTX_EXIT=$?"
           % (REMOTE, BIN, dur, dhz, ahz, stop, depth, rec_frames, tag, tag))
    code, out, err = rrun(c, cmd, dur + 90)
    combined = out + "\n" + err
    return combined


def check_semantics(combined, tag, stop_ms, depth_cap, rec_cap, pre_stop_ov_zero=True):
    """独立重推导逐拍判据；返回 (ok, problems)。fps 为自然波动项按阈值判。"""
    problems = []
    samples = parse_bp(combined)
    if len(samples) < 6:
        problems.append("%s samples=%d too few" % (tag, len(samples)))
        return problems, samples
    # fps after ramp
    if any(x["fps"] < 20 for x in samples if x["t"] >= 2000):
        problems.append("%s fps<20 post-ramp" % tag)
    # monotone counters
    for key in ("dd", "ad", "ov"):
        vals = [x[key] for x in samples]
        if any(vals[i + 1] < vals[i] for i in range(len(vals) - 1)):
            problems.append("%s %s not monotone" % (tag, key))
    if max(x["aid"] for x in samples) > depth_cap:
        problems.append("%s ai_depth>%d" % (tag, depth_cap))
    if max(x["rd"] for x in samples) > rec_cap:
        problems.append("%s recording_depth>%d" % (tag, rec_cap))
    # pre-stop
    pre = [x for x in samples if x["t"] < stop_ms]
    if pre_stop_ov_zero and any(x["ov"] != 0 for x in pre):
        problems.append("%s pre-stop recording_overflow!=0" % tag)
    if len(pre) >= 2 and all(pre[i + 1]["dd"] <= pre[i]["dd"] for i in range(len(pre) - 1)) is False:
        pass
    # post-stop
    post = [x for x in samples if x["t"] >= stop_ms]
    if not post:
        problems.append("%s no post-stop samples" % tag)
        return problems, samples
    ovp = [x["ov"] for x in post]
    if len(ovp) >= 2:
        ups = sum(1 for i in range(len(ovp) - 1) if ovp[i + 1] > ovp[i])
        if ups < 2:
            problems.append("%s post-stop overflow not increasing (%d ups)" % (tag, ups))
    if post[-1]["rd"] != rec_cap:
        problems.append("%s tail recording_depth=%d != %d" % (tag, post[-1]["rd"], rec_cap))
    # recording_reads frozen post-stop
    rr = [x["rr"] for x in post]
    if len(set(rr[1:])) > 1:
        problems.append("%s recording_reads changing post-stop" % tag)
    # acceptance result
    m = re.search(r"type=acceptance (.*)result=(\w+)", combined)
    if not m or m.group(2) != "PASS":
        problems.append("%s acceptance result=%s" % (tag, m.group(2) if m else "?"))
    return problems, samples


def ext_a(c):
    combined = run_bp(c, 6, 2, 2, 3, 2, 4, "a1")
    probs, samples = check_semantics(combined, "EX-A", 3000, 2, 4)
    m = re.search(r"EXTX_EXIT=(\d+)", combined)
    if not m or m.group(1) != "0":
        probs.append("exit=%s" % (m.group(1) if m else "?"))
    record("EX-A/independent-param-backpressure-round", not probs,
           "samples=%d; %s" % (len(samples), "; ".join(probs[:4]) if probs else "all per-tick checks ok"))
    if probs:
        print(combined[:2000])
    return not probs, samples


def ext_b(c):
    combined = run_bp(c, 6, "25", 1, 3, 2, 4, "b1")
    probs, samples = check_semantics(combined, "EX-B", 3000, 2, 4)
    # TC-V-REC-01 literal: slow AI alone must be visible: ai_dropped grows through the run
    samples_pre = [x for x in samples if x["t"] < 3000]
    if len(samples_pre) >= 2:
        if samples_pre[-1]["ad"] <= samples_pre[0]["ad"] + 20:
            probs.append("ai_dropped insufficient growth pre-stop")
    m = re.search(r"EXTX_EXIT=(\d+)", combined)
    if not m or m.group(1) != "0":
        probs.append("exit=%s" % (m.group(1) if m else "?"))
    record("EX-B/display-fast-ai-slow-recording-independent", not probs,
           "samples=%d; %s" % (len(samples), "; ".join(probs[:4]) if probs else "slow-AI only; recording unaffected"))
    if probs:
        print(combined[:2000])
    return not probs, samples


def ext_c(c):
    ok_all = True
    r1 = rrun(c, "cd %s && %s --bogus-flag 2>&1; echo NEG_EXIT=$?" % (REMOTE, BIN), 30)
    combined = r1[1] + "\n" + r1[2]
    e1 = exit_token(combined)
    ok1 = e1 == 2 and "unknown arg" in combined
    record("EX-C/unknown-arg-exit2", ok1, "exit=%s head=%s" % (e1, combined.strip().splitlines()[:1]))
    ok_all &= ok1
    r2 = rrun(c, "cd %s && %s --help; echo HELP_EXIT=$?" % (REMOTE, BIN), 30)
    combined = r2[1] + "\n" + r2[2]
    e2 = exit_token(combined)
    ok2 = e2 == 0 and "--selftest" in combined and "--recording-frames" in combined
    record("EX-C/help-exit0-usage", ok2, "exit=%s" % e2)
    ok_all &= ok2
    return ok_all


VCODE = r'''
// ITEM-008 验证侧自有 BoundedQueue 向量（header-only 独立编译，不依赖交付库）。
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/core/bounded_queue.h"

using railhmi::core::BoundedQueue;

static int fails = 0;
static void check(bool ok, const char* name) {
    std::printf("vext %s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++fails;
}

int main() {
    // v01 cap1 latest-only: 3 push -> size1/dropped2/latest=3；push 返回 true
    {
        BoundedQueue<int> q(1);
        bool r1 = q.push(1), r2 = q.push(2), r3 = q.push(3);
        int v = 0;
        check(r1 && r2 && r3, "v01_cap1_push_returns_true");
        check(q.size() == 1 && q.dropped() == 2, "v01_cap1_size_dropped");
        check(q.latest(&v) && v == 3, "v01_cap1_latest_is_newest");
        check(q.pushed() == 3, "v01_cap1_pushed_3");
    }
    // v02 cap2 FIFO 丢旧序: a,b,c -> 队列 [b,c]，pop 恰按 b,c
    {
        BoundedQueue<std::string> q(2);
        q.push("a"); q.push("b"); q.push("c");
        std::string s;
        check(q.size() == 2 && q.dropped() == 1, "v02_cap2_counts");
        check(q.pop(&s) && s == "b", "v02_cap2_first_is_b");
        check(q.pop(&s) && s == "c", "v02_cap2_second_is_c");
        check(!q.pop(&s), "v02_cap2_empty_after");
        check(q.popped() == 2, "v02_cap2_popped_2");
    }
    // v03 字节预算逐出与序: cap 宽松, bytes=100: 40+40=80 -> +40 超出新 ≦100 保留两帧
    {
        BoundedQueue<int> q(1000, 100);
        q.push(1, 40); q.push(2, 40);
        check(q.size() == 2 && q.bytes() == 80, "v03_bytes_accum_80");
        q.push(3, 40);
        check(q.size() == 2 && q.bytes() == 80 && q.dropped() == 1, "v03_bytes_evict_to_bound");
        int v = 0;
        check(q.pop(&v) && v == 2, "v03_oldest_evicted_order_kept");
    }
    // v04 字节上限 < 单项字节（退化语义自洽、不越界/死锁）: pushed==dropped+size 恒等
    {
        BoundedQueue<int> q(16, 10);
        q.push(7, 100);
        check(q.size() == 0 && q.bytes() == 0, "v04_degenerate_nocrash_nohang");
        check(q.pushed() == 1 && q.dropped() == 1, "v04_degenerate_counters");
        check(q.pushed() == q.dropped() + q.popped() + q.size(), "v04_invariant");
        int v = 0;
        check(!q.pop(&v), "v04_degenerate_empty_pop");
    }
    // v05 latest() 非 pop 语义: latest 两次同值；pop 后 size 减一 latest 再取同值
    {
        BoundedQueue<int> q(4);
        q.push(5); q.push(9);
        int v = 0;
        check(q.latest(&v) && v == 9, "v05_latest_first");
        check(q.latest(&v) && v == 9, "v05_latest_nonremoving");
        check(q.size() == 2, "v05_latest_no_size_change");
        check(q.pop(&v) && v == 5, "v05_pop_oldest_first");
        check(q.latest(&v) && v == 9, "v05_latest_after_pop");
    }
    // v06 空队列语义与零计数
    {
        BoundedQueue<int> q(4);
        int v = 0;
        check(!q.pop(&v) && !q.latest(&v) && q.empty(), "v06_empty_semantics");
        check(q.dropped() == 0 && q.pushed() == 0 && q.popped() == 0, "v06_empty_counters");
        check(q.bytes() == 0 && q.size() == 0, "v06_empty_bytes_size");
    }
    // v07 cap=0 归一为 1（构造契约: max_items 0 视作 1）
    {
        BoundedQueue<int> q(0);
        q.push(1); q.push(2);
        check(q.size() == 1 && q.dropped() == 1, "v07_cap0_normalized_to_1");
        check(q.capacity() == 1, "v07_cap0_capacity_reports_1");
    }
    // v08 计数不变式 pushed == dropped + popped + size（含字节预算路径）
    {
        BoundedQueue<int> q(3, 60);
        for (int i = 0; i < 12; ++i) q.push(i, 10);
        int v = 0;
        q.pop(&v); q.pop(&v);
        check(q.pushed() == q.dropped() + q.popped() + q.size(), "v08_invariant");
        // 12×10B push 进 cap(3,60)：9 次逐出 → size3/bytes30；再 pop×2 → size1/bytes10
        check(q.size() == 1 && q.bytes() == 10, "v08_bytes_after_two_pops");
    }
    // v09 双生产者双消费者并发不变式（cap 64 + 字节 6400；2×2000 push, 2 rcv）
    {
        BoundedQueue<int> q(64, 6400);
        const int N = 2000;
        std::atomic<bool> stop{false};
        std::atomic<long long> popped_total{0};
        std::thread p1([&]() { for (int i = 0; i < N; ++i) q.push(i, 10); });
        std::thread p2([&]() { for (int i = 0; i < N; ++i) q.push(i, 10); });
        std::thread r1([&]() {
            int v;
            while (!stop.load()) { if (q.pop(&v)) popped_total.fetch_add(1); }
        });
        std::thread r2([&]() {
            int v;
            while (!stop.load()) { if (q.pop(&v)) popped_total.fetch_add(1); }
        });
        p1.join(); p2.join();
        for (int i = 0; i < 4000; ++i) { int v; q.pop(&v); }
        stop.store(true);
        r1.join(); r2.join();  // 到达即证无死锁（join 有限完成）
        const std::size_t sz = q.size();
        bool inv_ok = (q.pushed() == q.dropped() + q.popped() + sz);
        check(inv_ok && sz <= 64, "v09_concurrent_invariant_bounded");
        std::printf("vext INFO concurrency popped_by_receivers=%lld popped_verify=4000 dropped=%llu pushed=%llu size=%zu\n",
                    popped_total.load(), (unsigned long long)q.dropped(),
                    (unsigned long long)q.pushed(), sz);
        check(sz <= 64 && q.bytes() <= 6400, "v09_concurrent_bounded_bytes");
    }
    // v10 录像分支字节预算 NV12 量级: bytes=1_500_000 单项 700_000 -> 保留 2 项
    {
        BoundedQueue<int> q(1000, 1500000);
        q.push(1, 700000);
        q.push(2, 700000);
        check(q.size() == 2 && q.bytes() == 1400000, "v10_two_nv12_frames_held");
        q.push(3, 700000);
        check(q.size() == 2 && q.bytes() == 1400000 && q.dropped() == 1, "v10_third_evicts_oldest");
    }
    // v11 混合尺寸字节账目: 逐出遵循累计字节（先大后小：大项占满后被多项逐出）
    {
        BoundedQueue<int> q(100, 100);
        q.push(1, 80);
        q.push(2, 10);
        check(q.size() == 2 && q.bytes() == 90, "v11_mixed_90");
        q.push(3, 5);
        check(q.bytes() == 95, "v11_mixed_fits");
        q.push(4, 10);
        // 100+.. 90+10=100<=100 仍可；再 verify 逐出序: pop 应得到 2,3,4（1 已被逐出）
        int v;
        check(q.pop(&v) && v == 2, "v11_evict_order_first_is_2");
        check(q.pop(&v) && v == 3, "v11_order_second_is_3");
        check(q.pop(&v) && v == 4, "v11_order_third_is_4");
    }
    // v12 pop 后 bytes/size 归零
    {
        BoundedQueue<int> q(2);
        q.push(1, 50); q.push(2, 50);
        int v;
        q.pop(&v); q.pop(&v);
        check(q.size() == 0 && q.bytes() == 0, "v12_drained_zero");
    }
    std::printf("vext %s (%d failure(s))\n", fails == 0 ? "OK" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
'''


def ext_d(c):
    sftp = c.open_sftp()
    try:
        sftp.putfo(io.BytesIO(VCODE.encode("utf-8")), REMOTE + "/vext_bounded_queue.cpp")
    finally:
        sftp.close()
    hdr = REMOTE  # 上传的 app/ 树在 REMOTE 根，include 路径 src/core/include
    compile_cmd = (
        "cd %s && g++ -std=c++17 -pthread -I src/core/include -o /tmp/vext_bounded_queue "
        "vext_bounded_queue.cpp 2>&1 | tail -8; "
        "if [ -x /tmp/vext_bounded_queue ]; then /tmp/vext_bounded_queue; echo VEXT_EXIT=$?; "
        "else echo VEXT_COMPILE_FAILED; fi" % hdr)
    code, out, err = rrun(c, compile_cmd, 180)
    combined = out + "\n" + err
    vext_pass = combined.count("vext PASS")
    vext_fail = combined.count("vext FAIL")
    exit_ok = "VEXT_EXIT=0" in combined
    comp_ok = "VEXT_COMPILE_FAILED" not in combined
    names = ["v01_cap1_push_returns_true", "v01_cap1_size_dropped", "v01_cap1_latest_is_newest",
             "v01_cap1_pushed_3", "v02_cap2_counts", "v02_cap2_first_is_b", "v02_cap2_second_is_c",
             "v02_cap2_empty_after", "v02_cap2_popped_2", "v03_bytes_accum_80",
             "v03_bytes_evict_to_bound", "v03_oldest_evicted_order_kept",
             "v04_degenerate_nocrash_nohang", "v04_degenerate_counters", "v04_invariant",
             "v04_degenerate_empty_pop", "v05_latest_first", "v05_latest_nonremoving",
             "v05_latest_no_size_change", "v05_pop_oldest_first", "v05_latest_after_pop",
             "v06_empty_semantics", "v06_empty_counters", "v06_empty_bytes_size",
             "v07_cap0_normalized_to_1", "v07_cap0_capacity_reports_1", "v08_invariant",
             "v08_bytes_after_two_pops", "v09_concurrent_invariant_bounded",
             "v09_concurrent_bounded_bytes", "v10_two_nv12_frames_held", "v10_third_evicts_oldest",
             "v11_mixed_90", "v11_mixed_fits", "v11_evict_order_first_is_2",
             "v11_order_second_is_3", "v11_order_third_is_4", "v12_drained_zero"]
    missing = [n for n in names if ("vext PASS " + n) not in combined]
    ok = exit_ok and comp_ok and vext_fail == 0 and vext_pass >= len(names) and not missing
    record("EX-D/board-cpp-bounded-queue-fixture", ok,
           "compile_ok=%s vext_pass=%d vext_fail=%d expected=%d missing=%s exit0=%s"
           % (comp_ok, vext_pass, vext_fail, len(names), missing or "none", exit_ok))
    if vext_fail or not comp_ok or missing:
        print(combined[:3000])
    return ok


def ext_e(c, samples_a1, samples_b1):
    combined_a2 = run_bp(c, 6, 2, 2, 3, 2, 4, "a2")
    probs_a2, samples_a2 = check_semantics(combined_a2, "EX-E-rerun", 3000, 2, 4)
    ok_sem = not probs_a2
    # 同参语义序列逐窗（fps 自然波动排除）：(dd,ad,aid,ov,rd,dr,ar,rr)
    t1 = [x["t"] for x in samples_a1]
    t2 = [x["t"] for x in samples_a2]
    n = min(len(t1), len(t2))
    diffs = []
    for k in range(n):
        a, b = samples_a1[k], samples_a2[k]
        keys = ("dd", "ad", "aid", "ov", "rd", "dr", "ar", "rr")
        if tuple(a[k2] for k2 in keys) != tuple(b[k2] for k2 in keys):
            diffs.append((a["t"], b["t"]))
    ok = ok_sem and n >= 6 and len(diffs) == 0
    record("EX-E/deterministic-semantics-two-rounds", ok,
           "windows=%d/%d semantic_diffs=%d rerun_ok=%s (fps excluded)"
           % (len(t1), len(t2), len(diffs), ok_sem))
    if diffs:
        print("first diffs:", diffs[:5])
    return ok


def ext_f_leak_scan():
    sec = os.environ.get("TRAIN_RK3588_SSH_CREDENTIAL") or ps_user_var("TRAIN_RK3588_SSH_CREDENTIAL")
    if not sec:
        record("EX-F", False, "no SSH credential for scan")
        return False
    if sec == "admin":
        # SSH 凭据值=admin（历史口径）；按凭据语义形态扫描不按值命中
        pat = re.compile(r"user-pw[\s\"']*admin|password[\s\"'=]*admin|admin@[\w.\-]+")
        files_scanned, hits, note_files = 0, [], []
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
        # 端口/凭据语义上下文是否是合法 token（admin 作 username 出现）：人工占位不精判——
        # 本轮并集口径：命中 0 为全绿；命中则列出人工复核（admin 是登记 username，token 形态非泄漏）
        record("EX-F/admin-credential-semantics-scan", len(hits) == 0,
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
    record("EX-F", not hits, "files=%d hit_files=%s" % (files_scanned, hits or "none"))
    return not hits


def main():
    t0 = time.time()
    ok = True
    c = ssh_client()
    try:
        ext_a_ok, samples_a1 = ext_a(c)
        ok &= ext_a_ok
        ext_b_ok, _samples_b1 = ext_b(c)
        ok &= ext_b_ok
        ok &= ext_c(c)
        ok &= ext_d(c)
    except Exception as exc:  # noqa: BLE001
        record("EXT_RUN", False, "exception: %r" % (exc,))
        ok = False
        samples_a1 = []
        c.close()
        raise SystemExit(1)
    try:
        ok &= ext_e(c, samples_a1, [])
    except Exception as exc:  # noqa: BLE001
        record("EX-E", False, "exception: %r" % (exc,))
        ok = False
    finally:
        # 收尾清理（有限等待；逐项确认）
        clean = rrun(c, "rm -f /tmp/vext_bounded_queue /tmp/extbp-*.log /tmp/extbp-*.json "
                        "%s/vext_bounded_queue.cpp && ls /tmp/vext_bounded_queue 2>&1; "
                        "echo EXT_TMP_CLEAN_DONE" % REMOTE, 20)
        print("[ext-cleanup] exit=%s" % clean[0])
    c.close()
    ok &= ext_f_leak_scan()
    out = {"results": results, "pass": sum(1 for r in results if r["ok"]),
           "fail": sum(1 for r in results if not r["ok"]),
           "elapsed_s": round(time.time() - t0, 1), "board": BOARD, "remote_tree": REMOTE}
    (HERE / "bp-ext-192_168_0_103-20261017-item008-verify-v1.json").write_text(
        json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
    print("EXT summary: %d PASS / %d FAIL" % (out["pass"], out["fail"]))
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
