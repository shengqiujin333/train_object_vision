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

SemanticAdapter::SemanticAdapter(SemanticConfig cfg) : cfg_(std::move(cfg)), tracker_(cfg_.tracker) {}

const char* to_string(TrackPolicy v) {
    switch (v) {
        case TrackPolicy::EnvelopeOnly: return "envelope_only";
        case TrackPolicy::Lightweight: return "lightweight";
        case TrackPolicy::CameraLevel: return "camera_level";
    }
    return "?";
}

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

    // 1) 白名单过滤（未知 label 拦截）。
    std::vector<EnvelopeBox> filtered;
    filtered.reserve(e.boxes.size());
    for (const EnvelopeBox& b : e.boxes) {
        if (!contains(cfg_.known_labels, b.label)) {
            ++r.stats.blocked_unknown_label;
            continue;
        }
        if (!contains(cfg_.allowed_labels, b.label)) {
            ++r.stats.blocked_not_allowed;
            continue;
        }
        filtered.push_back(b);
    }

    // 2) 目标关联：按策略得到每框 track_id；无法关联的框显式丢弃/标记。
    std::vector<std::int32_t> ids(filtered.size(), -1);
    std::vector<char> keep(filtered.size(), 1);
    if (cfg_.track_policy == TrackPolicy::Lightweight) {
        const auto tracked = tracker_.update(filtered, ts_ms);
        for (std::size_t i = 0; i < filtered.size(); ++i) {
            ids[i] = tracked[i].track_id;
            if (ids[i] < 0) {
                ++r.stats.untracked;
                keep[i] = 0;
            }
        }
    } else if (cfg_.track_policy == TrackPolicy::CameraLevel) {
        // 显式相机级适配：共享 null 关联键（-1），**不是 0**。
        ids.assign(filtered.size(), camera_level_track_id());
    } else {
        for (std::size_t i = 0; i < filtered.size(); ++i) {
            ids[i] = filtered[i].track_id < 0 ? -1 : filtered[i].track_id;
            if (cfg_.require_track && ids[i] < 0) {
                ++r.stats.dropped_no_track;
                keep[i] = 0;
            }
        }
    }

    // 3) 按 track 聚合（同 track 取最高分）；map 的有序性天然给出稳定 track 排序。
    std::map<int, Detection> by_track;
    for (std::size_t i = 0; i < filtered.size(); ++i) {
        if (!keep[i]) continue;
        const EnvelopeBox& b = filtered[i];
        Detection d;
        d.camera_id = e.frame.camera_id;
        d.profile = cfg_.profile;
        d.t_ms = ts_ms;
        d.track_id = ids[i];
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
    // 先处理 generation 变化：必须先 reset 跟踪器，再为当前帧分配 id。
    SemanticResult gen;
    note_generation(e.frame, rules, &gen);
    if (!gen.ok && gen.reason == semantic_reason::kStaleGeneration()) {
        if (out) *out = gen;
        return {};
    }
    SemanticResult r = adapt(e, ts_ms);
    r.stats.generation_reset_keys = gen.stats.generation_reset_keys;
    r.stats.tracker_resets = gen.stats.tracker_resets;
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
    tracker_.reset();  // 旧目标 track 不得跨代复用；id 继续单调递增
    ++r->stats.tracker_resets;
}

}  // namespace core
}  // namespace railhmi
