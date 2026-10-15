#pragma once

// railhmi-core: 配置驱动摄像头注册表与检测职责徽标（ITEM-012）。
// 由 cameras.yaml/app.yaml 派生的视图槽位（保持配置顺序）、位置名、检测职责徽标
// （DetectionBadge，仅 P4/P6，司机 P1 不显示）与 NVR 集成项（地址/已登记/状态/通道映射=未配置）。
// 纯逻辑，可用单元测试驱动；QML 视图消费本模型。
//
// 仅依赖 C++ 标准库。

#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/navigation.h"

namespace railhmi {
namespace core {

struct CameraSlot {
    std::string id;
    std::string name;               // 位置显示名（来自配置）
    CameraPosition position = CameraPosition::Primary;
    bool ai_enabled = false;
    AiProfile ai_profile = AiProfile::None;
    std::string detection_badge;    // 检测职责徽标文本
    std::string stream_state;       // 运行时连接状态（配置无关；此处为初始占位）
    std::string rtsp_url;           // 不含凭据；凭据经 credential_env
    std::string credential_env;
    int order = 0;                  // 配置顺序（0 基）
};

// 检测职责徽标文本（视觉规范 §7.12）。
std::string detection_badge_for(AiProfile p);

// 徽标可见范围：仅 P4 全摄像头 / P6 设备维护；司机 P1 及他页不显示。
bool detection_badge_visible(Page page);

// 由配置派生注册表槽位（保持 cameras.yaml 顺序）。
std::vector<CameraSlot> build_registry(const AppSettings& settings);

struct IntegrationSlotConfig {
    std::string id;
    std::string name;
    std::string address;
    bool registered = false;
    std::string state;              // probing/reachable/unreachable/unconfigured/unverified
    std::string protocol;           // 未提供时为 "—"
    std::string channel_mapping;    // 未提供时为 "未配置"
};
// 由配置派生外部设备集成项（如 NVR）；通道映射未提供时显示“未配置”，不虚构。
std::vector<IntegrationSlotConfig> build_integrations(const AppSettings& settings);

}  // namespace core
}  // namespace railhmi
