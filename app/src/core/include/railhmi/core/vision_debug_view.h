#pragma once

// railhmi-core: AI 调试视图数据与掩码叠加（视觉推理 ITEM-012）。
//
// 为 P5 提供**数据面**（Qt 渲染属桌面职责，本层不画）：每路 model_version / available /
// unavailable_reason / fps / latency / 最近结果 / 置信度 / 距离 / boxes / ROI，以及
// **仅在掩码真实可用时**提供的零拷贝掩码叠加载荷。
//
// 硬约束（VISION_DESIGN §8.3、INTERACTION §5.5、INTERFACE_CONTRACT §6）：
//   * 四个叠加开关（框/ROI/文字/掩码）**默认全部关闭**；
//   * 司机角色不显示 AI 调试页，也不得产生任何叠加数据（即使误开关）；
//   * 掩码叠加只在掩码真实可用（尺寸/stride/像素有效）时提供，否则只给不可用原因；
//   * 未知数值一律显示占位（"—"），**不得格式化为 0**。
//
// 纯标准库；不含 Qt/GStreamer/rknn。

#include <cstdint>
#include <string>
#include <vector>

#include "railhmi/core/camera_registry.h"   // CameraSlot
#include "railhmi/core/config.h"            // Role
#include "railhmi/core/inference.h"         // AiDebugChannel / AiRuntime
#include "railhmi/core/rules.h"             // RoiConfig

namespace railhmi {
namespace core {

// P5 调试叠加层四开关（框 / ROI / 文字 / 掩码）。
enum class DebugOverlay { Boxes = 0, Roi, Text, Mask };
const char* to_string(DebugOverlay v);
bool parse_debug_overlay(const std::string& s, DebugOverlay* out);
inline int debug_overlay_count() { return 4; }

struct OverlaySwitches {
    bool boxes = false;   // 默认关闭
    bool roi = false;     // 默认关闭
    bool text = false;    // 默认关闭
    bool mask = false;    // 默认关闭

    bool any() const { return boxes || roi || text || mask; }
    bool get(DebugOverlay o) const;
    void set(DebugOverlay o, bool v);
    // 已开启开关的稳定描述（证据用）："boxes,roi" 或 "none"。
    std::string describe() const;
};

// 解析 "boxes,roi,mask"（空串 -> 全关；未知名字返回 false）。
bool parse_overlay_switches(const std::string& spec, OverlaySwitches* out);

// 掩码叠加输入（零拷贝；调用方保证 plane 在本调用期间有效）。
struct OverlayMaskInput {
    bool available = false;
    std::string reason;                 // 不可用原因（SEG_UNAVAILABLE/NOT_READY/...）
    int width = 0;
    int height = 0;
    int stride_bytes = 0;
    const std::vector<std::uint8_t>* plane = nullptr;
    std::string kind = "rail";

    bool usable() const;                // 尺寸/stride/像素全有效
};

// 单路调试输入：复用 build_ai_debug 的行，再补叠加与格式化的原始数值。
struct DebugChannelInput {
    AiDebugChannel row;                 // model_version/available/unavailable_reason/fps/latency/boxes/distance_text/last_result_text
    bool has_roi = false;               // 该相机是否有 ROI（危险区/职责 ROI）
    RoiConfig roi;
    OverlayMaskInput mask;
    bool boxes_available = false;       // envelope ready（区别于“ready 但空框”）
    int box_count = 0;
};

struct DebugOverlayMask {
    int width = 0;
    int height = 0;
    int stride_bytes = 0;
    const std::vector<std::uint8_t>* plane = nullptr;   // 原图坐标二值 plane（0/非零）
    std::string kind = "rail";
    long long positive_pixels = 0;      // 供证据/调试统计（不改变像素本身）
};

struct DebugChannelView {
    AiDebugChannel row;
    // 未知占位文本（"—" 表示未知，绝不是 0）
    std::string model_version_text;
    std::string fps_text;
    std::string latency_text;
    std::string confidence_text;
    std::string distance_text;
    std::string last_result_text;
    std::string box_count_text;
    std::string unavailable_reason;     // 不可用时必须非空

    // 叠加层（仅当 visible && 对应开关开启 && 数据真实可用时提供）
    bool overlay_boxes = false;
    bool overlay_roi = false;
    bool overlay_text = false;
    bool overlay_mask = false;
    bool has_roi = false;
    RoiConfig roi;
    std::string roi_unavailable_reason;
    bool mask_available = false;
    std::string mask_unavailable_reason;
    DebugOverlayMask mask;              // 仅 overlay_mask=true 时有效
};

struct DebugViewInput {
    std::vector<CameraSlot> registry;
    AiRuntime runtime;
    std::vector<DebugChannelInput> channels;   // 与 registry 中 ai 路按 camera_id 匹配
    Role role = Role::Developer;
    OverlaySwitches switches;
    long long now_ms = 0;
    int stale_ms = 3000;
};

struct DebugViewModel {
    bool visible = false;              // 司机角色为 false
    std::string hidden_reason;
    OverlaySwitches switches;
    bool overlays_effective = false;   // = visible && switches.any()
    std::vector<DebugChannelView> channels;
};

// 组装 P5 数据面（不渲染）。
DebugViewModel build_debug_view(const DebugViewInput& in);

// 证据/诊断 JSON（含每路字段、叠加状态、掩码可用性与占位文本）。
std::string debug_view_json(const DebugViewModel& vm);

}  // namespace core
}  // namespace railhmi
