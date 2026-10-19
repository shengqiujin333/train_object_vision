// railhmi-core: AI 调试视图数据与掩码叠加实现（视觉推理 ITEM-012）。
//
// 关键不变量：
//   1) 司机角色 visible=false，且不产生任何叠加数据（四开关全部关闭输出）；
//   2) 掩码叠加载荷只在“开关开启 + plane 尺寸/stride 有效”时提供，否则只给原因；
//   3) 未知数值统一占位 "—"，绝不格式化为 "0"/"0.0"。

#include "railhmi/core/vision_debug_view.h"

#include <cstdio>
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

const char* kUnknown = "—";

std::string fmt(const char* f, double v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), f, v);
    return buf;
}

std::string fmt_int(const char* f, long long v) {
    char buf[48];
    std::snprintf(buf, sizeof(buf), f, v);
    return buf;
}

}  // namespace

const char* to_string(DebugOverlay v) {
    switch (v) {
        case DebugOverlay::Boxes: return "boxes";
        case DebugOverlay::Roi: return "roi";
        case DebugOverlay::Text: return "text";
        case DebugOverlay::Mask: return "mask";
    }
    return "?";
}

bool parse_debug_overlay(const std::string& s, DebugOverlay* out) {
    if (s == "boxes") { *out = DebugOverlay::Boxes; return true; }
    if (s == "roi") { *out = DebugOverlay::Roi; return true; }
    if (s == "text") { *out = DebugOverlay::Text; return true; }
    if (s == "mask") { *out = DebugOverlay::Mask; return true; }
    return false;
}

bool OverlaySwitches::get(DebugOverlay o) const {
    switch (o) {
        case DebugOverlay::Boxes: return boxes;
        case DebugOverlay::Roi: return roi;
        case DebugOverlay::Text: return text;
        case DebugOverlay::Mask: return mask;
    }
    return false;
}

void OverlaySwitches::set(DebugOverlay o, bool v) {
    switch (o) {
        case DebugOverlay::Boxes: boxes = v; break;
        case DebugOverlay::Roi: roi = v; break;
        case DebugOverlay::Text: text = v; break;
        case DebugOverlay::Mask: mask = v; break;
    }
}

std::string OverlaySwitches::describe() const {
    std::string o;
    if (boxes) o += "boxes";
    if (roi) o += (o.empty() ? "" : ",") + std::string("roi");
    if (text) o += (o.empty() ? "" : ",") + std::string("text");
    if (mask) o += (o.empty() ? "" : ",") + std::string("mask");
    return o.empty() ? "none" : o;
}

bool parse_overlay_switches(const std::string& spec, OverlaySwitches* out) {
    if (!out) return false;
    *out = OverlaySwitches{};
    // 显式“全关”写法（便于 CLI/配置表达默认态）。
    if (spec.empty() || spec == "none" || spec == "off") return true;
    std::size_t p = 0;
    while (p <= spec.size()) {
        const std::size_t c = spec.find(',', p);
        const std::string item =
            spec.substr(p, c == std::string::npos ? std::string::npos : c - p);
        if (!item.empty()) {
            DebugOverlay ov = DebugOverlay::Boxes;
            if (!parse_debug_overlay(item, &ov)) return false;
            out->set(ov, true);
        }
        if (c == std::string::npos) break;
        p = c + 1;
    }
    return true;
}

bool OverlayMaskInput::usable() const {
    if (!available || !plane) return false;
    if (width <= 0 || height <= 0 || stride_bytes < width) return false;
    if (plane->size() < static_cast<std::size_t>(stride_bytes) *
                             static_cast<std::size_t>(height))
        return false;
    return true;
}

DebugViewModel build_debug_view(const DebugViewInput& in) {
    DebugViewModel vm;
    vm.switches = in.switches;
    vm.visible = ai_debug_visible(in.role);
    if (!vm.visible) {
        // 司机（及任何无 AI 调试权限的角色）：不返回数据、不产生叠加。
        vm.hidden_reason = "driver role cannot view AI debug (P5 hidden; overlays off)";
        vm.overlays_effective = false;
        return vm;
    }
    vm.overlays_effective = in.switches.any();

    // 用既有 build_ai_debug 生成基础行（阈值/陈旧/可用性语义保持一致）。
    std::vector<DetectionResult> latest;
    for (const auto& ch : in.channels) {
        DetectionResult r;
        r.camera_id = ch.row.camera_id;
        r.profile = ch.row.profile;
        r.boxes = ch.row.boxes;
        r.max_confidence = ch.row.max_confidence;
        r.model_version = ch.row.model_version;
        r.latency_ms = ch.row.latency_ms;
        latest.push_back(r);
    }
    std::vector<AiDebugChannel> rows =
        build_ai_debug(in.registry, in.runtime, latest, in.now_ms, in.stale_ms);

    for (auto& row : rows) {
        DebugChannelView v;
        const DebugChannelInput* src = nullptr;
        for (const auto& ch : in.channels)
            if (ch.row.camera_id == row.camera_id) src = &ch;
        if (src) {
            // src->row 携带真实 ram 值（unavailable_reason/last_result_text/boxes），优先采用。
            if (!src->row.model_version.empty()) row.model_version = src->row.model_version;
            if (src->row.latency_ms > 0) row.latency_ms = src->row.latency_ms;
            if (!src->row.unavailable_reason.empty())
                row.unavailable_reason = src->row.unavailable_reason;
            row.available = src->row.available;
            row.boxes = src->row.boxes;
            row.max_confidence = src->row.max_confidence;
            row.last_result_age_ms = src->row.last_result_age_ms;
            row.last_result_text = src->row.last_result_text;
            row.distance_text = src->row.distance_text;
        }
        v.row = row;

        // 未知占位（绝不 0）
        v.model_version_text = row.model_version.empty() ? kUnknown : row.model_version;
        v.fps_text = row.fps > 0.0 ? fmt("%.1f fps", row.fps) : kUnknown;
        v.latency_text = row.latency_ms > 0 ? fmt_int("%lld ms", row.latency_ms) : kUnknown;
        const bool has_boxes = src ? src->boxes_available : false;
        const int box_count = src ? src->box_count : static_cast<int>(row.boxes.size());
        v.box_count_text = has_boxes ? std::to_string(box_count) : kUnknown;
        v.confidence_text =
            has_boxes && box_count > 0 && row.max_confidence > 0
                ? fmt("%.2f", static_cast<double>(row.max_confidence))
                : kUnknown;
        v.distance_text = row.distance_text.empty() ? kUnknown : row.distance_text;
        v.last_result_text = row.last_result_text.empty() ? kUnknown : row.last_result_text;
        v.unavailable_reason = row.available ? std::string() : row.unavailable_reason;
        if (!row.available && v.unavailable_reason.empty()) v.unavailable_reason = "AI 不可用";

        // 叠加层：仅 visible（已保证）+ 开关开启 + 数据真实可用
        v.overlay_boxes = in.switches.boxes;
        v.overlay_text = in.switches.text;
        v.overlay_roi = in.switches.roi && src && src->has_roi;
        v.has_roi = src && src->has_roi;
        if (src && src->has_roi) v.roi = src->roi;
        if (in.switches.roi && !v.has_roi) v.roi_unavailable_reason = "未配置 ROI";

        if (src) {
            v.mask_available = src->mask.usable();
            if (!v.mask_available)
                v.mask_unavailable_reason =
                    src->mask.reason.empty() ? "掩码不可用" : src->mask.reason;
            if (in.switches.mask && v.mask_available) {
                v.overlay_mask = true;
                v.mask.width = src->mask.width;
                v.mask.height = src->mask.height;
                v.mask.stride_bytes = src->mask.stride_bytes;
                v.mask.plane = src->mask.plane;
                v.mask.kind = src->mask.kind;
                long long positive = 0;
                const auto& p = *src->mask.plane;
                for (std::size_t y = 0; y < static_cast<std::size_t>(src->mask.height); ++y) {
                    const std::uint8_t* line =
                        p.data() + y * static_cast<std::size_t>(src->mask.stride_bytes);
                    for (int x = 0; x < src->mask.width; ++x)
                        if (line[x] != 0) ++positive;
                }
                v.mask.positive_pixels = positive;
            }
        } else if (in.switches.mask) {
            v.mask_unavailable_reason = "无掩码数据";
        }
        vm.channels.push_back(std::move(v));
    }
    return vm;
}

std::string debug_view_json(const DebugViewModel& vm) {
    std::ostringstream o;
    o << "{";
    o << "\"visible\":" << (vm.visible ? "true" : "false");
    o << ",\"hidden_reason\":\"" << json_escape(vm.hidden_reason) << "\"";
    o << ",\"switches\":{";
    o << "\"boxes\":" << (vm.switches.boxes ? "true" : "false");
    o << ",\"roi\":" << (vm.switches.roi ? "true" : "false");
    o << ",\"text\":" << (vm.switches.text ? "true" : "false");
    o << ",\"mask\":" << (vm.switches.mask ? "true" : "false");
    o << "},\"switches_desc\":\"" << json_escape(vm.switches.describe()) << "\"";
    o << ",\"overlays_effective\":" << (vm.overlays_effective ? "true" : "false");
    o << ",\"channels\":[";
    for (std::size_t i = 0; i < vm.channels.size(); ++i) {
        const DebugChannelView& c = vm.channels[i];
        if (i) o << ",";
        o << "{";
        o << "\"camera_id\":\"" << json_escape(c.row.camera_id) << "\"";
        o << ",\"name\":\"" << json_escape(c.row.name) << "\"";
        o << ",\"profile\":\"" << to_string(c.row.profile) << "\"";
        o << ",\"profile_label\":\"" << json_escape(c.row.profile_label) << "\"";
        o << ",\"available\":" << (c.row.available ? "true" : "false");
        o << ",\"unavailable_reason\":\"" << json_escape(c.unavailable_reason) << "\"";
        o << ",\"model_version\":\"" << json_escape(c.model_version_text) << "\"";
        o << ",\"fps_text\":\"" << json_escape(c.fps_text) << "\"";
        o << ",\"latency_text\":\"" << json_escape(c.latency_text) << "\"";
        o << ",\"confidence_text\":\"" << json_escape(c.confidence_text) << "\"";
        o << ",\"distance_text\":\"" << json_escape(c.distance_text) << "\"";
        o << ",\"last_result_text\":\"" << json_escape(c.last_result_text) << "\"";
        o << ",\"box_count_text\":\"" << json_escape(c.box_count_text) << "\"";
        o << ",\"fps\":" << c.row.fps;
        o << ",\"latency_ms\":" << c.row.latency_ms;
        o << ",\"max_confidence\":" << c.row.max_confidence;
        o << ",\"boxes\":[";
        for (std::size_t k = 0; k < c.row.boxes.size(); ++k) {
            const DetectionBox& b = c.row.boxes[k];
            if (k) o << ",";
            o << "{\"x\":" << b.x << ",\"y\":" << b.y << ",\"w\":" << b.w << ",\"h\":" << b.h
              << ",\"confidence\":" << b.confidence << ",\"class_id\":" << b.class_id
              << ",\"label\":\"" << json_escape(b.label) << "\""
              << ",\"in_roi\":" << (b.in_roi ? "true" : "false")
              << ",\"distance_m\":" << b.distance_m << "}";
        }
        o << "]";
        o << ",\"overlays\":{";
        o << "\"boxes\":" << (c.overlay_boxes ? "true" : "false");
        o << ",\"roi\":" << (c.overlay_roi ? "true" : "false");
        o << ",\"text\":" << (c.overlay_text ? "true" : "false");
        o << ",\"mask\":" << (c.overlay_mask ? "true" : "false");
        o << "}";
        o << ",\"has_roi\":" << (c.has_roi ? "true" : "false");
        if (c.has_roi)
            o << ",\"roi_rect\":{\"x\":" << c.roi.x << ",\"y\":" << c.roi.y << ",\"w\":" << c.roi.w
              << ",\"h\":" << c.roi.h << "}";
        if (!c.roi_unavailable_reason.empty())
            o << ",\"roi_unavailable_reason\":\"" << json_escape(c.roi_unavailable_reason) << "\"";
        o << ",\"mask_available\":" << (c.mask_available ? "true" : "false");
        if (!c.mask_unavailable_reason.empty())
            o << ",\"mask_unavailable_reason\":\"" << json_escape(c.mask_unavailable_reason)
              << "\"";
        if (c.overlay_mask) {
            o << ",\"mask\":{\"width\":" << c.mask.width << ",\"height\":" << c.mask.height
              << ",\"stride_bytes\":" << c.mask.stride_bytes
              << ",\"kind\":\"" << json_escape(c.mask.kind) << "\""
              << ",\"positive_pixels\":" << c.mask.positive_pixels
              << ",\"plane_present\":" << (c.mask.plane ? "true" : "false") << "}";
        }
        o << "}";
    }
    o << "]}";
    return o.str();
}

}  // namespace core
}  // namespace railhmi
