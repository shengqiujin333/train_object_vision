// railhmi-core 距离测量测试（视觉推理 ITEM-011 纯逻辑）。
// 覆盖 TC-T-DIST-01..03：未标定/档位未知 -> -1；标定换算与公式一致；未知显示占位非 0；
// 规则判定与 distance_m 无关（不假设规则消费该字段）。

#include <cmath>
#include <string>
#include <vector>

#include "test_util.h"

#include "railhmi/core/inference.h"
#include "railhmi/core/rules.h"
#include "railhmi/core/vision_distance.h"

namespace {

using namespace railhmi::core;

// 历史锚点：CAM-01 500 m 处站立人 ≈89 px（README 2026-10-13 / person_measure.cc）。
CameraCalibration cam01_zoom1() {
    CameraCalibration c;
    c.camera_id = "CAM-01";
    c.zoom_level = "Z1";
    c.reference_distance_m = 500.0;
    c.reference_height_px = 89.0;
    c.reference_object_height_m = 1.70;
    c.min_distance_m = 5.0;
    c.max_distance_m = 600.0;
    c.source = "unit-test:historical-anchor";
    return c;
}

DistanceCalibrationSet set_cam01() {
    DistanceCalibrationSet s;
    s.add(cam01_zoom1());
    return s;
}

DistanceRequest person_req(double px, bool zoom_known = true, const std::string& zoom = "Z1") {
    DistanceRequest r;
    r.camera_id = "CAM-01";
    r.label = "person";
    r.box_height_px = px;
    r.zoom_known = zoom_known;
    r.zoom_level = zoom;
    return r;
}

bool near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

}  // namespace

void run_vision_distance_tests() {
    using namespace railhmi_test;

    // ---- 标定自身校验 / 派生量 ----
    {
        const CameraCalibration c = cam01_zoom1();
        check(c.valid(), "distance_calibration_valid", "500m/89px/1.70m");
        check(near(c.reference_constant(), 44500.0), "distance_reference_constant",
              std::to_string(c.reference_constant()));
        check(near(c.px_per_m_at_reference(), 89.0 / 1.70, 1e-9),
              "distance_px_per_m_reference", "89/1.70 ≈ 52.35 px/m（历史锚点）");

        CameraCalibration bad = c;
        bad.zoom_level.clear();
        check(!bad.valid(), "distance_calibration_requires_zoom", "档位必填（禁止任意档位）");
        bad = c;
        bad.reference_height_px = 0.0;
        check(!bad.valid(), "distance_calibration_requires_ref_px", "参考像素高必须为正");
        bad = c;
        bad.reference_distance_m = -1.0;
        check(!bad.valid(), "distance_calibration_requires_ref_m", "参考距离必须为正");
        bad = c;
        bad.min_distance_m = 600.0;
        bad.max_distance_m = 5.0;
        check(!bad.valid(), "distance_calibration_rejects_contradictory_range", "min>max 非法");
    }

    // ---- 纯公式 ----
    {
        const CameraCalibration c = cam01_zoom1();
        check(near(distance_from_box_height_px(c, 89.0), 500.0), "distance_formula_at_reference",
              "89px -> 500m");
        check(near(distance_from_box_height_px(c, 178.0), 250.0), "distance_formula_half_px",
              "178px -> 250m");
        check(near(distance_from_box_height_px(c, 44.5), 1000.0), "distance_formula_double_px",
              "44.5px -> 1000m");
        check(distance_from_box_height_px(c, 0.0) < 0.0, "distance_formula_zero_px_unknown",
              "0px -> -1（不除零、不臆造）");
        check(distance_from_box_height_px(c, -5.0) < 0.0, "distance_formula_negative_px_unknown",
              "负像素高 -> -1");
    }

    // ---- TC-T-DIST-01：未标定 -> -1 + NO_CALIBRATION ----
    {
        DistanceCalibrationSet empty;
        const DistanceEstimate e = estimate_distance(empty, person_req(89.0));
        check(!e.valid && e.meters == -1.0 && e.reason == distance_reason::kNoCalibration(),
              "distance_no_calibration_unknown", e.reason);
        check(distance_text(e) == "—", "distance_no_calibration_text_placeholder",
              distance_text(e));
        check(distance_text(e) != "0" && distance_text(e).find("0.0") == std::string::npos,
              "distance_unknown_not_zero", distance_text(e));
    }

    // ---- 变焦档位门控（未知 / 不匹配）----
    {
        const DistanceCalibrationSet s = set_cam01();
        DistanceEstimate e = estimate_distance(s, person_req(89.0, /*zoom_known=*/false));
        check(!e.valid && e.reason == distance_reason::kZoomUnknown(),
              "distance_zoom_unknown_refused", e.detail);

        e = estimate_distance(s, person_req(89.0, true, "Z2"));
        check(!e.valid && e.reason == distance_reason::kZoomMismatch(),
              "distance_zoom_mismatch_refused", e.detail);

        e = estimate_distance(s, person_req(89.0, true, ""));
        check(!e.valid && e.reason == distance_reason::kZoomUnknown(),
              "distance_empty_zoom_refused", "空档位按未知处理");
    }

    // ---- 标签/像素门控 ----
    {
        const DistanceCalibrationSet s = set_cam01();
        DistanceRequest r = person_req(89.0);
        r.label = "obstacle";
        DistanceEstimate e = estimate_distance(s, r);
        check(!e.valid && e.reason == distance_reason::kNotPerson(),
              "distance_non_person_refused", "非人员框无身高先验");

        e = estimate_distance(s, person_req(0.0));
        check(!e.valid && e.reason == distance_reason::kInvalidBox(),
              "distance_zero_box_refused", e.reason);
    }

    // ---- 有效路径 + 证据字段 ----
    {
        const DistanceCalibrationSet s = set_cam01();
        const DistanceEstimate e = estimate_distance(s, person_req(89.0));
        check(e.valid && near(e.meters, 500.0) && e.reason == distance_reason::kOk(),
              "distance_reference_valid", std::to_string(e.meters));
        check(near(e.px_per_m_reference, 89.0 / 1.70, 1e-9), "distance_evidence_px_per_m",
              std::to_string(e.px_per_m_reference));
        check(e.used_zoom == "Z1" && near(e.box_height_px, 89.0), "distance_evidence_inputs",
              e.used_zoom);
        check(distance_text(e) == "500.0 m", "distance_text_valid", distance_text(e));
        const std::string j = distance_json(e);
        check(j.find("\"valid\":true") != std::string::npos &&
                  j.find("\"reason\":\"DISTANCE_OK\"") != std::string::npos &&
                  j.find("\"px_per_m_reference\":52.35") != std::string::npos,
              "distance_json_fields", j.substr(0, 140));
    }

    // ---- 有效范围：超范围返回未知，不 clamp ----
    {
        const DistanceCalibrationSet s = set_cam01();
        const DistanceEstimate near_ = estimate_distance(s, person_req(500.0));   // 89m? -> within
        check(near_.valid && near(near_.meters, 89.0), "distance_within_range_valid",
              std::to_string(near_.meters));

        const DistanceEstimate far_ = estimate_distance(s, person_req(40.0));     // 1112.5 m > 600
        check(!far_.valid && far_.meters == -1.0 && far_.reason == distance_reason::kOutOfRange(),
              "distance_out_of_range_unknown", far_.detail);

        const DistanceEstimate too_close = estimate_distance(s, person_req(9000.0));  // 4.94 m < 5
        check(!too_close.valid && too_close.reason == distance_reason::kOutOfRange(),
              "distance_below_range_unknown", too_close.detail);
    }

    // ---- 标定集合查询 ----
    {
        DistanceCalibrationSet s = set_cam01();
        CameraCalibration z2 = cam01_zoom1();
        z2.zoom_level = "Z2";
        z2.reference_distance_m = 250.0;
        s.add(z2);
        check(s.size() == 2 && s.has_camera("CAM-01") && !s.has_camera("CAM-09"),
              "distance_set_queries", "size/has_camera");
        check(s.zooms_of("CAM-01").size() == 2, "distance_set_zooms", "两个档位");
        const CameraCalibration* c = s.find("CAM-01", "Z2");
        check(c && near(c->reference_distance_m, 250.0), "distance_set_find_zoom",
              "按档位精确匹配");
        check(s.find("CAM-01", "Z3") == nullptr, "distance_set_find_unknown_zoom", "无匹配返回空");
        s.clear();
        check(s.size() == 0, "distance_set_clear", "clear");
    }

    // ---- 标定字符串解析（配置/CLI 入口）----
    {
        CameraCalibration c;
        std::string err;
        check(parse_calibration_spec(
                  "camera=CAM-01,zoom=Z1,ref_m=500,ref_h_px=89,obj_m=1.70,min=5,max=600,"
                  "source=README:2026-10-13",
                  &c, &err),
              "distance_parse_spec_ok", err);
        check(c.camera_id == "CAM-01" && c.zoom_level == "Z1" && near(c.reference_distance_m, 500.0) &&
                  near(c.reference_height_px, 89.0) && near(c.reference_object_height_m, 1.70) &&
                  near(c.min_distance_m, 5.0) && near(c.max_distance_m, 600.0) &&
                  c.source == "README:2026-10-13",
              "distance_parse_spec_fields", c.source);

        check(!parse_calibration_spec("zoom=Z1,ref_m=500,ref_h_px=89", &c, &err),
              "distance_parse_requires_camera", err);
        check(!parse_calibration_spec("camera=CAM-01,ref_m=500,ref_h_px=89", &c, &err),
              "distance_parse_requires_zoom", err);
        check(!parse_calibration_spec("camera=CAM-01,zoom=Z1,ref_m=500", &c, &err),
              "distance_parse_requires_ref_px", err);
        check(!parse_calibration_spec("camera=CAM-01,zoom=Z1,ref_m=abc,ref_h_px=89", &c, &err),
              "distance_parse_rejects_bad_number", err);
        check(!parse_calibration_spec("camera=CAM-01,zoom=Z1,ref_m=500,ref_h_px=89,foo=1", &c, &err),
              "distance_parse_rejects_unknown_key", err);
        check(!parse_calibration_spec("camera=CAM-01,zoom=Z1,ref_m=500,ref_h_px=0", &c, &err),
              "distance_parse_rejects_zero_px", err);
    }

    // ---- TC-T-DIST-03：规则不消费距离（事件判定与 distance_m 无关）----
    {
        RuleConfig rc;
        rc.confirm_frames = 1;
        rc.confirm_window_ms = 500;
        rc.confidence_min = 0.5;
        rc.suppress_window_ms = 3000;
        RuleEngine a(rc);
        RuleEngine b(rc);
        Detection d;
        d.camera_id = "CAM-01";
        d.profile = AiProfile::Front;
        d.t_ms = 1000;
        d.track_id = 1;
        d.in_roi = true;
        d.confidence = 0.9;
        d.label = "person";
        d.distance_m = -1.0;        // 未知
        Detection e = d;
        e.distance_m = 3.2;         // 已标定距离
        const auto ea = a.on_detection(d);
        const auto eb = b.on_detection(e);
        check(ea.size() == 1 && eb.size() == 1, "distance_rules_emit_same_count",
              "1 vs 1 事件");
        check(ea.size() == 1 && eb.size() == 1 && ea[0].type == eb[0].type &&
                  ea[0].level == eb[0].level,
              "distance_rules_same_type_level", "距离不改变报警类型/级别");
        check(ea.size() == 1 && ea[0].distance_m == -1.0, "distance_rule_keeps_unknown_field",
              "事件保留 -1（规则未消费）");
    }

    // ---- P5 调试视图：未知距离显示占位，不是 0 ----
    {
        std::vector<CameraSlot> reg;
        CameraSlot slot;
        slot.id = "CAM-01";
        slot.name = "车头前方";
        slot.ai_enabled = true;
        slot.ai_profile = AiProfile::Front;
        reg.push_back(slot);

        AiRuntime rt;
        rt.worker_alive = true;
        AiChannelRuntime ch;
        ch.camera_id = "CAM-01";
        ch.profile = AiProfile::Front;
        ch.last_result_ms = 1000;
        rt.channels.push_back(ch);

        DetectionResult r;
        r.camera_id = "CAM-01";
        r.profile = AiProfile::Front;
        DetectionBox box;
        box.label = "person";
        box.distance_m = -1.0f;
        r.boxes.push_back(box);
        std::vector<DetectionResult> latest = {r};

        auto dbg = build_ai_debug(reg, rt, latest, 1200);
        check(dbg.size() == 1 && dbg[0].distance_text == "—",
              "distance_debug_unknown_placeholder", dbg.empty() ? "" : dbg[0].distance_text);
        check(dbg.size() == 1 && dbg[0].distance_text != "0" &&
                  dbg[0].distance_text.find("0.0") == std::string::npos,
              "distance_debug_unknown_not_zero", dbg.empty() ? "" : dbg[0].distance_text);

        latest[0].boxes[0].distance_m = 123.4f;
        auto dbg2 = build_ai_debug(reg, rt, latest, 1200);
        check(dbg2.size() == 1 && dbg2[0].distance_text == "123.4 m",
              "distance_debug_valid_text", dbg2.empty() ? "" : dbg2[0].distance_text);
    }
}
