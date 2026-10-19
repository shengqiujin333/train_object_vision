// railhmi-core 同帧两阶段判定测试（视觉推理 ITEM-005 纯逻辑）。
// 覆盖 TC-T-TWOPHASE-01..06：同帧命中/不在掩码/跨帧跨代拒绝/分割失败不安全/danger_zone/缓存默认关。

#include <cmath>
#include <string>
#include <vector>

#include "test_util.h"

#include "railhmi/core/rules.h"
#include "railhmi/core/vision_two_stage.h"

namespace {

using namespace railhmi::core;

FrameId fid(const std::string& s, std::uint64_t g, std::uint64_t q) {
    FrameId f;
    f.session_id = s;
    f.camera_id = "CAM-01";
    f.generation = g;
    f.seq = q;
    return f;
}

SegMask band_mask(int w, int h, double y0, double y1) {
    SegMask m;
    m.width = w;
    m.height = h;
    m.stride_bytes = w;
    m.plane.assign(static_cast<std::size_t>(w) * h, 0);
    const int py0 = static_cast<int>(y0 * h);
    const int py1 = static_cast<int>(y1 * h);
    for (int y = py0; y <= py1 && y < h; ++y)
        for (int x = 0; x < w; ++x) {
            m.plane[static_cast<std::size_t>(y) * w + x] = 1;
            ++m.positive;
        }
    return m;
}

DetectBox person(double x, double y, double w, double h) {
    DetectBox b;
    b.x = static_cast<float>(x);
    b.y = static_cast<float>(y);
    b.w = static_cast<float>(w);
    b.h = static_cast<float>(h);
    b.score = 0.9f;
    b.class_id = 0;
    b.label = "person";
    return b;
}

RoiConfig dz() {
    RoiConfig r;
    r.camera = "CAM-01";
    r.name = "danger_zone";
    r.x = 0.08;
    r.y = 0.82;
    r.w = 0.84;
    r.h = 0.16;
    return r;
}

TwoStageInput mk(const FrameId& f, const std::vector<DetectBox>& p, const std::vector<SegMask>& m,
                 bool seg_ok = true, bool det_ok = true, long long now = 0) {
    TwoStageInput in;
    in.frame = f;
    in.detect_frame = f;
    in.seg_frame = f;
    in.detect_available = det_ok;
    in.seg_available = seg_ok;
    in.now_mono_us = now;
    in.persons = &p;
    in.rail_masks = &m;
    in.image_w = 1280;
    in.image_h = 720;
    return in;
}

void test_identity() {
    railhmi_test::check(same_frame(fid("s", 1, 5), fid("s", 1, 5)), "same_frame same");
    railhmi_test::check(!same_frame(fid("s", 1, 5), fid("s", 1, 6)), "same_frame seq differ");
    railhmi_test::check(!same_frame(fid("s", 1, 5), fid("s", 2, 5)), "same_frame gen differ");
    railhmi_test::check(!same_frame(fid("a", 1, 5), fid("b", 1, 5)), "same_frame session differ");
}

void test_hit_and_miss() {
    const std::string s = "s-unit";
    TwoStageConfig cfg;
    std::string e;
    const std::vector<SegMask> band = {band_mask(1280, 720, 0.6, 1.0)};
    {
        const std::vector<DetectBox> p = {person(0.45, 0.75, 0.1, 0.1)};
        TwoStageInput in = mk(fid(s, 1, 1), p, band);
        TwoStageResult r;
        railhmi_test::check(evaluate_two_stage(in, cfg, &r, &e) && r.available && r.alarm_hits == 1 &&
                                r.detections.size() == 1 && r.detections[0].mask_hit &&
                                r.detections[0].on_rail_alarm && r.detections[0].label == "person",
                            "center in mask -> alarm");
    }
    {
        const std::vector<DetectBox> p = {person(0.45, 0.15, 0.1, 0.1)};
        TwoStageInput in = mk(fid(s, 1, 2), p, band);
        TwoStageResult r;
        railhmi_test::check(evaluate_two_stage(in, cfg, &r, &e) && r.available && r.mask_hit == 0 &&
                                r.dropped_no_mask == 1 && r.alarm_hits == 0 &&
                                r.reason == two_stage_reason::kNoAlarm(),
                            "center out of mask -> no alarm");
    }
    {
        std::vector<DetectBox> none;
        TwoStageInput in = mk(fid(s, 1, 3), none, band);
        TwoStageResult r;
        railhmi_test::check(evaluate_two_stage(in, cfg, &r, &e) && r.available && r.alarm_hits == 0 &&
                                r.reason == two_stage_reason::kNoPerson(),
                            "no person -> NO_PERSON");
    }
    {
        const std::vector<DetectBox> p = {person(0.45, 0.75, 0.1, 0.1)};
        TwoStageInput in = mk(fid(s, 1, 4), p, band);
        in.seg_frame = fid(s, 1, 5);
        TwoStageResult r;
        railhmi_test::check(evaluate_two_stage(in, cfg, &r, &e) && !r.available &&
                                r.reason == two_stage_reason::kFrameMismatch(),
                            "cross-frame -> FRAME_MISMATCH");
    }
}

void test_availability_gate() {
    const std::string s = "s-unit";
    TwoStageConfig cfg;
    std::string e;
    const std::vector<SegMask> band = {band_mask(1280, 720, 0.6, 1.0)};
    const std::vector<DetectBox> p = {person(0.45, 0.75, 0.1, 0.1)};
    {
        TwoStageInput in = mk(fid(s, 1, 10), p, band, false, true);
        TwoStageResult r;
        railhmi_test::check(evaluate_two_stage(in, cfg, &r, &e) && !r.available && !r.ok &&
                                r.reason == two_stage_reason::kUnavailable() && r.detections.empty(),
                            "seg unavailable -> no safe conclusion");
    }
    {
        const std::vector<SegMask> em = {SegMask{}};
        TwoStageInput in = mk(fid(s, 1, 11), p, em);
        TwoStageResult r;
        railhmi_test::check(evaluate_two_stage(in, cfg, &r, &e) && !r.available &&
                                r.reason == two_stage_reason::kUnavailable(),
                            "empty mask -> unavailable");
    }
    {
        TwoStageInput in = mk(fid(s, 1, 12), p, band, true, false);
        TwoStageResult r;
        railhmi_test::check(evaluate_two_stage(in, cfg, &r, &e) && !r.available &&
                                r.reason == two_stage_reason::kUnavailable(),
                            "detect unavailable -> unavailable");
    }
    {
        const std::vector<SegMask> small = {band_mask(64, 48, 0.5, 1.0)};
        TwoStageInput in = mk(fid(s, 1, 13), p, small);
        TwoStageResult r;
        railhmi_test::check(evaluate_two_stage(in, cfg, &r, &e) && !r.available &&
                                r.reason == two_stage_reason::kUnavailable(),
                            "misaligned mask -> unavailable");
    }
}

void test_danger_zone() {
    const std::string s = "s-unit";
    RoiConfig z = dz();
    TwoStageConfig cfg;
    cfg.danger_zone = &z;
    std::string e;
    const std::vector<SegMask> band = {band_mask(1280, 720, 0.6, 1.0)};
    {
        const std::vector<DetectBox> p = {person(0.45, 0.85, 0.1, 0.1)};  // 中心 y=0.9 在 zone
        TwoStageInput in = mk(fid(s, 1, 20), p, band);
        TwoStageResult r;
        railhmi_test::check(evaluate_two_stage(in, cfg, &r, &e) && r.alarm_hits == 1 &&
                                r.detections[0].in_danger_zone && r.detections[0].on_rail_alarm,
                            "danger_zone hit -> alarm");
    }
    {
        const std::vector<DetectBox> p = {person(0.45, 0.65, 0.1, 0.1)};  // 中心 y=0.7 不在 zone
        TwoStageInput in = mk(fid(s, 1, 21), p, band);
        TwoStageResult r;
        railhmi_test::check(evaluate_two_stage(in, cfg, &r, &e) && r.mask_hit == 1 &&
                                r.dropped_out_of_zone == 1 && r.alarm_hits == 0 &&
                                !r.detections[0].on_rail_alarm,
                            "danger_zone miss -> no alarm");
    }
}

void test_gate_and_cache() {
    const std::string s = "s-unit";
    TwoStageConfig cfg;
    const std::vector<SegMask> band = {band_mask(1280, 720, 0.6, 1.0)};
    const std::vector<DetectBox> p = {person(0.45, 0.75, 0.1, 0.1)};
    {
        TwoStageEvaluator ev(cfg, s);
        railhmi_test::check(ev.evaluate(mk(fid(s, 1, 30), p, band)).ok, "gate first accepted");
        TwoStageResult dup = ev.evaluate(mk(fid(s, 1, 30), p, band));
        railhmi_test::check(!dup.ok && dup.reason == two_stage_reason::kStale(), "duplicate seq rejected");
        TwoStageResult ooo = ev.evaluate(mk(fid(s, 1, 29), p, band));
        railhmi_test::check(!ooo.ok && ooo.reason == two_stage_reason::kStale(), "out-of-order rejected");
        TwoStageResult stale = ev.evaluate(mk(fid(s, 0, 1), p, band));
        railhmi_test::check(!stale.ok && stale.reason == two_stage_reason::kStale(),
                            "stale generation rejected");
    }
    {
        // 缓存默认关闭：seg 不可用时不复用
        TwoStageEvaluator ev(cfg, s);
        (void)ev.evaluate(mk(fid(s, 1, 40), p, band, true, true, 0));
        TwoStageResult r = ev.evaluate(mk(fid(s, 1, 41), p, band, false, true, 100000));
        railhmi_test::check(!r.available && !r.used_cache, "cache disabled -> no reuse");
    }
    {
        TwoStageConfig cc = cfg;
        cc.cache.enabled = true;
        cc.cache.max_age_ms = 500;
        TwoStageEvaluator ev(cc, s);
        (void)ev.evaluate(mk(fid(s, 1, 50), p, band, true, true, 0));
        TwoStageResult r = ev.evaluate(mk(fid(s, 1, 51), p, band, false, true, 100000));
        railhmi_test::check(r.available && r.used_cache && r.cache_age_ms == 100 && r.alarm_hits == 1,
                            "cache reuse same generation within age");
        TwoStageResult exp = ev.evaluate(mk(fid(s, 1, 52), p, band, false, true, 900000));
        railhmi_test::check(!exp.available && !exp.used_cache, "cache expired rejected");
        TwoStageResult gen = ev.evaluate(mk(fid(s, 2, 1), p, band, false, true, 950000));
        railhmi_test::check(!gen.available && !gen.used_cache, "cache generation change rejected");
    }
    {
        // 缓存配置结构必须登记年龄与策略
        TwoStageCacheConfig c;
        railhmi_test::check(!c.enabled && c.max_age_ms == 0, "cache default off with zero age");
    }
}

void test_counts() {
    const std::string s = "s-unit";
    TwoStageConfig cfg;
    std::string e;
    const std::vector<SegMask> band = {band_mask(1280, 720, 0.6, 1.0)};
    const std::vector<DetectBox> p = {person(0.45, 0.75, 0.1, 0.1), person(0.45, 0.85, 0.1, 0.1),
                                      person(0.45, 0.15, 0.1, 0.1)};
    TwoStageInput in = mk(fid(s, 1, 60), p, band);
    TwoStageResult r;
    railhmi_test::check(evaluate_two_stage(in, cfg, &r, &e) && r.person_total == 3 && r.mask_hit == 2 &&
                            r.dropped_no_mask == 1 && r.alarm_hits == 2,
                        "multi person counts");
    {
        DetectBox out = person(1.02, 0.75, 0.1, 0.1);
        const std::vector<DetectBox> po = {out};
        TwoStageInput in2 = mk(fid(s, 1, 61), po, band);
        TwoStageResult r2;
        railhmi_test::check(evaluate_two_stage(in2, cfg, &r2, &e) && r2.dropped_out_of_bounds == 1 &&
                                r2.alarm_hits == 0,
                            "out-of-bounds center dropped");
    }
}

}  // namespace

void run_vision_two_stage_tests() {
    test_identity();
    test_hit_and_miss();
    test_availability_gate();
    test_danger_zone();
    test_gate_and_cache();
    test_counts();
}
