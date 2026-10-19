// railhmi-core: 语义适配与规则接入实现（视觉推理 ITEM-007）。仅标准库。

#include "railhmi/core/vision_semantic_adapter.h"

#include <algorithm>
#include <cmath>

namespace railhmi {
namespace core {

namespace {

bool contains(const std::vector<std::string>& v, const std::string& s) {
    return std::find(v.begin(), v.end(), s) != v.end();
}

}  // namespace

SemanticAdapter::SemanticAdapter(SemanticConfig cfg) : cfg_(std::move(cfg)) {}

int SemanticAdapter::invalidate_track(RuleEngine* rules, const std::string& camera_id, int track_id) {
    if (!rules) return 0;
    return rules->invalidate_track(camera_id, track_id);
}

int SemanticAdapter::invalidate_generation(RuleEngine* rules, const std::string& camera_id) {
    if (!rules) return 0;
    return rules->invalidate_camera(camera_id);
}

SemanticResult SemanticAdapter::adapt(const InferenceEnvelope& e, long long ts_ms) {
    SemanticResult r;
    if (cfg_.profile == AiProfile::None || cfg_.allowed_labels.empty() || cfg_.confidence_min < 0.0 ||
        cfg_.confidence_min > 1.0) {
        r.reason = semantic_reason::kBadConfig();
        r.detail = "profile/labels/threshold invalid";
        return r;
    }
    if (!e.ready()) {
        r.available = false;
        r.ok = false;
        r.reason = semantic_reason::kUnavailable();
        r.detail = std::string("envelope availability=") + to_string(e.availability) +
                   " reason=" + e.reason;
        return r;
    }
    r.available = true;
    r.stats.input_boxes = static_cast<long long>(e.boxes.size());

    // 按 track 聚合（同 track 取最高分）；map 的有序性天然给出稳定 track 排序。
    std::map<int, Detection> by_track;
    for (const EnvelopeBox& b : e.boxes) {
        if (cfg_.require_track && b.track_id < 0) {
            ++r.stats.dropped_no_track;
            continue;
        }
        if (!contains(cfg_.known_labels, b.label)) {
            ++r.stats.blocked_unknown_label;
            continue;
        }
        if (!contains(cfg_.allowed_labels, b.label)) {
            ++r.stats.blocked_not_allowed;
            continue;
        }
        Detection d;
        d.camera_id = e.frame.camera_id;
        d.profile = cfg_.profile;
        d.t_ms = ts_ms;
        d.track_id = b.track_id < 0 ? -1 : b.track_id;
        d.in_roi = cfg_.use_envelope_in_roi ? b.in_roi : false;
        d.confidence = b.confidence;
        d.distance_m = b.distance_m;
        d.label = b.label;
        auto it = by_track.find(d.track_id);
        if (it == by_track.end()) {
            by_track.emplace(d.track_id, d);
        } else {
            ++r.stats.aggregated;
            if (d.confidence > it->second.confidence) it->second = d;
        }
    }

    r.detections.reserve(by_track.size());
    r.candidate.reserve(by_track.size());
    for (auto& kv : by_track) {
        const Detection& d = kv.second;
        const bool cand = d.in_roi && d.confidence >= cfg_.confidence_min;
        if (cand) {
            ++r.stats.candidates;
        } else if (!d.in_roi) {
            ++r.stats.dropped_off_rail;
        } else {
            ++r.stats.dropped_low_confidence;
        }
        r.detections.push_back(d);
        r.candidate.push_back(cand ? 1 : 0);
    }
    r.stats.mapped = static_cast<long long>(r.detections.size());
    r.ok = true;
    if (r.stats.candidates > 0) {
        r.reason = semantic_reason::kOk();
    } else if (r.stats.mapped > 0) {
        r.reason = semantic_reason::kNoCandidate();
    } else {
        r.reason = semantic_reason::kAllBlocked();
    }
    r.detail = "in=" + std::to_string(r.stats.input_boxes) + " mapped=" +
               std::to_string(r.stats.mapped) + " candidates=" + std::to_string(r.stats.candidates) +
               " unknown=" + std::to_string(r.stats.blocked_unknown_label) +
               " no_track=" + std::to_string(r.stats.dropped_no_track);
    return r;
}

std::vector<AlarmEvent> SemanticAdapter::dispatch(SemanticResult* r, RuleEngine* rules) {
    std::vector<AlarmEvent> events;
    if (!r || !rules || !r->available || !r->ok) return events;
    for (std::size_t i = 0; i < r->detections.size(); ++i) {
        const Detection& d = r->detections[i];
        const bool cand = i < r->candidate.size() && r->candidate[i] != 0;
        if (cand) {
            auto ev = rules->on_detection(d);
            events.insert(events.end(), ev.begin(), ev.end());
        } else if (d.track_id >= 0) {
            // 非候选：只清理该目标自己的确认状态，不整路清除同相机其他目标。
            r->stats.track_reset_keys += rules->invalidate_track(d.camera_id, d.track_id);
        }
    }
    return events;
}

std::vector<AlarmEvent> SemanticAdapter::adapt_and_dispatch(const InferenceEnvelope& e, long long ts_ms,
                                                           RuleEngine* rules, SemanticResult* out) {
    SemanticResult r = adapt(e, ts_ms);
    note_generation(e.frame, rules, &r);
    auto events = dispatch(&r, rules);
    if (out) *out = r;
    return events;
}

void SemanticAdapter::note_generation(const FrameId& frame, RuleEngine* rules,
                                      SemanticResult* r) const {
    if (!r || !rules) return;
    auto it = last_generation_.find(frame.camera_id);
    if (it == last_generation_.end()) {
        last_generation_[frame.camera_id] = frame.generation;
        return;
    }
    if (frame.generation == it->second) return;
    if (frame.generation < it->second) {
        // 旧 generation：上游应已拒绝；此处防御性不下发。
        r->ok = false;
        r->available = false;
        r->reason = semantic_reason::kStaleGeneration();
        r->detections.clear();
        r->candidate.clear();
        return;
    }
    it->second = frame.generation;
    // generation 变化：该相机的确认/跟踪状态失效，受控清理（不删除事件）。
    r->stats.generation_reset_keys += rules->invalidate_camera(frame.camera_id);
}

}  // namespace core
}  // namespace railhmi
