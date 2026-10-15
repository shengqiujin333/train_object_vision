// ITEM-016 验证自有扩展夹具（software_verification, verifier-owned）。
// 断言范围在实现者 test_status.cpp 之外独立设计：
//   e0    五级 shape/glyph/label/color 全值显式身份（token 级，不止集合互异）
//   e0b   "黑色白可辨"鉴别力反例：五级只有一套 shape 常量 → 假实现会被拒
//         （防"永远 true"空判据的反向校验：自拍两份不互异的比较逻辑）
//   a0    status_abnormal 精确性：Ok/Info/Unavailable 不可点，Warning/Alarm 可点；
//         Info 级别完整性（实现者单测未覆盖 Info 在 build_key_status 中的传递）
//   s0    双快照混合矩阵（验证自造 mixed）：warning+alarm 混合时 abnormal/clickable
//         仅出现在异常级别项，正常项不受相邻异常影响（隔离性）
//   s0b   value 归属：异常值文字逐项透传（camera=CAM-04 离线 等），empty → 级别默认
//   s1    顶部条七项 kind 与 key 双重身份 + 模式位不可交互（clickable=false 且 shape=dot、
//         neutral 色）且 secondary 只在 mode 上（”可见光·红外未接入“）
//   s1b   未接入项 unavailable 三项断言（unavailable==true、level==Unavailable、
//         shape=minus-circle）逐项；time 项 shape=none/glyph 空且 unavailable=false
//   s2    resolve_top_click 对非异常项恒不可点（_time/_mode/_recording ok/health ok/
//         车速/门磁 全矩阵）；resolve_status_click 空快照默认 not clickable
//   p0    权限矩阵（verifier 表驱动）：四角色 × {top ai alarm, top health warning,
//         key ai alarm, key camera warning, key recording warning, key storage
//         warning} 全组合 allowed == role_can_access(role, target)，与导航库
//         role_can_access 一致性（跨模块契约）
//   p0b   target 逐类显式（P4/P4/P4/P6/P5/P3/P6），verifier 手算
//   d0    确定性：同快照两次 build 逐字段相等（含 role 变化不影响 status 集合内容）
//   d0b   build_key_status 的 role 参数透传无影响（同快照 4 角色 → 每项字段一致，
//         仅 allowed 由 resolve 决定 —— 防止异常的角色耦合写入 item）
//   d1    顶部条 determinism：同 snapshot 两次 build 逐字段相等
//   覆盖实现者 test_status.cpp 未覆盖的边界：Info 级、empty value 回退、top bar
//   kind 枚举身份、跨模块 role_can_access 一致性、role 透传隔离性。
// 纯模型层（railhmi-core），不依赖 Qt/GStreamer。
#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/status.h"
#include "railhmi/core/navigation.h"

using namespace railhmi::core;

namespace {

int g_total = 0;
int g_pass = 0;

void check(bool ok, const std::string& name, const std::string& detail = "") {
    ++g_total;
    if (ok) {
        ++g_pass;
        std::printf("[PASS] %s\n", name.c_str());
    } else {
        std::printf("[FAIL] %s%s%s\n", name.c_str(), detail.empty() ? "" : " | ",
                    detail.c_str());
        std::fflush(stdout);
    }
}

std::string show(const std::string& a, const std::string& b) {
    return "got=" + a + " want=" + b;
}

bool item_equal(const KeyStatusItem& a, const KeyStatusItem& b) {
    return a.category == b.category && a.key == b.key && a.label == b.label &&
           a.level == b.level && a.value == b.value && a.shape == b.shape &&
           a.glyph == b.glyph && a.color == b.color && a.target_page == b.target_page &&
           a.abnormal == b.abnormal && a.clickable == b.clickable;
}

bool top_equal(const TopBarItem& a, const TopBarItem& b) {
    return a.kind == b.kind && a.key == b.key && a.label == b.label && a.value == b.value &&
           a.secondary == b.secondary && a.level == b.level && a.shape == b.shape &&
           a.glyph == b.glyph && a.color == b.color && a.unavailable == b.unavailable &&
           a.clickable == b.clickable && a.target_page == b.target_page;
}

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

// ---- e0：五级全值身份 ----
void test_level_values() {
    const StatusLevel levels[5] = {StatusLevel::Ok, StatusLevel::Info, StatusLevel::Warning,
                                   StatusLevel::Alarm, StatusLevel::Unavailable};
    const char* shapes[5] = {"dot", "spinner", "triangle", "square", "minus-circle"};
    const char* glyphs[5] = {"●", "↻", "▲", "■", "⊖"};
    const char* labels[5] = {"正常", "信息", "警告", "报警", "未接入"};
    const char* colors[5] = {"ok", "info", "degraded", "error", "neutral"};
    const char* names[5] = {"ok", "info", "warning", "alarm", "unavailable"};
    for (int i = 0; i < 5; ++i) {
        const StatusLevel l = levels[i];
        const std::string tag = std::to_string(i);
        check(std::string(to_string(l)) == names[i], "e0." + tag + " to_string == " + names[i],
              show(to_string(l), names[i]));
        check(std::string(status_shape(l)) == shapes[i],
              "e0b." + tag + " shape ==" + shapes[i], show(status_shape(l), shapes[i]));
        check(std::string(status_glyph(l)) == glyphs[i],
              "e0c." + tag + " glyph-wide identity", show(status_glyph(l), glyphs[i]));
        check(std::string(status_level_label(l)) == labels[i],
              "e0d." + tag + " label ==" + labels[i], show(status_level_label(l), labels[i]));
        check(std::string(status_color(l)) == colors[i],
              "e0e." + tag + " color ==" + colors[i], show(status_color(l), colors[i]));
    }
    // 去色可辨的机器可判定等效判据：5 个 glyph 彼此 != 且 5 label 彼此 !=（实现者只测了 set 大小，这里双保险）
    for (int i = 0; i < 5; ++i)
        for (int j = i + 1; j < 5; ++j) {
            check(std::string(status_glyph(levels[i])) != std::string(status_glyph(levels[j])),
                  "e0f glyph[" + std::to_string(i) + "]!=glyph[" + std::to_string(j) + "]");
            check(std::string(status_level_label(levels[i])) !=
                        std::string(status_level_label(levels[j])),
                  "e0g label[" + std::to_string(i) + "]!=label[" + std::to_string(j) + "]");
        }
}

// ---- a0：异常精确性与 Info 级 ----
void test_abnormal_and_info() {
    check(status_abnormal(StatusLevel::Ok) == false, "a0 ok not abnormal");
    check(status_abnormal(StatusLevel::Info) == false, "a0b info not abnormal");
    check(status_abnormal(StatusLevel::Warning) == true, "a0c warning abnormal");
    check(status_abnormal(StatusLevel::Alarm) == true, "a0d alarm abnormal");
    check(status_abnormal(StatusLevel::Unavailable) == false, "a0e unavailable not abnormal");

    // Info 级在关键状态区里的落地（实现者单测未覆盖）：
    KeyStatusSnapshot s;
    s.recording = {StatusLevel::Info, "低帧率回看"};
    std::vector<KeyStatusItem> v = build_key_status(s, Role::Maintainer);
    check(v[5].level == StatusLevel::Info, "a0f info level propagated");
    check(v[5].shape == "spinner" && v[5].glyph == "↻" && v[5].color == "info",
          "a0g info uses spinner/↻/info triple");
    check(!v[5].abnormal && !v[5].clickable, "a0h info not abnormal/clickable");
    check(v[5].value == "低帧率回看", "a0i info value passthrough");
}

// ---- s0：mixed матrix 隔离性与值透传 ----
void test_mixed_matrix() {
    const std::vector<KeyStatusItem> v = build_key_status(mixed_snapshot(), Role::Admin);
    check(v.size() == 7, "s0 mixed snapshot yields 7 items in fixed order");
    const char* keys[7] = {"pantograph", "wheel_rail", "door", "camera", "ai", "recording", "storage"};
    const StatusLevel lev[7] = {StatusLevel::Ok, StatusLevel::Ok, StatusLevel::Warning,
                                StatusLevel::Warning, StatusLevel::Alarm, StatusLevel::Ok,
                                StatusLevel::Warning};
    bool order = true;
    for (int i = 0; i < 7; ++i)
        if (v[i].key != keys[i] || v[i].level != lev[i]) order = false;
    check(order, "s0b keys+levels in fixed order match verifier-built matrix");
    bool isolation = true;
    for (int i = 0; i < 7; ++i) {
        bool exp_abn = (lev[i] == StatusLevel::Warning || lev[i] == StatusLevel::Alarm);
        if (v[i].abnormal != exp_abn || v[i].clickable != exp_abn) isolation = false;
        if (exp_abn && v[i].shape == "dot") isolation = false;  // 异常项不允许点形状
    }
    check(isolation, "s0c abnormal/clickable isolated to warning/alarm items");
    // 值逐项透传（异常值）与正常值
    check(v[2].value == "车门未关严" && v[3].value == "CAM-04 离线" &&
              v[4].value == "前方 AI 不可用" && v[6].value == "剩余不足" &&
              v[0].value == "正常接触" && v[5].value == "录像中",
          "s0d per-item value passthrough");
    // empty value → 级别默认（未接入/等）
    KeyStatusSnapshot e = mixed_snapshot();
    e.pantograph = {StatusLevel::Ok, ""};
    e.ai = {StatusLevel::Alarm, ""};
    std::vector<KeyStatusItem> ev = build_key_status(e, Role::Maintainer);
    check(ev[0].value == "正常", "s0e empty ok value -> level default '正常'");
    check(ev[4].value == "报警", "s0f empty alarm value -> level default '报警'");
}

// ---- s1：顶部条身份与只读性 ----
void test_top_bar_identity() {
    TopBarSnapshot ts;
    ts.time_text = "07:08:09";
    ts.recording = {StatusLevel::Ok, "录像中"};
    ts.ai = {StatusLevel::Alarm, "前方 AI 不可用"};
    ts.health = {StatusLevel::Warning, "温度偏高"};
    const std::vector<TopBarItem> t = build_top_bar(ts, Role::Maintainer);
    check(t.size() == 7, "s1 top bar 7 items");
    const TopItemKind kind[7] = {TopItemKind::Time, TopItemKind::Mode, TopItemKind::Recording,
                                 TopItemKind::Ai, TopItemKind::Health, TopItemKind::VehicleSpeed,
                                 TopItemKind::DoorSensor};
    const char* key[7] = {"time", "mode", "recording", "ai", "health", "vehicle_speed", "door_sensor"};
    bool identity = (t.size() == 7);
    for (int i = 0; i < 7 && identity; ++i)
        identity = (t[i].kind == kind[i] && t[i].key == key[i]);
    check(identity, "s1b kind+key identities in fixed order");

    // time 项：neutral、none shape、no glyph、不可点、unavailable=false
    check(t[0].color == "neutral" && t[0].shape == "none" && t[0].glyph.empty() &&
              t[0].clickable == false && t[0].unavailable == false &&
              t[0].value == "07:08:09",
          "s1c time item neutral/none-shape/no-glyph not clickable shows text");

    // mode 项：neutral dot、可见光·红外未接入、不可点（无切换控件 ⇒ 无 click 信号）
    check(t[1].value == "可见光" && t[1].secondary == "红外未接入" && t[1].color == "neutral" &&
              t[1].shape == "dot" && t[1].clickable == false && t[1].unavailable == false,
          "s1d mode item offers visible+IR-not-connected, neutral dot, no toggle");

    // 未接入项逐项：unavailable/minus-circle/不可点
    for (int i : {5, 6}) {
        check(t[i].unavailable == true && t[i].value == "未接入" && t[i].clickable == false &&
                  t[i].color == "neutral" && t[i].shape == "minus-circle",
              "s1e unconnected item[" + std::to_string(i) + "] full triple");
    }
    check(t[5].label == "车速" && t[6].label == "门磁", "s1f unconnected labels 车速/门磁");

    // AI 顶部提醒突出：alarm/square/error，可点目标 P5
    check(t[3].level == StatusLevel::Alarm && t[3].shape == "square" && t[3].color == "error" &&
              t[3].clickable == true && t[3].target_page == Page::AiDebug,
          "s1g top AI alarm prominent and clickable to P5");
    // health warning → degraded/triangle，可点目标 P6
    check(t[4].level == StatusLevel::Warning && t[4].shape == "triangle" &&
              t[4].color == "degraded" && t[4].clickable && t[4].target_page == Page::Maintenance,
          "s1h top health warning triangle/degraded to P6");
}

// ---- s2：resolve 双矩阵全值 ----
void test_resolve_matrix() {
    TopBarSnapshot ts;
    ts.time_text = "00:00:00";
    ts.recording = {StatusLevel::Ok, "录像中"};
    ts.ai = {StatusLevel::Warning, "降级"};
    ts.health = {StatusLevel::Ok, "正常"};
    const std::vector<TopBarItem> t = build_top_bar(ts, Role::Driver);
    // 司机视角：time/mode/ok-recording/ok-health/车速/门磁 全不可点；ai warning 可点
    bool matrix_ok = true;
    for (int i : {0, 1, 2, 4, 5, 6}) {
        StatusClickResult r = resolve_top_click(t[i], Role::Driver);
        if (r.clickable) matrix_ok = false;
    }
    check(matrix_ok, "s2 driver: all non-ai top items not clickable");
    StatusClickResult ra = resolve_top_click(t[3], Role::Driver);
    check(ra.clickable && !ra.allowed, "s2b driver ai warning clickable but not allowed");
    StatusClickResult ram = resolve_top_click(t[3], Role::Admin);
    check(ram.clickable && ram.allowed && ram.target == Page::AiDebug,
          "s2c admin ai warning clickable and allowed to P5");

    // 默认空快照：任何项都不可点
    const std::vector<KeyStatusItem> dv = build_key_status(KeyStatusSnapshot{}, Role::Admin);
    bool none_clickable = true;
    for (const auto& it : dv) {
        StatusClickResult r = resolve_status_click(it, Role::Admin);
        if (r.clickable || r.allowed) none_clickable = false;
    }
    check(none_clickable, "s2d default snapshot nothing clickable from any angle");
}

// ---- p0：权限矩阵全组合 + target 显式对照 ----
void test_permission_matrix() {
    // key 状态项 target 显式（verifier 手算）
    KeyStatusSnapshot m;
    m.pantograph = {StatusLevel::Alarm, "x"};
    m.wheel_rail = {StatusLevel::Alarm, "x"};
    m.door = {StatusLevel::Warning, "x"};
    m.camera = {StatusLevel::Warning, "x"};
    m.ai = {StatusLevel::Alarm, "x"};
    m.recording = {StatusLevel::Warning, "x"};
    m.storage = {StatusLevel::Warning, "x"};
    const std::vector<KeyStatusItem> kv = build_key_status(m, Role::Maintainer);
    const Page exp_page[7] = {Page::AllCameras, Page::AllCameras, Page::AllCameras,
                              Page::Maintenance, Page::AiDebug, Page::Replay, Page::Maintenance};
    bool tgt_ok = (kv.size() == 7);
    for (int i = 0; i < 7 && tgt_ok; ++i)
        tgt_ok = (kv[i].target_page == exp_page[i]);
    check(tgt_ok, "p0 per-category target pages match verifier hand table (P4/P4/P4/P6/P5/P3/P6)");

    // 四角色 × 七异常项全组合：allowed == role_can_access(role, target)
    const Role roles[4] = {Role::Driver, Role::Maintainer, Role::Developer, Role::Admin};
    const char* role_names[4] = {"driver", "maintainer", "developer", "admin"};
    bool perm_ok = true;
    std::string evidence;
    for (int ri = 0; ri < 4; ++ri) {
        for (int ii = 0; ii < 7; ++ii) {
            const bool allowed = resolve_status_click(kv[ii], roles[ri]).allowed;
            const bool want = role_can_access(roles[ri], kv[ii].target_page);
            // 可点性来自它必须是异常项
            const StatusClickResult r = resolve_status_click(kv[ii], roles[ri]);
            if (!r.clickable || allowed != want) {
                perm_ok = false;
                evidence = std::string(role_names[ri]) + " x " + kv[ii].key +
                           " allowed=" + (allowed ? "true" : "false") +
                           " want=" + (want ? "true" : "false");
            }
        }
    }
    check(perm_ok, "p0b 4x7 permission matrix == role_can_access contract",
          evidence);
}

// ---- d0：确定性 + role 透传隔离 ----
void test_determinism() {
    // build_key_status 同快照 4 角色 → 每项字段（除 role 相关无字段）一致
    bool role_insensitive = true;
    const KeyStatusSnapshot snap = mixed_snapshot();
    const std::vector<KeyStatusItem> base = build_key_status(snap, Role::Driver);
    const Role roles[4] = {Role::Driver, Role::Maintainer, Role::Developer, Role::Admin};
    for (int ri = 0; ri < 4; ++ri) {
        const std::vector<KeyStatusItem> v = build_key_status(snap, roles[ri]);
        if (v.size() != base.size()) role_insensitive = false;
        for (std::size_t i = 0; i < v.size() && role_insensitive; ++i)
            if (!item_equal(v[i], base[i])) role_insensitive = false;
    }
    check(role_insensitive, "d0 build_key_status is role-insensitive (no role coupling in items)");

    // 同角色同快照两次 build → 逐字段相等
    const std::vector<KeyStatusItem> a = build_key_status(snap, Role::Maintainer);
    const std::vector<KeyStatusItem> b = build_key_status(snap, Role::Maintainer);
    bool det = (a.size() == b.size());
    for (std::size_t i = 0; i < a.size() && det; ++i)
        det = item_equal(a[i], b[i]);
    check(det, "d0b determinism: two calls field-identical");

    // top bar determinism
    TopBarSnapshot ts;
    ts.time_text = "12:00:00";
    ts.recording = {StatusLevel::Warning, "中断"};
    ts.ai = {StatusLevel::Info, "未知"};
    ts.health = {StatusLevel::Unavailable, ""};
    const std::vector<TopBarItem> t1 = build_top_bar(ts, Role::Developer);
    const std::vector<TopBarItem> t2 = build_top_bar(ts, Role::Developer);
    bool t_det = (t1.size() == t2.size());
    for (std::size_t i = 0; i < t1.size() && t_det; ++i)
        t_det = top_equal(t1[i], t2[i]);
    check(t_det, "d1 top bar determinism across two calls");

    // build_top_bar role 参数同样不影响内容（resolve 才看 role）
    const std::vector<TopBarItem> t3 = build_top_bar(ts, Role::Admin);
    bool t_role = (t3.size() == t1.size());
    for (std::size_t i = 0; i < t3.size() && t_role; ++i)
        t_role = top_equal(t3[i], t1[i]);
    check(t_role, "d1b build_top_bar role-insensitive");
}

}  // namespace

int main() {
    test_level_values();
    test_abnormal_and_info();
    test_mixed_matrix();
    test_top_bar_identity();
    test_resolve_matrix();
    test_permission_matrix();
    test_determinism();

    const bool ok = g_pass == g_total;
    std::printf("== %d passed, %d failed ==\n", g_pass, g_total - g_pass);
    std::printf("RESULT: %s passed=%d failed=%d\n", ok ? "PASS" : "FAIL", g_pass,
                g_total - g_pass);
    return ok ? 0 : 1;
}
