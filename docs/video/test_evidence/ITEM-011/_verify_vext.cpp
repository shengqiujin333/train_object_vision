// vext_recording.cpp — ITEM-011 验证侧自有扩展夹具（software_verification, VERIFY-20261017-v1）
// 直接链接交付 core 层 compressed_ring.cpp / recording_source.cpp + media 层 recording_tap.cpp，
// 独立复验边界；判据全部验证侧独立构造，不采信 core-tests/selftest 判例。
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
    // --- 无限上限语义: max_bytes=0 与 duration_ms<=0 均按契约表示不限 ---
    {
        CompressedRingBuffer r(0, 0);
        for (int i = 0; i < 50; ++i) r.push(pkt(i * 10, 64));
        CompressedRingStats s = r.stats();
        check(s.packets == 50 && s.evicted_duration == 0 && s.evicted_bytes == 0,
              "v01_unlimited_no_eviction", "packets=" + std::to_string(s.packets));
        check(s.span_ms == 490 && s.bytes == 50u * 64u, "v02_unlimited_full_window",
              "span=" + std::to_string(s.span_ms));
    }
    {
        // 0 语义按构造函数注释: max_bytes<=0 表示不限字节（用于“仅时长”情形须给足字节）
        CompressedRingBuffer r(static_cast<std::size_t>(1) << 30, 0);
        for (int i = 0; i < 30; ++i) r.push(pkt(i, 10));
        check(r.stats().packets == 30, "v03_zero_duration_unlimited");
    }
    // --- 单包超宽: 超字节上限的单包被立即淘空（bytes 归零），且不阻后续包 ---
    {
        CompressedRingBuffer r(100, 0);
        r.push(pkt(0, 500));
        CompressedRingStats s = r.stats();
        check(s.bytes == 0 && s.packets == 0 && s.evicted_bytes == 1,
              "v04_oversize_packet_evicted", "bytes=" + std::to_string(s.bytes));
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
        r.push(pkt(500, 8, true));
        check(r.first_keyframe_at_or_after(100) == 100, "v09_kf_exact_boundary");
        check(r.first_keyframe_at_or_after(101) == 300, "v10_kf_after");
        check(r.first_keyframe_at_or_after(401) == 500, "v11_kf_next_after_gap");
        check(r.first_keyframe_at_or_after(501) == -1, "v12_no_kf_minus1");
        CompressedRingBuffer e(100, 1000);
        check(e.first_keyframe_at_or_after(0) == -1, "v13_empty_kf_minus1");
    }
    // --- 淘汰分账: 时长上限先淘汰（evicted_bytes==0），收紧字节后 bytes 分账启用 ---
    {
        CompressedRingBuffer r(500, 300);
        for (int i = 0; i <= 10; ++i) r.push(pkt(i * 100, 100));
        CompressedRingStats s = r.stats();
        check(s.span_ms <= 300 && s.evicted_duration >= 1 && s.evicted_bytes == 0,
              "v14_duration_first_account", "ev_d=" + std::to_string(s.evicted_duration) +
              " ev_b=" + std::to_string(s.evicted_bytes));
        r.set_limits(150, 300);
        s = r.stats();
        check(s.bytes <= 150 && s.evicted_bytes >= 1 && s.evicted_duration >= 1,
              "v15_byte_after_shrink", "bytes=" + std::to_string(s.bytes));
        check(r.size() <= 2, "v16_shrink_packet_bound", "size=" + std::to_string(r.size()));
    }
    // --- 大窗口不变量: 1000 包混合尺寸流经双上限，逐包断言界与守恒 ---
    {
        CompressedRingBuffer r(20000, 5000);
        std::uint64_t total_pushed_bytes = 0;
        bool invariants_hold = true;
        for (int i = 0; i < 1000; ++i) {
            const std::size_t b = 30 + static_cast<std::size_t>(i % 7) * 25;
            total_pushed_bytes += b;
            r.push(pkt(i * 20, b, i % 40 == 0));
            CompressedRingStats s = r.stats();
            if (s.bytes > 20000 || s.span_ms > 5000 ||
                s.pushed != s.packets + s.evicted_duration + s.evicted_bytes) {
                invariants_hold = false;
                check(false, "v17_mid_invariant", "i=" + std::to_string(i) +
                      " bytes=" + std::to_string(s.bytes) + " span=" + std::to_string(s.span_ms));
                break;
            }
        }
        check(invariants_hold, "v17_mid_invariants_hold");
        CompressedRingStats s = r.stats();
        check(s.pushed == 1000 && s.bytes <= 20000 && s.span_ms <= 5000,
              "v19_big_window_final", "pushed=" + std::to_string(s.pushed) +
              " bytes=" + std::to_string(s.bytes) + " span=" + std::to_string(s.span_ms));
        // 驻留 keyframe 精确预言（独立模拟逐出算法重推导）:
        // ring(20000,5000), ts=i*20, bytes=30+(i%7)*25, kf=i%40==0 → 终态驻留
        // packets=190 bytes=20000 span_ms=3780 keyframes=4 (ts 16800/17600/18400/19200),
        // evicted_duration=0 evicted_bytes=810 (字节上限主导).
        check(s.packets == 190 && s.bytes == 20000 && s.span_ms == 3780,
              "v20_big_window_terminal", "packets=" + std::to_string(s.packets) +
              " bytes=" + std::to_string(s.bytes) + " span=" + std::to_string(s.span_ms));
        check(s.keyframes == 4 && s.evicted_duration == 0 && s.evicted_bytes == 810,
              "v20b_resident_kf_oracle", "kf=" + std::to_string(s.keyframes) +
              " ev_d=" + std::to_string(s.evicted_duration) +
              " ev_b=" + std::to_string(s.evicted_bytes));
        check(r.first_keyframe_at_or_after(16000) == 16800 &&
                  r.first_keyframe_at_or_after(16801) == 17600,
              "v20c_kf_lookup_on_resident_window");
        check(s.bytes < total_pushed_bytes, "v21_ring_discards_old_data",
              "kept=" + std::to_string(s.bytes) + " total=" + std::to_string(total_pushed_bytes));
    }
    // --- ts 回退防护观察: 契约声明“假定 ts_ms 单调不减”，回退不崩且界保持（登记观察） ---
    {
        CompressedRingBuffer r(10000, 1000);
        r.push(pkt(5000, 50, true));
        r.push(pkt(4900, 50));
        CompressedRingStats s = r.stats();
        check(s.bytes <= 10000 && s.packets <= 21, "v22_ts_regression_bounded",
              "packets=" + std::to_string(s.packets));
    }
    // --- RecordingTap 线程安全: 4 写 2 读并发 1600 包，终态不变量成立 ---
    {
        RecordingTap tap(8000, 1500);
        std::atomic<bool> stop{false};
        std::atomic<int> read_errors{0};
        std::vector<std::thread> ths;
        for (int w = 0; w < 4; ++w) {
            ths.emplace_back([&tap, w]() {
                for (int i = 0; i < 400; ++i) {
                    CompressedPacket p = pkt(i * 5, 40 + static_cast<std::size_t>(i % 5) * 8, i % 50 == 0);
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
                  st.reason == "NVR_UNCONFIGURED",
              "v26_tap_source_status_fallback");
        tap.set_source_status(resolve_recording_source(RecordingSource::Local, true));
        st = tap.source_status();
        check(st.effective == RecordingAvailability::LocalOnly && !st.nvr_enabled,
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
    // --- 来源策略细分: Local 显式拒绝 NVR 即便已配置；Both 配置齐才双启 ---
    {
        auto s = resolve_recording_source(RecordingSource::Local, true);
        check(s.effective == RecordingAvailability::LocalOnly && !s.nvr_enabled && s.reason.empty(),
              "v31_local_request_ignores_nvr");
        auto b = resolve_recording_source(RecordingSource::Both, true);
        check(b.effective == RecordingAvailability::Both && b.nvr_enabled && b.local_enabled,
              "v32_both_configured_enables_nvr");
        auto n = resolve_recording_source(RecordingSource::Nvr, true);
        check(n.effective == RecordingAvailability::NvrOnly && n.nvr_enabled && !n.local_enabled,
              "v33_nvr_only_configured");
    }

    std::printf("vext %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
