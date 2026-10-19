// railhmi-core 语义适配与规则接入测试（视觉推理 ITEM-007 纯逻辑）。
// 覆盖 TC-T-SEM-01..05：person 语义/未知 label 拦截/多目标聚合排序/受控清理/无 track 禁用 0。

#include <string>
#include <vector>

#include "test_util.h"

#include "railhmi/core/rules.h"
#include "railhmi/core/vision_semantic_adapter.h"

namespace {

using namespace railhmi::core;

FrameId fid(std::uint64_t gen, std::uint64_t seq) {
    FrameId f;
    f.session_id = "s";
    f.camera_id = "CAM-01";
    f.generation = gen;
    f.seq = seq;
    return f;
}

DetectBox bx(const std::string& label, double x, double y, double w, double h, double score = 0.9) {
    DetectBox b;
    b.x = static_cast<float>(x);
    b.y = static_cast<float>(y);
    b.w = static_cast<float>(w);
    b.h = static_cast<float>(h);
    b.score = static_cast<float>(score);
    b.label = label;
    return b;
}

InferenceEnvelope mk(const FrameId& f, const std::vector<DetectBox>& boxes,
                     const std::vector<std::int32_t>& tracks, const std::vector<char>& roi) {
    EnvelopeBuildInput in;
    in.frame = f;
    in.profile = AiProfile::Front;
    in.model_version = "m";
    in.capture_mono_us = 1000;
    in.result_mono_us = 20000;
    in.requires_segmentation = false;
    in.boxes = &boxes;
    in.track_ids = tracks;
    in.in_roi_flags = roi;
    return build_envelope(in);
}

SemanticConfig scfg(double conf = 0.5) {
    SemanticConfig c;
    c.profile = AiProfile::Front;
    c.allowed_labels = {"person", "obstacle"};
    c.known_labels = {"person", "obstacle"};
    c.confidence_min = conf;
    c.require_track = true;
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

void test_person_and_unknown() {
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg());
        std::size_t events = 0;
        for (int i = 0; i < 3; ++i) {
            auto ev = ad.adapt_and_dispatch(
                mk(fid(1, i + 1), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}), (i + 1) * 100, &rules,
                nullptr);
            events += ev.size();
        }
        railhmi_test::check(events == 1 && rules.events().size() == 1, "person on rail -> 1 event");
        railhmi_test::check(!rules.events().empty() &&
                                rules.events()[0].type == AlarmType::PersonOnRail &&
                                rules.events()[0].level == AlarmLevel::L1,
                            "person_on_rail L1");
    }
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg());
        std::size_t events = 0;
        long long unknown = 0;
        for (int i = 0; i < 3; ++i) {
            SemanticResult r;
            auto ev = ad.adapt_and_dispatch(
                mk(fid(1, i + 1), {bx("car", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}), (i + 1) * 100, &rules, &r);
            events += ev.size();
            unknown += r.stats.blocked_unknown_label;
        }
        railhmi_test::check(events == 0 && unknown == 3, "unknown label blocked, no alarm");
        railhmi_test::check(rules.events().empty(), "no event for unknown label");
    }
}

void test_multi_and_aggregate() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    std::size_t events = 0;
    for (int i = 0; i < 3; ++i) {
        auto ev = ad.adapt_and_dispatch(
            mk(fid(1, i + 1), {bx("person", 0.2, 0.7, 0.1, 0.1), bx("person", 0.6, 0.7, 0.1, 0.1)},
               {1, 2}, {1, 1}),
            (i + 1) * 100, &rules, nullptr);
        events += ev.size();
    }
    railhmi_test::check(events == 2, "multi target -> 2 events (one per track)");
    // 同帧重复框聚合 + 排序
    SemanticResult r = ad.adapt(mk(fid(1, 9),
                                   {bx("person", 0.5, 0.7, 0.1, 0.1, 0.6),
                                    bx("person", 0.5, 0.7, 0.1, 0.1, 0.9),
                                    bx("person", 0.1, 0.7, 0.1, 0.1, 0.8)},
                                   {5, 5, 2}, {1, 1, 1}),
                              100);
    railhmi_test::check(r.detections.size() == 2 && r.stats.aggregated == 1, "same track aggregated");
    railhmi_test::check(r.detections[0].track_id == 2 && r.detections[1].track_id == 5,
                        "sorted by track_id");
    railhmi_test::check(r.detections[1].confidence == 0.9f, "keeps highest confidence");
}

void test_off_rail_does_not_clear() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    (void)ad.adapt_and_dispatch(mk(fid(1, 1), {bx("person", 0.2, 0.7, 0.1, 0.1)}, {1}, {1}), 100, &rules,
                                nullptr);
    (void)ad.adapt_and_dispatch(mk(fid(1, 2), {bx("person", 0.2, 0.7, 0.1, 0.1)}, {1}, {1}), 200, &rules,
                                nullptr);
    auto ev = ad.adapt_and_dispatch(
        mk(fid(1, 3), {bx("person", 0.2, 0.7, 0.1, 0.1), bx("person", 0.2, 0.1, 0.1, 0.1)}, {1, 2},
           {1, 0}),
        300, &rules, nullptr);
    railhmi_test::check(ev.size() == 1, "off-rail target does not clear other target's confirm");
}

void test_generation_reset() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    (void)ad.adapt_and_dispatch(mk(fid(1, 1), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}), 100, &rules,
                                nullptr);
    (void)ad.adapt_and_dispatch(mk(fid(1, 2), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}), 200, &rules,
                                nullptr);
    SemanticResult r3;
    auto ev3 = ad.adapt_and_dispatch(mk(fid(2, 1), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}), 300,
                                     &rules, &r3);
    railhmi_test::check(ev3.empty(), "generation change resets confirm");
    railhmi_test::check(r3.stats.generation_reset_keys > 0, "generation reset keys observable");
    auto ev4 = ad.adapt_and_dispatch(mk(fid(2, 2), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}), 400,
                                     &rules, nullptr);
    auto ev5 = ad.adapt_and_dispatch(mk(fid(2, 3), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {1}, {1}), 500,
                                     &rules, nullptr);
    railhmi_test::check(ev4.empty() && ev5.size() == 1, "new generation confirms from scratch");
}

void test_controlled_cleanup_scope() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    for (int i = 0; i < 2; ++i) {
        (void)ad.adapt_and_dispatch(
            mk(fid(1, i + 1), {bx("person", 0.2, 0.7, 0.1, 0.1), bx("person", 0.6, 0.7, 0.1, 0.1)},
               {1, 2}, {1, 1}),
            (i + 1) * 100, &rules, nullptr);
    }
    // 按 track 清理 track2：track1 的确认应保留，第 3 帧仍能报警
    const int keys = SemanticAdapter::invalidate_track(&rules, "CAM-01", 2);
    railhmi_test::check(keys > 0, "invalidate_track clears keys");
    auto ev = ad.adapt_and_dispatch(
        mk(fid(1, 3), {bx("person", 0.2, 0.7, 0.1, 0.1), bx("person", 0.6, 0.7, 0.1, 0.1)}, {1, 2},
           {1, 1}),
        300, &rules, nullptr);
    railhmi_test::check(ev.size() == 1 && !ev.empty() && ev[0].track_id == 1,
                        "invalidate_track does not clear unrelated track");
    // 按相机清理：两个 track 的确认都清掉
    const int all = SemanticAdapter::invalidate_generation(&rules, "CAM-01");
    railhmi_test::check(all >= 0, "invalidate_camera returns key count");
    auto ev2 = ad.adapt_and_dispatch(
        mk(fid(1, 4), {bx("person", 0.2, 0.7, 0.1, 0.1), bx("person", 0.6, 0.7, 0.1, 0.1)}, {1, 2},
           {1, 1}),
        400, &rules, nullptr);
    railhmi_test::check(ev2.empty(), "invalidate_camera clears both tracks");
    railhmi_test::check(rules.events().size() == 1, "cleanup never deletes events");
}

void test_no_track_and_low_conf() {
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg());
        std::size_t events = 0;
        long long no_track = 0;
        for (int i = 0; i < 3; ++i) {
            SemanticResult r;
            auto ev = ad.adapt_and_dispatch(
                mk(fid(1, i + 1), {bx("person", 0.45, 0.7, 0.1, 0.1)}, {-1}, {1}), (i + 1) * 100, &rules,
                &r);
            events += ev.size();
            no_track += r.stats.dropped_no_track;
            railhmi_test::check(r.detections.empty(), "null track not emitted as track 0");
        }
        railhmi_test::check(events == 0 && no_track == 3, "null track dropped + counted");
    }
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg(0.5));
        std::size_t events = 0;
        long long low = 0;
        for (int i = 0; i < 3; ++i) {
            SemanticResult r;
            auto ev = ad.adapt_and_dispatch(
                mk(fid(1, i + 1), {bx("person", 0.45, 0.7, 0.1, 0.1, 0.3)}, {1}, {1}), (i + 1) * 100,
                &rules, &r);
            events += ev.size();
            low += r.stats.dropped_low_confidence;
        }
        railhmi_test::check(events == 0 && low == 3, "low confidence not dispatched");
    }
}

void test_obstacle_and_unavailable() {
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg());
        for (int i = 0; i < 3; ++i)
            (void)ad.adapt_and_dispatch(mk(fid(1, i + 1), {bx("obstacle", 0.45, 0.7, 0.1, 0.1)}, {3}, {1}),
                                        (i + 1) * 100, &rules, nullptr);
        railhmi_test::check(rules.events().size() == 1 &&
                                rules.events()[0].type == AlarmType::FrontObstacle,
                            "obstacle -> FrontObstacle");
    }
    {
        RuleEngine rules(rcfg());
        SemanticAdapter ad(scfg());
        InferenceEnvelope e = make_unavailable_envelope(fid(1, 1), AiProfile::Front, "m", 0, 1000, "");
        SemanticResult r;
        auto ev = ad.adapt_and_dispatch(e, 100, &rules, &r);
        railhmi_test::check(ev.empty() && !r.available && r.detections.empty() &&
                                r.reason == semantic_reason::kUnavailable(),
                            "unavailable -> no detections/alarms");
    }
}

}  // namespace

void run_vision_semantic_adapter_tests() {
    test_person_and_unknown();
    test_multi_and_aggregate();
    test_off_rail_does_not_clear();
    test_generation_reset();
    test_controlled_cleanup_scope();
    test_no_track_and_low_conf();
    test_obstacle_and_unavailable();
}
