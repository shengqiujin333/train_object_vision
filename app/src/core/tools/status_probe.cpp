// railhmi 关键状态区/顶部条探针（ITEM-016）
// 打印七类关键状态与顶部条每项的颜色/形状字形/文字三重编码，异常项点击按角色解析；
// 用于无 Qt 环境下核对“黑白仍可区分”与点击权限。纯 core，无 Qt/GStreamer。
//
// 用法: railhmi-status-probe [--scenario unavailable|normal|mixed] [--role driver|maintainer|developer|admin]
//                             [--out log]
// 退出码: 0 正常（含验收判定行）；2 参数错误。

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <set>
#include <string>
#include <vector>

#include "railhmi/core/status.h"

namespace {
using namespace railhmi::core;

std::string fmt(const char* s) { return s ? std::string(s) : std::string(); }

KeyStatusSnapshot mixed_snapshot() {
    KeyStatusSnapshot s;
    s.pantograph = {StatusLevel::Ok, "正常接触"};
    s.wheel_rail = {StatusLevel::Ok, "正常"};
    s.door = {StatusLevel::Warning, "车门未关严"};
    s.camera = {StatusLevel::Warning, "CAM-04 离线"};
    s.ai = {StatusLevel::Alarm, "前方 AI 不可用"};
    s.recording = {StatusLevel::Ok, "录像中"};
    s.storage = {StatusLevel::Warning, "剩余不足"};
    return s;
}
}  // namespace

int main(int argc, char** argv) {
    std::string scenario = "unavailable";
    std::string role_s = "maintainer";
    std::string out;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--scenario") next(&scenario);
        else if (a == "--role") next(&role_s);
        else if (a == "--out") next(&out);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-status-probe [--scenario unavailable|normal|mixed] "
                        "[--role driver|maintainer|developer|admin] [--out log]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    Role role = Role::Maintainer;
    if (!parse_role(role_s, &role)) {
        std::fprintf(stderr, "bad role: %s\n", role_s.c_str());
        return 2;
    }

    KeyStatusSnapshot snap;
    if (scenario == "mixed") snap = mixed_snapshot();
    else if (scenario == "normal") {
        snap.pantograph = {StatusLevel::Ok, "正常接触"};
        snap.wheel_rail = {StatusLevel::Ok, "正常"};
        snap.door = {StatusLevel::Ok, "关闭"};
        snap.camera = {StatusLevel::Ok, "6/6 在线"};
        snap.ai = {StatusLevel::Ok, "运行"};
        snap.recording = {StatusLevel::Ok, "录像中"};
        snap.storage = {StatusLevel::Ok, "充足"};
    }

    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) fo << line << "\n";
    };

    const std::vector<KeyStatusItem> items = build_key_status(snap, role);
    emit("type=status scenario=" + scenario + " role=" + to_string(role) +
         " items=" + std::to_string(items.size()));
    for (const auto& it : items) {
        const StatusClickResult r = resolve_status_click(it, role);
        emit("item key=" + it.key + " label=" + it.label + " level=" + to_string(it.level) +
             " shape=" + it.shape + " glyph=" + it.glyph + " color=" + it.color + " value=" + it.value +
             " abnormal=" + (it.abnormal ? "true" : "false") +
             " clickable=" + (it.clickable ? "true" : "false") +
             " target=" + to_string(it.target_page) +
             " allowed=" + (r.allowed ? "true" : "false"));
    }

    TopBarSnapshot ts;
    ts.time_text = "12:34:56";
    ts.recording = snap.recording;
    ts.ai = snap.ai;
    ts.health = (scenario == "mixed") ? CategoryStatus{StatusLevel::Warning, "温度偏高"}
                                      : CategoryStatus{StatusLevel::Ok, "正常"};
    const std::vector<TopBarItem> tops = build_top_bar(ts, role);
    emit("type=topbar scenario=" + scenario + " role=" + to_string(role) +
         " items=" + std::to_string(tops.size()));
    for (const auto& t : tops) {
        const StatusClickResult r = resolve_top_click(t, role);
        emit("item key=" + t.key + " label=" + t.label + " level=" + to_string(t.level) +
             " shape=" + t.shape + " glyph=" + t.glyph + " color=" + t.color + " value=" + t.value +
             (t.secondary.empty() ? "" : " secondary=" + t.secondary) +
             " unavailable=" + (t.unavailable ? "true" : "false") +
             " clickable=" + (t.clickable ? "true" : "false") +
             " target=" + to_string(t.target_page) +
             " allowed=" + (r.allowed ? "true" : "false"));
    }

    const StatusLevel levels[5] = {StatusLevel::Ok, StatusLevel::Info, StatusLevel::Warning,
                                   StatusLevel::Alarm, StatusLevel::Unavailable};
    std::set<std::string> shapes, glyphs, labels, colors;
    for (StatusLevel l : levels) {
        shapes.insert(fmt(status_shape(l)));
        glyphs.insert(fmt(status_glyph(l)));
        labels.insert(fmt(status_level_label(l)));
        colors.insert(fmt(status_color(l)));
    }
    emit("type=triple-encoding distinct_shapes=" + std::to_string(shapes.size()) +
         " distinct_glyphs=" + std::to_string(glyphs.size()) +
         " distinct_labels=" + std::to_string(labels.size()) +
         " distinct_colors=" + std::to_string(colors.size()));

    bool mode_ok = false, unconnected_ok = false;
    for (const auto& t : tops) {
        if (t.key == "mode")
            mode_ok = (t.value == "可见光" && t.secondary == "红外未接入" && !t.clickable);
        if (t.key == "vehicle_speed" || t.key == "door_sensor")
            unconnected_ok = unconnected_ok || (t.unavailable && t.value == "未接入");
    }
    const bool triple_ok = shapes.size() == 5 && glyphs.size() == 5 && labels.size() == 5 && colors.size() == 5;
    const bool ok = items.size() == 7 && tops.size() == 7 && triple_ok && mode_ok && unconnected_ok;
    emit("type=acceptance items=" + std::to_string(items.size()) +
         " top_items=" + std::to_string(tops.size()) +
         " triple_encoding_distinct=" + (triple_ok ? "true" : "false") +
         " mode_bit_ok=" + (mode_ok ? "true" : "false") +
         " unconnected_shown=" + (unconnected_ok ? "true" : "false") +
         " result=" + (ok ? "PASS" : "FAIL"));
    return ok ? 0 : 1;
}
