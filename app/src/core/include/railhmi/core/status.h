#pragma once

// railhmi-core: 关键状态区与顶部模式信息条（ITEM-016）。
// 关键状态区七类：受电弓 / 轮轨 / 车门 / 摄像头 / AI / 录像 / 存储。
// 顶部条（现场需求精简后）：时间 / 摄像头 / 录像 / AI模型 / 系统健康 / 车辆状态 / 门磁。
//   * 删除「模式（可见光·红外未接入）」与「AI」两项；
//   * 「车速」->「车辆状态」（静止/运动/未接入，人工可选，无数据源不臆测）；
//   * 新增「摄像头」（x/N 路实时）；「录像」按真实 NVR 连接显示 NVR正常/NVR异常；
//   * 「门磁」按开门/关门/传感器异常三态显示。
// 每项状态同时具备**颜色 + 形状字形 + 文字**三重编码（视觉规范 §3.4/§3.5/§7.6、交互 §9），
// 因此黑白（去色）下仍能靠形状与文字区分。异常项点击跳转详情，按角色校验（不允许则提示无权限）。
// 纯逻辑，无 Qt/GStreamer/相机依赖。

#include <string>
#include <vector>

#include "railhmi/core/navigation.h"

namespace railhmi {
namespace core {

// 状态级别（关键状态区/顶部条统一）。
enum class StatusLevel { Ok, Info, Warning, Alarm, Unavailable };

const char* to_string(StatusLevel l);
const char* status_level_label(StatusLevel l);  // 正常 / 信息 / 警告 / 报警 / 未接入
const char* status_shape(StatusLevel l);        // dot/spinner/triangle/square/minus-circle
const char* status_glyph(StatusLevel l);        // ● / ↻ / ▲ / ■ / ⊖（形状字形的文本表示）
const char* status_color(StatusLevel l);        // ok/info/degraded/error/neutral（令牌名）
bool status_abnormal(StatusLevel l);            // Warning 或 Alarm

// 关键状态区七类。
enum class KeyCategory { Pantograph, WheelRail, Door, Camera, Ai, Recording, Storage, Count };
const char* to_string(KeyCategory c);
const char* key_category_label(KeyCategory c);
Page key_category_page(KeyCategory c);          // 点击异常项时的详情目标页

struct CategoryStatus {
    StatusLevel level = StatusLevel::Unavailable;
    std::string value = "未接入";               // 值文字（正常简洁、异常突出）
};

struct KeyStatusSnapshot {
    CategoryStatus pantograph;
    CategoryStatus wheel_rail;
    CategoryStatus door;
    CategoryStatus camera;
    CategoryStatus ai;
    CategoryStatus recording;
    CategoryStatus storage;

    CategoryStatus get(KeyCategory c) const;
};

struct KeyStatusItem {
    KeyCategory category = KeyCategory::Pantograph;
    std::string key;
    std::string label;
    StatusLevel level = StatusLevel::Unavailable;
    std::string value;
    std::string shape;
    std::string glyph;
    std::string color;
    Page target_page = Page::Maintenance;
    bool abnormal = false;
    bool clickable = false;                     // 仅异常项可点（角色允许与否在 resolve 时判定）
};

// 固定顺序构建七项（受电弓→轮轨→车门→摄像头→AI→录像→存储）。
std::vector<KeyStatusItem> build_key_status(const KeyStatusSnapshot& snap, Role role);

// 顶部条。
// 「AI模型」位（2026-11-17 现场需求）：如实显示推理线程是否接入（接入/未接入），
// 模型名称在 AI 调试页与设备维护页展示；顶栏只给接入结论，避免宽度溢出。
enum class TopItemKind { Time, Camera, Recording, AiModel, Health, VehicleState, DoorSensor };

struct TopBarItem {
    TopItemKind kind = TopItemKind::Time;
    std::string key;
    std::string label;
    std::string value;
    std::string secondary;                      // 模式位的第二段（红外未接入）
    StatusLevel level = StatusLevel::Unavailable;
    std::string shape;
    std::string glyph;
    std::string color;
    bool unavailable = false;                   // 未接入项（车速/门磁）
    bool clickable = false;
    Page target_page = Page::Realtime;
};

struct TopBarSnapshot {
    std::string time_text = "--:--:--";         // 由调用方格式化
    CategoryStatus camera;                      // 摄像头接入路数
    CategoryStatus recording;                   // 录像（NVR 连接状态）
    CategoryStatus ai_model;                    // AI 模型接入（接入/未接入）
    CategoryStatus health;                      // 系统健康
    CategoryStatus vehicle_state;               // 车辆状态（静止/运动/未接入）
    CategoryStatus door_sensor;                 // 门磁（开门/关门/传感器异常）
};

// 固定顺序：时间/摄像头/录像/AI模型/系统健康/车辆状态/门磁（7 项）。
// 所有类别均由调用方注入真实值；缺省 CategoryStatus{} = 未接入/不可用，
// 未知状态绝不显示成正常。车辆状态位额外可点击（人工在 静止/运动 间切换）。
std::vector<TopBarItem> build_top_bar(const TopBarSnapshot& snap, Role role);

// 顶栏五类真值映射（纯逻辑，供 UI 桥接直接调用并单测，避免在 QML/C++ 里散落文案）：
//   * 摄像头：live/total 路实时，全部在线为 ok，部分离线为 warning，全部离线为 alarm；
//   * 录像：只按**真实 NVR 连接探测**结果给 NVR正常/NVR异常，未连上绝不显示正常；
//   * AI模型：只有推理工作线程真实启动且至少一路模型就绪才显示「接入」，否则「未接入」；
//   * 门磁：BLE 汇总 level 映射为 开门/关门/传感器异常 三态；
//   * 车辆状态：0=未接入、1=静止、2=运动（人工置位，非传感器真值）。
CategoryStatus camera_top_status(int live, int total);
CategoryStatus nvr_top_status(bool connected);
CategoryStatus ai_model_top_status(bool worker_alive, int ready_lanes);
CategoryStatus door_top_status(const std::string& summary_level_token);
CategoryStatus vehicle_state_top_status(int mode);

// 点击解析：正常项不可点；异常/未接入可点项按角色判定是否允许，不允许则界面提示无权限。
struct StatusClickResult {
    bool clickable = false;
    bool allowed = false;
    Page target = Page::Realtime;
};
StatusClickResult resolve_status_click(const KeyStatusItem& item, Role role);
StatusClickResult resolve_top_click(const TopBarItem& item, Role role);

}  // namespace core
}  // namespace railhmi
