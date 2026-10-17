#pragma once

// railhmi-core: 录像来源策略与显式状态（视频 ITEM-011 纯逻辑）。
//
// 依 app.yaml recording_source（local|nvr|both）与 NVR 集成配置解析**有效来源**：
// NVR 未配置（缺 protocol/channel_mapping）时**显式回落本机保护片段**，不静默。
// 状态以可观察字段（effective + reason）呈现，供诊断/UI 显示“NVR 未配置”。

#include <string>

#include "railhmi/core/config.h"

namespace railhmi {
namespace core {

// NVR 等外部设备是否配置完整（需要 protocol 与 channel_mapping）。
bool integration_is_configured(const IntegrationConfig& cfg);

enum class RecordingAvailability {
    LocalOnly,                       // 仅本机保护片段
    NvrOnly,                         // 仅 NVR 连续录像
    Both,                            // 本机 + NVR
    LocalFallbackNvrUnconfigured,    // 请求含 NVR 但未配置 -> 回落本机
    Unavailable,                     // 请求 NVR 且无本机回落能力（保留）
};
const char* to_string(RecordingAvailability v);

struct RecordingSourceStatus {
    RecordingSource requested = RecordingSource::Both;
    bool nvr_configured = false;
    RecordingAvailability effective = RecordingAvailability::LocalOnly;
    bool local_enabled = true;
    bool nvr_enabled = false;
    std::string reason;   // 例如 "NVR_UNCONFIGURED"；无异常时为空

    std::string describe() const;   // 一行可观察状态
};

// 解析有效录像来源；NVR 未配置时回落本机并给出显式 reason。
RecordingSourceStatus resolve_recording_source(RecordingSource requested, bool nvr_configured);

}  // namespace core
}  // namespace railhmi
