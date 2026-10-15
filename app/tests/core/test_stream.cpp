// railhmi-core 流状态机与质量判定测试（ITEM-005 / TC-CORE-02、TC-CORE-03）。
// 用模拟单调时钟驱动全部六态与阈值升级；复现“断流→重连计数→达阈值升级”序列。

#include "test_util.h"

#include <string>
#include <vector>

#include "railhmi/core/stream_state.h"

namespace {

using namespace railhmi::core;

bool has_event(const std::vector<StreamEvent>& evs, StreamEventKind k) {
    for (const auto& e : evs) if (e.kind == k) return true;
    return false;
}

void test_six_states_and_escalation() {
    StreamThresholds th;
    th.first_frame_timeout_ms = 100;
    th.freeze_timeout_ms = 50;
    th.reconnect_threshold = 3;
    th.reconnect_backoff_ms = 20;
    StreamStateMachine m("CAM-01", th);

    railhmi_test::check(m.state() == StreamState::Offline, "initial state = offline");
    railhmi_test::check(!m.may_present_as_live(0), "offline cannot present as live");

    m.start(0);
    railhmi_test::check(m.state() == StreamState::Connecting, "start -> connecting");
    railhmi_test::check(!m.may_present_as_live(10), "connecting cannot present as live");

    // 首帧 -> LIVE
    QualityFlags ok;
    m.on_frame(10, ok);
    railhmi_test::check(m.state() == StreamState::Live, "first frame -> live");
    {
        auto evs = m.drain_events();
        railhmi_test::check(has_event(evs, StreamEventKind::Started), "Started event emitted");
        railhmi_test::check(has_event(evs, StreamEventKind::FirstFrame), "FirstFrame event emitted");
    }
    railhmi_test::check(m.may_present_as_live(30), "live presents fresh frame as live");

    // 超时无新帧 -> FROZEN
    m.tick(10 + th.freeze_timeout_ms);
    railhmi_test::check(m.state() == StreamState::Frozen, "no new frame -> frozen");
    railhmi_test::check(!m.may_present_as_live(10 + th.freeze_timeout_ms),
                        "frozen cannot present old frame as live");
    railhmi_test::check(has_event(m.events(), StreamEventKind::Freeze), "Freeze event emitted");

    // FROZEN -> RECONNECTING（计数 1）
    long long t = 10 + th.freeze_timeout_ms;
    m.tick(t + th.freeze_timeout_ms);
    railhmi_test::check(m.state() == StreamState::Reconnecting, "frozen -> reconnecting");
    railhmi_test::check(m.reconnect_count() == 1, "reconnect count = 1");

    // RECONNECTING -> CONNECTING（退避）
    t += th.freeze_timeout_ms;
    m.tick(t + th.reconnect_backoff_ms);
    railhmi_test::check(m.state() == StreamState::Connecting, "reconnecting -> connecting (retry)");

    // 再次首帧超时 -> 计数 2
    t += th.reconnect_backoff_ms;
    m.tick(t + th.first_frame_timeout_ms);
    railhmi_test::check(m.state() == StreamState::Reconnecting && m.reconnect_count() == 2,
                        "retry timeout -> reconnecting count 2");
    t += th.first_frame_timeout_ms;

    // 再次退避 -> CONNECTING，再超时 -> 计数 3 >= 阈值 -> OFFLINE + 升级
    m.tick(t + th.reconnect_backoff_ms);
    railhmi_test::check(m.state() == StreamState::Connecting, "retry again -> connecting");
    t += th.reconnect_backoff_ms;
    m.tick(t + th.first_frame_timeout_ms);
    railhmi_test::check(m.state() == StreamState::Offline, "threshold reached -> offline");
    railhmi_test::check(m.reconnect_count() == 3, "reconnect count = 3 (threshold)");
    railhmi_test::check(has_event(m.events(), StreamEventKind::Escalated),
                        "Escalated device event emitted at threshold");
    railhmi_test::check(!m.may_present_as_live(t), "offline cannot present as live");

    // 恢复 -> LIVE，计数清零
    m.on_frame(t + 1, ok);
    railhmi_test::check(m.state() == StreamState::Live, "recover from offline -> live");
    railhmi_test::check(m.reconnect_count() == 0, "reconnect count reset on recovery");
    railhmi_test::check(has_event(m.events(), StreamEventKind::Recovered), "Recovered event emitted");
}

void test_source_error_escalation() {
    StreamThresholds th;
    th.reconnect_threshold = 2;
    StreamStateMachine m("CAM-02", th);
    m.start(0);
    m.on_frame(1, QualityFlags{});
    railhmi_test::check(m.state() == StreamState::Live, "source-error test live");

    m.on_source_error(2, "rtsp error");
    railhmi_test::check(m.state() == StreamState::Reconnecting && m.reconnect_count() == 1,
                        "source error -> reconnecting count 1");
    m.on_source_error(3, "rtsp error");
    railhmi_test::check(m.state() == StreamState::Offline && m.reconnect_count() == 2,
                        "second source error -> offline (threshold 2)");
    railhmi_test::check(has_event(m.events(), StreamEventKind::Escalated),
                        "source-error escalation event emitted");
    railhmi_test::check(has_event(m.events(), StreamEventKind::SourceError),
                        "SourceError event emitted");
}

void test_degraded_quality() {
    StreamThresholds th;
    StreamStateMachine m("CAM-04", th);
    m.start(0);
    QualityFlags dark;
    dark.too_dark = true;
    m.on_frame(5, dark);
    railhmi_test::check(m.state() == StreamState::Degraded, "quality flag -> degraded");
    railhmi_test::check(m.is_streaming(), "degraded counts as streaming (degraded available)");
    railhmi_test::check(m.may_present_as_live(10), "degraded may present fresh frame");
    {
        auto evs = m.drain_events();
        bool q = false;
        for (auto& e : evs) if (e.kind == StreamEventKind::QualityDegraded) q = true;
        railhmi_test::check(q, "QualityDegraded event emitted");
    }
}

void test_classify_quality() {
    QualityThresholds t;  // dark<40, bright>220, stddev<3 occluded, lapvar<30 blurred
    railhmi_test::check(classify_quality(FrameStats{10, 40, 200}, t).too_dark,
                        "classify: low mean -> too_dark");
    railhmi_test::check(classify_quality(FrameStats{250, 40, 200}, t).too_bright,
                        "classify: high mean -> too_bright");
    railhmi_test::check(classify_quality(FrameStats{128, 1, 200}, t).occluded,
                        "classify: near-uniform -> occluded");
    railhmi_test::check(classify_quality(FrameStats{128, 40, 5}, t).blurred,
                        "classify: low lapvar -> blurred");
    QualityFlags normal = classify_quality(FrameStats{128, 40, 200}, t);
    railhmi_test::check(!normal.any(), "classify: normal frame -> no flags");
    // 无效统计不产生标志
    railhmi_test::check(!classify_quality(FrameStats{-1, -1, -1}, t).any(),
                        "classify: invalid stats -> no flags");
}

}  // namespace

void run_stream_tests() {
    test_six_states_and_escalation();
    test_source_error_escalation();
    test_degraded_quality();
    test_classify_quality();
}
