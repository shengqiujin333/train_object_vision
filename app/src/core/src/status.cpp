#include "railhmi/core/status.h"

#include <array>

namespace railhmi {
namespace core {

const char* to_string(StatusLevel l) {
    switch (l) {
        case StatusLevel::Ok: return "ok";
        case StatusLevel::Info: return "info";
        case StatusLevel::Warning: return "warning";
        case StatusLevel::Alarm: return "alarm";
        case StatusLevel::Unavailable: return "unavailable";
    }
    return "unavailable";
}

const char* status_level_label(StatusLevel l) {
    switch (l) {
        case StatusLevel::Ok: return "正常";
        case StatusLevel::Info: return "信息";
        case StatusLevel::Warning: return "警告";
        case StatusLevel::Alarm: return "报警";
        case StatusLevel::Unavailable: return "未接入";
    }
    return "未接入";
}

// 形状字形：每个级别形状互不相同（去色后仍可区分）。
const char* status_shape(StatusLevel l) {
    switch (l) {
        case StatusLevel::Ok: return "dot";
        case StatusLevel::Info: return "spinner";
        case StatusLevel::Warning: return "triangle";
        case StatusLevel::Alarm: return "square";
        case StatusLevel::Unavailable: return "minus-circle";
    }
    return "minus-circle";
}

const char* status_glyph(StatusLevel l) {
    switch (l) {
        case StatusLevel::Ok: return "\u25CF";            // ●
        case StatusLevel::Info: return "\u21BB";          // ↻
        case StatusLevel::Warning: return "\u25B2";       // ▲
        case StatusLevel::Alarm: return "\u25A0";         // ■
        case StatusLevel::Unavailable: return "\u2296";   // ⊖
    }
    return "\u2296";
}

const char* status_color(StatusLevel l) {
    switch (l) {
        case StatusLevel::Ok: return "ok";
        case StatusLevel::Info: return "info";
        case StatusLevel::Warning: return "degraded";
        case StatusLevel::Alarm: return "error";
        case StatusLevel::Unavailable: return "neutral";
    }
    return "neutral";
}

bool status_abnormal(StatusLevel l) { return l == StatusLevel::Warning || l == StatusLevel::Alarm; }

const char* to_string(KeyCategory c) {
    switch (c) {
        case KeyCategory::Pantograph: return "pantograph";
        case KeyCategory::WheelRail: return "wheel_rail";
        case KeyCategory::Door: return "door";
        case KeyCategory::Camera: return "camera";
        case KeyCategory::Ai: return "ai";
        case KeyCategory::Recording: return "recording";
        case KeyCategory::Storage: return "storage";
        case KeyCategory::Count: break;
    }
    return "unknown";
}

const char* key_category_label(KeyCategory c) {
    switch (c) {
        case KeyCategory::Pantograph: return "受电弓";
        case KeyCategory::WheelRail: return "轮轨";
        case KeyCategory::Door: return "车门";
        case KeyCategory::Camera: return "摄像头";
        case KeyCategory::Ai: return "AI";
        case KeyCategory::Recording: return "录像";
        case KeyCategory::Storage: return "存储";
        case KeyCategory::Count: break;
    }
    return "未知";
}

Page key_category_page(KeyCategory c) {
    switch (c) {
        case KeyCategory::Pantograph:
        case KeyCategory::WheelRail:
        case KeyCategory::Door: return Page::AllCameras;      // P4 对应摄像头
        case KeyCategory::Camera: return Page::Maintenance;   // P6
        case KeyCategory::Ai: return Page::AiDebug;           // P5（按角色，不允许则提示无权限）
        case KeyCategory::Recording: return Page::Replay;     // P3
        case KeyCategory::Storage: return Page::Maintenance;  // P6
        case KeyCategory::Count: break;
    }
    return Page::Maintenance;
}

CategoryStatus KeyStatusSnapshot::get(KeyCategory c) const {
    switch (c) {
        case KeyCategory::Pantograph: return pantograph;
        case KeyCategory::WheelRail: return wheel_rail;
        case KeyCategory::Door: return door;
        case KeyCategory::Camera: return camera;
        case KeyCategory::Ai: return ai;
        case KeyCategory::Recording: return recording;
        case KeyCategory::Storage: return storage;
        case KeyCategory::Count: break;
    }
    return CategoryStatus{};
}

namespace {
KeyStatusItem make_item(KeyCategory c, const CategoryStatus& s) {
    KeyStatusItem it;
    it.category = c;
    it.key = to_string(c);
    it.label = key_category_label(c);
    it.level = s.level;
    it.value = s.value.empty() ? status_level_label(s.level) : s.value;
    it.shape = status_shape(s.level);
    it.glyph = status_glyph(s.level);
    it.color = status_color(s.level);
    it.target_page = key_category_page(c);
    it.abnormal = status_abnormal(s.level);
    it.clickable = it.abnormal;   // 仅异常项可点（角色在 resolve 时判定）
    return it;
}
}  // namespace

std::vector<KeyStatusItem> build_key_status(const KeyStatusSnapshot& snap, Role /*role*/) {
    const std::array<KeyCategory, 7> order = {
        KeyCategory::Pantograph, KeyCategory::WheelRail, KeyCategory::Door, KeyCategory::Camera,
        KeyCategory::Ai, KeyCategory::Recording, KeyCategory::Storage};
    std::vector<KeyStatusItem> out;
    out.reserve(order.size());
    for (KeyCategory c : order) out.push_back(make_item(c, snap.get(c)));
    return out;
}

std::vector<TopBarItem> build_top_bar(const TopBarSnapshot& snap, Role /*role*/) {
    std::vector<TopBarItem> out;

    TopBarItem time;
    time.kind = TopItemKind::Time;
    time.key = "time";
    time.label = "时间";
    time.value = snap.time_text;
    time.level = StatusLevel::Unavailable;   // 中性灰，不误读为异常
    time.shape = "none";
    time.glyph = "";
    time.color = "neutral";
    time.clickable = false;
    out.push_back(time);

    TopBarItem mode;
    mode.kind = TopItemKind::Mode;
    mode.key = "mode";
    mode.label = "模式";
    mode.value = "可见光";
    mode.secondary = "红外未接入";
    mode.level = StatusLevel::Unavailable;   // neutral chip，无告警色
    mode.shape = "dot";
    mode.glyph = status_glyph(StatusLevel::Ok);
    mode.color = "neutral";
    mode.clickable = false;
    out.push_back(mode);

    auto cat = [&](TopItemKind kind, const char* key, const char* label, const CategoryStatus& s,
                   Page target) {
        TopBarItem it;
        it.kind = kind;
        it.key = key;
        it.label = label;
        it.value = s.value.empty() ? status_level_label(s.level) : s.value;
        it.level = s.level;
        it.shape = status_shape(s.level);
        it.glyph = status_glyph(s.level);
        it.color = status_color(s.level);
        it.unavailable = s.level == StatusLevel::Unavailable;
        it.clickable = status_abnormal(s.level);
        it.target_page = target;
        out.push_back(it);
    };
    cat(TopItemKind::Recording, "recording", "录像", snap.recording, Page::Replay);
    cat(TopItemKind::Ai, "ai", "AI", snap.ai, Page::AiDebug);
    cat(TopItemKind::Health, "health", "系统健康", snap.health, Page::Maintenance);

    auto unconnected = [&](TopItemKind kind, const char* key, const char* label) {
        TopBarItem it;
        it.kind = kind;
        it.key = key;
        it.label = label;
        it.value = "未接入";
        it.level = StatusLevel::Unavailable;
        it.shape = status_shape(StatusLevel::Unavailable);
        it.glyph = status_glyph(StatusLevel::Unavailable);
        it.color = status_color(StatusLevel::Unavailable);
        it.unavailable = true;
        it.clickable = false;
        out.push_back(it);
    };
    unconnected(TopItemKind::VehicleSpeed, "vehicle_speed", "车速");
    unconnected(TopItemKind::DoorSensor, "door_sensor", "门磁");
    return out;
}

StatusClickResult resolve_status_click(const KeyStatusItem& item, Role role) {
    StatusClickResult r;
    r.clickable = item.clickable;
    r.target = item.target_page;
    if (!item.clickable) return r;
    r.allowed = role_can_access(role, item.target_page);
    return r;
}

StatusClickResult resolve_top_click(const TopBarItem& item, Role role) {
    StatusClickResult r;
    r.clickable = item.clickable;
    r.target = item.target_page;
    if (!item.clickable) return r;
    r.allowed = role_can_access(role, item.target_page);
    return r;
}

}  // namespace core
}  // namespace railhmi
