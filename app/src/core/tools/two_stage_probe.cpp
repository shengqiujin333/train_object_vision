// railhmi-vision-twostage (视觉推理 ITEM-005)
//
// 同帧两阶段判定探针：同一帧身份下组合人员检测与轨道分割；人员框中心点在 rail 掩码内 ->
// in_roi/person 语义；配置 danger_zone 时再与 roi.yaml 求交；分割失败/掩码为空 -> 不可用
// （不输出“轨道内安全”）；旧 generation/乱序/重复 seq 拒绝；跨帧缓存默认关闭。
//
// 用法:
//   railhmi-vision-twostage --selftest [--out log] [--json file]
//   railhmi-vision-twostage --fixture center_in|center_out|no_person|seg_unavailable|mask_empty|
//       detect_unavailable|frame_mismatch|danger_zone_hit|danger_zone_miss|stale|cache_disabled|
//       cache_hit|cache_expired|cache_gen_change|bounds|multi|mask_misaligned
//   railhmi-vision-twostage --config DIR   # danger_zone 取自 roi.yaml CAM-01
//
// 退出码: 0 通过；1 失败；2 参数错误。

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/rules.h"
#include "railhmi/core/vision_two_stage.h"

#ifndef RAILHMI_CONFIG_DIR
#define RAILHMI_CONFIG_DIR "config"
#endif

using namespace railhmi::core;

namespace {

int g_fail = 0;
std::vector<std::string> g_lines;
std::ofstream g_fo;

void emit(const std::string& l) {
    std::printf("%s\n", l.c_str());
    g_lines.push_back(l);
    if (g_fo) g_fo << l << "\n";
}
void check(bool ok, const std::string& name, const std::string& detail = "") {
    emit(std::string("[") + (ok ? "PASS" : "FAIL") + "] " + name + (detail.empty() ? "" : (" :: " + detail)));
    if (!ok) ++g_fail;
}
bool near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

FrameId fid(const std::string& sess, std::uint64_t gen, std::uint64_t seq) {
    FrameId f;
    f.session_id = sess;
    f.camera_id = "CAM-01";
    f.generation = gen;
    f.seq = seq;
    return f;
}

SegMask rail_mask(int w, int h, double x0, double y0, double x1, double y1) {
    SegMask m;
    m.width = w;
    m.height = h;
    m.stride_bytes = w;
    m.plane.assign(static_cast<std::size_t>(w) * h, 0);
    const int px0 = std::max(0, std::min(static_cast<int>(x0 * w), w - 1));
    const int px1 = std::max(0, std::min(static_cast<int>(x1 * w), w - 1));
    const int py0 = std::max(0, std::min(static_cast<int>(y0 * h), h - 1));
    const int py1 = std::max(0, std::min(static_cast<int>(y1 * h), h - 1));
    for (int y = py0; y <= py1; ++y)
        for (int x = px0; x <= px1; ++x) {
            m.plane[static_cast<std::size_t>(y) * w + x] = 1;
            ++m.positive;
        }
    return m;
}

DetectBox person(double x, double y, double w, double h, double score = 0.9) {
    DetectBox b;
    b.x = static_cast<float>(x);
    b.y = static_cast<float>(y);
    b.w = static_cast<float>(w);
    b.h = static_cast<float>(h);
    b.score = static_cast<float>(score);
    b.class_id = 0;
    b.label = "person";
    return b;
}

std::string res_line(const TwoStageResult& r) {
    char b[400];
    std::snprintf(b, sizeof(b),
                  "type=two_stage ok=%d available=%d reason=%s persons=%lld mask_hit=%lld alarms=%lld "
                  "dropped_no_mask=%lld dropped_zone=%lld dropped_bounds=%lld used_cache=%d cache_age_ms=%lld",
                  r.ok ? 1 : 0, r.available ? 1 : 0, r.reason.c_str(), r.person_total, r.mask_hit,
                  r.alarm_hits, r.dropped_no_mask, r.dropped_out_of_zone, r.dropped_out_of_bounds,
                  r.used_cache ? 1 : 0, r.cache_age_ms);
    return b;
}

TwoStageInput mk_input(const FrameId& frame, const std::vector<DetectBox>& persons,
                       const std::vector<SegMask>& masks, bool seg_ok, bool detect_ok,
                       long long now_us = 0) {
    TwoStageInput in;
    in.frame = frame;
    in.detect_frame = frame;
    in.seg_frame = frame;
    in.detect_available = detect_ok;
    in.seg_available = seg_ok;
    in.now_mono_us = now_us;
    in.persons = &persons;
    in.rail_masks = &masks;
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

int run_selftest() {
    const std::string sess = "sess-unit";
    const std::vector<SegMask> band = {rail_mask(1280, 720, 0.0, 0.6, 1.0, 1.0)};
    const std::vector<SegMask> empty_mask = {SegMask{}};  // 尺寸 0 -> 不对齐/无正像素
    TwoStageConfig cfg;
    std::string e;

    // 帧身份比较
    check(same_frame(fid(sess, 1, 5), fid(sess, 1, 5)), "same_frame identical");
    check(!same_frame(fid(sess, 1, 5), fid(sess, 1, 6)), "same_frame different seq");
    check(!same_frame(fid(sess, 1, 5), fid(sess, 2, 5)), "same_frame different generation");
    check(!same_frame(fid("other", 1, 5), fid(sess, 1, 5)), "same_frame different session");

    // 中心命中 / 未命中
    {
        const FrameId f = fid(sess, 1, 1);
        const std::vector<DetectBox> persons = {person(0.45, 0.75, 0.1, 0.1)};  // 中心 (0.5,0.8)
        TwoStageInput in = mk_input(f, persons, band, true, true);
        TwoStageResult r;
        check(evaluate_two_stage(in, cfg, &r, &e), "center_in ok", e);
        check(r.available && r.alarm_hits == 1 && r.mask_hit == 1 && r.reason == two_stage_reason::kOk(),
              "center_in alarm", res_line(r));
        check(!r.detections.empty() && r.detections[0].mask_hit && r.detections[0].on_rail_alarm &&
                  r.detections[0].label == "person",
              "center_in detection semantics");
    }
    {
        const FrameId f = fid(sess, 1, 2);
        const std::vector<DetectBox> persons = {person(0.45, 0.15, 0.1, 0.1)};  // 中心 (0.5,0.2)
        TwoStageInput in = mk_input(f, persons, band, true, true);
        TwoStageResult r;
        check(evaluate_two_stage(in, cfg, &r, &e) && r.available && r.mask_hit == 0 &&
                  r.dropped_no_mask == 1 && r.alarm_hits == 0 &&
                  r.reason == two_stage_reason::kNoAlarm(),
              "center_out no alarm", res_line(r));
    }
    // 无人员
    {
        const FrameId f = fid(sess, 1, 3);
        std::vector<DetectBox> none;
        TwoStageInput in = mk_input(f, none, band, true, true);
        TwoStageResult r;
        check(evaluate_two_stage(in, cfg, &r, &e) && r.available && r.alarm_hits == 0 &&
                  r.reason == two_stage_reason::kNoPerson(),
              "no person -> NO_PERSON", res_line(r));
    }
    // 可用性：分割/掩码/检测不可用 -> 不可用，且不输出框
    {
        const FrameId f = fid(sess, 1, 4);
        const std::vector<DetectBox> persons = {person(0.45, 0.75, 0.1, 0.1)};
        TwoStageInput in = mk_input(f, persons, band, false, true);
        TwoStageResult r;
        check(evaluate_two_stage(in, cfg, &r, &e) && !r.available && !r.ok &&
                  r.reason == two_stage_reason::kUnavailable() && r.detections.empty(),
              "seg unavailable -> UNAVAILABLE (no safe conclusion)", res_line(r));
    }
    {
        const FrameId f = fid(sess, 1, 5);
        const std::vector<DetectBox> persons = {person(0.45, 0.75, 0.1, 0.1)};
        TwoStageInput in = mk_input(f, persons, empty_mask, true, true);
        TwoStageResult r;
        check(evaluate_two_stage(in, cfg, &r, &e) && !r.available &&
                  r.reason == two_stage_reason::kUnavailable(),
              "empty mask -> UNAVAILABLE", res_line(r));
    }
    {
        const FrameId f = fid(sess, 1, 6);
        const std::vector<DetectBox> persons = {person(0.45, 0.75, 0.1, 0.1)};
        TwoStageInput in = mk_input(f, persons, band, true, false);
        TwoStageResult r;
        check(evaluate_two_stage(in, cfg, &r, &e) && !r.available &&
                  r.reason == two_stage_reason::kUnavailable(),
              "detect unavailable -> UNAVAILABLE", res_line(r));
    }
    // 帧不一致
    {
        const FrameId f = fid(sess, 1, 7);
        const std::vector<DetectBox> persons = {person(0.45, 0.75, 0.1, 0.1)};
        TwoStageInput in = mk_input(f, persons, band, true, true);
        in.seg_frame = fid(sess, 1, 8);  // 分割来自另一帧
        TwoStageResult r;
        check(evaluate_two_stage(in, cfg, &r, &e) && !r.available &&
                  r.reason == two_stage_reason::kFrameMismatch(),
              "seg_frame != frame -> FRAME_MISMATCH", res_line(r));
    }
    {
        const FrameId f = fid(sess, 1, 9);
        const std::vector<DetectBox> persons = {person(0.45, 0.75, 0.1, 0.1)};
        TwoStageInput in = mk_input(f, persons, band, true, true);
        in.detect_frame = fid(sess, 2, 9);
        TwoStageResult r;
        check(evaluate_two_stage(in, cfg, &r, &e) && !r.available &&
                  r.reason == two_stage_reason::kFrameMismatch(),
              "detect_frame != frame -> FRAME_MISMATCH", res_line(r));
    }
    // danger_zone
    {
        const RoiConfig dz = danger_zone();
        TwoStageConfig dc = cfg;
        dc.danger_zone = &dz;
        const FrameId f = fid(sess, 1, 10);
        const std::vector<DetectBox> persons = {person(0.45, 0.85, 0.1, 0.1)};  // 中心 (0.5,0.9)
        TwoStageInput in = mk_input(f, persons, band, true, true);
        TwoStageResult r;
        check(evaluate_two_stage(in, dc, &r, &e) && r.alarm_hits == 1 && !r.detections.empty() &&
                  r.detections[0].in_danger_zone && r.detections[0].on_rail_alarm,
              "danger_zone hit", res_line(r));
    }
    {
        const RoiConfig dz = danger_zone();
        TwoStageConfig dc = cfg;
        dc.danger_zone = &dz;
        const FrameId f = fid(sess, 1, 11);
        const std::vector<DetectBox> persons = {person(0.45, 0.65, 0.1, 0.1)};  // 中心 (0.5,0.7)
        TwoStageInput in = mk_input(f, persons, band, true, true);
        TwoStageResult r;
        check(evaluate_two_stage(in, dc, &r, &e) && r.mask_hit == 1 && r.dropped_out_of_zone == 1 &&
                  r.alarm_hits == 0 && !r.detections.empty() && !r.detections[0].on_rail_alarm,
              "danger_zone miss (on rail but outside zone)", res_line(r));
    }
    // 身份接纳（evaluator）
    {
        TwoStageEvaluator ev(cfg, sess);
        const std::vector<DetectBox> persons = {person(0.45, 0.75, 0.1, 0.1)};
        TwoStageInput in1 = mk_input(fid(sess, 1, 20), persons, band, true, true);
        TwoStageResult r1 = ev.evaluate(in1);
        TwoStageInput in2 = mk_input(fid(sess, 1, 20), persons, band, true, true);  // 重复 seq
        TwoStageResult r2 = ev.evaluate(in2);
        check(r1.ok && !r2.ok && r2.reason == two_stage_reason::kStale() && !r2.available,
              "duplicate seq rejected", r2.detail);
        TwoStageInput in3 = mk_input(fid(sess, 1, 19), persons, band, true, true);  // 乱序
        TwoStageResult r3 = ev.evaluate(in3);
        check(!r3.ok && r3.reason == two_stage_reason::kStale(), "out-of-order seq rejected", r3.detail);
        TwoStageInput in4 = mk_input(fid(sess, 0, 1), persons, band, true, true);   // 旧 generation
        TwoStageResult r4 = ev.evaluate(in4);
        check(!r4.ok && r4.reason == two_stage_reason::kStale(), "stale generation rejected", r4.detail);
    }
    // 缓存：默认关闭
    {
        TwoStageEvaluator ev(cfg, sess);
        const std::vector<DetectBox> persons = {person(0.45, 0.75, 0.1, 0.1)};
        (void)ev.evaluate(mk_input(fid(sess, 1, 30), persons, band, true, true, 0));
        TwoStageResult r = ev.evaluate(mk_input(fid(sess, 1, 31), persons, band, false, true, 100000));
        check(!r.available && r.reason == two_stage_reason::kUnavailable() && !r.used_cache,
              "cache disabled -> no reuse", res_line(r));
    }
    // 缓存：启用 + 同 generation 近期 -> 复用
    {
        TwoStageConfig cc = cfg;
        cc.cache.enabled = true;
        cc.cache.max_age_ms = 500;
        cc.cache.policy_note = "test";
        TwoStageEvaluator ev(cc, sess);
        const std::vector<DetectBox> persons = {person(0.45, 0.75, 0.1, 0.1)};
        TwoStageResult r1 = ev.evaluate(mk_input(fid(sess, 1, 40), persons, band, true, true, 0));
        TwoStageResult r2 = ev.evaluate(mk_input(fid(sess, 1, 41), persons, band, false, true, 100000));
        check(r1.ok && r2.available && r2.used_cache && r2.cache_age_ms == 100 && r2.alarm_hits == 1,
              "cache enabled same generation -> reuse", res_line(r2));
        // 过期
        TwoStageResult r3 = ev.evaluate(mk_input(fid(sess, 1, 42), persons, band, false, true, 900000));
        check(!r3.available && !r3.used_cache, "cache expired -> unavailable", res_line(r3));
        // generation 变化
        TwoStageResult r4 = ev.evaluate(mk_input(fid(sess, 2, 1), persons, band, false, true, 950000));
        check(!r4.available && !r4.used_cache, "cache generation change rejected", res_line(r4));
    }
    // 越界与多目标
    {
        const FrameId f = fid(sess, 1, 50);
        const std::vector<DetectBox> persons = {person(0.45, 0.75, 0.1, 0.1),
                                                person(0.45, 0.85, 0.1, 0.1),
                                                person(0.45, 0.15, 0.1, 0.1)};
        TwoStageInput in = mk_input(f, persons, band, true, true);
        TwoStageResult r;
        check(evaluate_two_stage(in, cfg, &r, &e) && r.person_total == 3 && r.mask_hit == 2 &&
                  r.dropped_no_mask == 1 && r.alarm_hits == 2,
              "multi person aggregation", res_line(r));
    }
    {
        const FrameId f = fid(sess, 1, 51);
        DetectBox out = person(1.02, 0.75, 0.1, 0.1);  // 中心 >1
        const std::vector<DetectBox> persons = {out};
        TwoStageInput in = mk_input(f, persons, band, true, true);
        TwoStageResult r;
        check(evaluate_two_stage(in, cfg, &r, &e) && r.dropped_out_of_bounds == 1 && r.alarm_hits == 0,
              "out-of-bounds center dropped", res_line(r));
    }
    // 掩码与图像尺寸不对齐
    {
        const FrameId f = fid(sess, 1, 52);
        const std::vector<DetectBox> persons = {person(0.45, 0.75, 0.1, 0.1)};
        const std::vector<SegMask> small = {rail_mask(64, 48, 0.0, 0.5, 1.0, 1.0)};
        TwoStageInput in = mk_input(f, persons, small, true, true);
        TwoStageResult r;
        check(evaluate_two_stage(in, cfg, &r, &e) && !r.available &&
                  r.reason == two_stage_reason::kUnavailable(),
              "misaligned mask -> UNAVAILABLE", res_line(r));
    }
    emit("RESULT: " + std::string(g_fail == 0 ? "PASS" : "FAIL") + " failures=" + std::to_string(g_fail));
    return g_fail == 0 ? 0 : 1;
}

int run_fixture(const std::string& name, const RoiConfig* dz) {
    const std::string sess = "sess-fx";
    TwoStageConfig cfg;
    cfg.danger_zone = dz;
    if (dz) cfg.danger_zone_source = "roi.yaml CAM-01/danger_zone";
    const std::vector<SegMask> band = {rail_mask(1280, 720, 0.0, 0.6, 1.0, 1.0)};
    const std::vector<DetectBox> in_band = {person(0.45, 0.75, 0.1, 0.1)};
    const std::vector<DetectBox> out_band = {person(0.45, 0.15, 0.1, 0.1)};
    std::vector<DetectBox> none;

    TwoStageResult r;
    std::string e;
    if (name == "center_in") {
        TwoStageInput in = mk_input(fid(sess, 1, 1), in_band, band, true, true);
        evaluate_two_stage(in, cfg, &r, &e);
    } else if (name == "center_out") {
        TwoStageInput in = mk_input(fid(sess, 1, 2), out_band, band, true, true);
        evaluate_two_stage(in, cfg, &r, &e);
    } else if (name == "no_person") {
        TwoStageInput in = mk_input(fid(sess, 1, 3), none, band, true, true);
        evaluate_two_stage(in, cfg, &r, &e);
    } else if (name == "seg_unavailable") {
        TwoStageInput in = mk_input(fid(sess, 1, 4), in_band, band, false, true);
        evaluate_two_stage(in, cfg, &r, &e);
    } else if (name == "mask_empty") {
        const std::vector<SegMask> em = {SegMask{}};
        TwoStageInput in = mk_input(fid(sess, 1, 5), in_band, em, true, true);
        evaluate_two_stage(in, cfg, &r, &e);
    } else if (name == "detect_unavailable") {
        TwoStageInput in = mk_input(fid(sess, 1, 6), in_band, band, true, false);
        evaluate_two_stage(in, cfg, &r, &e);
    } else if (name == "frame_mismatch") {
        TwoStageInput in = mk_input(fid(sess, 1, 7), in_band, band, true, true);
        in.seg_frame = fid(sess, 1, 8);
        evaluate_two_stage(in, cfg, &r, &e);
    } else if (name == "danger_zone_hit") {
        const RoiConfig z = danger_zone();
        TwoStageConfig dc = cfg;
        dc.danger_zone = &z;
        const std::vector<DetectBox> ps = {person(0.45, 0.85, 0.1, 0.1)};
        TwoStageInput in = mk_input(fid(sess, 1, 10), ps, band, true, true);
        evaluate_two_stage(in, dc, &r, &e);
    } else if (name == "danger_zone_miss") {
        const RoiConfig z = danger_zone();
        TwoStageConfig dc = cfg;
        dc.danger_zone = &z;
        const std::vector<DetectBox> ps = {person(0.45, 0.65, 0.1, 0.1)};
        TwoStageInput in = mk_input(fid(sess, 1, 11), ps, band, true, true);
        evaluate_two_stage(in, dc, &r, &e);
    } else if (name == "stale") {
        TwoStageEvaluator ev(cfg, sess);
        (void)ev.evaluate(mk_input(fid(sess, 1, 20), in_band, band, true, true));
        r = ev.evaluate(mk_input(fid(sess, 1, 20), in_band, band, true, true));
    } else if (name == "cache_disabled") {
        TwoStageEvaluator ev(cfg, sess);
        (void)ev.evaluate(mk_input(fid(sess, 1, 30), in_band, band, true, true, 0));
        r = ev.evaluate(mk_input(fid(sess, 1, 31), in_band, band, false, true, 100000));
    } else if (name == "cache_hit" || name == "cache_expired" || name == "cache_gen_change") {
        TwoStageConfig cc = cfg;
        cc.cache.enabled = true;
        cc.cache.max_age_ms = 500;
        cc.cache.policy_note = "max_age_ms=500; reject_on_generation_change=1";
        TwoStageEvaluator ev(cc, sess);
        (void)ev.evaluate(mk_input(fid(sess, 1, 40), in_band, band, true, true, 0));
        if (name == "cache_hit")
            r = ev.evaluate(mk_input(fid(sess, 1, 41), in_band, band, false, true, 100000));
        else if (name == "cache_expired")
            r = ev.evaluate(mk_input(fid(sess, 1, 42), in_band, band, false, true, 900000));
        else
            r = ev.evaluate(mk_input(fid(sess, 2, 1), in_band, band, false, true, 950000));
    } else if (name == "bounds") {
        const std::vector<DetectBox> ps = {person(1.02, 0.75, 0.1, 0.1)};
        TwoStageInput in = mk_input(fid(sess, 1, 50), ps, band, true, true);
        evaluate_two_stage(in, cfg, &r, &e);
    } else if (name == "multi") {
        const std::vector<DetectBox> ps = {person(0.45, 0.75, 0.1, 0.1), person(0.45, 0.85, 0.1, 0.1),
                                           person(0.45, 0.15, 0.1, 0.1)};
        TwoStageInput in = mk_input(fid(sess, 1, 51), ps, band, true, true);
        evaluate_two_stage(in, cfg, &r, &e);
    } else if (name == "mask_misaligned") {
        const std::vector<SegMask> small = {rail_mask(64, 48, 0.0, 0.5, 1.0, 1.0)};
        TwoStageInput in = mk_input(fid(sess, 1, 52), in_band, small, true, true);
        evaluate_two_stage(in, cfg, &r, &e);
    } else {
        std::fprintf(stderr, "unknown fixture: %s\n", name.c_str());
        return 2;
    }
    const bool zone_used = (name == "danger_zone_hit" || name == "danger_zone_miss" || dz != nullptr);
    emit("type=config require_same_frame=1 danger_zone=" + std::string(zone_used ? "1" : "0") +
         " cache_enabled=" + std::string(cfg.cache.enabled ? "1" : "0"));
    emit(res_line(r));
    for (std::size_t i = 0; i < r.detections.size(); ++i) {
        const auto& d = r.detections[i];
        char b[220];
        std::snprintf(b, sizeof(b), "det[%zu] xywh=(%.4f,%.4f,%.4f,%.4f) score=%.3f mask_hit=%d zone=%d alarm=%d",
                      i, d.box.x, d.box.y, d.box.w, d.box.h, d.box.score, d.mask_hit ? 1 : 0,
                      d.in_danger_zone ? 1 : 0, d.on_rail_alarm ? 1 : 0);
        emit(b);
    }
    // 期望：不可用类必须 !available 且 reason 明确；可用类必须 available 且 reason 非 UNAVAILABLE
    const bool unavailable_case = (name == "seg_unavailable" || name == "mask_empty" ||
                                   name == "detect_unavailable" || name == "frame_mismatch" ||
                                   name == "stale" || name == "cache_disabled" ||
                                   name == "cache_expired" || name == "cache_gen_change" ||
                                   name == "mask_misaligned");
    const bool pass = unavailable_case ? (!r.available && !r.reason.empty())
                                       : (r.available && r.ok);
    emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
    return pass ? 0 : 1;
}

int run_config(const std::string& dir) {
    ConfigLoadResult cfg = load_config_dir(dir);
    if (!cfg.ok) {
        emit("type=error config invalid");
        for (const auto& er : cfg.errors) emit("  " + er.str());
        return 1;
    }
    const RoiConfig* dz = find_roi(cfg.settings.roi, "CAM-01", "danger_zone");
    emit("type=config dir=" + dir + " danger_zone=" + std::string(dz ? "found" : "missing") +
         (dz ? (" x=" + std::to_string(dz->x) + " y=" + std::to_string(dz->y) + " w=" +
                std::to_string(dz->w) + " h=" + std::to_string(dz->h))
             : ""));
    if (!dz) {
        emit("RESULT: FAIL (danger_zone not configured)");
        return 1;
    }
    return run_fixture("danger_zone_hit", dz);
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode, name = "center_in", out, json, dir = RAILHMI_CONFIG_DIR;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--selftest") mode = "selftest";
        else if (a == "--fixture") { next(&name); mode = "fixture"; }
        else if (a == "--config") { next(&dir); mode = "config"; }
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-vision-twostage --selftest | --fixture NAME | --config DIR\n");
            return 0;
        } else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }
    if (mode.empty()) { std::fprintf(stderr, "need --selftest/--fixture/--config\n"); return 2; }
    if (!out.empty()) g_fo.open(out, std::ios::trunc);
    int rc = 0;
    if (mode == "selftest") rc = run_selftest();
    else if (mode == "fixture") rc = run_fixture(name, nullptr);
    else rc = run_config(dir);
    if (!json.empty()) {
        std::ofstream fj(json, std::ios::trunc);
        if (fj) {
            fj << "{\"mode\":\"" << mode << "\",\"failures\":" << g_fail << ",\"lines\":[";
            for (std::size_t i = 0; i < g_lines.size(); ++i) {
                if (i) fj << ",";
                std::string esc;
                for (char ch : g_lines[i]) { if (ch == '"' || ch == '\\') esc += '\\'; esc += ch; }
                fj << "\"" << esc << "\"";
            }
            fj << "]}";
        }
    }
    return rc;
}
