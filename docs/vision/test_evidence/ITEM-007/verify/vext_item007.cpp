// vext_item007.cpp — ITEM-007 验证自有扩展夹具（software_tester.software_verification）
// 断言 CoreTests/selftest 未覆盖的验收边界。板上 g++ 直编链接 librailhmi_core.a。
// 判据全部为「正确语义在场」方向：板上 FAIL 即真缺陷呈证。
//
// 依据：任务书（白名单严格映射/未知拦截/聚合排序/禁 0 冒充/受控清理不误清）、
// InterfaceContract §6、实现语义映射表（vision_semantic_adapter.h 注释）、
// rules.cpp erase_keys 后缀匹配边界、Detection/AlarmEvent 公开观察面。

#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/rules.h"
#include "railhmi/core/vision_inference_envelope.h"
#include "railhmi/core/vision_semantic_adapter.h"

using namespace railhmi::core;

namespace {

int g_fail = 0;

void check(bool ok, const std::string& name, const std::string& detail = "") {
    std::printf("vext %s %s%s\n", ok ? "PASS" : "FAIL", name.c_str(),
                detail.empty() ? "" : (" :: " + detail).c_str());
    if (!ok) ++g_fail;
}

FrameId fid(const std::string& cam, unsigned long long gen, unsigned long long seq) {
    FrameId f;
    f.session_id = "s-vext7";
    f.camera_id = cam;
    f.generation = gen;
    f.seq = seq;
    return f;
}

DetectBox bx(const std::string& label, double x, double score) {
    DetectBox b;
    b.x = static_cast<float>(x);
    b.y = 0.7f;
    b.w = 0.1f;
    b.h = 0.1f;
    b.score = static_cast<float>(score);
    b.class_id = label == "person" ? 0 : 1;
    b.label = label;
    return b;
}

InferenceEnvelope mk(const FrameId& f, const std::vector<DetectBox>& boxes,
                     const std::vector<std::int32_t>& tracks, const std::vector<char>& roi,
                     const std::vector<float>& dists = {}) {
    EnvelopeBuildInput in;
    in.frame = f;
    in.profile = AiProfile::Front;
    in.model_version = "rknn-2.3.2/yolov5s";
    in.capture_mono_us = 1000;
    in.result_mono_us = 21000;
    in.detect_ready = true;
    in.seg_ready = true;
    in.requires_segmentation = false;
    in.boxes = &boxes;
    in.track_ids = tracks;
    in.in_roi_flags = roi;
    in.distances_m = dists;
    return build_envelope(in);
}

SemanticConfig scfg(double conf_min = 0.5) {
    SemanticConfig c;
    c.profile = AiProfile::Front;
    c.allowed_labels = {"person", "obstacle"};
    c.known_labels = {"person", "obstacle"};
    c.confidence_min = conf_min;
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

struct Tick {
    std::vector<DetectBox> boxes;
    std::vector<std::int32_t> tracks;
    std::vector<char> roi;
};

Tick tick(std::vector<DetectBox> b, std::vector<std::int32_t> t, std::vector<char> r) {
    return Tick{std::move(b), std::move(t), std::move(r)};
}

// 跑一组帧（全部 gen1、camera 可变由 FrameId 决定），返回逐帧事件数与 stats 增量。
struct RunOut {
    std::vector<long long> per_frame;
    long long gen_reset = 0;
    long long track_reset = 0;
    long long no_track = 0;
    long long not_allowed = 0;
    long long unknown = 0;
    long long low_conf = 0;
};

RunOut run(RuleEngine* rules, SemanticAdapter* ad, const std::vector<FrameId>& ids,
           const std::vector<Tick>& ticks) {
    RunOut o;
    for (std::size_t i = 0; i < ticks.size(); ++i) {
        InferenceEnvelope e = mk(ids[i], ticks[i].boxes, ticks[i].tracks, ticks[i].roi);
        SemanticResult r;
        auto ev = ad->adapt_and_dispatch(e, static_cast<long long>(i + 1) * 100, rules, &r);
        o.per_frame.push_back(static_cast<long long>(ev.size()));
        o.gen_reset += r.stats.generation_reset_keys;
        o.track_reset += r.stats.track_reset_keys;
        o.no_track += r.stats.dropped_no_track;
        o.not_allowed += r.stats.blocked_not_allowed;
        o.unknown += r.stats.blocked_unknown_label;
        o.low_conf += r.stats.dropped_low_confidence;
    }
    return o;
}

// 场景基础：camera 上单 track 命中 n 帧。
FrameId fid1(unsigned long long gen, unsigned long long seq) {
    return fid("CAM-01", gen, seq);
}

// g01/g03 共用：track1 与 track11 各确认 2 帧。
std::vector<Tick> two_tracks_two_frames(std::int32_t ta, std::int32_t tb) {
    std::vector<Tick> fs;
    for (int i = 0; i < 2; ++i) {
        fs.push_back(tick({bx("person", 0.2, 0.9), bx("person", 0.6, 0.9)},
                          {ta, tb}, {1, 1}));
    }
    return fs;
}

void g01_invalidate_track1_keeps_track11_confirm() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    std::vector<FrameId> ids = {fid1(1, 1), fid1(1, 2)};
    (void)run(&rules, &ad, ids, two_tracks_two_frames(1, 11));
    const int keys = SemanticAdapter::invalidate_track(&rules, "CAM-01", 1);
    // 正确：只有 track1 自己的 confirm key 被清（两 track 均未 emit，各 1 个 confirm key）。
    check(keys == 1, "g01_invalidate_track1_keeps_track11_confirm", "keys=" + std::to_string(keys));
    // 第 3 帧：track1 从 1 重新累计，track11 第 3 帧确认。
    auto ev = run(&rules, &ad, {fid1(1, 3)},
                  {tick({bx("person", 0.2, 0.9), bx("person", 0.6, 0.9)}, {1, 11}, {1, 1})});
    check(ev.per_frame.size() == 1 && ev.per_frame[0] == 1 && !rules.events().empty() &&
              rules.events()[0].track_id == 11,
          "g01_track11_confirms_on_3rd_frame_after_track1_cleanup",
          "per0=" + std::to_string(ev.per_frame.empty() ? -1 : ev.per_frame[0]));
}

void g02_invalidate_track0_keeps_track10_confirm() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    std::vector<FrameId> ids = {fid1(1, 1), fid1(1, 2)};
    (void)run(&rules, &ad, ids, two_tracks_two_frames(0, 10));
    const int keys = SemanticAdapter::invalidate_track(&rules, "CAM-01", 0);
    check(keys == 1, "g02_invalidate_track0_keeps_track10_confirm", "keys=" + std::to_string(keys));
    auto ev = run(&rules, &ad, {fid1(1, 3)},
                  {tick({bx("person", 0.2, 0.9), bx("person", 0.6, 0.9)}, {0, 10}, {1, 1})});
    check(ev.per_frame.size() == 1 && ev.per_frame[0] == 1 && !rules.events().empty() &&
              rules.events()[0].track_id == 10,
          "g02_track10_confirms_on_3rd_frame_after_track0_cleanup",
          "per0=" + std::to_string(ev.per_frame.empty() ? -1 : ev.per_frame[0]));
}

void g03_invalidate_track11_keeps_track1_confirm() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    std::vector<FrameId> ids = {fid1(1, 1), fid1(1, 2)};
    (void)run(&rules, &ad, ids, two_tracks_two_frames(1, 11));
    const int keys = SemanticAdapter::invalidate_track(&rules, "CAM-01", 11);
    check(keys == 1, "g03_invalidate_track11_keeps_track1_confirm", "keys=" + std::to_string(keys));
    auto ev = run(&rules, &ad, {fid1(1, 3)},
                  {tick({bx("person", 0.2, 0.9), bx("person", 0.6, 0.9)}, {1, 11}, {1, 1})});
    check(ev.per_frame.size() == 1 && ev.per_frame[0] == 1 && !rules.events().empty() &&
              rules.events()[0].track_id == 1,
          "g03_track1_confirms_on_3rd_frame_after_track11_cleanup",
          "per0=" + std::to_string(ev.per_frame.empty() ? -1 : ev.per_frame[0]));
}

void g04_track_zero_is_legal_not_null() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    std::vector<Tick> fs;
    for (int i = 0; i < 3; ++i)
        fs.push_back(tick({bx("person", 0.45, 0.9)}, {0}, {1}));
    std::vector<FrameId> ids = {fid1(1, 1), fid1(1, 2), fid1(1, 3)};
    RunOut o = run(&rules, &ad, ids, fs);
    check(o.per_frame == std::vector<long long>{0, 0, 1} && rules.events().size() == 1,
          "g04_track_zero_is_legal_not_null",
          "ev=" + std::to_string(rules.events().size()));
    check(rules.events()[0].track_id == 0, "g04_event_carries_track_zero",
          "track=" + std::to_string(rules.events()[0].track_id));
}

void g05_no_track_camera_level_adaptation_track_minus1() {
    RuleEngine rules(rcfg());
    SemanticConfig c = scfg();
    c.require_track = false;  // 显式相机级事件适配路径（契约 §6 允许，跟踪未就绪）
    SemanticAdapter ad(c);
    std::vector<Tick> fs;
    for (int i = 0; i < 3; ++i)
        fs.push_back(tick({bx("person", 0.45, 0.9)}, {-1}, {1}));
    std::vector<FrameId> ids = {fid1(1, 1), fid1(1, 2), fid1(1, 3)};
    RunOut o = run(&rules, &ad, ids, fs);
    check(o.no_track == 0 && o.per_frame == std::vector<long long>{0, 0, 1},
          "g05_camera_level_adaptation_confirms_without_track",
          "no_track=" + std::to_string(o.no_track));
    check(rules.events().size() == 1 && rules.events()[0].track_id == -1,
          "g05_event_track_is_minus1_never_fabricated_0",
          "track=" + std::to_string(rules.events()[0].track_id));
}

void g06_known_but_not_allowed_blocked() {
    RuleEngine rules(rcfg());
    SemanticConfig c = scfg();
    c.known_labels = {"person", "obstacle", "car"};  // 模型已知 ⊃ front 受支持
    c.allowed_labels = {"person", "obstacle"};
    SemanticAdapter ad(c);
    std::vector<Tick> fs;
    for (int i = 0; i < 3; ++i)
        fs.push_back(tick({bx("car", 0.45, 0.9)}, {1}, {1}));
    std::vector<FrameId> ids = {fid1(1, 1), fid1(1, 2), fid1(1, 3)};
    RunOut o = run(&rules, &ad, ids, fs);
    check(o.not_allowed == 3 && o.unknown == 0, "g06_known_but_not_allowed_counted",
          "not_allowed=" + std::to_string(o.not_allowed) + " unknown=" + std::to_string(o.unknown));
    check(rules.events().empty(), "g06_known_but_not_allowed_no_alarm",
          "events=" + std::to_string(rules.events().size()));
}

void g07_empty_and_mixed_case_labels_blocked() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    std::vector<Tick> fs = {tick({bx("", 0.45, 0.9)}, {1}, {1}),
                            tick({bx("Person", 0.45, 0.9)}, {1}, {1}),
                            tick({bx("dog", 0.45, 0.9)}, {1}, {1})};
    std::vector<FrameId> ids = {fid1(1, 1), fid1(1, 2), fid1(1, 3)};
    RunOut o = run(&rules, &ad, ids, fs);
    check(o.unknown == 3 && rules.events().empty(),
          "g07_empty_case_and_unknown_labels_strictly_blocked",
          "unknown=" + std::to_string(o.unknown) + " events=" +
              std::to_string(rules.events().size()));
}

void g08_generation_reset_isolated_to_camera() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    // CAM-01 与 CAM-02 各自 track1 确认 2 帧（同 adapter，generation 均 1）。
    std::vector<FrameId> ids = {fid("CAM-01", 1, 1), fid("CAM-02", 1, 1),
                                fid("CAM-01", 1, 2), fid("CAM-02", 1, 2)};
    std::vector<Tick> ticks;
    for (int i = 0; i < 2; ++i) {
        ticks.push_back(tick({bx("person", 0.2, 0.9)}, {1}, {1}));   // CAM-01
        ticks.push_back(tick({bx("person", 0.6, 0.9)}, {1}, {1}));   // CAM-02
    }
    (void)run(&rules, &ad, ids, ticks);
    // CAM-01 换代第 3 帧（先），再 CAM-02 第 3 帧（同代）：只有 CAM-02 确认。
    SemanticResult r1;
    (void)ad.adapt_and_dispatch(mk(fid("CAM-01", 2, 1), {bx("person", 0.2, 0.9)}, {1}, {1}),
                                300, &rules, &r1);
    check(r1.stats.generation_reset_keys > 0, "g08_generation_reset_keys_on_cam01",
          "keys=" + std::to_string(r1.stats.generation_reset_keys));
    auto ev2 = run(&rules, &ad, {fid("CAM-02", 1, 3)},
                   {tick({bx("person", 0.6, 0.9)}, {1}, {1})});
    check(ev2.per_frame[0] == 1 && rules.events().size() == 1 &&
              rules.events()[0].source_cam == "CAM-02",
          "g08_cam02_confirm_survives_cam01_generation_reset",
          "events=" + std::to_string(rules.events().size()));
}

void g09_invalidate_track_isolated_to_camera() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    std::vector<FrameId> ids = {fid("CAM-01", 1, 1), fid("CAM-02", 1, 1),
                                fid("CAM-01", 1, 2), fid("CAM-02", 1, 2)};
    std::vector<Tick> ticks;
    for (int i = 0; i < 2; ++i) {
        ticks.push_back(tick({bx("person", 0.2, 0.9)}, {1}, {1}));
        ticks.push_back(tick({bx("person", 0.6, 0.9)}, {1}, {1}));
    }
    (void)run(&rules, &ad, ids, ticks);
    const int keys = SemanticAdapter::invalidate_track(&rules, "CAM-01", 1);
    check(keys == 1, "g09_invalidate_track_only_own_camera_keys", "keys=" + std::to_string(keys));
    auto ev = run(&rules, &ad, {fid("CAM-01", 1, 3), fid("CAM-02", 1, 3)},
                  {tick({bx("person", 0.2, 0.9)}, {1}, {1}),
                   tick({bx("person", 0.6, 0.9)}, {1}, {1})});
    check(ev.per_frame == std::vector<long long>{0, 1} && rules.events().size() == 1 &&
              rules.events()[0].source_cam == "CAM-02",
          "g09_cam02_track1_confirm_survives_cam01_invalidate",
          "events=" + std::to_string(rules.events().size()));
}

void g10_stale_generation_no_state_damage() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    (void)ad.adapt_and_dispatch(mk(fid1(1, 1), {bx("person", 0.45, 0.9)}, {1}, {1}), 100,
                                &rules, nullptr);
    (void)ad.adapt_and_dispatch(mk(fid1(1, 2), {bx("person", 0.45, 0.9)}, {1}, {1}), 200,
                                &rules, nullptr);
    SemanticResult rs;
    auto evs = ad.adapt_and_dispatch(mk(fid1(0, 3), {bx("person", 0.45, 0.9)}, {1}, {1}), 300,
                                     &rules, &rs);
    check(evs.empty() && !rs.ok && !rs.available &&
              rs.reason == semantic_reason::kStaleGeneration(),
          "g10_stale_generation_defensive_no_dispatch",
          "reason=" + rs.reason);
    // 旧代帧不破坏确认状态：gen1 第 3 帧仍确认。
    auto ev4 = ad.adapt_and_dispatch(mk(fid1(1, 4), {bx("person", 0.45, 0.9)}, {1}, {1}), 400,
                                     &rules, nullptr);
    check(ev4.size() == 1 && rules.events().size() == 1,
          "g10_confirm_survives_stale_frame",
          "events=" + std::to_string(rules.events().size()));
}

void g11_multi_track_ascending_order_detections_and_events() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    SemanticResult r = ad.adapt(mk(fid1(1, 1),
                                   {bx("person", 0.7, 0.9), bx("person", 0.3, 0.9),
                                    bx("person", 0.5, 0.9)},
                                   {7, 3, 5}, {1, 1, 1}),
                                100);
    check(r.detections.size() == 3 && r.detections[0].track_id == 3 &&
              r.detections[1].track_id == 5 && r.detections[2].track_id == 7,
          "g11_detections_strictly_ascending_by_track",
          "n=" + std::to_string(r.detections.size()));
    std::vector<FrameId> ids = {fid1(1, 1), fid1(1, 2), fid1(1, 3)};
    std::vector<Tick> ticks(3, tick({bx("person", 0.7, 0.9), bx("person", 0.3, 0.9),
                                     bx("person", 0.5, 0.9)},
                                    {7, 3, 5}, {1, 1, 1}));
    (void)run(&rules, &ad, ids, ticks);
    check(rules.events().size() == 3 && rules.events()[0].track_id == 3 &&
              rules.events()[1].track_id == 5 && rules.events()[2].track_id == 7,
          "g11_events_emitted_in_track_order",
          "events=" + std::to_string(rules.events().size()));
}

void g12_aggregate_keeps_higher_score_frame_fields() {
    SemanticAdapter ad(scfg());
    SemanticResult r = ad.adapt(mk(fid1(1, 1),
                                   {bx("person", 0.5, 0.6), bx("person", 0.8, 0.9)},
                                   {5, 5}, {1, 1}, {10.0f, 20.0f}),
                                100);
    check(r.detections.size() == 1 && r.stats.aggregated == 1,
          "g12_same_track_aggregated", "n=" + std::to_string(r.detections.size()));
    // confidence 由 float 0.9f 提升，精确期望值是 (double)0.9f（非字面量 0.9）。
    check(r.detections[0].confidence == static_cast<double>(0.9f) &&
              r.detections[0].distance_m == 20.0,
          "g12_whole_higher_score_frame_kept",
          "conf=" + std::to_string(r.detections[0].confidence) + " dist=" +
              std::to_string(r.detections[0].distance_m));
}

void g13_confidence_exact_equal_is_candidate() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg(0.5));
    std::vector<Tick> fs;
    for (int i = 0; i < 3; ++i)
        fs.push_back(tick({bx("person", 0.45, 0.5)}, {1}, {1}));  // score 恰等 conf_min
    std::vector<FrameId> ids = {fid1(1, 1), fid1(1, 2), fid1(1, 3)};
    RunOut o = run(&rules, &ad, ids, fs);
    check(o.low_conf == 0 && o.per_frame == std::vector<long long>{0, 0, 1} &&
              rules.events().size() == 1,
          "g13_confidence_exact_equal_is_candidate",
          "low=" + std::to_string(o.low_conf) + " events=" +
              std::to_string(rules.events().size()));
}

void g14_low_confidence_resets_own_track_confirm() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    // 2 高 + 1 低 + 2 高：低置信帧按 track 清理该目标确认（实现映射表声明语义），
    // 候选累计被打断，第 6 帧（高）才达到 3 帧确认。
    std::vector<Tick> fs = {tick({bx("person", 0.45, 0.9)}, {1}, {1}),
                            tick({bx("person", 0.45, 0.9)}, {1}, {1}),
                            tick({bx("person", 0.45, 0.3)}, {1}, {1}),
                            tick({bx("person", 0.45, 0.9)}, {1}, {1}),
                            tick({bx("person", 0.45, 0.9)}, {1}, {1}),
                            tick({bx("person", 0.45, 0.9)}, {1}, {1})};
    std::vector<FrameId> ids = {fid1(1, 1), fid1(1, 2), fid1(1, 3),
                                fid1(1, 4), fid1(1, 5), fid1(1, 6)};
    RunOut o = run(&rules, &ad, ids, fs);
    check(o.per_frame == std::vector<long long>{0, 0, 0, 0, 0, 1} && rules.events().size() == 1,
          "g14_low_confidence_frame_resets_confirm_accumulation",
          "low=" + std::to_string(o.low_conf) + " track_reset=" +
              std::to_string(o.track_reset));
    check(o.track_reset == 1 && o.low_conf == 1, "g14_reset_counted_once_on_low_frame",
          "track_reset=" + std::to_string(o.track_reset));
}

void g15_ready_empty_boxes_no_alarm() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    SemanticResult r;
    auto ev = ad.adapt_and_dispatch(mk(fid1(1, 1), {}, {}, {}), 100, &rules, &r);
    check(ev.empty() && r.available && r.ok && r.detections.empty(),
          "g15_ready_empty_boxes_no_alarm_no_judgement",
          "avail=" + std::string(r.available ? "1" : "0"));
    check(rules.events().empty(), "g15_no_events_from_empty_envelope",
          "events=" + std::to_string(rules.events().size()));
}

void g16_same_generation_no_reset_keys() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    std::vector<Tick> fs;
    for (int i = 0; i < 5; ++i)
        fs.push_back(tick({bx("person", 0.45, 0.3)}, {1}, {1}));  // 低置信避免事件噪声
    std::vector<FrameId> ids;
    for (int i = 0; i < 5; ++i) ids.push_back(fid1(1, i + 1));
    RunOut o = run(&rules, &ad, ids, fs);
    check(o.gen_reset == 0, "g16_same_generation_no_reset_keys",
          "gen_reset=" + std::to_string(o.gen_reset));
}

void g17_invalidate_camera_scope_count_and_events_kept() {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    // track1 3 帧确认出事件（confirm/last_emit/key_event_id 各 1 key）；
    // CAM-02 track3 各确认 2 帧（1 confirm key）。
    std::vector<FrameId> ids = {fid1(1, 1), fid("CAM-02", 1, 1), fid1(1, 2),
                                fid("CAM-02", 1, 2), fid1(1, 3)};
    std::vector<Tick> ticks = {tick({bx("person", 0.2, 0.9)}, {1}, {1}),
                               tick({bx("person", 0.6, 0.9)}, {3}, {1}),
                               tick({bx("person", 0.2, 0.9)}, {1}, {1}),
                               tick({bx("person", 0.6, 0.9)}, {3}, {1}),
                               tick({bx("person", 0.2, 0.9)}, {1}, {1})};
    (void)run(&rules, &ad, ids, ticks);
    check(rules.events().size() == 1 && rules.events()[0].track_id == 1,
          "g17_track1_emitted_before_cleanup", "events=" + std::to_string(rules.events().size()));
    const int keys = SemanticAdapter::invalidate_generation(&rules, "CAM-01");
    // 正确：track1 的 3 个 key（confirm/last_emit/key_event_id）= 3。
    check(keys == 3, "g17_invalidate_camera_counts_own_camera_keys",
          "keys=" + std::to_string(keys));
    // 事件不删除；CAM-02 track3 确认保留，第 3 帧确认。
    auto ev = run(&rules, &ad, {fid("CAM-02", 1, 3)},
                  {tick({bx("person", 0.6, 0.9)}, {3}, {1})});
    check(rules.events().size() == 2 && ev.per_frame[0] == 1 &&
              rules.events()[1].source_cam == "CAM-02",
          "g17_events_kept_and_cam02_confirms",
          "events=" + std::to_string(rules.events().size()));
}

int run_all() {
    g01_invalidate_track1_keeps_track11_confirm();
    g02_invalidate_track0_keeps_track10_confirm();
    g03_invalidate_track11_keeps_track1_confirm();
    g04_track_zero_is_legal_not_null();
    g05_no_track_camera_level_adaptation_track_minus1();
    g06_known_but_not_allowed_blocked();
    g07_empty_and_mixed_case_labels_blocked();
    g08_generation_reset_isolated_to_camera();
    g09_invalidate_track_isolated_to_camera();
    g10_stale_generation_no_state_damage();
    g11_multi_track_ascending_order_detections_and_events();
    g12_aggregate_keeps_higher_score_frame_fields();
    g13_confidence_exact_equal_is_candidate();
    g14_low_confidence_resets_own_track_confirm();
    g15_ready_empty_boxes_no_alarm();
    g16_same_generation_no_reset_keys();
    g17_invalidate_camera_scope_count_and_events_kept();
    std::printf("vext %s (%d failures)\n", g_fail == 0 ? "OK" : "FAILED", g_fail);
    return g_fail == 0 ? 0 : 1;
}

}  // namespace

int main() {
    return run_all();
}
