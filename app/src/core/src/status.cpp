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
    out.reserve(7);

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

    // 六项固定顺序：时间/摄像头/录像/系统健康/车辆状态/门磁。
    // 模式位与 AI 位按现场需求删除；每一项的值由运行时真实注入，缺省一律未接入。
    auto cat = [&](TopItemKind kind, const char* key, const char* label, const CategoryStatus& s,
                   Page target, const char* secondary = "", bool always_clickable = false) {
        TopBarItem it;
        it.kind = kind;
        it.key = key;
        it.label = label;
        it.value = s.value.empty() ? status_level_label(s.level) : s.value;
        it.secondary = secondary;
        it.level = s.level;
        it.shape = status_shape(s.level);
        it.glyph = status_glyph(s.level);
        it.color = status_color(s.level);
        it.unavailable = s.level == StatusLevel::Unavailable;
        it.clickable = always_clickable || status_abnormal(s.level);
        it.target_page = target;
        out.push_back(it);
    };
    cat(TopItemKind::Camera, "camera", "摄像头", snap.camera, Page::AllCameras);
    cat(TopItemKind::Recording, "recording", "录像", snap.recording, Page::Replay);
    // AI 模型位：只报「接入/未接入」这一真实结论；模型名在 P5/P6 展示（顶栏宽度有限）。
    cat(TopItemKind::AiModel, "ai_model", "AI模型", snap.ai_model, Page::AiDebug);
    cat(TopItemKind::Health, "health", "系统健康", snap.health, Page::Maintenance);
    // 车辆状态无车速/GPS 数据源：点击顶栏该项由司机人工在 静止/运动/未接入 间切换。
    // 未选中时提示「点击切换」，选中后标注「人工置位」，不把人工输入伪装成传感器真值。
    cat(TopItemKind::VehicleState, "vehicle_state", "车辆状态", snap.vehicle_state,
        Page::Realtime,
        snap.vehicle_state.level == StatusLevel::Unavailable ? "点击切换" : "人工置位", true);
    // 门磁三态由调用方把 BLE 汇总映射为 开门/关门/传感器异常。
    cat(TopItemKind::DoorSensor, "door_sensor", "门磁", snap.door_sensor, Page::Maintenance);
    return out;
}

CategoryStatus camera_top_status(int live, int total) {
    if (total <= 0) return {StatusLevel::Unavailable, "未接入"};
    if (live < 0 || live > total) return {StatusLevel::Unavailable, "未接入"};
    const std::string value = std::to_string(live) + "/" + std::to_string(total) + " 路实时";
    if (live == total) return {StatusLevel::Ok, value};
    if (live == 0) return {StatusLevel::Alarm, value};
    return {StatusLevel::Warning, value};
}

CategoryStatus nvr_top_status(bool connected) {
    // 只有探测到真实连接才报正常；未配置/超时/网络错误一律 NVR异常。
    return connected ? CategoryStatus{StatusLevel::Ok, "NVR正常"}
                     : CategoryStatus{StatusLevel::Alarm, "NVR异常"};
}

CategoryStatus ai_model_top_status(bool worker_alive, int ready_lanes) {
    // 「接入」必须同时满足：推理工作线程真实存活 + 至少一路模型核验就绪。
    // 只加载了模型但线程没起（或反之）都算未接入，绝不因为配置里登记了模型就报接入。
    if (worker_alive && ready_lanes > 0) return {StatusLevel::Ok, "接入"};
    return {StatusLevel::Unavailable, "未接入"};
}

CategoryStatus door_top_status(const std::string& token) {
    if (token == "alarm") return {StatusLevel::Alarm, "开门"};
    if (token == "ok") return {StatusLevel::Ok, "关门"};
    // warning（低电量/未核验）、info、unavailable（未接入/超时/BlueZ 失败）都归为异常：
    // 这些状态下不能声称门已关，统一显示「传感器异常」并保持可点击查看详情。
    return {StatusLevel::Warning, "传感器异常"};
}

CategoryStatus vehicle_state_top_status(int mode) {
    switch (mode) {
        case 1: return {StatusLevel::Ok, "静止"};
        case 2: return {StatusLevel::Info, "运动"};
        default: return {StatusLevel::Unavailable, "未接入"};
    }
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
