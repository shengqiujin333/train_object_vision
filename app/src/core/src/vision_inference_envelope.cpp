// railhmi-core: 推理结果关联与发布实现（视觉推理 ITEM-006）。仅标准库。

#include "railhmi/core/vision_inference_envelope.h"

#include <algorithm>
#include <cstdio>

namespace railhmi {
namespace core {

const char* to_string(AiAvailability v) {
    switch (v) {
        case AiAvailability::Ready: return "ready";
        case AiAvailability::Unavailable: return "unavailable";
    }
    return "?";
}

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
                if (static_cast<unsigned char>(c) < 0x20) {
                    char b[8];
                    std::snprintf(b, sizeof(b), "\\u%04x", c);
                    o += b;
                } else {
                    o += c;
                }
        }
    }
    return o;
}

std::string num_f(double v) {
    char b[48];
    std::snprintf(b, sizeof(b), "%.6g", v);
    return b;
}

}  // namespace

int latency_ms_of(long long capture_mono_us, long long result_mono_us) {
    const long long d = result_mono_us - capture_mono_us;
    if (d <= 0) return 0;
    return static_cast<int>(d / 1000);
}

InferenceEnvelope build_envelope(const EnvelopeBuildInput& in) {
    InferenceEnvelope e;
    e.frame = in.frame;
    e.profile = in.profile;
    e.model_version = in.model_version;
    e.capture_mono_us = in.capture_mono_us;
    e.result_mono_us = in.result_mono_us;
    e.latency_ms = latency_ms_of(in.capture_mono_us, in.result_mono_us);

    const bool ready = in.detect_ready && (!in.requires_segmentation || in.seg_ready);
    if (!ready) {
        e.availability = AiAvailability::Unavailable;
        if (!in.unavailable_reason.empty()) {
            e.reason = in.unavailable_reason;
        } else {
            e.reason = in.detect_ready ? envelope_reason::kSegUnavailable()
                                       : envelope_reason::kAiUnavailable();
        }
        // 不可用 -> 空框（与“ready 但无目标”语义区分）
        return e;
    }

    e.availability = AiAvailability::Ready;
    e.reason = envelope_reason::kReady();
    if (in.boxes) {
        for (std::size_t i = 0; i < in.boxes->size(); ++i) {
            const DetectBox& b = (*in.boxes)[i];
            EnvelopeBox o;
            o.x = b.x;
            o.y = b.y;
            o.w = b.w;
            o.h = b.h;
            o.confidence = b.score;
            o.class_id = b.class_id;
            o.label = b.label;
            o.in_roi = false;      // 由 in_roi_flags 覆盖（语义层判定）
            o.distance_m = -1.0f;  // 由 distances_m 覆盖；-1 = 未知
            // track_id：未知必须是 null（-1），禁止用 0 冒充
            o.track_id = (i < in.track_ids.size()) ? in.track_ids[i] : -1;
            if (i < in.distances_m.size()) o.distance_m = in.distances_m[i];
            if (i < in.in_roi_flags.size()) o.in_roi = (in.in_roi_flags[i] != 0);
            e.boxes.push_back(std::move(o));
        }
    }
    if (in.masks) e.masks = *in.masks;
    return e;
}

InferenceEnvelope make_unavailable_envelope(const FrameId& frame, AiProfile profile,
                                            const std::string& model_version,
                                            long long capture_mono_us, long long result_mono_us,
                                            const std::string& reason) {
    InferenceEnvelope e;
    e.frame = frame;
    e.profile = profile;
    e.model_version = model_version;
    e.capture_mono_us = capture_mono_us;
    e.result_mono_us = result_mono_us;
    e.latency_ms = latency_ms_of(capture_mono_us, result_mono_us);
    e.availability = AiAvailability::Unavailable;
    e.reason = reason.empty() ? envelope_reason::kAiUnavailable() : reason;
    return e;
}

EnvelopePublisher::EnvelopePublisher(std::string session_id, int queue_depth)
    : gate_(std::move(session_id)), queue_(static_cast<std::size_t>(queue_depth > 0 ? queue_depth : 1)) {}

bool EnvelopePublisher::submit(const InferenceEnvelope& e, std::string* reject_reason) {
    ++stats_.submitted;
    const FrameAdmission adm = gate_.admit(e.frame);
    if (adm != FrameAdmission::Accepted) {
        ++stats_.rejected;
        switch (adm) {
            case FrameAdmission::StaleSession: ++stats_.stale_session; break;
            case FrameAdmission::StaleGeneration: ++stats_.stale_generation; break;
            case FrameAdmission::DuplicateSeq: ++stats_.duplicate_seq; break;
            case FrameAdmission::OutOfOrderSeq: ++stats_.out_of_order_seq; break;
            case FrameAdmission::Accepted: break;
        }
        if (reject_reason) *reject_reason = to_string(adm);
        return false;
    }
    ++stats_.accepted;
    if (e.availability == AiAvailability::Unavailable) {
        ++stats_.unavailable;
    } else if (e.empty_boxes()) {
        ++stats_.empty_boxes;
    } else {
        ++stats_.non_empty;
    }
    queue_.push(e);
    stats_.queued = static_cast<long long>(queue_.pushed());
    stats_.queue_dropped = static_cast<long long>(queue_.dropped());
    if (reject_reason) reject_reason->clear();
    return true;
}

bool EnvelopePublisher::pop(InferenceEnvelope* out) {
    if (!queue_.pop(out)) return false;
    ++stats_.popped;
    return true;
}

std::string envelope_json(const InferenceEnvelope& e) {
    std::string o = "{";
    o += "\"session_id\":\"" + json_escape(e.frame.session_id) + "\",";
    o += "\"camera_id\":\"" + json_escape(e.frame.camera_id) + "\",";
    o += "\"generation\":" + std::to_string(e.frame.generation) + ",";
    o += "\"seq\":" + std::to_string(e.frame.seq) + ",";
    o += "\"profile\":\"" + std::string(to_string(e.profile)) + "\",";
    o += "\"capture_mono_us\":" + std::to_string(e.capture_mono_us) + ",";
    o += "\"result_mono_us\":" + std::to_string(e.result_mono_us) + ",";
    o += "\"latency_ms\":" + std::to_string(e.latency_ms) + ",";
    o += "\"model_version\":\"" + json_escape(e.model_version) + "\",";
    o += "\"availability\":\"" + std::string(to_string(e.availability)) + "\",";
    o += "\"reason\":\"" + json_escape(e.reason) + "\",";
    o += "\"box_count\":" + std::to_string(e.boxes.size()) + ",";
    o += "\"boxes\":[";
    for (std::size_t i = 0; i < e.boxes.size(); ++i) {
        const EnvelopeBox& b = e.boxes[i];
        if (i) o += ",";
        o += "{\"x\":" + num_f(b.x) + ",\"y\":" + num_f(b.y) + ",\"w\":" + num_f(b.w) +
             ",\"h\":" + num_f(b.h) + ",\"confidence\":" + num_f(b.confidence) +
             ",\"class_id\":" + std::to_string(b.class_id) + ",\"label\":\"" + json_escape(b.label) +
             "\",\"in_roi\":" + (b.in_roi ? "true" : "false") +
             ",\"distance_m\":" + num_f(b.distance_m) + ",\"track_id\":" + std::to_string(b.track_id) +
             "}";
    }
    o += "],\"masks\":[";
    for (std::size_t i = 0; i < e.masks.size(); ++i) {
        const MaskRef& m = e.masks[i];
        if (i) o += ",";
        o += "{\"box_index\":" + std::to_string(m.box_index) + ",\"width\":" + std::to_string(m.width) +
             ",\"height\":" + std::to_string(m.height) + ",\"stride_bytes\":" +
             std::to_string(m.stride_bytes) + ",\"kind\":\"" + json_escape(m.kind) +
             "\",\"plane_present\":" + (m.plane ? "true" : "false") + "}";
    }
    o += "]}";
    return o;
}

std::string envelope_stats_json(const EnvelopeStats& s) {
    std::string o = "{";
    o += "\"submitted\":" + std::to_string(s.submitted);
    o += ",\"accepted\":" + std::to_string(s.accepted);
    o += ",\"rejected\":" + std::to_string(s.rejected);
    o += ",\"stale_session\":" + std::to_string(s.stale_session);
    o += ",\"stale_generation\":" + std::to_string(s.stale_generation);
    o += ",\"duplicate_seq\":" + std::to_string(s.duplicate_seq);
    o += ",\"out_of_order_seq\":" + std::to_string(s.out_of_order_seq);
    o += ",\"queued\":" + std::to_string(s.queued);
    o += ",\"queue_dropped\":" + std::to_string(s.queue_dropped);
    o += ",\"popped\":" + std::to_string(s.popped);
    o += ",\"unavailable\":" + std::to_string(s.unavailable);
    o += ",\"empty_boxes\":" + std::to_string(s.empty_boxes);
    o += ",\"non_empty\":" + std::to_string(s.non_empty);
    o += "}";
    return o;
}

}  // namespace core
}  // namespace railhmi
