#include "railhmi/core/ai_alarm.h"

#include <algorithm>

namespace railhmi {
namespace core {
namespace {

bool is_person_label(const EnvelopeBox& b) {
    // COCO 白名单当前只有 person（class 0）；标签缺失时按类号判定，避免漏报。
    return b.class_id == 0 || b.label == "person";
}

}  // namespace

const char* ai_alarm_type_token(AlarmType t) {
    switch (t) {
        case AlarmType::PersonOnRail: return "person_on_rail";
        case AlarmType::FrontObstacle: return "front_obstacle";
        case AlarmType::WheelDerail: return "wheel_derail";
        case AlarmType::PantographAbnormal: return "pantograph_abnormal";
        case AlarmType::DoorAjar: return "door_ajar";
        case AlarmType::FrontCameraDevice: return "front_camera_device";
        case AlarmType::SideCameraDevice: return "side_camera_device";
        case AlarmType::Device: return "device";
    }
    return "device";
}

const char* ai_alarm_description(AiProfile profile, AlarmType t) {
    switch (t) {
        case AlarmType::PersonOnRail: return "前方轨道区域内检测到人员";
        case AlarmType::FrontObstacle: return "前方轨道区域内检测到障碍物";
        case AlarmType::WheelDerail: return "轮轨画面检测到异常目标（临时复用检测模型）";
        case AlarmType::PantographAbnormal: return "受电弓画面检测到异常目标（临时复用检测模型）";
        default:
            return profile == AiProfile::None ? "未接入检测职责" : "检测到异常";
    }
}

AiAlarmCandidate evaluate_ai_alarm(AiProfile profile, const std::vector<EnvelopeBox>& boxes,
                                   double confidence_min) {
    AiAlarmCandidate c;
    const double min_conf = confidence_min > 0.0 ? confidence_min : 0.0;

    if (profile == AiProfile::Front) {
        // 两阶段：只有 in_roi（轨道掩码内）的框才算「人在轨道上/异物侵入」。
        // 轨道外的行人属于正常路侧目标，不报警。
        bool any_person = false;
        for (const auto& b : boxes) {
            if (!b.in_roi) continue;
            if (static_cast<double>(b.confidence) < min_conf) continue;
            ++c.boxes;
            if (static_cast<double>(b.confidence) > c.confidence) {
                c.confidence = b.confidence;
                any_person = is_person_label(b);
            }
        }
        if (c.boxes > 0) {
            c.hit = true;
            c.type = any_person ? AlarmType::PersonOnRail : AlarmType::FrontObstacle;
            c.level = map_level(c.type);
        }
        return c;
    }

    if (profile == AiProfile::Pantograph || profile == AiProfile::WheelRail) {
        // 临时复用检测模型：没有专用的受电弓三态 / 轮轨几何模型，因此「检测到目标」
        // 即视为异常并如实标注来源（不声称具备专用判定能力）。
        for (const auto& b : boxes) {
            if (static_cast<double>(b.confidence) < min_conf) continue;
            ++c.boxes;
            if (static_cast<double>(b.confidence) > c.confidence) c.confidence = b.confidence;
        }
        if (c.boxes > 0) {
            c.hit = true;
            c.type = profile == AiProfile::Pantograph ? AlarmType::PantographAbnormal
                                                      : AlarmType::WheelDerail;
            c.level = map_level(c.type);
        }
        return c;
    }

    // cab_door 由 BLE 门磁负责；none 不检测。两者都不产生 AI 报警。
    return c;
}

AiAlarmTracker::AiAlarmTracker(AiAlarmConfig cfg) : cfg_(cfg) {
    if (cfg_.confirm_frames < 1) cfg_.confirm_frames = 1;
    if (cfg_.clear_hold_ms < 0) cfg_.clear_hold_ms = 0;
}

bool AiAlarmTracker::update(const std::string& camera_id, AiProfile profile,
                            const std::vector<EnvelopeBox>& boxes, long long now_ms,
                            AiAlarmState* out) {
    AiAlarmState& st = states_[camera_id];
    const bool was_active = st.active;
    const AiAlarmCandidate cand = evaluate_ai_alarm(profile, boxes, cfg_.confidence_min);

    st.boxes = cand.boxes;
    if (cand.hit) {
        if (st.confirm < cfg_.confirm_frames) ++st.confirm;
        st.last_hit_ms = now_ms;
        st.confidence = cand.confidence;
        st.level = cand.level;
        st.type = cand.type;
        st.description = ai_alarm_description(profile, cand.type);
        if (!st.active && st.confirm >= cfg_.confirm_frames) {
            st.active = true;
            st.since_ms = now_ms;
        }
    } else {
        st.confirm = 0;
        if (st.active && st.last_hit_ms >= 0 && now_ms - st.last_hit_ms >= cfg_.clear_hold_ms) {
            st.active = false;
            st.since_ms = 0;
            st.confidence = -1.0;
        }
    }

    if (out) *out = st;
    return was_active != st.active;
}

AiAlarmState AiAlarmTracker::state_of(const std::string& camera_id) const {
    const auto it = states_.find(camera_id);
    return it == states_.end() ? AiAlarmState() : it->second;
}

bool AiAlarmTracker::active(const std::string& camera_id) const {
    const auto it = states_.find(camera_id);
    return it != states_.end() && it->second.active;
}

std::size_t AiAlarmTracker::active_count() const {
    std::size_t n = 0;
    for (const auto& kv : states_)
        if (kv.second.active) ++n;
    return n;
}

void AiAlarmTracker::clear() {
    for (auto& kv : states_) {
        kv.second.active = false;
        kv.second.confirm = 0;
        kv.second.confidence = -1.0;
        kv.second.boxes = 0;
        kv.second.since_ms = 0;
    }
}

}  // namespace core
}  // namespace railhmi
