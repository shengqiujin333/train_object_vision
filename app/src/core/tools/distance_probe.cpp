// railhmi-vision-distance（视觉推理 ITEM-011）
// 距离测量探针：仅当提供“相机标定 + 变焦档位”时由人员框像素高换算 distance_m；
// 未标定/档位未知或不匹配/非人员框/超范围 -> -1（未知），绝不臆造米值或 clamp。
//
// 用法:
//   railhmi-vision-distance --selftest [--json out.json]
//   railhmi-vision-distance --fixture no_calib|zoom_unknown|zoom_mismatch|non_person|out_of_range|valid
//   railhmi-vision-distance --calib 'camera=CAM-01,zoom=Z1,ref_m=500,ref_h_px=89,obj_m=1.70,min=5,max=600' \
//       --zoom Z1 --box-h 89 [--camera CAM-01] [--label person] [--expect valid|unknown]
//
// 退出码: 0 通过/一致；1 判据失败；2 参数错误。

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/vision_distance.h"

using namespace railhmi::core;

namespace {

int g_fail = 0;
std::ofstream g_fo;

void emit(const std::string& line) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
    if (g_fo) {
        g_fo << line << "\n";
        g_fo.flush();
    }
}

void check(bool ok, const std::string& name, const std::string& detail = "") {
    emit(std::string("[") + (ok ? "PASS" : "FAIL") + "] " + name +
         (detail.empty() ? "" : (" :: " + detail)));
    if (!ok) ++g_fail;
}

const char* kAnchor =
    "camera=CAM-01,zoom=Z1,ref_m=500,ref_h_px=89,obj_m=1.70,min=5,max=600,"
    "source=README:2026-10-13:500m/89px";

DistanceCalibrationSet anchor_set() {
    DistanceCalibrationSet s;
    CameraCalibration c;
    std::string err;
    if (parse_calibration_spec(kAnchor, &c, &err)) s.add(c);
    return s;
}

DistanceRequest req(double px, bool zoom_known, const std::string& zoom,
                    const std::string& label = "person") {
    DistanceRequest r;
    r.camera_id = "CAM-01";
    r.label = label;
    r.box_height_px = px;
    r.zoom_known = zoom_known;
    r.zoom_level = zoom;
    return r;
}

bool near(double a, double b, double tol = 1e-6) { return std::fabs(a - b) <= tol; }

int run_selftest() {
    emit("type=selftest note=纯逻辑夹具（不加载 RKNN/不连相机）；锚点 CAM-01 500m/89px");

    const DistanceCalibrationSet set = anchor_set();
    check(set.size() == 1, "calib_spec_parsed", kAnchor);

    // 公式与锚点
    const DistanceEstimate at_ref = estimate_distance(set, req(89.0, true, "Z1"));
    check(at_ref.valid && near(at_ref.meters, 500.0), "anchor_89px_is_500m",
          std::to_string(at_ref.meters));
    const DistanceEstimate half = estimate_distance(set, req(178.0, true, "Z1"));
    check(half.valid && near(half.meters, 250.0), "half_px_double_distance",
          std::to_string(half.meters));
    check(near(at_ref.px_per_m_reference, 89.0 / 1.70, 1e-9), "px_per_m_reference",
          std::to_string(at_ref.px_per_m_reference));
    check(distance_text(at_ref) == "500.0 m", "text_valid", distance_text(at_ref));

    // 未标定
    DistanceCalibrationSet empty;
    const DistanceEstimate none = estimate_distance(empty, req(89.0, true, "Z1"));
    check(!none.valid && none.meters == -1.0 && none.reason == distance_reason::kNoCalibration(),
          "no_calibration_unknown", none.reason);
    check(distance_text(none) == "—", "unknown_text_placeholder", distance_text(none));

    // 档位门控
    const DistanceEstimate z_unk = estimate_distance(set, req(89.0, false, ""));
    check(!z_unk.valid && z_unk.reason == distance_reason::kZoomUnknown(), "zoom_unknown_refused",
          z_unk.reason);
    const DistanceEstimate z_mis = estimate_distance(set, req(89.0, true, "Z2"));
    check(!z_mis.valid && z_mis.reason == distance_reason::kZoomMismatch(), "zoom_mismatch_refused",
          z_mis.reason);

    // 非人员/非法像素
    const DistanceEstimate np = estimate_distance(set, req(89.0, true, "Z1", "obstacle"));
    check(!np.valid && np.reason == distance_reason::kNotPerson(), "non_person_refused", np.reason);
    const DistanceEstimate zb = estimate_distance(set, req(0.0, true, "Z1"));
    check(!zb.valid && zb.reason == distance_reason::kInvalidBox(), "zero_box_refused", zb.reason);

    // 范围：不 clamp
    const DistanceEstimate far_ = estimate_distance(set, req(40.0, true, "Z1"));
    check(!far_.valid && far_.reason == distance_reason::kOutOfRange() && far_.meters == -1.0,
          "out_of_range_unknown_not_clamped", far_.detail);

    // JSON 可核对
    const std::string j = distance_json(at_ref);
    check(j.find("\"distance_m\":500") != std::string::npos &&
              j.find("\"reason\":\"DISTANCE_OK\"") != std::string::npos,
          "json_distance_ok", j.substr(0, 120));
    const std::string ju = distance_json(none);
    check(ju.find("\"distance_m\":-1") != std::string::npos &&
              ju.find("\"text\":\"—\"") != std::string::npos,
          "json_unknown_minus_one", ju.substr(0, 120));

    const bool ok = g_fail == 0;
    emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") + " failures=" + std::to_string(g_fail));
    return ok ? 0 : 1;
}

int run_fixture(const std::string& name) {
    const DistanceCalibrationSet set = anchor_set();
    DistanceRequest r = req(89.0, true, "Z1");
    std::string expect_reason;
    bool expect_valid = false;
    if (name == "no_calib") {
        expect_reason = distance_reason::kNoCalibration();
    } else if (name == "zoom_unknown") {
        r.zoom_known = false;
        r.zoom_level.clear();
        expect_reason = distance_reason::kZoomUnknown();
    } else if (name == "zoom_mismatch") {
        r.zoom_level = "Z2";
        expect_reason = distance_reason::kZoomMismatch();
    } else if (name == "non_person") {
        r.label = "obstacle";
        expect_reason = distance_reason::kNotPerson();
    } else if (name == "out_of_range") {
        r.box_height_px = 40.0;
        expect_reason = distance_reason::kOutOfRange();
    } else if (name == "valid") {
        expect_reason = distance_reason::kOk();
        expect_valid = true;
    } else {
        std::fprintf(stderr, "unknown fixture: %s\n", name.c_str());
        return 2;
    }
    const DistanceEstimate e =
        name == "no_calib" ? estimate_distance(DistanceCalibrationSet{}, r)
                           : estimate_distance(set, r);
    emit("type=fixture name=" + name + " json=" + distance_json(e));
    const bool ok = e.valid == expect_valid && e.reason == expect_reason;
    emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") + " failures=" + std::to_string(ok ? 0 : 1));
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string out;
    std::string json;
    std::string fixture;
    std::string camera = "CAM-01";
    std::string label = "person";
    std::string zoom;
    std::string expect = "unknown";
    bool zoom_known = false;
    double box_h = -1.0;
    std::vector<std::string> specs;
    bool selftest = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) {
            if (i + 1 < argc) *d = argv[++i];
        };
        if (a == "--calib") { std::string v; next(&v); specs.push_back(v); }
        else if (a == "--camera") next(&camera);
        else if (a == "--label") next(&label);
        else if (a == "--zoom") { next(&zoom); zoom_known = !zoom.empty(); }
        else if (a == "--no-zoom") zoom_known = false;
        else if (a == "--box-h") { std::string v; next(&v); box_h = std::atof(v.c_str()); }
        else if (a == "--expect") next(&expect);
        else if (a == "--fixture") next(&fixture);
        else if (a == "--selftest") selftest = true;
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--help" || a == "-h") {
            std::printf(
                "usage: railhmi-vision-distance --selftest [--out LOG] [--json JSON]\n"
                "       railhmi-vision-distance --fixture no_calib|zoom_unknown|zoom_mismatch|"
                "non_person|out_of_range|valid\n"
                "       railhmi-vision-distance --calib 'camera=..,zoom=..,ref_m=..,ref_h_px=..' "
                "--zoom Z1 --box-h 89 [--camera CAM-01] [--label person] "
                "[--expect valid|unknown]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    g_fo.open(out, std::ios::trunc);
    int rc = 0;
    if (!fixture.empty()) {
        rc = run_fixture(fixture);
    } else if (!specs.empty()) {
        // 逐一解析标定并求值（可多次 --calib 覆盖多档位）。
        DistanceCalibrationSet set;
        for (const auto& spec : specs) {
            CameraCalibration c;
            std::string err;
            if (!parse_calibration_spec(spec, &c, &err)) {
                emit("type=error bad --calib: " + err);
                return 2;
            }
            set.add(c);
            emit("type=calibration json={\"camera\":\"" + c.camera_id + "\",\"zoom\":\"" +
                 c.zoom_level + "\",\"ref_m\":" + std::to_string(c.reference_distance_m) +
                 ",\"ref_h_px\":" + std::to_string(c.reference_height_px) +
                 ",\"px_per_m_reference\":" + std::to_string(c.px_per_m_at_reference()) + "}");
        }
        if (box_h < 0.0) {
            emit("type=error --box-h required");
            return 2;
        }
        DistanceRequest r;
        r.camera_id = camera;
        r.label = label;
        r.box_height_px = box_h;
        r.zoom_known = zoom_known;
        r.zoom_level = zoom;
        const DistanceEstimate e = estimate_distance(set, r);
        emit("type=request camera=" + camera + " label=" + label + " box_h_px=" +
             std::to_string(box_h) + " zoom=" + (zoom_known ? zoom : std::string("(unknown)")));
        emit("type=estimate json=" + distance_json(e));
        emit("type=debug text=" + distance_text(e));
        bool ok = true;
        if (expect == "valid") ok = e.valid;
        else if (expect == "unknown") ok = !e.valid && e.meters == -1.0;
        emit(std::string("type=expect ") + (ok ? "ok" : "mismatch") + " expect=" + expect);
        emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") +
             " failures=" + std::to_string(ok ? 0 : 1));
        rc = ok ? 0 : 1;
    } else {
        rc = run_selftest();
    }

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        if (j) j << "{\"tool\":\"railhmi-vision-distance\",\"failures\":" << g_fail
                 << ",\"result\":\"" << (g_fail == 0 ? "PASS" : "FAIL") << "\"}\n";
    }
    return rc;
}
