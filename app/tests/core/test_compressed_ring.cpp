// railhmi-core 压缩滚动缓冲测试（视频 ITEM-011 纯逻辑）。
// 字节+时长双上限、最旧淘汰、关键帧对齐、窗口收集、计数。

#include "test_util.h"

#include <cstdint>
#include <vector>

#include "railhmi/core/compressed_ring.h"

namespace {

using namespace railhmi::core;

CompressedPacket pkt(int64_t ts, std::size_t bytes, bool key = false) {
    CompressedPacket p;
    p.ts_ms = ts;
    p.keyframe = key;
    p.data.assign(bytes, static_cast<std::uint8_t>(ts & 0xff));
    return p;
}

void test_duration_bound() {
    CompressedRingBuffer ring(1u << 30, 1000);  // 字节宽松、时长 1s
    for (int i = 0; i <= 20; ++i) ring.push(pkt(i * 100, 50));
    const CompressedRingStats s = ring.stats();
    railhmi_test::check(s.span_ms <= 1000, "duration span<=1000", std::to_string(s.span_ms));
    railhmi_test::check(s.first_ts_ms == 1000 && s.last_ts_ms == 2000, "duration window [1000,2000]");
    railhmi_test::check(s.evicted_duration == 10 && s.evicted_bytes == 0, "duration evicted=10 no bytes");
    railhmi_test::check(s.packets == 11, "duration keeps 11");
}

void test_byte_bound() {
    CompressedRingBuffer ring(250, 0);  // 仅字节上限
    for (int i = 0; i < 10; ++i) ring.push(pkt(i, 100));
    const CompressedRingStats s = ring.stats();
    railhmi_test::check(s.bytes <= 250, "bytes <=250", std::to_string(s.bytes));
    railhmi_test::check(s.packets == 2, "bytes keeps 2");
    railhmi_test::check(s.evicted_bytes == 8 && s.evicted_duration == 0, "bytes evicted=8");
}

void test_dual_bound() {
    CompressedRingBuffer ring(1000, 500);  // 1KB 与 0.5s
    for (int i = 0; i <= 20; ++i) ring.push(pkt(i * 100, 100));  // 21 包 * 100B = 2100B
    const CompressedRingStats s = ring.stats();
    railhmi_test::check(s.bytes <= 1000, "dual bytes <=1000");
    railhmi_test::check(s.span_ms <= 500, "dual span<=500");
    // 时长先淘汰（保留 6 包 = 600B <= 1000），字节不再淘汰。
    railhmi_test::check(s.packets == 6 && s.evicted_duration >= 1, "dual duration binds first");
}

void test_overwide_single_packet() {
    CompressedRingBuffer ring(100, 0);
    ring.push(pkt(0, 500));  // 单包超字节上限 -> 淘汰，字节归零
    const CompressedRingStats s = ring.stats();
    railhmi_test::check(s.bytes == 0 && s.packets == 0, "oversize packet evicted");
    railhmi_test::check(s.evicted_bytes == 1, "oversize evicted counted");
}

void test_collect_and_keyframe() {
    CompressedRingBuffer ring(1u << 30, 100000);
    for (int i = 0; i <= 10; ++i) ring.push(pkt(i * 100, 10, i == 0 || i == 5));
    const auto win = ring.collect(200, 700);
    railhmi_test::check(win.size() == 6, "collect [200,700] size=6");
    railhmi_test::check(win.front().ts_ms == 200 && win.back().ts_ms == 700, "collect order");
    railhmi_test::check(ring.first_keyframe_at_or_after(0) == 0, "kf at 0");
    railhmi_test::check(ring.first_keyframe_at_or_after(300) == 500, "kf at/after 300 -> 500");
    railhmi_test::check(ring.first_keyframe_at_or_after(800) == -1, "no kf after 800 -> -1");
    railhmi_test::check(ring.stats().keyframes == 2, "keyframes=2");
}

void test_set_limits_shrink() {
    CompressedRingBuffer ring(1u << 30, 100000);
    for (int i = 0; i < 10; ++i) ring.push(pkt(i * 100, 10));
    railhmi_test::check(ring.size() == 10, "before shrink size=10");
    ring.set_limits(1u << 30, 300);
    const CompressedRingStats s = ring.stats();
    railhmi_test::check(s.span_ms <= 300, "shrink span<=300");
    railhmi_test::check(s.evicted_duration > 0, "shrink evicted");
    ring.clear();
    railhmi_test::check(ring.empty() && ring.bytes() == 0, "clear empties");
}

void test_empty_semantics() {
    CompressedRingBuffer ring(100, 1000);
    const CompressedRingStats s = ring.stats();
    railhmi_test::check(s.packets == 0 && s.bytes == 0 && s.first_ts_ms == -1 && s.span_ms == 0,
                        "empty stats");
    railhmi_test::check(ring.collect(0, 100).empty(), "empty collect");
    railhmi_test::check(ring.first_keyframe_at_or_after(0) == -1, "empty no keyframe");
}

}  // namespace

void run_compressed_ring_tests() {
    test_duration_bound();
    test_byte_bound();
    test_dual_bound();
    test_overwide_single_packet();
    test_collect_and_keyframe();
    test_set_limits_shrink();
    test_empty_semantics();
}
