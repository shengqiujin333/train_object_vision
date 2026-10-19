// railhmi-core 轻量跟踪与多帧确认协同测试（视觉推理 ITEM-008 纯逻辑）。
// 覆盖 TC-T-TRK-01..04：同目标 id 稳定/跟踪不可用路径/目标互不干扰/重连（generation）清理。

#include <string>
#include <vector>

#include "test_util.h"

#include "railhmi/core/rules.h"
#include "railhmi/core/vision_semantic_adapter.h"
#include "railhmi/core/vision_tracker.h"

namespace {

using namespace railhmi::core;

EnvelopeBox eb(const std::string& label, double x, double y, double w = 0.1, double h = 0.1,
               double conf = 0.9) {
    EnvelopeBox b;
    b.label = label;
    b.x = static_cast<float>(x);
    b.y = static_cast<float>(y);
    b.w = static_cast<float>(w);
    b.h = static_cast<float>(h);
    b.confidence = static_cast<float>(conf);
    b.in_roi = true;  // 夹具：默认在轨道内（ITEM-005 语义）
    return b;
}

FrameId fid(std::uint64_t gen, std::uint64_t seq) {
    FrameId f;
    f.session_id = "s";
    f.camera_id = "CAM-01";
    f.generation = gen;
    f.seq = seq;
    return f;
}

InferenceEnvelope mk(const FrameId& f, const std::vector<EnvelopeBox>& boxes) {
    EnvelopeBuildInput in;
    in.frame = f;
    in.profile = AiProfile::Front;
    in.model_version = "m";
    in.capture_mono_us = 1000;
    in.result_mono_us = 20000;
    in.requires_segmentation = false;
    InferenceEnvelope e = build_envelope(in);
    e.boxes = boxes;
    return e;
}

SemanticConfig scfg(TrackPolicy p, double conf = 0.5) {
    SemanticConfig c;
    c.profile = AiProfile::Front;
    c.allowed_labels = {"person", "obstacle"};
    c.known_labels = {"person", "obstacle"};
    c.confidence_min = conf;
    c.track_policy = p;
    c.tracker.max_misses = 3;
    c.tracker.max_tracks = 32;
    c.tracker.max_gap_ms = 1000;
    return c;
}

RuleConfig rcfg() {
    RuleConfig r;
    r.confirm_frames = 3;
    r.confirm_window_ms = 500;
    r.confidence_min = 0.5;
    r.suppress_window_ms = 3000;
    return r;
}

void test_tracker_basics() {
    LightweightTracker t;
    auto a = t.update({eb("person", 0.4, 0.7)}, 100);
    railhmi_test::check(a.size() == 1 && a[0].track_id == 1 && a[0].is_new, "new track id 1");
    auto b = t.update({eb("person", 0.405, 0.705)}, 200);
    railhmi_test::check(b.size() == 1 && b[0].track_id == 1 && !b[0].is_new, "same target same id");
    auto c = t.update({eb("person", 0.85, 0.1)}, 300);
    railhmi_test::check(c.size() == 1 && c[0].track_id == 2, "new target new id");
    railhmi_test::check(a[0].track_id != 0 && c[0].track_id != 0, "ids never 0");
    // 不同标签同位置不串 id
    LightweightTracker t2;
    (void)t2.update({eb("person", 0.4, 0.7)}, 100);
    auto r = t2.update({eb("obstacle", 0.4, 0.7)}, 200);
    railhmi_test::check(r.size() == 1 && r[0].track_id == 2, "label change -> new id");
}

void test_tracker_loss_and_reset() {
    TrackerConfig cfg;
    cfg.max_misses = 2;
    LightweightTracker t(cfg);
    (void)t.update({eb("person", 0.4, 0.7)}, 100);
    for (int i = 0; i < 3; ++i) (void)t.update({}, 200 + i * 100);
    railhmi_test::check(t.size() == 0, "missed track dropped");
    auto r = t.update({eb("person", 0.4, 0.7)}, 700);
    railhmi_test::check(r.size() == 1 && r[0].track_id == 2 && r[0].is_new,
                        "reappear -> new id (not reused)");
    t.reset();
    railhmi_test::check(t.size() == 0, "reset clears tracks");
    auto r2 = t.update({eb("person", 0.4, 0.7)}, 800);
    railhmi_test::check(r2.size() == 1 && r2[0].track_id == 3 && r2[0].track_id != 0,
                        "post-reset id continues (no reuse)");
    // 时间鸿沟
    TrackerConfig g;
    g.max_gap_ms = 100;
    LightweightTracker gt(g);
    (void)gt.update({eb("person", 0.4, 0.7)}, 100);
    auto rg = gt.update({eb("person", 0.4, 0.7)}, 1000);
    railhmi_test::check(rg.size() == 1 && rg[0].track_id == 2, "stale gap -> new id");
    // max_tracks
    TrackerConfig m;
    m.max_tracks = 1;
    LightweightTracker mt(m);
    auto rm = mt.update({eb("person", 0.1, 0.1), eb("person", 0.8, 0.8)}, 100);
    railhmi_test::check(rm.size() == 2 && rm[0].track_id == 1 && rm[1].track_id == -1 &&
                            mt.untracked() == 1,
                        "max_tracks -> untracked (-1)");
}

void test_adapter_lightweight() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg(TrackPolicy::Lightweight));
    std::size_t events = 0;
    int32_t id = -1;
    for (int i = 0; i < 3; ++i) {
        SemanticResult r;
        auto ev = ad.adapt_and_dispatch(mk(fid(1, i + 1), {eb("person", 0.4 + 0.005 * i, 0.7)}),
                                        (i + 1) * 100, &rules, &r);
        events += ev.size();
        if (!r.detections.empty()) id = r.detections[0].track_id;
    }
    railhmi_test::check(events == 1 && rules.events().size() == 1 && id == 1,
                        "lightweight: same target -> 1 event");
    // 两个目标 -> 两个事件，互不清除
    RuleEngine rules2(rcfg());
    SemanticAdapter ad2(scfg(TrackPolicy::Lightweight));
    for (int i = 0; i < 3; ++i)
        (void)ad2.adapt_and_dispatch(
            mk(fid(1, i + 1), {eb("person", 0.1 + 0.005 * i, 0.7), eb("person", 0.7 + 0.005 * i, 0.7)}),
            (i + 1) * 100, &rules2, nullptr);
    railhmi_test::check(rules2.events().size() == 2 && rules2.events()[0].track_id !=
                            rules2.events()[1].track_id,
                        "two targets independent");
}

void test_adapter_generation_and_modes() {
    // generation 变化：tracker reset + 规则清理 + 新 id
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg(TrackPolicy::Lightweight));
    for (int i = 0; i < 2; ++i)
        (void)ad.adapt_and_dispatch(mk(fid(1, i + 1), {eb("person", 0.4, 0.7)}), (i + 1) * 100,
                                    &rules, nullptr);
    SemanticResult r3;
    auto ev3 = ad.adapt_and_dispatch(mk(fid(2, 1), {eb("person", 0.4, 0.7)}), 300, &rules, &r3);
    railhmi_test::check(ev3.empty(), "generation change: no alarm on first new frame");
    railhmi_test::check(r3.stats.tracker_resets == 1 && r3.stats.generation_reset_keys > 0,
                        "generation resets tracker + rule keys");
    const int32_t nid = r3.detections.empty() ? -1 : r3.detections[0].track_id;
    railhmi_test::check(nid >= 1 && nid != 1, "new generation new id");
    auto ev4 = ad.adapt_and_dispatch(mk(fid(2, 2), {eb("person", 0.4, 0.7)}), 400, &rules, nullptr);
    auto ev5 = ad.adapt_and_dispatch(mk(fid(2, 3), {eb("person", 0.4, 0.7)}), 500, &rules, nullptr);
    railhmi_test::check(ev4.empty() && ev5.size() == 1, "new generation re-confirms");

    // CameraLevel：共享 -1（不是 0），一个相机级事件
    RuleEngine rules2(rcfg());
    SemanticAdapter ad2(scfg(TrackPolicy::CameraLevel));
    int32_t id = 0;
    for (int i = 0; i < 3; ++i) {
        SemanticResult r;
        (void)ad2.adapt_and_dispatch(
            mk(fid(1, i + 1), {eb("person", 0.1, 0.7), eb("person", 0.7, 0.7)}), (i + 1) * 100,
            &rules2, &r);
        if (!r.detections.empty()) id = r.detections[0].track_id;
    }
    railhmi_test::check(id == -1 && rules2.events().size() == 1 &&
                            rules2.events()[0].track_id == -1,
                        "camera-level: one event, null track (-1)");

    // EnvelopeOnly 回归：null track 丢弃
    RuleEngine rules3(rcfg());
    SemanticAdapter ad3(scfg(TrackPolicy::EnvelopeOnly));
    SemanticResult r;
    (void)ad3.adapt_and_dispatch(mk(fid(1, 1), {eb("person", 0.4, 0.7)}), 100, &rules3, &r);
    railhmi_test::check(r.detections.empty() && r.stats.dropped_no_track == 1,
                        "envelope_only: null track dropped");
}

}  // namespace

void run_vision_tracker_tests() {
    test_tracker_basics();
    test_tracker_loss_and_reset();
    test_adapter_lightweight();
    test_adapter_generation_and_modes();
}
