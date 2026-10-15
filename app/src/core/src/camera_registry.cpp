#include "railhmi/core/camera_registry.h"

namespace railhmi {
namespace core {

std::string detection_badge_for(AiProfile p) {
    switch (p) {
        case AiProfile::Front: return "前方障碍/人员";
        case AiProfile::Pantograph: return "受电弓三状态";
        case AiProfile::WheelRail: return "轮轨";
        case AiProfile::CabDoor: return "车门";
        case AiProfile::None: return "广角·无检测";
    }
    return "广角·无检测";
}

bool detection_badge_visible(Page page) {
    return page == Page::AllCameras || page == Page::Maintenance;
}

std::vector<CameraSlot> build_registry(const AppSettings& settings) {
    std::vector<CameraSlot> out;
    out.reserve(settings.cameras.size());
    int order = 0;
    for (const auto& c : settings.cameras) {
        CameraSlot s;
        s.id = c.id;
        s.name = c.name;
        s.position = c.position;
        s.ai_enabled = c.ai_enabled;
        s.ai_profile = c.ai_profile;
        s.detection_badge = detection_badge_for(c.ai_profile);
        s.stream_state = "connecting";  // 运行时初始占位，非配置
        s.rtsp_url = c.rtsp_url;
        s.credential_env = c.credential_env;
        s.order = order++;
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<IntegrationSlotConfig> build_integrations(const AppSettings& settings) {
    std::vector<IntegrationSlotConfig> out;
    for (const auto& in : settings.app.integrations) {
        IntegrationSlotConfig s;
        s.id = in.id;
        s.name = in.name;
        s.address = in.address;
        s.registered = in.registered;
        s.state = to_string(initial_integration_state(in));
        s.protocol = in.has_protocol ? in.protocol : "—";
        s.channel_mapping = in.has_channel_mapping ? "已配置" : "未配置";
        out.push_back(std::move(s));
    }
    return out;
}

}  // namespace core
}  // namespace railhmi
