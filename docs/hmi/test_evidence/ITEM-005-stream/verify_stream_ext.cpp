// 软件验证自有补充用例（ITEM-005 独立验证；不改动产品源码/实现测试）。
// 覆盖：质量阈值边界与组合、全非 LIVE 状态拒绝 present-as-live、显式重连升级、
// DEGRADED 冻结→升级链、OFFLINE tick 粘滞、start 重置计数、drain 清空、
// 首帧仍带质量标志、冻结期坏帧恢复为 DEGRADED。
#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/stream_state.h"

namespace {

using namespace railhmi::core;

int g_pass = 0;
int g_fail = 0;

void check(bool ok, const std::string& name) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name.c_str());
    if (ok) ++g_pass; else ++g_fail;
}

bool has_event(const std::vector<StreamEvent>& evs, StreamEventKind k) {
    for (const auto& e : evs) if (e.kind == k) return true;
    return false;
}

int count_event(const std::vector<StreamEvent>& evs, StreamEventKind k) {
    int n = 0;
    for (const auto& e : evs) if (e.kind == k) ++n;
    return n;
}

void test_classify_boundaries() {
    const QualityThresholds qt;  // dark<40, bright>220, stddev<3, lapvar<30
    check(!classify_quality(FrameStats{40.0, 40, 200}, qt).too_dark,
          "boundary mean==40 not too_dark (strict <)");
    check(classify_quality(FrameStats{39.999, 40, 200}, qt).too_dark,
          "mean 39.999 -> too_dark");
    check(!classify_quality(FrameStats{220.0, 40, 200}, qt).too_bright,
          "boundary mean==220 not too_bright (strict >)");
    check(classify_quality(FrameStats{220.001, 40, 200}, qt).too_bright,
          "mean 220.001 -> too_bright");
    check(!classify_quality(FrameStats{128, 3.0, 200}, qt).occluded,
          "boundary stddev==3 not occluded");
    check(classify_quality(FrameStats{128, 2.999, 200}, qt).occluded,
          "stddev 2.999 -> occluded");
    check(!classify_quality(FrameStats{128, 40, 30.0}, qt).blurred,
          "boundary lapvar==30 not blurred (strict <)");
    check(classify_quality(FrameStats{128, 40, 0.0}, qt).blurred,
          "lapvar 0 -> blurred");
    {
        QualityFlags f = classify_quality(FrameStats{-1, 1.0, -1}, qt);
        check(!f.too_dark && !f.too_bright && f.occluded && !f.blurred,
              "partial invalid stats -> only occluded");
    }
    // too_dark 与 too_bright 互斥（mean<40 vs mean>220），可实现的组合是
    // too_bright+occluded+blurred（v2 轮误把两者写进同一期望，已按事实修正）。
    {
        QualityFlags f = classify_quality(FrameStats{240, 1, 5}, qt);
        check(f.any() && f.describe() == "too_bright,occluded,blurred",
              "multi-flag describe order");
    }
}

void test_non_live_and_explicit_reconnect() {
    StreamThresholds th;
    th.first_frame_timeout_ms = 100;
    th.freeze_timeout_ms = 50;
    th.reconnect_threshold = 2;
    th.reconnect_backoff_ms = 10;
    StreamStateMachine m("VC-01", th);

    check(!m.may_present_as_live(0), "offline rejects present-as-live");
    m.start(0);
    check(!m.may_present_as_live(1), "connecting rejects present-as-live");
    m.tick(100);
    check(m.state() == StreamState::Reconnecting && m.reconnect_count() == 1,
          "first-frame timeout -> reconnecting");
    check(!m.may_present_as_live(105), "reconnecting rejects present-as-live");
    m.tick(110);
    check(m.state() == StreamState::Connecting, "backoff elapsed -> connecting");
    m.on_reconnect_attempt(115);
    check(m.state() == StreamState::Offline && m.reconnect_count() == 2,
          "explicit attempt reaches threshold -> offline");
    check(has_event(m.events(), StreamEventKind::Escalated),
          "explicit attempt emits Escalated");
    check(!m.may_present_as_live(115), "escalated offline rejects present-as-live");
    check(m.drain_events().size() == 4, "drain returns all buffered events");
    m.start(200);
    check(m.state() == StreamState::Connecting && m.reconnect_count() == 0,
          "start after escalation resets count");
    check(m.drain_events().size() == 1, "start emits Started");
    check(m.drain_events().empty(), "second drain is empty");
    m.on_frame(210, QualityFlags{});
    check(m.state() == StreamState::Live, "frame after restart -> live");
    check(m.may_present_as_live(259), "49ms old frame presents as live");
    check(!m.may_present_as_live(260), "exactly freeze_timeout old rejects");
}

void test_degraded_chain_escalation_and_recovery() {
    StreamThresholds th;
    th.freeze_timeout_ms = 50;
    th.reconnect_threshold = 1;
    th.reconnect_backoff_ms = 10;
    StreamStateMachine m("VC-02", th);
    m.start(0);
    QualityFlags bad;
    bad.blurred = true;
    m.on_frame(5, bad);
    check(m.state() == StreamState::Degraded && m.is_streaming(),
          "bad-quality first frame -> degraded streaming");
    check(m.drain_events().size() == 3,
          "connecting->degraded emits Started+QualityDegraded+FirstFrame");
    m.tick(55);
    check(m.state() == StreamState::Frozen, "degraded no-frame -> frozen");
    check(!m.may_present_as_live(56), "frozen after degraded rejects");
    m.tick(105);
    check(m.state() == StreamState::Offline && m.reconnect_count() == 1,
          "degraded->frozen->reconnect escalates at threshold 1");
    check(has_event(m.events(), StreamEventKind::Escalated),
          "escalated emitted in degraded chain");
    QualityFlags bad2;
    bad2.too_dark = true;
    m.on_frame(106, bad2);
    check(m.state() == StreamState::Degraded && m.reconnect_count() == 0,
          "recovery with flags -> degraded, count reset");
    check(has_event(m.events(), StreamEventKind::Recovered),
          "recovered emitted from offline");
    check(!m.may_present_as_live(156), "degraded old frame (50ms) rejects");
}

void test_attempts_without_frames() {
    StreamThresholds th;
    th.reconnect_threshold = 3;
    StreamStateMachine m("VC-03", th);
    m.start(0);
    m.on_reconnect_attempt(1);
    m.on_reconnect_attempt(2);
    check(m.state() == StreamState::Reconnecting && m.reconnect_count() == 2,
          "two attempts below threshold stay reconnecting");
    check(count_event(m.events(), StreamEventKind::ReconnectAttempt) == 2,
          "one ReconnectAttempt event per attempt");
    m.on_reconnect_attempt(3);
    check(m.state() == StreamState::Offline, "third attempt escalates");
}

void test_offline_sticky() {
    StreamThresholds th;
    th.reconnect_threshold = 1;
    StreamStateMachine m("VC-04", th);
    m.start(0);
    m.on_reconnect_attempt(1);
    check(m.state() == StreamState::Offline, "arrived offline");
    m.tick(100000);
    check(m.state() == StreamState::Offline, "offline sticky under tick");
    check(m.drain_events().size() == 3, "offline chain event buffer");
}

}  // namespace

int main() {
    std::printf("== verify-stream-ext (software tester fixture, ITEM-005) ==\n");
    test_classify_boundaries();
    test_non_live_and_explicit_reconnect();
    test_degraded_chain_escalation_and_recovery();
    test_attempts_without_frames();
    test_offline_sticky();
    std::printf("== %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
