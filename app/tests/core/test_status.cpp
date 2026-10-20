// railhmi-core 关键状态区与顶部模式信息条测试（ITEM-016 / 视觉 §3.4/§3.5/§7.6、交互 §9）。
// 覆盖：七类固定顺序、颜色/形状/文字三重编码在各级别互不相同（黑白可区分）、
// 正常简洁/异常突出、顶部条时间/模式/录像/AI/健康与未接入项、点击异常项按角色判定。

#include "test_util.h"

#include <set>
#include <string>
#include <vector>

#include "railhmi/core/status.h"

namespace {

using namespace railhmi::core;

void test_triple_encoding_distinct() {
    const StatusLevel levels[5] = {StatusLevel::Ok, StatusLevel::Info, StatusLevel::Warning,
                                   StatusLevel::Alarm, StatusLevel::Unavailable};
    std::set<std::string> shapes, glyphs, labels, colors;
    for (StatusLevel l : levels) {
        shapes.insert(status_shape(l));
        glyphs.insert(status_glyph(l));
        labels.insert(status_level_label(l));
        colors.insert(status_color(l));
    }
    railhmi_test::check(shapes.size() == 5, "5 levels have 5 distinct shapes");
    railhmi_test::check(glyphs.size() == 5, "5 levels have 5 distinct glyphs (B&W distinguishable)");
    railhmi_test::check(labels.size() == 5, "5 levels have 5 distinct labels");
    railhmi_test::check(colors.size() == 5, "5 levels have 5 distinct color tokens");
    railhmi_test::check(status_abnormal(StatusLevel::Warning) && status_abnormal(StatusLevel::Alarm),
                        "warning/alarm are abnormal");
    railhmi_test::check(!status_abnormal(StatusLevel::Ok) && !status_abnormal(StatusLevel::Unavailable),
                        "ok/unavailable are not abnormal");
}

void test_seven_items_order_and_default() {
    const std::vector<KeyStatusItem> v = build_key_status(KeyStatusSnapshot{}, Role::Maintainer);
    railhmi_test::check(v.size() == 7, "key status has 7 items");
    const char* expect[7] = {"pantograph", "wheel_rail", "door", "camera", "ai", "recording", "storage"};
    bool order = true;
    for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i].key != expect[i]) order = false;
    railhmi_test::check(order, "7 items in fixed order");
    bool all_unavail = true;
    for (const auto& it : v)
        if (it.level != StatusLevel::Unavailable || it.value != "未接入" || it.abnormal || it.clickable)
            all_unavail = false;
    railhmi_test::check(all_unavail, "default snapshot -> all 未接入, not abnormal, not clickable");
    railhmi_test::check(v[0].label == "受电弓" && v[6].label == "存储", "item labels");
}

void test_normal_simple_abnormal_prominent() {
    KeyStatusSnapshot n;
    n.pantograph = {StatusLevel::Ok, "正常接触"};
    n.wheel_rail = {StatusLevel::Ok, "正常"};
    n.door = {StatusLevel::Ok, "关闭"};
    n.camera = {StatusLevel::Ok, "6/6 在线"};
    n.ai = {StatusLevel::Ok, "运行"};
    n.recording = {StatusLevel::Ok, "录像中"};
    n.storage = {StatusLevel::Ok, "充足"};
    const std::vector<KeyStatusItem> all_ok = build_key_status(n, Role::Maintainer);
    bool none_abnormal = true, values_ok = true;
    for (const auto& it : all_ok) {
        if (it.abnormal || it.clickable) none_abnormal = false;
        if (it.color != "ok" || it.shape != "dot") values_ok = false;
    }
    railhmi_test::check(none_abnormal, "all-ok strip: nothing abnormal/clickable");
    railhmi_test::check(values_ok, "all-ok uses ok color + dot shape (简单)");

    KeyStatusSnapshot m = n;
    m.camera = {StatusLevel::Warning, "CAM-04 离线"};
    m.ai = {StatusLevel::Alarm, "前方 AI 不可用"};
    const std::vector<KeyStatusItem> mixed = build_key_status(m, Role::Maintainer);
    railhmi_test::check(mixed[3].abnormal && mixed[3].clickable && mixed[3].shape == "triangle" &&
                            mixed[3].color == "degraded",
                        "camera warning prominent (triangle/degraded)");
    railhmi_test::check(mixed[4].abnormal && mixed[4].clickable && mixed[4].shape == "square" &&
                            mixed[4].color == "error",
                        "ai alarm prominent (square/error)");
    railhmi_test::check(!mixed[0].abnormal && mixed[0].shape == "dot", "pantograph stays simple");
}

void test_target_pages() {
    KeyStatusSnapshot m;
    m.pantograph = {StatusLevel::Alarm, "异常"};
    m.wheel_rail = {StatusLevel::Alarm, "异常"};
    m.door = {StatusLevel::Warning, "未关严"};
    m.camera = {StatusLevel::Warning, "离线"};
    m.ai = {StatusLevel::Alarm, "不可用"};
    m.recording = {StatusLevel::Warning, "中断"};
    m.storage = {StatusLevel::Warning, "不足"};
    const std::vector<KeyStatusItem> v = build_key_status(m, Role::Maintainer);
    railhmi_test::check(v[0].target_page == Page::AllCameras, "pantograph -> P4");
    railhmi_test::check(v[1].target_page == Page::AllCameras, "wheel_rail -> P4");
    railhmi_test::check(v[2].target_page == Page::AllCameras, "door -> P4");
    railhmi_test::check(v[3].target_page == Page::Maintenance, "camera -> P6");
    railhmi_test::check(v[4].target_page == Page::AiDebug, "ai -> P5");
    railhmi_test::check(v[5].target_page == Page::Replay, "recording -> P3");
}

void test_click_permission() {
    KeyStatusSnapshot m;
    m.ai = {StatusLevel::Alarm, "不可用"};
    m.camera = {StatusLevel::Ok, "在线"};
    const std::vector<KeyStatusItem> v = build_key_status(m, Role::Maintainer);

    StatusClickResult driver_ai = resolve_status_click(v[4], Role::Driver);
    railhmi_test::check(driver_ai.clickable && driver_ai.allowed,
                        "driver clicking AI alarm opens AI debug");
    StatusClickResult maint_ai = resolve_status_click(v[4], Role::Maintainer);
    railhmi_test::check(maint_ai.clickable && maint_ai.allowed && maint_ai.target == Page::AiDebug,
                        "maintainer clicking AI alarm -> allowed to P5");
    StatusClickResult normal = resolve_status_click(v[3], Role::Maintainer);
    railhmi_test::check(!normal.clickable, "normal item is not clickable");
}

void test_top_bar() {
    TopBarSnapshot s;
    s.time_text = "12:34:56";
    s.recording = {StatusLevel::Ok, "录像中"};
    s.ai = {StatusLevel::Warning, "降级"};
    s.health = {StatusLevel::Ok, "正常"};
    const std::vector<TopBarItem> t = build_top_bar(s, Role::Maintainer);
    railhmi_test::check(t.size() == 7, "top bar has 7 items");
    const char* keys[7] = {"time", "mode", "recording", "ai", "health", "vehicle_speed", "door_sensor"};
    bool order = true;
    for (std::size_t i = 0; i < t.size(); ++i)
        if (t[i].key != keys[i]) order = false;
    railhmi_test::check(order, "top bar fixed order");
    railhmi_test::check(t[0].value == "12:34:56", "top bar shows time");
    railhmi_test::check(t[1].value == "可见光" && t[1].secondary == "红外未接入" && t[1].color == "neutral",
                        "mode bit: visible + IR not connected, neutral, no alarm color");
    railhmi_test::check(t[1].clickable == false, "mode bit has no toggle/click");
    railhmi_test::check(t[5].unavailable && t[5].value == "未接入" && t[6].unavailable && t[6].value == "未接入",
                        "vehicle speed / door sensor shown as 未接入");
    railhmi_test::check(t[3].clickable && t[3].shape == "triangle", "AI warning in top bar clickable");

    StatusClickResult r = resolve_top_click(t[3], Role::Driver);
    railhmi_test::check(r.clickable && r.allowed, "driver clicking AI warning opens AI debug");
}

}  // namespace

void run_status_tests() {
    test_triple_encoding_distinct();
    test_seven_items_order_and_default();
    test_normal_simple_abnormal_prominent();
    test_target_pages();
    test_click_permission();
    test_top_bar();
}
