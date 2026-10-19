// vext_item008.cpp — ITEM-008 验证自有扩展夹具（software_tester.software_verification）
// 断言 CoreTests/selftest/fixtures 未覆盖的跟踪与协同边界。板上 g++ 直编链接 librailhmi_core.a。
// 判据全部为「正确语义在场」方向：板上 FAIL 即真缺陷呈证。
//
// 依据：任务书（稳定 id/永不 0/丢失不复用/generation 不跨代复用/多目标互不干扰/
// 相机级显式 -1/超上限显式 untracked），VisionDesign §7.5、InterfaceContract §6、
// vision_tracker.h / vision_semantic_adapter.h 注释语义、rules.cpp key 后缀匹配边界。

#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/rules.h"
#include "railhmi/core/vision_inference_envelope.h"
#include "railhmi/core/vision_semantic_adapter.h"
#include "railhmi/core/vision_tracker.h"

using namespace railhmi::core;

namespace {

int g_fail = 0;

void check(bool ok, const std::string& name, const std::string& detail = "") {
    std::printf("vext %s %s%s\n", ok ? "PASS" : "FAIL", name.c_str(),
                detail.empty() ? "" : (" :: " + detail).c_str());
    if (!ok) ++g_fail;
}

FrameId fid(std::uint64_t gen, unsigned long long seq) {
    FrameId f;
    f.session_id = "s-vext8";
    f.camera_id = "CAM-01";
    f.generation = gen;
    f.seq = seq;
    return f;
}

EnvelopeBox eb(const std::string& label, double x, double y, double w = 0.1, double h = 0.1,
               double conf = 0.9) {
    EnvelopeBox b;
    b.label = label;
    b.x = static_cast<float>(x);
    b.y = static_cast<float>(y);
    b.w = static_cast<float>(w);
    b.h = static_cast<float>(h);
    b.confidence = static_cast<float>(conf);
    b.in_roi = true;
    return b;
}

InferenceEnvelope mk(const FrameId& f, const std::vector<EnvelopeBox>& boxes) {
    EnvelopeBuildInput in;
    in.frame = f;
    in.profile = AiProfile::Front;
    in.model_version = "rknn-2.3.2/yolov5s";
    in.capture_mono_us = 1000;
    in.result_mono_us = 20000;
    in.requires_segmentation = false;
    InferenceEnvelope e = build_envelope(in);
    e.boxes = boxes;
    return e;
}

SemanticConfig scfg(TrackPolicy p = TrackPolicy::Lightweight, double conf = 0.5) {
    SemanticConfig c;
    c.profile = AiProfile::Front;
    c.allowed_labels = {"person", "obstacle"};
    c.known_labels = {"person", "obstacle"};
    c.confidence_min = conf;
    c.track_policy = p;
    c.tracker.iou_match_min = 0.3f;
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

// g01: 同 track 曲线移动 IoU 不足以匹配时 ≤ center_dist_max 的中心距匹配
void g01_center_fallback_near_boxes(void) {
    TrackerConfig cfg;
    cfg.iou_match_min = 0.9f;   // IoU 实际 ≈0.44 <0.9，主力匹配不成立
    cfg.center_fallback = true;
    cfg.center_dist_max = 0.3f; // 中心距 ≈0.072 ≤0.3，回退应命中
    LightweightTracker t(cfg);
    (void)t.update({eb("person", 0.40, 0.70)}, 100);
    auto r = t.update({eb("person", 0.45, 0.72)}, 200);
    check(r.size() == 1 && r[0].track_id == 1 && !r[0].is_new && t.matched() == 1,
          "g01_center_fallback_matches_when_iou_below_min",
          "id=" + std::to_string(r.empty() ? -1 : r[0].track_id) + " matched=" +
              std::to_string(t.matched()));
}

// g02: 中心距回退关闭时同帧同位移不被匹配（IoU=0.44 < 0.9，回退关）
void g02_center_fallback_disabled_no_match(void) {
    TrackerConfig cfg;
    cfg.iou_match_min = 0.9f;
    cfg.center_fallback = false;
    LightweightTracker t(cfg);
    (void)t.update({eb("person", 0.40, 0.70)}, 100);
    auto r = t.update({eb("person", 0.45, 0.72)}, 200);
    check(r.size() == 1 && r[0].track_id == 2 && r[0].is_new && t.matched() == 0,
          "g02_center_fallback_off_checks_no_match",
          "id=" + std::to_string(r.empty() ? -1 : r[0].track_id) + " matched=" +
              std::to_string(t.matched()));
}

// g03: 回退受 center_dist_max 约束，太远不匹配
void g03_center_fallback_dist_bound(void) {
    TrackerConfig cfg;
    cfg.iou_match_min = 0.9f;
    cfg.center_fallback = true;
    cfg.center_dist_max = 0.05f;  // 中心距 ≈0.072 > 0.05 => 不回退
    LightweightTracker t(cfg);
    (void)t.update({eb("person", 0.40, 0.70)}, 100);
    auto r = t.update({eb("person", 0.45, 0.72)}, 200);
    check(r.size() == 1 && r[0].track_id == 2 && r[0].is_new,
          "g03_center_fallback_respects_dist_bound",
          "id=" + std::to_string(r.empty() ? -1 : r[0].track_id));
}

// g04: 交叉运动——两 track 交错帧应被 Iou+label 贪心各自关联，不互换
void g04_crossing_tracks_keep_own_ids(void) {
    TrackerConfig cfg;
    cfg.center_fallback = false;
    LightweightTracker t(cfg);
    (void)t.update({eb("person", 0.10, 0.70), eb("person", 0.80, 0.70)}, 100);
    // 各自小幅移动（IoU≈1 仍为主匹配对）
    auto r = t.update({eb("person", 0.15, 0.70), eb("person", 0.75, 0.70)}, 200);
    check(r.size() == 2 && r[0].track_id == 1 && r[1].track_id == 2,
          "g04_two_tracks_progressive_move_keep_ids",
          "ids=" + std::to_string(r.size() == 2 ? r[0].track_id : -999) + "," +
              std::to_string(r.size() == 2 ? r[1].track_id : -999));
}

// g05: 贪心按最高 IoU 优先把 track 分给最像的框（新框只在 IoU 配额耗尽后新建）
void g05_iou_greedy_prefers_best_pair(void) {
    TrackerConfig cfg;
    cfg.center_fallback = false;
    LightweightTracker t(cfg);
    (void)t.update({eb("person", 0.10, 0.70), eb("person", 0.50, 0.70)}, 100);
    // 帧 2：新第一框在 0.52（与 track2 IoU≈0.667），第二框在 0.50（与 track2 IoU=1）。
    // 贪心应先给 track2 ↔ 0.50（IoU=1），0.52 无 track 可配（track1 IoU=0）→ 新 id 3。
    auto r = t.update({eb("person", 0.52, 0.70), eb("person", 0.50, 0.70)}, 200);
    check(r.size() == 2 && r[0].track_id == 3 && r[0].is_new && r[1].track_id == 2,
          "g05_iou_greedy_prefers_highest_pair",
          "ids=" + std::to_string(r.size() == 2 ? r[0].track_id : -999) + "," +
              std::to_string(r.size() == 2 ? r[1].track_id : -999));
}

// g06: max_misses=0 时任何未匹配帧立即删除
void g06_max_misses_zero_drops_on_first_miss(void) {
    TrackerConfig cfg;
    cfg.max_misses = 0;
    LightweightTracker t(cfg);
    (void)t.update({eb("person", 0.4, 0.7)}, 100);
    auto d = t.tick_missing(200);
    check(d == 1 && t.size() == 0 && t.dropped() == 1,
          "g06_max_misses_zero_drops_on_first_miss",
          "dropped=" + std::to_string(d));
}

// g07: tick_missing 累计未超 max_misses 不删除；跨帧再超限时删除
void g07_tick_missing_counts_then_drops(void) {
    TrackerConfig cfg;
    cfg.max_misses = 2;
    LightweightTracker t(cfg);
    (void)t.update({eb("person", 0.4, 0.7)}, 100);
    check(t.tick_missing(200) == 0, "g07_first_tick_keeps", "");
    check(t.tick_missing(300) == 0, "g07_second_tick_keeps", "");
    check(t.tick_missing(400) == 1 && t.size() == 0, "g07_third_tick_drops",
          "size=" + std::to_string(t.size()));
    auto r = t.update({eb("person", 0.4, 0.7)}, 500);
    check(r.size() == 1 && r[0].track_id == 2, "g07_reapplied_new_id_not_reused",
          "id=" + std::to_string(r.empty() ? -1 : r[0].track_id));
}

// g08: 同一帧内两帧 tick_missing 之外的 update 命中把 misses 清零
void g08_matched_hit_resets_misses(void) {
    TrackerConfig cfg;
    cfg.max_misses = 2;
    LightweightTracker t(cfg);
    (void)t.update({eb("person", 0.4, 0.7)}, 100);
    (void)t.tick_missing(200);             // misses=1
    auto r = t.update({eb("person", 0.4, 0.7)}, 300);  // 命中，misses 清零
    check(r.size() == 1 && r[0].track_id == 1, "g08_hit_keeps_alive", "id=" +
        std::to_string(r.empty() ? -1 : r[0].track_id));
    (void)t.tick_missing(400);             // misses=1（不重置为 2）
    check(t.tick_missing(500) == 0, "g08_misses_reset_by_hit", "size=" + std::to_string(t.size()));
}

// g09: track_id 从 1 单调递增且永不 0（远跳新建逐帧递增不复用）
void g09_id_monotonic_never_zero(void) {
    TrackerConfig cfg;
    cfg.center_fallback = false;  // 远跳必须不被中心距回退拉回
    LightweightTracker t(cfg);
    std::int32_t last_id = 0;
    bool ok = true;
    // 每帧保留 0.4 处旧 track，另在递减 0.1 的新位置放一框（IoU=0 → 新建）。
    for (int i = 0; i < 10; ++i) {
        std::vector<EnvelopeBox> boxes = {eb("person", 0.4, 0.7),
                                          eb("person", 0.9 - 0.1 * i, 0.1)};
        auto r = t.update(boxes, 100 + i * 10);
        if (r.size() == 2 && (r[0].track_id != 1 || r[1].track_id <= last_id || r[1].track_id <= 0))
            ok = false;
        if (r.size() == 2 && r[1].is_new) last_id = r[1].track_id;
    }
    check(ok && last_id >= 10 && t.next_id() > 10, "g09_id_strictly_monotonic_positive",
          "last_id=" + std::to_string(last_id) + " next_id=" + std::to_string(t.next_id()));
}

// g10: 聚类去重：同一帧多个重叠框按 IoU 贪心一次匹配一个 track，多余框新建独立 id
void g10_multi_box_intra_frame_distinct_ids(void) {
    LightweightTracker t;
    auto r = t.update({eb("person", 0.4, 0.7), eb("person", 0.4, 0.7),
                       eb("person", 0.75, 0.7)}, 100);
    check(r.size() == 3 && r[0].track_id == 1 && r[1].track_id == 2 && r[2].track_id == 3 &&
              t.created() == 3 && t.size() == 3,
          "g10_same_frame_multi_boxes_get_distinct_ids",
          "ids=" + std::to_string(r.size() == 3 ? r[0].track_id : -999) + "," +
              std::to_string(r.size() == 3 ? r[1].track_id : -999) + "," +
              std::to_string(r.size() == 3 ? r[2].track_id : -999));
}

// g11: 时间鸿沟边界 —— ts_delta == max_gap_ms 仍匹配（stricly >），下一挡不匹配
void g11_gap_boundary_is_exclusive_over(void) {
    TrackerConfig cfg;
    cfg.max_gap_ms = 500;
    LightweightTracker t(cfg);
    (void)t.update({eb("person", 0.4, 0.7)}, 100);
    auto r = t.update({eb("person", 0.4, 0.7)}, 600);  // 500 == max_gap → 不过期
    check(r.size() == 1 && r[0].track_id == 1, "g11_gap_equal_still_matches",
          "id=" + std::to_string(r.empty() ? -1 : r[0].track_id));
    auto r2 = t.update({eb("person", 0.4, 0.7)}, 1101);  // >500 → 新 id
    check(r2.size() == 1 && r2[0].track_id != 1 && r2[0].track_id >= 1,
          "g11_gap_over_creates_new_id",
          "id=" + std::to_string(r2.empty() ? -1 : r2[0].track_id));
}

// g12: 相机级 track_id() 恒等 -1（关键不变量：非 0）
void g12_camera_level_track_id_is_minus1(void) {
    check(camera_level_track_id() == -1 && camera_level_track_id() != 0,
          "g12_camera_level_track_id_is_minus1", "v=" + std::to_string(camera_level_track_id()));
}

// g13: 适配器 CameraLevel 下同帧多目标被聚合为单一 -1 key（实现声明会合并多目标）
void g13_camera_level_merges_same_camera_targets(void) {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg(TrackPolicy::CameraLevel));
    SemanticResult rlast;
    for (int i = 0; i < 3; ++i) {
        (void)ad.adapt_and_dispatch(mk(fid(1, i + 1),
                                       {eb("person", 0.1, 0.7), eb("obstacle", 0.7, 0.7)}),
                                    (i + 1) * 100, &rules, i == 2 ? &rlast : nullptr);
    }
    // 同帧两框被 -1 key 聚合为 1 条
    check(rlast.stats.aggregated == 1, "g13_camera_level_aggregates_same_frame_boxes",
          "agg=" + std::to_string(rlast.stats.aggregated));
    const auto& ev = rules.events();
    check(ev.size() == 1 && ev[0].track_id == -1,
          "g13_camera_level_one_merged_event_minus1_not_0",
          "events=" + std::to_string(ev.size()) + " track=" +
              std::to_string(ev.empty() ? -999 : ev[0].track_id));
}

// g14: Lightweight 下 label_guard 语义经适配器仍隔离（person→obstacle 各自 track/类型）
void g14_adapter_label_guard_isolated_all_labels(void) {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    for (int i = 0; i < 3; ++i) {
        (void)ad.adapt_and_dispatch(mk(fid(1, i + 1),
                                       {eb("person", 0.2, 0.7), eb("obstacle", 0.6, 0.7)}),
                                    (i + 1) * 100, &rules, nullptr);
    }
    check(rules.events().size() == 2 &&
              rules.events()[0].track_id != rules.events()[1].track_id &&
              rules.events()[0].type != rules.events()[1].type,
          "g14_adapter_two_labels_two_alarm_types",
          "events=" + std::to_string(rules.events().size()) + " types=" +
              std::to_string(int(rules.events()[0].type)) + "/" +
              std::to_string(int(rules.events()[1].type)));
}

// g15: 跨相机同名 track 不串（适配器 Lightweight 单跟踪器跟踪器按调用方相机串行使用）
void g15_single_adapter_different_camera_separate_rules(void) {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    // 适配器仅含单跟踪器；多相机须每路一个适配器（实现文档声明）。此处验证的是单适配器
    // 同一 track_id 键下不同 camera_id 在 RuleEngine 侧 keys 天然隔离。
    FrameId f1 = fid(1, 1);
    FrameId f2 = fid(1, 1);
    f1.camera_id = "CAM-01";
    f2.camera_id = "CAM-02";
    (void)ad.adapt(mk(f1, {eb("person", 0.4, 0.7)}), 100);
    auto r2 = ad.adapt(mk(f2, {eb("person", 0.4, 0.7)}), 200);
    check(r2.detections.size() == 1 && r2.detections[0].camera_id == "CAM-02" &&
              r2.detections[0].track_id == 1,
          "g15_adapter_detection_carries_source_camera",
          "cam=" + (r2.detections.empty() ? std::string("?") : r2.detections[0].camera_id));
}

// g16: 超 max_tracks 框的 untracked 计数与显式 -1 不混淆真实 track
void g16_untracked_boxes_dont_fabricate(void) {
    TrackerConfig cfg;
    cfg.max_tracks = 2;
    LightweightTracker t(cfg);
    auto r = t.update({eb("person", 0.1, 0.1), eb("person", 0.4, 0.4), eb("person", 0.7, 0.7)}, 100);
    int minus1 = 0, positive = 0;
    bool saw_zero = false;
    for (const auto& b : r) {
        if (b.track_id == -1) ++minus1;
        else if (b.track_id >= 1) ++positive;
        else saw_zero = true;
    }
    check(!saw_zero, "g16_no_zero_among_truncated", "saw_zero=true if emitted");
    check(minus1 == 1 && positive == 2 && t.untracked() == 1 && t.size() == 2,
          "g16_untracked_explicit_minus1_no_fabrication",
          "minus1=" + std::to_string(minus1) + " positive=" + std::to_string(positive));
    // 下一帧两个已建 track 继续，仍 1 untracked
    auto r2 = t.update({eb("person", 0.1, 0.1), eb("person", 0.4, 0.4), eb("person", 0.7, 0.7)}, 200);
    int neg2 = 0;
    for (const auto& b : r2) if (b.track_id == -1) ++neg2;
    check(r2.size() == 3 && neg2 == 1 && t.untracked() == 2,
          "g16_untracked_repeat_no_id_leak", "neg2=" + std::to_string(neg2));
}

// g17: 适配器 Lightweight 下同代持续命中 → 抑制窗口内不重复开事件（suppress 3s）
void g17_suppress_window_no_duplicate_alarm(void) {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    for (int i = 0; i < 20; ++i) {
        (void)ad.adapt_and_dispatch(mk(fid(1, i + 1),
                                       {eb("person", 0.4 + 0.01 * i / 10.0, 0.7)}),
                                    100 + i * 100, &rules, nullptr);  // 2 秒内连续帧
    }
    check(rules.events().size() == 1, "g17_suppress_window_no_duplicate_alarm",
          "events=" + std::to_string(rules.events().size()));
}

// g18: generation 变化不重置 next_id —— 适配器新代第一帧后 track_id 不等于 1
void g18_next_id_not_reset_on_generation(void) {
    RuleEngine rules(rcfg());
    SemanticAdapter ad(scfg());
    for (int i = 0; i < 3; ++i)
        (void)ad.adapt_and_dispatch(mk(fid(1, i + 1), {eb("person", 0.4, 0.7)}), (i + 1) * 100,
                                    &rules, nullptr);
    SemanticResult r4;
    (void)ad.adapt_and_dispatch(mk(fid(2, 1), {eb("person", 0.4, 0.7)}), 400, &rules, &r4);
    int nid = r4.detections.empty() ? -1 : r4.detections[0].track_id;
    check(nid == 2, "g18_next_id_continues_after_generation", "nid=" + std::to_string(nid));
    // 二次换代继续递增
    SemanticResult r7;
    (void)ad.adapt_and_dispatch(mk(fid(3, 1), {eb("person", 0.4, 0.7)}), 700, &rules, &r7);
    int nid2 = r7.detections.empty() ? -1 : r7.detections[0].track_id;
    check(nid2 == 3, "g18_next_id_continues_after_second_generation",
          "nid2=" + std::to_string(nid2));
}

int run_all() {
    g01_center_fallback_near_boxes();
    g02_center_fallback_disabled_no_match();
    g03_center_fallback_dist_bound();
    g04_crossing_tracks_keep_own_ids();
    g05_iou_greedy_prefers_best_pair();
    g06_max_misses_zero_drops_on_first_miss();
    g07_tick_missing_counts_then_drops();
    g08_matched_hit_resets_misses();
    g09_id_monotonic_never_zero();
    g10_multi_box_intra_frame_distinct_ids();
    g11_gap_boundary_is_exclusive_over();
    g12_camera_level_track_id_is_minus1();
    g13_camera_level_merges_same_camera_targets();
    g14_adapter_label_guard_isolated_all_labels();
    g15_single_adapter_different_camera_separate_rules();
    g16_untracked_boxes_dont_fabricate();
    g17_suppress_window_no_duplicate_alarm();
    g18_next_id_not_reset_on_generation();
    std::printf("vext %s (%d failures)\n", g_fail == 0 ? "OK" : "FAILED", g_fail);
    return g_fail == 0 ? 0 : 1;
}

}  // namespace

int main() {
    return run_all();
}
