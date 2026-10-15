#include "railhmi/core/rules.h"

namespace railhmi {
namespace core {

const char* to_string(AlarmLevel v) {
    switch (v) {
        case AlarmLevel::L1: return "L1";
        case AlarmLevel::L2: return "L2";
        case AlarmLevel::L3: return "L3";
    }
    return "?";
}

const char* to_string(AlarmType v) {
    switch (v) {
        case AlarmType::PersonOnRail: return "person_on_rail";
        case AlarmType::FrontObstacle: return "front_obstacle";
        case AlarmType::WheelDerail: return "wheel_derail";
        case AlarmType::PantographAbnormal: return "pantograph_abnormal";
        case AlarmType::DoorAjar: return "door_ajar";
        case AlarmType::FrontCameraDevice: return "front_camera_device";
        case AlarmType::SideCameraDevice: return "side_camera_device";
        case AlarmType::Device: return "device";
    }
    return "?";
}

const char* to_string(AckState v) {
    return v == AckState::Acked ? "acked" : "unacked";
}

AlarmLevel map_level(AlarmType t) {
    switch (t) {
        case AlarmType::PersonOnRail:
        case AlarmType::FrontObstacle:
        case AlarmType::WheelDerail:
            return AlarmLevel::L1;
        case AlarmType::PantographAbnormal:
        case AlarmType::DoorAjar:
        case AlarmType::FrontCameraDevice:
        case AlarmType::Device:
            return AlarmLevel::L2;
        case AlarmType::SideCameraDevice:
            return AlarmLevel::L3;
    }
    return AlarmLevel::L2;
}

AlarmLevel map_device_level(DeviceCategory c) {
    switch (c) {
        case DeviceCategory::FrontPrimary: return AlarmLevel::L2;  // PRD §8.2：前方主摄离线/冻结=2
        case DeviceCategory::Side: return AlarmLevel::L3;           // PRD §8.2：侧视离线=3
        case DeviceCategory::Other: return AlarmLevel::L2;          // 其它设备故障（PRD 未列举）
    }
    return AlarmLevel::L2;
}

bool roi_contains(const RoiConfig& r, double nx, double ny) {
    return nx >= r.x && nx <= r.x + r.w && ny >= r.y && ny <= r.y + r.h;
}

const RoiConfig* find_roi(const std::vector<RoiConfig>& rois,
                          const std::string& camera, const std::string& name) {
    for (const auto& r : rois) {
        if (r.camera == camera && r.name == name) return &r;
    }
    return nullptr;
}

RuleEngine::RuleEngine(RuleConfig cfg) : cfg_(cfg) {}

std::string RuleEngine::make_key(const std::string& cam, AlarmType type, int track_id) const {
    return cam + "|" + to_string(type) + "|" + std::to_string(track_id);
}

void RuleEngine::reset_camera_confirm(const std::string& cam) {
    const std::string prefix = cam + "|";
    for (auto it = confirm_.begin(); it != confirm_.end();) {
        if (it->first.compare(0, prefix.size(), prefix) == 0) it = confirm_.erase(it);
        else ++it;
    }
}

AlarmEvent RuleEngine::make_event(const Detection& d, AlarmType type, long long t_ms) {
    AlarmEvent e;
    e.id = next_id_++;
    e.type = type;
    e.level = map_level(type);
    e.source_cam = d.camera_id;
    e.ts_ms = t_ms;
    e.last_update_ms = t_ms;
    e.track_id = d.track_id;
    e.confidence = d.confidence;
    e.distance_m = d.distance_m;
    e.description = std::string(to_string(type)) + " @ " + d.camera_id;
    e.ack_state = AckState::Unacked;
    e.record_available = false;
    return e;
}

void RuleEngine::update_event(AlarmEvent* e, const Detection& d) {
    if (!e) return;
    e->last_update_ms = d.t_ms;
    e->confidence = d.confidence;
    if (d.distance_m >= 0.0) e->distance_m = d.distance_m;
}

std::vector<AlarmEvent> RuleEngine::on_detection(const Detection& d) {
    if (d.profile == AiProfile::None) return {};  // 左右侧广角：不检测，不产生事件

    AlarmType type = AlarmType::Device;
    bool candidate = false;
    switch (d.profile) {
        case AiProfile::Front:
            if (d.in_roi && d.confidence >= cfg_.confidence_min) {
                type = (d.label == "person") ? AlarmType::PersonOnRail : AlarmType::FrontObstacle;
                candidate = true;
            }
            break;
        case AiProfile::Pantograph:
            if (d.pantograph == PantographState::Abnormal && d.confidence >= cfg_.confidence_min) {
                type = AlarmType::PantographAbnormal;
                candidate = true;
            }
            break;
        case AiProfile::WheelRail:
            if (d.wheel_abnormal && d.confidence >= cfg_.confidence_min) {
                type = AlarmType::WheelDerail;
                candidate = true;
            }
            break;
        case AiProfile::CabDoor:
            if (d.door == DoorState::Ajar && d.confidence >= cfg_.confidence_min) {
                type = AlarmType::DoorAjar;
                candidate = true;
            }
            break;
        case AiProfile::None:
            return {};
    }

    if (!candidate) {
        reset_camera_confirm(d.camera_id);
        return {};
    }

    const std::string key = make_key(d.camera_id, type, d.track_id);

    // 抑制窗口：同一 key 在窗口内不新增事件，只更新已有事件的元数据（ALM-005）。
    auto le = last_emit_ms_.find(key);
    if (le != last_emit_ms_.end() && d.t_ms - le->second < cfg_.suppress_window_ms) {
        auto eid = key_event_id_.find(key);
        if (eid != key_event_id_.end()) {
            for (auto& e : events_) {
                if (e.id == eid->second) { update_event(&e, d); break; }
            }
        }
        confirm_[key] = Confirm{0, 0, 0};
        return {};
    }

    // 多帧确认：在 confirm_window_ms 内累计连续命中帧数。
    Confirm& cs = confirm_[key];
    if (cs.count == 0 || d.t_ms - cs.last_ms > cfg_.confirm_window_ms) {
        cs.count = 1;
        cs.first_ms = d.t_ms;
    } else {
        ++cs.count;
    }
    cs.last_ms = d.t_ms;
    if (cs.count < cfg_.confirm_frames) return {};

    AlarmEvent e = make_event(d, type, d.t_ms);
    events_.push_back(e);
    last_emit_ms_[key] = d.t_ms;
    key_event_id_[key] = e.id;
    confirm_[key] = Confirm{0, 0, 0};
    return {e};
}

std::vector<AlarmEvent> RuleEngine::on_device_event(const std::string& camera_id, DeviceCategory cat,
                                                    long long t_ms, const std::string& detail) {
    const AlarmType type = (cat == DeviceCategory::FrontPrimary)
                               ? AlarmType::FrontCameraDevice
                               : (cat == DeviceCategory::Side ? AlarmType::SideCameraDevice
                                                              : AlarmType::Device);
    const std::string key = make_key(camera_id, type, 0);
    auto le = last_emit_ms_.find(key);
    if (le != last_emit_ms_.end() && t_ms - le->second < cfg_.suppress_window_ms) {
        return {};  // 抑制窗口内重复设备事件不新增
    }
    AlarmEvent e;
    e.id = next_id_++;
    e.type = type;
    e.level = map_device_level(cat);
    e.source_cam = camera_id;
    e.ts_ms = t_ms;
    e.last_update_ms = t_ms;
    e.description = detail.empty() ? std::string(to_string(type)) : detail;
    e.ack_state = AckState::Unacked;
    events_.push_back(e);
    last_emit_ms_[key] = t_ms;
    key_event_id_[key] = e.id;
    return {e};
}

bool RuleEngine::ack(long long id) {
    for (auto& e : events_) {
        if (e.id == id) {
            e.ack_state = AckState::Acked;  // 只改确认状态，不删除事件
            return true;
        }
    }
    return false;
}

const AlarmEvent* RuleEngine::find(long long id) const {
    for (const auto& e : events_) {
        if (e.id == id) return &e;
    }
    return nullptr;
}

std::size_t RuleEngine::unacked_count() const {
    std::size_t n = 0;
    for (const auto& e : events_) if (e.ack_state == AckState::Unacked) ++n;
    return n;
}

}  // namespace core
}  // namespace railhmi
