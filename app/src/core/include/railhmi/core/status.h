#pragma once

// railhmi-core: 关键状态区与顶部模式信息条（ITEM-016）。
// 关键状态区七类：受电弓 / 轮轨 / 车门 / 摄像头 / AI / 录像 / 存储。
// 顶部条：时间 / 模式（可见光·红外未接入）/ 录像 / AI / 系统健康 / 未接入项（车速、门磁）。
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
enum class TopItemKind { Time, Mode, Recording, Ai, Health, VehicleSpeed, DoorSensor };

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
    CategoryStatus recording;                   // 录像
    CategoryStatus ai;                          // AI
    CategoryStatus health;                      // 系统健康
};

// 固定顺序：时间/模式/录像/AI/系统健康/车速(未接入)/门磁(未接入)。
std::vector<TopBarItem> build_top_bar(const TopBarSnapshot& snap, Role role);

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
