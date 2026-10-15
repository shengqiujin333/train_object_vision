#include "railhmi/core/inference.h"

#include <algorithm>

#include "railhmi/core/channel_view.h"   // format_last_frame

namespace railhmi {
namespace core {

BoundedFrameQueue::BoundedFrameQueue(int depth) : depth_(depth > 0 ? depth : 1) {}

bool BoundedFrameQueue::push(const FrameRef& f) {
    bool dropped = false;
    while (static_cast<int>(q_.size()) >= depth_) {
        q_.pop_front();
        ++dropped_;
        dropped = true;
    }
    q_.push_back(f);
    return dropped;
}

bool BoundedFrameQueue::pop(FrameRef* out) {
    if (q_.empty()) return false;
    *out = q_.front();
    q_.pop_front();
    return true;
}

int ai_priority(AiProfile p) {
    switch (p) {
        case AiProfile::Front: return 1;
        case AiProfile::WheelRail: return 2;
        case AiProfile::Pantograph: return 3;
        case AiProfile::CabDoor: return 4;
        case AiProfile::None: return 99;
    }
    return 99;
}

bool ai_eligible(AiProfile p) { return p != AiProfile::None; }

std::string ai_profile_label(AiProfile p) { return detection_badge_for(p); }

bool frame_due(uint64_t seq, int interval) {
    if (interval <= 1) return true;
    return (seq % static_cast<uint64_t>(interval)) == 0;
}

StatusLevel ai_status_level(const AiRuntime& rt, long long now_ms, int stale_ms) {
    if (!rt.worker_alive) return StatusLevel::Unavailable;
    bool any_ok = false;
    for (const auto& c : rt.channels) {
        if (!ai_eligible(c.profile)) continue;
        if (c.last_result_ms >= 0 && (now_ms - c.last_result_ms) <= stale_ms) any_ok = true;
    }
    return any_ok ? StatusLevel::Ok : StatusLevel::Warning;
}

std::string ai_status_text(const AiRuntime& rt, long long now_ms, int stale_ms) {
    const StatusLevel l = ai_status_level(rt, now_ms, stale_ms);
    if (l == StatusLevel::Unavailable) return "AI 不可用";
    if (l == StatusLevel::Warning) return "推理陈旧";
    return "运行";
}

CategoryStatus ai_status_category(const AiRuntime& rt, long long now_ms, int stale_ms) {
    CategoryStatus s;
    s.level = ai_status_level(rt, now_ms, stale_ms);
    s.value = ai_status_text(rt, now_ms, stale_ms);
    return s;
}

std::string ai_alarm_annotation(bool ai_available) { return ai_available ? std::string() : "AI 不可用"; }

SchedDecision plan_inference(const SchedInput& in, const AiSchedulerConfig& cfg) {
    SchedDecision d;
    if (!in.worker_alive) return d;   // 线程已停 -> 不调度（上层转“AI 不可用”）

    int budget = cfg.npu_budget;
    if (in.temp_c > cfg.temp_limit_c) {
        d.temperature_guard = true;
        budget = std::max(1, cfg.npu_budget / 2);   // 降载但不停（HLT/温度告警）
    }

    std::vector<AiChannelRuntime> eligible;
    for (const auto& c : in.channels)
        if (ai_eligible(c.profile)) eligible.push_back(c);
    std::stable_sort(eligible.begin(), eligible.end(),
                     [](const AiChannelRuntime& a, const AiChannelRuntime& b) {
                         return ai_priority(a.profile) < ai_priority(b.profile);
                     });
    for (std::size_t i = 0; i < eligible.size(); ++i) {
        if (static_cast<int>(i) < budget) d.run.push_back(eligible[i].camera_id);
        else d.skipped.push_back(eligible[i].camera_id);
    }
    return d;
}

std::vector<AiDebugChannel> build_ai_debug(const std::vector<CameraSlot>& registry,
                                           const AiRuntime& rt,
                                           const std::vector<DetectionResult>& latest,
                                           long long now_ms,
                                           int stale_ms) {
    std::vector<AiDebugChannel> out;
    for (const auto& slot : registry) {
        if (!slot.ai_enabled || !ai_eligible(slot.ai_profile)) continue;
        AiDebugChannel d;
        d.camera_id = slot.id;
        d.name = slot.name;
        d.profile = slot.ai_profile;
        d.profile_label = ai_profile_label(slot.ai_profile);
        d.model_version = "";
        d.fps = 0;
        d.latency_ms = 0;
        d.available = false;

        for (const auto& c : rt.channels) {
            if (c.camera_id == slot.id) {
                d.model_version = c.model_version;
                d.fps = c.fps;
                d.latency_ms = c.latency_ms;
                if (rt.worker_alive) {
                    d.available = true;
                }
                if (c.last_result_ms >= 0) {
                    d.last_result_age_ms = now_ms - c.last_result_ms;
                    d.last_result_text = format_last_frame(d.last_result_age_ms);
                }
                break;
            }
        }
        for (const auto& r : latest) {
            if (r.camera_id == slot.id) {
                d.boxes = r.boxes;
                d.max_confidence = r.max_confidence;
                if (!r.model_version.empty()) d.model_version = r.model_version;
                if (r.latency_ms > 0) d.latency_ms = r.latency_ms;
                break;
            }
        }
        if (!rt.worker_alive) {
            d.available = false;
            d.unavailable_reason = "AI 不可用";
        } else if (d.last_result_age_ms < 0) {
            d.available = false;
            d.unavailable_reason = "等待结果";
        } else if (d.last_result_age_ms > stale_ms) {
            d.available = false;
            d.unavailable_reason = "推理陈旧";
        }
        if (d.last_result_text.empty()) d.last_result_text = "—";
        out.push_back(d);
    }
    return out;
}

}  // namespace core
}  // namespace railhmi
