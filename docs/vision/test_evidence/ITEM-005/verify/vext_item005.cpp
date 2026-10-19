// vext_item005 — 验证自有扩展夹具（software_tester.software_verification，
// VERIFY-20261019-item005-v1）。断言交付 core-tests/selftest 未覆盖的验收边界。
//
// 链接交付 build-core/src/core/librailhmi_core.a，直连 vision_two_stage.cpp 行为面。
// 判例命名 g##_；像素/ROI 预言已离线 float32 dry-run（_dryrun_expect.py）。

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/frame_identity.h"
#include "railhmi/core/rules.h"
#include "railhmi/core/vision_detect_postprocess.h"
#include "railhmi/core/vision_seg_postprocess.h"
#include "railhmi/core/vision_two_stage.h"

using namespace railhmi::core;

namespace {

int g_fail = 0;

void check(bool ok, const std::string& name, const std::string& detail = "") {
    std::printf("vext %s %s%s%s\n", ok ? "PASS" : "FAIL", name.c_str(),
                detail.empty() ? "" : " :: ", detail.c_str());
    if (!ok) ++g_fail;
}

FrameId fid(const std::string& s, std::uint64_t gen, std::uint64_t seq) {
    FrameId f;
    f.session_id = s;
    f.camera_id = "CAM-01";
    f.generation = gen;
    f.seq = seq;
    return f;
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

SegMask band_mask(int w, int h, int y0, int y1) {
    SegMask m;
    m.width = w;
    m.height = h;
    m.stride_bytes = w;
    m.plane.assign(static_cast<std::size_t>(w) * h, 0);
    for (int y = y0; y <= y1; ++y)
        for (int x = 0; x < w; ++x) {
            m.plane[static_cast<std::size_t>(y) * w + x] = 1;
            ++m.positive;
        }
    return m;
}

// 全零掩码（尺寸合法但无正像素）
SegMask zero_mask(int w, int h) {
    SegMask m;
    m.width = w;
    m.height = h;
    m.stride_bytes = w;
    m.plane.assign(static_cast<std::size_t>(w) * h, 0);
    m.positive = 0;
    return m;
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

RoiConfig danger_zone() {
    RoiConfig r;
    r.camera = "CAM-01";
    r.name = "danger_zone";
    r.x = 0.08;
    r.y = 0.82;
    r.w = 0.84;
    r.h = 0.16;
    return r;
}

void test_mask_pixel_semantics() {
    const std::string s = "s-vext";
    TwoStageConfig cfg;
    std::string e;
    // g01 中心 y 恰在带起点像素行（float32 中心 0.6000000126659870 -> py=432 命中行 432）
    {
        const std::vector<SegMask> band = {band_mask(1280, 720, 432, 719)};
        const std::vector<DetectBox> p = {person(0.45, 0.55, 0.1, 0.1)};
        TwoStageInput in = mk(fid(s, 1, 1), p, band);
        TwoStageResult r;
        evaluate_two_stage(in, cfg, &r, &e);
        check(r.mask_hit == 1 && r.alarm_hits == 1, "g01_center_row432_exact_band_start_hit",
              "mask_hit=" + std::to_string(r.mask_hit) + " alarms=" + std::to_string(r.alarm_hits));
    }
    // g01b 行 431（带上一行）：不命中
    {
        const std::vector<SegMask> band = {band_mask(1280, 720, 432, 719)};
        const std::vector<DetectBox> p = {person(0.45, 0.5499, 0.1, 0.1)};
        TwoStageInput in = mk(fid(s, 1, 2), p, band);
        TwoStageResult r;
        evaluate_two_stage(in, cfg, &r, &e);
        check(r.mask_hit == 0 && r.dropped_no_mask == 1 && r.alarm_hits == 0,
              "g01b_center_row431_just_above_miss");
    }
    // g02 中心恰在图像右缘（px=1279 clamp 内）命中；cx>1 越界丢弃
    {
        const std::vector<SegMask> band = {band_mask(1280, 720, 432, 719)};
        const std::vector<DetectBox> p = {person(0.95, 0.85, 0.1, 0.1)};
        TwoStageInput in = mk(fid(s, 1, 3), p, band);
        TwoStageResult r;
        evaluate_two_stage(in, cfg, &r, &e);
        check(r.mask_hit == 1 && r.alarm_hits == 1 && r.dropped_out_of_bounds == 0,
              "g02_center_px1279_image_edge_hit");
    }
    {
        const std::vector<SegMask> band = {band_mask(1280, 720, 432, 719)};
        const std::vector<DetectBox> p = {person(0.950001, 0.85, 0.1, 0.1)};  // 中心 >1
        TwoStageInput in = mk(fid(s, 1, 4), p, band);
        TwoStageResult r;
        evaluate_two_stage(in, cfg, &r, &e);
        check(r.dropped_out_of_bounds == 1 && r.mask_hit == 0 && r.alarm_hits == 0,
              "g02b_center_beyond_right_edge_dropped");
    }
    // g03 stride > width（带 padding 列）：内容区内仍按 stride 定位
    {
        SegMask m = band_mask(1280, 720, 432, 719);
        m.stride_bytes = 1288;  // 8 padding 字节/行
        m.plane.assign(static_cast<std::size_t>(1288) * 720, 0);
        for (int y = 432; y <= 719; ++y)
            for (int x = 0; x < 1280; ++x) {
                m.plane[static_cast<std::size_t>(y) * 1288 + x] = 1;
                ++m.positive;
            }
        const std::vector<SegMask> masks = {m};
        const std::vector<DetectBox> p = {person(0.45, 0.75, 0.1, 0.1)};
        TwoStageInput in = mk(fid(s, 1, 5), p, masks);
        TwoStageResult r;
        evaluate_two_stage(in, cfg, &r, &e);
        check(r.mask_hit == 1 && r.alarm_hits == 1, "g03_stride_padded_mask_hit");
    }
    // g03b stride < width：不合法掩码 → 整体不可用
    {
        SegMask m = band_mask(1280, 720, 432, 719);
        m.stride_bytes = 1279;
        const std::vector<SegMask> masks = {m};
        const std::vector<DetectBox> p = {person(0.45, 0.75, 0.1, 0.1)};
        TwoStageInput in = mk(fid(s, 1, 6), p, masks);
        TwoStageResult r;
        evaluate_two_stage(in, cfg, &r, &e);
        check(!r.available && r.reason == two_stage_reason::kUnavailable(),
              "g03b_stride_less_than_width_unavailable");
    }
    // g04 多掩码：第一掩码未命中、第二掩码命中
    {
        const std::vector<SegMask> masks = {band_mask(1280, 720, 700, 719),
                                            band_mask(1280, 720, 432, 460)};
        const std::vector<DetectBox> p = {person(0.45, 0.58, 0.1, 0.1)};  // 中心 y=0.63 -> row 453
        TwoStageInput in = mk(fid(s, 1, 7), p, masks);
        TwoStageResult r;
        evaluate_two_stage(in, cfg, &r, &e);
        check(r.mask_hit == 1 && r.alarm_hits == 1, "g04_second_mask_hit");
    }
    // g05 全零掩码（尺寸合法、positive=0）→ 不可用（不输出安全结论）
    {
        const std::vector<SegMask> masks = {zero_mask(1280, 720)};
        const std::vector<DetectBox> p = {person(0.45, 0.75, 0.1, 0.1)};
        TwoStageInput in = mk(fid(s, 1, 8), p, masks);
        TwoStageResult r;
        evaluate_two_stage(in, cfg, &r, &e);
        check(!r.available && !r.ok && r.detections.empty()
                  && r.reason == two_stage_reason::kUnavailable(),
              "g05_zero_positive_mask_unavailable_no_safe_output");
    }
    // g06 最小命中域：1×1 像素掩码（中心像素恰在那一点）
    {
        SegMask m = zero_mask(1280, 720);
        m.plane[static_cast<std::size_t>(432) * 1280 + 511] = 1;
        m.positive = 1;
        const std::vector<SegMask> masks = {m};
        const std::vector<DetectBox> p = {person(0.35, 0.55, 0.1, 0.1)};  // 中心 px=511 py=432
        TwoStageInput in = mk(fid(s, 1, 9), p, masks);
        TwoStageResult r;
        evaluate_two_stage(in, cfg, &r, &e);
        check(r.mask_hit == 1 && r.alarm_hits == 1, "g06_single_pixel_mask_exact_hit");
    }
}

void test_danger_zone_boundary() {
    const std::string s = "s-vext";
    const RoiConfig z = danger_zone();
    TwoStageConfig cfg;
    cfg.danger_zone = &z;
    std::string e;
    const std::vector<SegMask> band = {band_mask(1280, 720, 432, 719)};
    // g07 roi_contains 闭区间边界：左上角 (0.08,0.82) 在；右/下边界用同一 double 算术构造
    // （0.08+0.84=0.9199999999999999 < 字面 0.92、0.82+0.16=0.97999999999999998 < 字面 0.98——
    //  预期必须与被测实现使用同一算术表达式，不能写十进制字面量）
    check(roi_contains(z, z.x, z.y), "g07_roi_contains_left_top_corner_inclusive");
    check(roi_contains(z, z.x + z.w, z.y + z.h), "g07b_roi_contains_right_bottom_corner_inclusive");
    check(!roi_contains(z, 0.0799, 0.9), "g07c_roi_contains_just_left_out");
    check(!roi_contains(z, 0.5, 0.8199), "g07d_roi_contains_just_above_out");
    // g08 中心恰在 zone 上缘 y=0.82：掩码命中 + zone 命中
    // （float32 算术下 0.77+0.05=0.81999998 落缘外，0.7701+0.05=0.82009999 是紧贴上缘内的
    // 可构造中心——roi_contains 为 double 闭区间，float32 中心取值离散，边界内最近点验证包含性）
    {
        const std::vector<DetectBox> p = {person(0.45, 0.7701, 0.1, 0.1)};  // 中心 ≈0.8201
        TwoStageInput in = mk(fid(s, 1, 10), p, band);
        TwoStageResult r;
        evaluate_two_stage(in, cfg, &r, &e);
        check(r.mask_hit == 1 && r.alarm_hits == 1 && !r.detections.empty() &&
                  r.detections[0].in_danger_zone,
              "g08_center_just_inside_zone_top_edge");
    }
    // g08b 中心紧贴 zone 下缘内 y≈0.9799（0.9299+0.05，float32 下 0.97989999≤0.98）
    {
        const std::vector<DetectBox> p = {person(0.45, 0.9299, 0.1, 0.1)};  // 中心 ≈0.9799
        TwoStageInput in = mk(fid(s, 1, 11), p, band);
        TwoStageResult r;
        evaluate_two_stage(in, cfg, &r, &e);
        check(r.mask_hit == 1 && r.alarm_hits == 1 && r.detections[0].in_danger_zone,
              "g08b_center_just_inside_zone_bottom_edge");
    }
    // g08c 中心紧贴 zone 下缘外 y≈0.9801（0.9301+0.05 → >0.98）：掩码命中但 zone 排除
    {
        const std::vector<DetectBox> p = {person(0.45, 0.9301, 0.1, 0.1)};
        TwoStageInput in = mk(fid(s, 1, 46), p, band);
        TwoStageResult r;
        evaluate_two_stage(in, cfg, &r, &e);
        check(r.mask_hit == 1 && r.alarm_hits == 0 && r.dropped_out_of_zone == 1 &&
                  !r.detections[0].in_danger_zone,
              "g08c_center_just_outside_zone_bottom_edge");
    }
    // g09 空白名单语义：非 person 标签框不参与（已由上游过滤，这里验证 label 透传）
    {
        const std::vector<DetectBox> p = {person(0.45, 0.85, 0.1, 0.1)};
        TwoStageInput in = mk(fid(s, 1, 12), p, band);
        TwoStageResult r;
        evaluate_two_stage(in, cfg, &r, &e);
        check(!r.detections.empty() && r.detections[0].label == "person",
              "g09_label_person_preserved");
    }
}

void test_cache_policy() {
    const std::string s = "s-vext";
    const std::vector<SegMask> band = {band_mask(1280, 720, 432, 719)};
    const std::vector<DetectBox> p = {person(0.45, 0.75, 0.1, 0.1)};
    std::string e;
    // g10 启用缓存但 max_age_ms<=0：store 拒绝、lookup 拒绝（登记要求强制）
    {
        TwoStageConfig cc;
        cc.cache.enabled = true;
        cc.cache.max_age_ms = 0;
        TwoStageEvaluator ev(cc, s);
        (void)ev.evaluate(mk(fid(s, 1, 20), p, band, true, true, 0));
        TwoStageResult r = ev.evaluate(mk(fid(s, 1, 21), p, band, false, true, 100000));
        check(!r.available && !r.used_cache, "g10_enabled_with_zero_age_never_reuses");
    }
    // g11 时间倒退（age<0）拒绝复用
    {
        TwoStageConfig cc;
        cc.cache.enabled = true;
        cc.cache.max_age_ms = 500;
        TwoStageEvaluator ev(cc, s);
        (void)ev.evaluate(mk(fid(s, 1, 22), p, band, true, true, 500000));
        TwoStageResult r = ev.evaluate(mk(fid(s, 1, 23), p, band, false, true, 400000));
        check(!r.available && !r.used_cache, "g11_negative_age_rejected");
    }
    // g12 恰在 max_age 边界（age==max_age_ms）复用（<= 语义）
    {
        TwoStageConfig cc;
        cc.cache.enabled = true;
        cc.cache.max_age_ms = 500;
        TwoStageEvaluator ev(cc, s);
        (void)ev.evaluate(mk(fid(s, 1, 24), p, band, true, true, 0));
        TwoStageResult r = ev.evaluate(mk(fid(s, 1, 25), p, band, false, true, 500000));
        check(r.available && r.used_cache && r.cache_age_ms == 500,
              "g12_age_equal_max_boundary_reuses");
    }
    // g13 max_age+1ms 即过期
    {
        TwoStageConfig cc;
        cc.cache.enabled = true;
        cc.cache.max_age_ms = 500;
        TwoStageEvaluator ev(cc, s);
        (void)ev.evaluate(mk(fid(s, 1, 26), p, band, true, true, 0));
        TwoStageResult r = ev.evaluate(mk(fid(s, 1, 27), p, band, false, true, 501000));
        check(!r.available && !r.used_cache, "g13_age_plus_one_expired");
    }
    // g14 reject_on_generation_change=false 时跨代可复用（策略显式登记才允许）
    {
        TwoStageConfig cc;
        cc.cache.enabled = true;
        cc.cache.max_age_ms = 500;
        cc.cache.reject_on_generation_change = false;
        TwoStageEvaluator ev(cc, s);
        (void)ev.evaluate(mk(fid(s, 1, 28), p, band, true, true, 0));
        TwoStageResult r = ev.evaluate(mk(fid(s, 2, 1), p, band, false, true, 100000));
        check(r.available && r.used_cache, "g14_policy_allows_generation_change_reuse");
    }
    // g14b 同策略下跨 session 仍拒绝（reject_on_generation_change 也挡 session）
    {
        TwoStageConfig cc;
        cc.cache.enabled = true;
        cc.cache.max_age_ms = 500;
        cc.cache.reject_on_generation_change = false;
        TwoStageEvaluator ev(cc, s);
        (void)ev.evaluate(mk(fid(s, 1, 29), p, band, true, true, 0));
        TwoStageResult r = ev.evaluate(mk(fid("other-sess", 1, 1), p, band, false, true, 100000));
        check(!r.available && !r.used_cache, "g14b_session_change_still_rejected");
    }
    // g15 缓存命中后 detections 完整（不可用绝不静默转安全）
    {
        TwoStageConfig cc;
        cc.cache.enabled = true;
        cc.cache.max_age_ms = 500;
        TwoStageEvaluator ev(cc, s);
        (void)ev.evaluate(mk(fid(s, 1, 30), p, band, true, true, 0));
        TwoStageResult r = ev.evaluate(mk(fid(s, 1, 31), p, band, false, true, 100000));
        check(r.available && r.used_cache && r.detections.size() == 1 &&
                  r.detections[0].mask_hit && r.alarm_hits == 1,
              "g15_cache_hit_full_detection_semantics");
    }
    // g16 缓存禁用评估器：seg 可用帧正常判定后不产生缓存
    {
        TwoStageEvaluator ev(TwoStageConfig{}, s);
        (void)ev.evaluate(mk(fid(s, 1, 32), p, band, true, true, 0));
        check(!ev.cache().has(), "g16_cache_disabled_never_stores");
    }
}

void test_config_semantics() {
    const std::string s = "s-vext";
    const std::vector<SegMask> band = {band_mask(1280, 720, 432, 719)};
    const std::vector<DetectBox> p = {person(0.45, 0.75, 0.1, 0.1)};
    std::string e;
    // g17 require_same_frame=false 时跨帧分割结果仍可用（显式配置语义）
    {
        TwoStageConfig cc;
        cc.require_same_frame = false;
        TwoStageInput in = mk(fid(s, 1, 40), p, band);
        in.seg_frame = fid(s, 1, 39);  // 分割来自前一帧
        TwoStageResult r;
        evaluate_two_stage(in, cc, &r, &e);
        check(r.available && r.mask_hit == 1 && r.alarm_hits == 1,
              "g17_require_same_frame_false_allows_cross_frame");
    }
    // g18 null 输出指针 → 参数错误返回 false
    {
        TwoStageInput in = mk(fid(s, 1, 41), p, band);
        std::string e2;
        check(!evaluate_two_stage(in, TwoStageConfig{}, nullptr, &e2), "g18_null_out_param_error");
    }
    // g19 非正图像尺寸 → 参数错误
    {
        TwoStageInput in = mk(fid(s, 1, 42), p, band);
        in.image_w = 0;
        TwoStageResult r;
        check(!evaluate_two_stage(in, TwoStageConfig{}, &r, &e) &&
                  r.reason == two_stage_reason::kBadConfig(),
              "g19_nonpositive_image_size_bad_config");
    }
    // g20 persons=nullptr：available 且 0 人（NO_PERSON）
    {
        TwoStageInput in = mk(fid(s, 1, 43), p, band);
        in.persons = nullptr;
        TwoStageResult r;
        evaluate_two_stage(in, TwoStageConfig{}, &r, &e);
        check(r.available && r.person_total == 0 && r.alarm_hits == 0 &&
                  r.reason == two_stage_reason::kNoPerson(),
              "g20_null_persons_no_person");
    }
    // g21 rail_masks=nullptr：不可用
    {
        TwoStageInput in = mk(fid(s, 1, 44), p, band);
        in.rail_masks = nullptr;
        TwoStageResult r;
        evaluate_two_stage(in, TwoStageConfig{}, &r, &e);
        check(!r.available && r.reason == two_stage_reason::kUnavailable(),
              "g21_null_masks_unavailable");
    }
    // g22 不可用时 detections 恒空（不输出安全结论的结构性保证）
    {
        std::vector<DetectBox> many = {person(0.45, 0.75, 0.1, 0.1), person(0.45, 0.85, 0.1, 0.1)};
        TwoStageInput in = mk(fid(s, 1, 45), many, band);
        in.seg_available = false;
        TwoStageResult r;
        evaluate_two_stage(in, TwoStageConfig{}, &r, &e);
        check(!r.available && r.detections.empty() && r.mask_hit == 0 && r.alarm_hits == 0 &&
                  r.person_total == 0,
              "g22_unavailable_empty_detections_no_safe_conclusion");
    }
    // g23 reason token 稳定性（跨轮身份 token 集合）
    {
        check(std::string(two_stage_reason::kOk()) == "TWO_STAGE_OK" &&
                  std::string(two_stage_reason::kUnavailable()) == "TWO_STAGE_UNAVAILABLE" &&
                  std::string(two_stage_reason::kStale()) == "TWO_STAGE_STALE" &&
                  std::string(two_stage_reason::kFrameMismatch()) == "TWO_STAGE_FRAME_MISMATCH" &&
                  std::string(two_stage_reason::kNoPerson()) == "TWO_STAGE_NO_PERSON" &&
                  std::string(two_stage_reason::kNoAlarm()) == "TWO_STAGE_NO_ALARM" &&
                  std::string(two_stage_reason::kBadConfig()) == "TWO_STAGE_BAD_CONFIG",
              "g23_reason_tokens_stable");
    }
    // g24 确定性：同参两轮全字段一致
    {
        TwoStageResult r1, r2;
        TwoStageInput in1 = mk(fid(s, 1, 50), p, band);
        TwoStageInput in2 = mk(fid(s, 1, 50), p, band);
        evaluate_two_stage(in1, TwoStageConfig{}, &r1, &e);
        evaluate_two_stage(in2, TwoStageConfig{}, &r2, &e);
        check(r1.ok == r2.ok && r1.available == r2.available && r1.reason == r2.reason &&
                  r1.mask_hit == r2.mask_hit && r1.alarm_hits == r2.alarm_hits &&
                  r1.detections.size() == r2.detections.size(),
              "g24_same_input_deterministic");
    }
}

}  // namespace

int main() {
    test_mask_pixel_semantics();
    test_danger_zone_boundary();
    test_cache_policy();
    test_config_semantics();
    std::printf("vext %s (%d failures)\n", g_fail == 0 ? "OK" : "FAILED", g_fail);
    return g_fail == 0 ? 0 : 1;
}
