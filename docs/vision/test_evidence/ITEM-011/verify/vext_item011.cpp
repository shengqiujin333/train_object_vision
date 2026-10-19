// ITEM-011 验证侧自有夹具（vext_item011.cpp）——板端 g++ 直编。
//
// 覆盖交付自测（core 47 distance 判例 + selftest 14 + media 6）未覆盖的边界：
//   A. worker 集成 zoom 门控路径（VisionAiWorker 真实发帧链路）：
//      zoom_unknown / zoom_mismatch / out_of_range / non_person（selftest 只有
//      valid 与 no-calib 两条）；多档位标定下按档位精确换算。
//   B. core 门控顺序互斥（同请求只产一个 reason，按固定顺序）。
//   C. 有效范围端点语义（[5,600] 半开/闭判定、0=不限、单边限定）。
//   D. 多相机/多档位集合 find/has 消费面。
//   E. distance_json 数值格式（整数距离无小数点、-1 恒等输出）。
//   F. parse 边界（负 min、max=0 不限、obj_m 缺省 px/m 记 -1）。
//
// GStreamer 直驱 FrameStore::publish 真租约链路（视频线 ITEM-010 形态）。

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include <gst/gst.h>
#include <gst/video/video.h>

#include "railhmi/core/frame_identity.h"
#include "railhmi/core/vision_distance.h"
#include "railhmi/media/vision_ai_worker.h"
#include "railhmi/media/frame_store.h"

using namespace railhmi;
using core::CameraCalibration;
using core::DistanceCalibrationSet;
using core::DistanceRequest;
using core::estimate_distance;
using core::parse_calibration_spec;

static int fails = 0;
static int passn = 0;
static void check(bool ok, const char* name, const std::string& detail = "") {
    std::printf("vext %s %s%s%s\n", ok ? "PASS" : "FAIL", name,
                detail.empty() ? "" : " :: ",
                detail.empty() ? "" : detail.c_str());
    std::fflush(stdout);
    if (ok) ++passn; else ++fails;
}

static bool near(double a, double b, double tol = 1e-6) {
    return std::fabs(a - b) <= tol;
}

// 全局锚点标定（与交付 doc 公布常数一致，纯验证侧独立构造）
static CameraCalibration anchor() {
    CameraCalibration c;
    std::string err;
    if (!parse_calibration_spec(
            "camera=CAM-01,zoom=Z1,ref_m=500,ref_h_px=89,obj_m=1.70,min=5,max=600,"
            "source=vext:independent",
            &c, &err)) {
        std::printf("vext FATAL fixture_anchor_parse_failed: %s\n", err.c_str());
        std::exit(3);
    }
    return c;
}

static DistanceRequest person_req(double px, bool known, const std::string& zoom,
                                  const std::string& label = "person") {
    DistanceRequest r;
    r.camera_id = "CAM-01";
    r.label = label;
    r.box_height_px = px;
    r.zoom_known = known;
    r.zoom_level = zoom;
    return r;
}

// 真实 GStreamer NV12 sample -> FrameStore::publish -> latest() 真租约。
static media::FrameLease publish_gray_nv12(media::FrameStore& store, core::FrameMeta meta,
                                           int h = 180) {
    const int w = 320, hh = h, stride = 320;
    const size_t y_size = (size_t)stride * hh;
    const size_t buf_size = y_size + (size_t)stride * (hh / 2);
    std::vector<unsigned char> buf(buf_size);
    std::memset(buf.data(), 200, y_size);
    for (size_t i = 0; i < (size_t)stride * (hh / 2); i += 2) {
        buf[y_size + i] = 128;
        buf[y_size + i + 1] = 128;
    }
    GstBuffer* gbuf = gst_buffer_new_wrapped(g_memdup(buf.data(), buf_size), buf_size);
    GstCaps* caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "NV12",
                                        "width", G_TYPE_INT, w, "height", G_TYPE_INT, hh,
                                        "framerate", GST_TYPE_FRACTION, 25, 1,
                                        "colorimetry", G_TYPE_STRING, "bt709", nullptr);
    GstSample* sample = gst_sample_new(gbuf, caps, nullptr, nullptr);
    gst_caps_unref(caps);
    gst_buffer_unref(gbuf);
    std::string err;
    core::LayoutStatus st = store.publish(sample, meta, "bt709_full", "simulation", &err);
    gst_sample_unref(sample);
    if (st != core::LayoutStatus::Ok) {
        std::printf("vext INFO publish rejected: status=%s err=%s\n",
                    core::to_string(st), err.c_str());
    }
    return store.latest();
}

static core::SemanticConfig base_semantic() {
    core::SemanticConfig sc;
    sc.profile = core::AiProfile::Front;
    sc.confidence_min = 0.5;
    return sc;
}

// Worker 一帧直驱：返回 CAM-01 lane 快照。
static media::VisionLaneSnapshot drive_worker(
    std::map<std::string, std::string> zoom_cfg,
    const std::vector<CameraCalibration>& cals, int img_h = 180) {
    media::VisionWorkerConfig wc;
    wc.sched = {1, 2, 4, 85.0};
    wc.cameras = {{"CAM-01", true, "front"}};
    wc.semantic = base_semantic();
    for (const auto& c : cals) wc.calibrations.add(c);
    wc.camera_zoom = zoom_cfg;
    media::StubVisionDetector::Config stubc;
    stubc.sleep_us = 0;
    auto det = std::make_shared<media::StubVisionDetector>(stubc);
    media::VisionAiWorker worker(wc, det, nullptr, nullptr);

    media::FrameStore store("CAM-01");
    core::FrameSequencer seq("sess-vext", "CAM-01");
    seq.begin_generation();
    media::FrameLease lease = publish_gray_nv12(store, seq.next(1000, -1, -1), img_h);
    worker.submit(lease);
    worker.step();
    media::VisionWorkerSnapshot s = worker.snapshot();
    for (const auto& l : s.lanes)
        if (l.camera_id == "CAM-01") return l;
    return media::VisionLaneSnapshot();
}

int main() {
    gst_init(nullptr, nullptr);

    // 夹具自检（非判据）：真租约构造成功。
    {
        media::FrameStore st("CAM-X");
        core::FrameSequencer fseq("sess-fix", "CAM-X");
        fseq.begin_generation();
        media::FrameLease l0 = publish_gray_nv12(st, fseq.next(1000, -1, -1), 180);
        if (!l0.valid() || l0.plane_data(0) == nullptr) {
            std::printf("vext FATAL fixture_publish_failed\n");
            return 3;
        }
        std::printf("vext INFO fixture publish ok seq=%llu h=%d\n",
                    (unsigned long long)l0.descriptor().seq,
                    (int)l0.descriptor().layout.height);
    }

    const CameraCalibration ac = anchor();
    check(near(ac.reference_constant(), 44500.0), "A00_anchor_constant_independent",
          "44500 px m");
    check(near(ac.px_per_m_at_reference(), 89.0 / 1.70, 1e-9),
          "A01_anchor_reference_px_per_m_independent", "52.352941");

    // ===== A. worker 集成 zoom 门控路径（真租约 + 真线程链路）=====
    // stub 检测器框高恒 0.20（归一化）。锚点 [5,600] 下 36px -> 1236m 落在范围外，
    // 正向用例改用 372px 图像：0.20*372 = 74.4px -> 44500/74.4 ≈ 598.118 m。
    {
        // A1 有标定 + 档位匹配 -> 按公式 500*89/74.4 = 597.984（含范围校验通过）
        auto l = drive_worker({{"CAM-01", "Z1"}}, {ac}, 372);
        check(!l.boxes.empty() &&
                  std::fabs(l.boxes[0].distance_m - 500.0 * 89.0 / 74.4) < 0.2 &&
                  l.distance_valid == 1 && l.distance_unknown == 0,
              "A1_worker_valid_formula",
              "dist=" + std::to_string(l.boxes.empty() ? -1.0f : l.boxes[0].distance_m)
                            .substr(0, 20));
        check(l.distance_text == "598.1 m" && l.last_distance_m > 0,
              "A2_worker_text_formula", l.distance_text);
    }
    {
        // A2 标定在场但无档位 -> ZOOM_UNKNOWN（worker 层自测未覆盖项）
        auto l = drive_worker({}, {ac});
        check(!l.boxes.empty() && l.boxes[0].distance_m == -1.0f &&
                  l.distance_unknown == 1 && l.distance_valid == 0 &&
                  l.distance_unknown_by_reason.count("ZOOM_UNKNOWN") == 1,
              "A3_worker_zoom_unknown", l.distance_unknown_by_reason.empty()
                                            ? "no reasons"
                                            : l.distance_unknown_by_reason.begin()->first);
        check(l.distance_text == "—" && l.last_distance_m == -1.0f,
              "A4_worker_zoom_unknown_text", l.distance_text);
    }
    {
        // A3 档位与标定不符 -> ZOOM_MISMATCH（worker 层自测未覆盖项）
        auto l = drive_worker({{"CAM-01", "Z9"}}, {ac});
        check(!l.boxes.empty() && l.boxes[0].distance_m == -1.0f &&
                  l.distance_unknown == 1 &&
                  l.distance_unknown_by_reason.count("ZOOM_MISMATCH") == 1,
              "A5_worker_zoom_mismatch",
              l.distance_unknown_by_reason.count("ZOOM_MISMATCH") ? "ZOOM_MISMATCH" : "?");
    }
    {
        // A4 多档位标定：Z2 也登记（250m 锚点 -> 同 74.4px 换算: 250*89/74.4 ≈ 298.992）
        CameraCalibration z2 = ac;
        z2.zoom_level = "Z2";
        z2.reference_distance_m = 250.0;
        auto l = drive_worker({{"CAM-01", "Z2"}}, {ac, z2}, 372);
        check(!l.boxes.empty() &&
                  std::fabs(l.boxes[0].distance_m - 250.0 * 89.0 / 74.4) < 0.2,
              "A6_worker_multi_zoom_level_exact",
              "dist=" + std::to_string(l.boxes.empty() ? -1.0f : l.boxes[0].distance_m));
    }
    {
        // A5 超出标定范围（36px 在 500m 锚点下 1236.1 > 600 -> OUT_OF_RANGE,
        //    无法由 worker 路径触发不同 reason —— 用 min=0/max=0 不限范围对照全开放）
        CameraCalibration open_cal = ac;
        open_cal.min_distance_m = 0.0;
        open_cal.max_distance_m = 0.0;
        auto l = drive_worker({{"CAM-01", "Z1"}}, {open_cal});
        check(!l.boxes.empty() && l.boxes[0].distance_m > 0.0f,
              "A7_worker_unlimited_range_passes",
              "dist=" + std::to_string(l.boxes.empty() ? -1.0f : l.boxes[0].distance_m));
    }

    // ===== B. core 门控顺序互斥（同请求单 reason；固定顺序）=====
    {
        DistanceCalibrationSet s;
        s.add(ac);
        // 非人员优先于档位门控: 非 person + 档位未知 -> NOT_PERSON（不是 ZOOM_UNKNOWN）
        auto e1 = estimate_distance(s, person_req(89.0, false, "", "obstacle"));
        check(!e1.valid && e1.reason == "NOT_PERSON", "B1_not_person_precedes_zoom",
              e1.reason);
        // 无标定优先于档位未知: 空集合 + 档位未知 -> NO_CALIBRATION
        DistanceCalibrationSet empty;
        auto e2 = estimate_distance(empty, person_req(89.0, false, ""));
        check(!e2.valid && e2.reason == "NO_CALIBRATION", "B2_no_calib_precedes_zoom",
              e2.reason);
        // 在跑文档 anchor 下档位未知优先于非法像素: zoom unknown + px=-3 -> ZOOM_UNKNOWN
        auto e3 = estimate_distance(s, person_req(-3.0, false, ""));
        check(!e3.valid && e3.reason == "ZOOM_UNKNOWN", "B3_zoom_unknown_precedes_invalid",
              e3.reason);
        // 档位未知优先于像素非正: zoom_known=False + px=0 -> ZOOM_UNKNOWN（同上）
        auto e4 = estimate_distance(s, person_req(0.0, false, ""));
        check(!e4.valid && e4.reason == "ZOOM_UNKNOWN", "B4_zoom_unknown_precedes_zero_px",
              e4.reason);
        // 档位不匹配优先于像素非法: mismatch + px=-5 -> ZOOM_MISMATCH
        auto e5 = estimate_distance(s, person_req(-5.0, true, "Z2"));
        check(!e5.valid && e5.reason == "ZOOM_MISMATCH", "B5_mismatch_precedes_invalid",
              e5.reason);
        // 非法像素优先于范围: px=-8 -> INVALID_BOX（不越界提示）
        auto e6 = estimate_distance(s, person_req(-8.0, true, "Z1"));
        check(!e6.valid && e6.reason == "INVALID_BOX", "B6_invalid_box_precedes_range",
              e6.reason);
    }

    // ===== C. 有效范围端点语义 =====
    {
        DistanceCalibrationSet s;
        s.add(ac);
        // 恰在 min=5: 8900px -> 5 m 恰在边界（含端点应通过）
        auto lo = estimate_distance(s, person_req(8900.0, true, "Z1"));
        check(lo.valid && near(lo.meters, 5.0), "C1_exact_lower_bound_valid",
              std::to_string(lo.meters));
        // min=5 之下: 9000px -> 4.944 m < 5 -> OUT_OF_RANGE
        auto bel = estimate_distance(s, person_req(9000.0, true, "Z1"));
        check(!bel.valid && bel.reason == "OUT_OF_RANGE", "C2_below_lower_bound_refused",
              std::to_string(9000.0 * 89.0 / 9000.0));
        // 恰在上限（含端点应通过）: max=800 与 px=55.625 均为二进制精确值，
        // 44500/55.625 数学上恰为 800，浮点除法无舍入偏差（600/74.167 不精确，不可用）。
        CameraCalibration hi_cal = ac;
        hi_cal.max_distance_m = 800.0;
        DistanceCalibrationSet hs;
        hs.add(hi_cal);
        auto hi = estimate_distance(hs, person_req(55.625, true, "Z1"));
        check(hi.valid && near(hi.meters, 800.0), "C3_exact_upper_bound_valid",
              std::to_string(hi.meters));
        // max 之上: 40px -> 1112.5 > 800
        auto ab = estimate_distance(hs, person_req(40.0, true, "Z1"));
        check(!ab.valid && ab.meters == -1.0 && ab.reason == "OUT_OF_RANGE",
              "C4_above_upper_bound_unknown_not_clamped", std::to_string(ab.meters));
    }
    {
        // 0=不限语义（min/max 任一为 0 该侧无界）
        CameraCalibration c = ac;
        c.min_distance_m = 0.0;   // 下限不限
        c.max_distance_m = 600.0;
        DistanceCalibrationSet s;
        s.add(c);
        auto e = estimate_distance(s, person_req(9000.0, true, "Z1"));  // 4.944 m
        check(e.valid, "C5_zero_min_unlimited", std::to_string(e.meters));
    }
    {
        // 单边 max 不限（max=0）；下限 5 且 px 大 -> 极远目标（8000 为 44500/5.5625 精确值）
        CameraCalibration c = ac;
        c.min_distance_m = 5.0;
        c.max_distance_m = 0.0;
        DistanceCalibrationSet s;
        s.add(c);
        auto e = estimate_distance(s, person_req(44500.0 / 8000.0, true, "Z1"));
        check(e.valid && near(e.meters, 8000.0), "C6_zero_max_unlimited",
              std::to_string(e.meters));
    }

    // ===== D. 多相机/多档位集合消费面 =====
    {
        CameraCalibration cam2 = ac;
        cam2.camera_id = "CAM-02";
        cam2.zoom_level = "Z1";
        cam2.reference_distance_m = 250.0;
        DistanceCalibrationSet s;
        s.add(ac);          // CAM-01 Z1（500）
        s.add(cam2);        // CAM-02 Z1（250）
        check(s.has_camera("CAM-01") && s.has_camera("CAM-02") && !s.has_camera("CAM-09"),
              "D1_multi_camera_membership", "2 cameras");
        check(s.zooms_of("CAM-01").size() == 1 && s.zooms_of("CAM-02").size() == 1,
              "D2_zooms_of_camera", "");
        // CAM-01 Z1 -> 500; CAM-02 Z1 -> 250（同框高/同档位，不同相机不同常数）
        auto e1 = estimate_distance(s, person_req(89.0, true, "Z1"));
        DistanceRequest r2 = person_req(89.0, true, "Z1");
        r2.camera_id = "CAM-02";
        auto e2 = estimate_distance(s, r2);
        check(near(e1.meters, 500.0) && near(e2.meters, 250.0),
              "D3_per_camera_constants_isolated",
              "e1=" + std::to_string(e1.meters) + " e2=" + std::to_string(e2.meters));
        // 同一相机同档位重复登记: find 返回首个（先登记优先）。
        DistanceCalibrationSet sd;
        sd.add(ac);
        CameraCalibration dup = ac;
        dup.reference_distance_m = 888.0;
        sd.add(dup);
        const CameraCalibration* f = sd.find("CAM-01", "Z1");
        check(f && near(f->reference_distance_m, 500.0), "D4_duplicate_first_wins",
              "ref_m=" + std::to_string(f ? f->reference_distance_m : -1));
    }

    // ===== E. distance_json 数值格式 =====
    {
        DistanceCalibrationSet s;
        s.add(ac);
        auto e = estimate_distance(s, person_req(89.0, true, "Z1"));
        std::string valid = core::distance_json(e);
        check(valid.find("\"distance_m\":500,") != std::string::npos,
              "E1_json_integer_distance_no_decimal",
              "valid: " + valid.substr(0, 120));
        auto u = estimate_distance(DistanceCalibrationSet{}, person_req(89.0, true, "Z1"));
        std::string unk = core::distance_json(u);
        check(unk.find("\"distance_m\":-1,") != std::string::npos,
              "E2_json_unknown_minus_one_token",
              "unk: " + unk.substr(0, 120));
        // 双重换算: distance_from_box_height_px 与 estimate_distance 一致
        double raw = core::distance_from_box_height_px(ac, 44.5);
        check(near(raw, 1000.0), "E3_raw_formula_pointer_agreement",
              "44.5px -> " + std::to_string(raw));
        // 原始公式直接法在 estimate 之外独立可用（无范围判断）:
        // 0.5px -> 89000（超过 max，仅证明 raw 不做范围门控）
        double far_ = core::distance_from_box_height_px(ac, 0.5);
        check(near(far_, 89000.0), "E4_raw_formula_beyond_range_still_formula",
              "0.5px -> " + std::to_string(far_));
        auto est_far = estimate_distance(s, person_req(0.5, true, "Z1"));
        check(!est_far.valid && est_far.reason == "OUT_OF_RANGE",
              "E5_estimate_guards_ranges_but_raw_does_not", est_far.reason);
    }

    // ===== F. parse 边界 =====
    {
        CameraCalibration c;
        std::string err;
        // obj_m 缺省 -> px/m 记 -1
        check(parse_calibration_spec("camera=CAM-01,zoom=Z1,ref_m=500,ref_h_px=89",
                                     &c, &err),
              "F1_obj_m_optional", err.empty() ? "no err" : err);
        check(c.valid() && near(c.px_per_m_at_reference(), -1.0),
              "F1b_obj_m_missing_px_per_m_is_minus_one",
              "px/m=" + std::to_string(c.px_per_m_at_reference()));
        // max=0 -> 上限不限
        check(parse_calibration_spec(
                  "camera=CAM-01,zoom=Z1,ref_m=500,ref_h_px=89,min=0,max=0", &c, &err),
              "F2_zero_min_max_unlimited", err.empty() ? "no err" : err);
        check(c.valid(), "F2b_zero_range_calibration_valid", "");
        // 空 spec / 缺等号 / 负 ref 拒绝
        check(!parse_calibration_spec("", &c, &err), "F3_empty_spec_refused", err);
        check(!parse_calibration_spec("camera=CAM-01 zoom=Z1", &c, &err),
              "F4_item_without_equals_refused", err);
        check(!parse_calibration_spec("camera=CAM-01,zoom=Z1,ref_m=-5,ref_h_px=89",
                                      &c, &err),
              "F5_negative_ref_m_refused", err);
        check(!parse_calibration_spec("camera=CAM-01,zoom=Z1,ref_m=500,ref_h_px=nan",
                                      &c, &err),
              "F6_nan_refused", err);
        check(!parse_calibration_spec("camera=CAM-01,zoom=Z1,ref_m=inf,ref_h_px=89",
                                      &c, &err),
              "F6b_inf_refused", err);
    }

    std::printf("vext OK pass=%d fail=%d\n", passn, fails);
    return fails == 0 ? 0 : 1;
}
