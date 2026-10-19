// railhmi-core: 距离测量实现（视觉推理 ITEM-011）。
//
// 公式（与历史锚点 500 m / 89 px 一致）：distance = reference_distance_m * reference_height_px / px
// 任何门控不满足 -> metres = -1 + 稳定 reason；不做 clamp、不用框尺寸臆造米值。

#include "railhmi/core/vision_distance.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace railhmi {
namespace core {

namespace {

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) o += ' ';
                else o += c;
        }
    }
    return o;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::size_t p = 0;
    while (p <= s.size()) {
        const std::size_t c = s.find(sep, p);
        out.push_back(s.substr(p, c == std::string::npos ? std::string::npos : c - p));
        if (c == std::string::npos) break;
        p = c + 1;
    }
    return out;
}

bool parse_double(const std::string& s, double* out) {
    if (s.empty()) return false;
    char* end = nullptr;
    const double v = std::strtod(s.c_str(), &end);
    if (!end || *end != '\0') return false;
    if (!std::isfinite(v)) return false;
    *out = v;
    return true;
}

}  // namespace

bool CameraCalibration::valid() const {
    if (camera_id.empty()) return false;
    if (zoom_level.empty()) return false;          // 必须显式档位
    if (!(reference_distance_m > 0.0)) return false;
    if (!(reference_height_px > 0.0)) return false;
    if (reference_object_height_m < 0.0) return false;
    if (!std::isfinite(reference_distance_m) || !std::isfinite(reference_height_px)) return false;
    if (max_distance_m > 0.0 && min_distance_m > 0.0 && max_distance_m < min_distance_m) return false;
    return true;
}

double CameraCalibration::px_per_m_at_reference() const {
    if (!(reference_object_height_m > 0.0) || !(reference_height_px > 0.0)) return -1.0;
    return reference_height_px / reference_object_height_m;
}

double CameraCalibration::reference_constant() const {
    return reference_distance_m * reference_height_px;
}

double distance_from_box_height_px(const CameraCalibration& c, double box_height_px) {
    if (!c.valid()) return -1.0;
    if (!(box_height_px > 0.0) || !std::isfinite(box_height_px)) return -1.0;
    const double d = c.reference_constant() / box_height_px;
    if (!std::isfinite(d) || d <= 0.0) return -1.0;
    return d;
}

void DistanceCalibrationSet::add(const CameraCalibration& c) { items_.push_back(c); }

const CameraCalibration* DistanceCalibrationSet::find(const std::string& camera_id,
                                                      const std::string& zoom_level) const {
    for (const auto& c : items_)
        if (c.camera_id == camera_id && c.zoom_level == zoom_level) return &c;
    return nullptr;
}

bool DistanceCalibrationSet::has_camera(const std::string& camera_id) const {
    for (const auto& c : items_)
        if (c.camera_id == camera_id) return true;
    return false;
}

std::vector<std::string> DistanceCalibrationSet::zooms_of(const std::string& camera_id) const {
    std::vector<std::string> out;
    for (const auto& c : items_)
        if (c.camera_id == camera_id) out.push_back(c.zoom_level);
    return out;
}

DistanceEstimate estimate_distance(const DistanceCalibrationSet& set, const DistanceRequest& req) {
    DistanceEstimate e;
    e.box_height_px = req.box_height_px;
    e.used_zoom = req.zoom_known ? req.zoom_level : std::string();

    // 1) 只对人员框换算（其它标签没有身高先验，不得借框尺寸臆造）。
    if (req.label != "person") {
        e.reason = distance_reason::kNotPerson();
        e.detail = "only person boxes have a height prior";
        return e;
    }

    // 2) 相机标定必须存在。
    if (!set.has_camera(req.camera_id)) {
        e.reason = distance_reason::kNoCalibration();
        e.detail = "camera '" + req.camera_id + "' has no calibration";
        return e;
    }

    // 3) 当前变焦档位必须已知（档位未知时同一 px 高对应不同距离，禁止换算）。
    if (!req.zoom_known || req.zoom_level.empty()) {
        e.reason = distance_reason::kZoomUnknown();
        const auto zooms = set.zooms_of(req.camera_id);
        e.detail = "current zoom level is unknown; calibration exists for " +
                   std::to_string(zooms.size()) + " zoom level(s)";
        return e;
    }

    // 4) 档位必须与标定一致。
    const CameraCalibration* c = set.find(req.camera_id, req.zoom_level);
    if (!c) {
        e.reason = distance_reason::kZoomMismatch();
        e.detail = "no calibration for zoom '" + req.zoom_level + "'";
        return e;
    }
    if (!c->valid()) {
        e.reason = distance_reason::kBadCalibration();
        e.detail = "calibration parameters are invalid";
        return e;
    }
    e.px_per_m_reference = c->px_per_m_at_reference();

    // 5) 像素高必须可用。
    if (!(req.box_height_px > 0.0) || !std::isfinite(req.box_height_px)) {
        e.reason = distance_reason::kInvalidBox();
        e.detail = "box height in pixels must be positive";
        return e;
    }

    // 6) 公式 + 标定有效范围（超范围返回未知，不 clamp）。
    const double d = distance_from_box_height_px(*c, req.box_height_px);
    if (!(d > 0.0)) {
        e.reason = distance_reason::kInvalidBox();
        e.detail = "formula produced a non-positive distance";
        return e;
    }
    if ((c->min_distance_m > 0.0 && d < c->min_distance_m) ||
        (c->max_distance_m > 0.0 && d > c->max_distance_m)) {
        e.reason = distance_reason::kOutOfRange();
        char buf[128];
        std::snprintf(buf, sizeof(buf), "distance %.1f m outside calibrated range [%.1f, %.1f]", d,
                      c->min_distance_m, c->max_distance_m);
        e.detail = buf;
        return e;
    }

    e.valid = true;
    e.meters = d;
    e.reason = distance_reason::kOk();
    e.detail = "distance = ref_m*ref_px/px";
    return e;
}

bool parse_calibration_spec(const std::string& spec, CameraCalibration* out, std::string* err) {
    auto fail = [&](const std::string& why) {
        if (err) *err = why;
        return false;
    };
    if (!out) return fail("null output");
    CameraCalibration c;
    bool has_ref_m = false;
    bool has_ref_px = false;
    for (const auto& item : split(spec, ',')) {
        if (item.empty()) continue;
        const std::size_t eq = item.find('=');
        if (eq == std::string::npos) return fail("item without '=': " + item);
        const std::string k = item.substr(0, eq);
        const std::string v = item.substr(eq + 1);
        if (k == "camera") {
            c.camera_id = v;
        } else if (k == "zoom") {
            c.zoom_level = v;
        } else if (k == "ref_m") {
            if (!parse_double(v, &c.reference_distance_m)) return fail("bad ref_m: " + v);
            has_ref_m = true;
        } else if (k == "ref_h_px") {
            if (!parse_double(v, &c.reference_height_px)) return fail("bad ref_h_px: " + v);
            has_ref_px = true;
        } else if (k == "obj_m") {
            if (!parse_double(v, &c.reference_object_height_m)) return fail("bad obj_m: " + v);
        } else if (k == "min") {
            if (!parse_double(v, &c.min_distance_m)) return fail("bad min: " + v);
        } else if (k == "max") {
            if (!parse_double(v, &c.max_distance_m)) return fail("bad max: " + v);
        } else if (k == "source") {
            c.source = v;
        } else {
            return fail("unknown key: " + k);
        }
    }
    if (c.camera_id.empty()) return fail("camera is required");
    if (c.zoom_level.empty()) return fail("zoom is required (calibration must be zoom-specific)");
    if (!has_ref_m || !has_ref_px) return fail("ref_m and ref_h_px are required");
    if (!c.valid()) return fail("calibration parameters invalid (non-positive or contradictory)");
    *out = c;
    return true;
}

std::string distance_text(const DistanceEstimate& e) {
    if (!e.valid) return "—";   // 未知占位：不是 "0"、"0 m" 或任何臆造值
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.1f m", e.meters);
    return buf;
}

std::string distance_json(const DistanceEstimate& e) {
    std::ostringstream o;
    o << "{";
    o << "\"valid\":" << (e.valid ? "true" : "false");
    o << ",\"distance_m\":" << (e.valid ? e.meters : -1.0);
    o << ",\"text\":\"" << json_escape(distance_text(e)) << "\"";
    o << ",\"reason\":\"" << json_escape(e.reason) << "\"";
    o << ",\"detail\":\"" << json_escape(e.detail) << "\"";
    o << ",\"zoom\":\"" << json_escape(e.used_zoom) << "\"";
    o << ",\"box_height_px\":" << e.box_height_px;
    o << ",\"px_per_m_reference\":" << e.px_per_m_reference;
    o << "}";
    return o.str();
}

}  // namespace core
}  // namespace railhmi
