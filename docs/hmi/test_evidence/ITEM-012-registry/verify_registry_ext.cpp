// ITEM-012 验证自有扩展夹具（software_verification, verifier-owned）。
// 断言范围在实现者 self-test 之外独立设计：
//   d0..d6   检测职责徽标全值身份 + 逐页可见性全真值表 + 仅改 ai_profile 徽标同步
//   r0..r6   注册表派生对配置的响应矩阵（改名/删首位/删中间/插入中间/全倒序 + 确定性）
//   n0..n4   NVR 集成项派生矩阵（baseline/补全 protocol+channel_mapping/
//            registered=false/无 integrations 段 applied_defaults）
//   b0..b2   缺失必填字段负例（CAM-06 rtsp_url / CAM-03 name / CAM-02 credential_env）
//            逐字路径判断，换实现者未测的槽位
// 纯模型层（railhmi-core），不依赖 Qt。
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "railhmi/core/camera_registry.h"
#include "railhmi/core/config.h"

#ifndef RAILHMI_CONFIG_DIR
#define RAILHMI_CONFIG_DIR "config"
#endif

namespace {

using namespace railhmi::core;
static int g_total = 0;
static int g_pass = 0;

void check(bool ok, const std::string& name, const std::string& detail = "") {
    ++g_total;
    if (ok) {
        ++g_pass;
        std::printf("[PASS] %s\n", name.c_str());
    } else {
        std::printf("[FAIL] %s%s%s\n", name.c_str(),
                    detail.empty() ? "" : " | ", detail.c_str());
        std::fflush(stdout);
    }
}

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool replace_first(std::string* s, const std::string& from, const std::string& to) {
    auto p = s->find(from);
    if (p == std::string::npos) return false;
    s->replace(p, from.size(), to);
    return true;
}

// 在 id 的 block 内，把以 field 开头的那一行整行替换为 new_line。
bool replace_field_line(std::string* s, const std::string& id, const std::string& field,
                        const std::string& new_line) {
    auto idp = s->find("id: " + id);
    if (idp == std::string::npos) return false;
    auto fp = s->find("    " + field + ":", idp);
    if (fp == std::string::npos) return false;
    auto eol = s->find('\n', fp);
    if (eol == std::string::npos) return false;
    s->replace(fp, eol - fp, new_line);
    return true;
}

// 在 block 内删除 field 行（b 负例用）。
bool erase_field_line(std::string* s, const std::string& id, const std::string& field) {
    auto idp = s->find("id: " + id);
    if (idp == std::string::npos) return false;
    auto fp = s->find("    " + field + ":", idp);
    if (fp == std::string::npos) return false;
    auto eol = s->find('\n', fp);
    if (eol == std::string::npos) return false;
    s->erase(fp, eol + 1 - fp);
    return true;
}

struct Baseline {
    std::string cameras, app, thresholds, roi;
    bool loaded = false;
};

Baseline baseline() {
    Baseline b;
    const std::string d = RAILHMI_CONFIG_DIR;
    b.cameras = read_file(d + "/cameras.yaml");
    b.app = read_file(d + "/app.yaml");
    b.thresholds = read_file(d + "/thresholds.yaml");
    b.roi = read_file(d + "/roi.yaml");
    b.loaded = !b.cameras.empty() && !b.app.empty() && !b.thresholds.empty() && !b.roi.empty();
    return b;
}

ConfigLoadResult load(const Baseline& b) {
    return load_config_from_strings(b.cameras, b.app, b.thresholds, b.roi);
}

bool has_error_path(const ConfigLoadResult& r, const std::string& path) {
    for (const auto& e : r.errors) {
        if (e.path == path) return true;
    }
    return false;
}

bool slot_equal(const CameraSlot& a, const CameraSlot& c) {
    return a.id == c.id && a.name == c.name && a.position == c.position &&
           a.ai_enabled == c.ai_enabled && a.ai_profile == c.ai_profile &&
           a.detection_badge == c.detection_badge && a.stream_state == c.stream_state &&
           a.rtsp_url == c.rtsp_url && a.credential_env == c.credential_env &&
           a.order == c.order;
}

void test_badges(const Baseline& b) {
    auto r = load(b);
    check(r.ok, "d0 baseline config loads");
    auto reg = build_registry(r.settings);
    check(reg.size() == 6, "d0b baseline 6 slots");

    check(detection_badge_for(AiProfile::Front) == "前方障碍/人员" &&
              detection_badge_for(AiProfile::Pantograph) == "受电弓三状态" &&
              detection_badge_for(AiProfile::WheelRail) == "轮轨" &&
              detection_badge_for(AiProfile::CabDoor) == "车门" &&
              detection_badge_for(AiProfile::None) == "广角·无检测",
          "d1 badge texts exact for all five profiles");
    check(detection_badge_for(AiProfile::Front) != detection_badge_for(AiProfile::Pantograph) &&
              detection_badge_for(AiProfile::Pantograph) != detection_badge_for(AiProfile::WheelRail) &&
              detection_badge_for(AiProfile::WheelRail) != detection_badge_for(AiProfile::CabDoor) &&
              detection_badge_for(AiProfile::CabDoor) != detection_badge_for(AiProfile::None) &&
              detection_badge_for(AiProfile::None) != detection_badge_for(AiProfile::Front),
          "d1b five badge texts pairwise distinct");

    // 仅改 ai_profile（同步 ai_enabled）徽标即变：CAM-02 none->front
    {
        std::string cams = b.cameras;
        bool e = replace_field_line(&cams, "CAM-02", "ai_profile", "    ai_profile: front") &&
                 replace_field_line(&cams, "CAM-02", "ai_enabled", "    ai_enabled: true");
        check(e, "d2a config edit applied (CAM-02 -> front)");
        auto r2 = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
        check(r2.ok, "d2b edited config loads");
        auto reg2 = build_registry(r2.settings);
        bool hit = false;
        for (auto& s : reg2) {
            if (s.id == "CAM-02") {
                hit = true;
                check(s.ai_enabled && s.ai_profile == AiProfile::Front &&
                          s.detection_badge == "前方障碍/人员",
                      "d2c CAM-02 badge follows edited ai_profile", s.detection_badge);
            }
        }
        check(hit, "d2d CAM-02 present in edited registry");
    }
    // 反向: CAM-01 front->none，徽标随 profile（不随任何其他字段）
    {
        std::string cams = b.cameras;
        check(replace_field_line(&cams, "CAM-01", "ai_profile", "    ai_profile: none") &&
                  replace_field_line(&cams, "CAM-01", "ai_enabled", "    ai_enabled: false"),
              "d2e config edit applied (CAM-01 -> none)");
        auto r2 = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
        check(r2.ok, "d2f edited config loads");
        auto reg2 = build_registry(r2.settings);
        for (auto& s : reg2) {
            if (s.id == "CAM-01") {
                check(!s.ai_enabled && s.ai_profile == AiProfile::None &&
                          s.detection_badge == "广角·无检测",
                      "d2g CAM-01 front->none badge sync", s.detection_badge);
            }
        }
    }

    check(!detection_badge_visible(Page::Realtime) &&
              !detection_badge_visible(Page::AlarmCenter) &&
              !detection_badge_visible(Page::Replay) &&
              !detection_badge_visible(Page::AiDebug) &&
              detection_badge_visible(Page::AllCameras) &&
              detection_badge_visible(Page::Maintenance),
          "d3 badge visibility full 6-page truth table (only P4/P6 true)");

    bool order_ok = reg.size() == 6;
    for (int i = 0; i < 6 && order_ok; ++i) {
        order_ok = reg[(size_t)i].id == ("CAM-0" + std::to_string(i + 1)) &&
                   reg[(size_t)i].order == i;
    }
    check(order_ok, "d4 baseline slot order FILE-ORDER + order continuity 0..5");

    check(reg.size() >= 2 && reg[0].stream_state == reg[1].stream_state &&
              reg[0].stream_state == "connecting",
          "d5 stream_state uniform placeholder (not config-driven)");

    bool m = reg.size() == 6;
    if (m) {
        bool ok = reg[0].detection_badge == "前方障碍/人员" &&
                  reg[1].detection_badge == "广角·无检测" &&
                  reg[2].detection_badge == "广角·无检测" &&
                  reg[3].detection_badge == "受电弓三状态" &&
                  reg[4].detection_badge == "轮轨" &&
                  reg[5].detection_badge == "车门";
        for (auto& s : reg) if (s.detection_badge != detection_badge_for(s.ai_profile)) ok = false;
        check(ok, "d6 per-slot badge matches profile for every slot");
    } else {
        check(false, "d6 per-slot badge matches profile (registry != 6 slots)");
    }
}

void test_registry_matrix(const Baseline& b) {
    // r1 改名 CAM-04 -> 反映在 slot.name；order/position 保留
    {
        std::string cams = b.cameras;
        check(replace_field_line(&cams, "CAM-04", "name", "    name: 顶棚检查相机"),
              "r1a config edit applied for CAM-04 name");
        auto r = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
        check(r.ok, "r1b edited config loads");
        auto reg = build_registry(r.settings);
        check(reg.size() == 6, "r1c still 6 slots");
        bool hit = false;
        for (auto& s : reg) {
            if (s.id == "CAM-04") {
                hit = s.name == "顶棚检查相机" && s.order == 3 &&
                      s.position == CameraPosition::Roof;
            }
        }
        check(hit, "r1d rename reflected, order/position preserved");
    }
    // r2 删首位（CAM-01）-> 5 slots，order 0..4 重编号
    {
        auto s0 = b.cameras.find("  - id: CAM-01");
        auto s1 = b.cameras.find("  - id: CAM-02");
        check(s0 != std::string::npos && s1 != std::string::npos && s1 > s0,
              "r2a CAM-01..02 block boundaries found");
        if (s0 != std::string::npos && s1 != std::string::npos) {
            std::string cams = b.cameras.substr(0, s0) + b.cameras.substr(s1);
            // roi.yaml 仍引用 CAM-01 会按设计拒绝（未知相机），负例轮已核；
            // 本轮只核注册表派生，roi 用空集。
            auto r = load_config_from_strings(cams, b.app, b.thresholds, "roi: []\n");
            check(r.ok, "r2b 5-camera config loads (roi: [])");
            auto reg = build_registry(r.settings);
            check(reg.size() == 5, "r2c remove first -> 5 slots", std::to_string(reg.size()));
            bool ok = reg.size() == 5;
            for (int i = 0; i < 5 && ok; ++i) {
                ok = reg[(size_t)i].id == ("CAM-0" + std::to_string(i + 2)) && reg[(size_t)i].order == i;
            }
            check(ok, "r2d CAM-02..06 with order renumbered 0..4");
        }
    }
    // r3 删中间（CAM-03）-> 相邻槽位直接拼接
    {
        auto s0 = b.cameras.find("  - id: CAM-03");
        auto s1 = b.cameras.find("  - id: CAM-04");
        check(s0 != std::string::npos && s1 != std::string::npos && s1 > s0,
              "r3a CAM-03..04 block boundaries found");
        if (s0 != std::string::npos && s1 != std::string::npos) {
            std::string cams = b.cameras.substr(0, s0) + b.cameras.substr(s1);
            auto r = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
            check(r.ok, "r3b config loads");
            auto reg = build_registry(r.settings);
            check(reg.size() == 5 && reg[1].id == "CAM-02" && reg[2].id == "CAM-04",
                  "r3c remove middle -> neighbors adjacent");
        }
    }
    // r4 插入 CAM-07 于 CAM-06 之前 -> 占据配置位置，后继 order 后移
    {
        std::string cams = b.cameras;
        std::string extra =
            "  - id: CAM-07\n    name: 备用相机\n    position: left\n"
            "    rtsp_url: \"rtsp://192.168.1.170:554/h264/ch1/main/av_stream\"\n"
            "    transport: udp\n    username: admin\n"
            "    credential_env: TRAIN_SIDE_RTSP_CREDENTIAL\n    spectrum: visible\n"
            "    ai_enabled: false\n    ai_profile: none\n    display_fps: 15\n";
        auto s = cams.find("  - id: CAM-06");
        check(s != std::string::npos, "r4a CAM-06 anchor found");
        if (s != std::string::npos) {
            std::string ins = cams.substr(0, s) + extra + cams.substr(s);
            auto r = load_config_from_strings(ins, b.app, b.thresholds, b.roi);
            check(r.ok, "r4b 7-camera config loads");
            auto reg = build_registry(r.settings);
            check(reg.size() == 7, "r4c insert-before -> 7 slots");
            check(reg.size() == 7 && reg[5].id == "CAM-07" && reg[5].order == 5 &&
                      reg[6].id == "CAM-06" && reg[6].order == 6,
                  "r4d inserted slot takes config position, successor shifted");
        }
    }
    // r5 六路完全倒序（完整字段相机配置）-> 注册表完全跟随
    {
        const char* ids[6] = {"CAM-06", "CAM-05", "CAM-04", "CAM-03", "CAM-02", "CAM-01"};
        const char* names[6] = {"司机室车门", "车底轮轨", "车顶受电弓", "车辆右侧", "车辆左侧", "车头前方"};
        const char* pos[6] = {"cab", "underfloor", "roof", "right", "left", "primary"};
        const char* urls[6] = {
            "rtsp://192.168.1.167:554/h264/ch1/main/av_stream",
            "rtsp://192.168.1.165:554/h264/ch1/main/av_stream",
            "rtsp://192.168.1.163:554/h264/ch1/main/av_stream",
            "rtsp://192.168.1.162:554/h264/ch1/main/av_stream",
            "rtsp://192.168.1.160:554/h264/ch1/main/av_stream",
            "rtsp://192.168.1.65:554/cam/realmonitor?channel=1&subtype=0"};
        const char* prof[6] = {"cab_door", "wheel_rail", "pantograph", "none", "none", "front"};
        const char* ae[6] = {"true", "true", "true", "false", "false", "true"};
        const char* fps[6] = {"15", "15", "15", "15", "15", "25"};
        bool rear_ok = false;
        for (int i = 0; i < 6; ++i) {
            (void)fps[i];
            rear_ok = true;
        }
        check(rear_ok, "r5a reversed fixture prepared");
        std::ostringstream s;
        s << "cameras:\n";
        for (int i = 0; i < 6; ++i) {
            s << "  - id: " << ids[i] << "\n    name: " << names[i] << "\n    position: " << pos[i]
              << "\n    rtsp_url: \"" << urls[i] << "\"\n    transport: udp\n    username: admin\n"
              << "    credential_env: TRAIN_SIDE_RTSP_CREDENTIAL\n    spectrum: visible\n"
              << "    ai_enabled: " << ae[i] << "\n    ai_profile: " << prof[i]
              << "\n    display_fps: " << fps[i] << "\n";
        }
        auto r = load_config_from_strings(s.str(), b.app, b.thresholds, "roi: []\n");
        check(r.ok, "r5b fully reversed config loads");
        auto reg = build_registry(r.settings);
        check(reg.size() == 6, "r5c reversed -> 6 slots");
        bool ok = reg.size() == 6;
        for (int i = 0; i < 6 && ok; ++i) {
            ok = reg[(size_t)i].id == ids[i] && reg[(size_t)i].order == i &&
                 reg[(size_t)i].name == names[i];
        }
        check(ok, "r5d registry follows fully reversed config order");
    }
    // r6 注册表派生确定性（同基线两轮逐字段相等）
    {
        auto a = build_registry(load(b).settings);
        auto c = build_registry(load(b).settings);
        bool eq = a.size() == c.size();
        for (size_t i = 0; i < a.size() && eq; ++i) eq = slot_equal(a[i], c[i]);
        check(eq, "r6 build_registry deterministic field-by-field");
    }
}

void test_integrations(const Baseline& b) {
    {
        auto r = load(b);
        check(r.ok, "n0 baseline loads");
        auto ints = build_integrations(r.settings);
        check(ints.size() == 1, "n0b one integration (NVR)");
        bool ok = ints.size() == 1;
        if (ok) {
            const auto& n = ints[0];
            ok = n.id == "NVR" && n.name == "网络录像机" && n.address == "192.168.1.4" &&
                 n.registered && n.state == "unconfigured" &&
                 n.channel_mapping == "未配置" && n.protocol == "—";
        }
        check(ok, "n0c baseline NVR full-field contract");
    }
    // 补足 protocol+channel_mapping -> 仍 registered，状态转 unverified（待探测），
    // 通道映射按契约只作布尔区分显示 已配置（camera_registry 不透传原值），非“未配置”。
    {
        std::string appc = b.app;
        check(replace_first(&appc, "      registered: true\n",
                            "      registered: true\n      protocol: \"rtsp\"\n"
                            "      channel_mapping: \"ch1..ch6\"\n"),
              "n1a protocol+channel_mapping added");
        auto r = load_config_from_strings(b.cameras, appc, b.thresholds, b.roi);
        check(r.ok, "n1b config with full integration loads");
        auto ints = build_integrations(r.settings);
        check(ints.size() == 1 && ints[0].state == "unverified" &&
                  ints[0].channel_mapping == "已配置" && ints[0].protocol == "rtsp",
              "n1c full integration -> unverified + channel_mapping flag=已配置",
              ints.empty() ? "" : ints[0].channel_mapping);
    }
    // registered:false -> unverified（未登记），channel_mapping 仍显示 未配置
    {
        std::string appc = b.app;
        check(replace_first(&appc, "      registered: true\n", "      registered: false\n"),
              "n2a registered=false edit");
        auto r = load_config_from_strings(b.cameras, appc, b.thresholds, b.roi);
        check(r.ok, "n2b config loads");
        auto ints = build_integrations(r.settings);
        check(ints.size() == 1 && !ints[0].registered && ints[0].state == "unverified" &&
                  ints[0].channel_mapping == "未配置",
              "n2c unregistered -> unverified + mapping still 未配置",
              ints.empty() ? "" : ints[0].state);
    }
    // 无 integrations 段 -> applied_defaults 记录 + 空注册表
    {
        auto s0 = b.app.find("  integrations:");
        check(s0 != std::string::npos, "n3a integrations section present in baseline");
        if (s0 != std::string::npos) {
            std::string cut = b.app.substr(0, s0);
            auto r = load_config_from_strings(b.cameras, cut, b.thresholds, b.roi);
            check(r.ok, "n3b no-integrations config loads");
            bool logged = false;
            for (auto& d : r.settings.applied_defaults) if (d == "app.integrations") logged = true;
            check(logged, "n3c applied_defaults records app.integrations");
            check(build_integrations(r.settings).empty(), "n3d integrations list empty");
        }
    }
}

void test_required_field_negatives(const Baseline& b) {
    // b1 CAM-06 缺 rtsp_url（实现者测的是 CAM-01；本轮换槽位, 逐字路径）
    {
        std::string cams = b.cameras;
        check(erase_field_line(&cams, "CAM-06", "rtsp_url"), "b1a CAM-06 rtsp_url line removed");
        auto r = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
        check(!r.ok && has_error_path(r, "cameras[5].rtsp_url"),
              "b1b CAM-06 missing rtsp_url -> cameras[5].rtsp_url");
    }
    // b2 CAM-03 缺 name（此前实现者未测 name 缺失路径）
    {
        std::string cams = b.cameras;
        check(erase_field_line(&cams, "CAM-03", "name"), "b2a CAM-03 name line removed");
        auto r = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
        check(!r.ok && has_error_path(r, "cameras[2].name"),
              "b2b CAM-03 missing name -> cameras[2].name");
    }
    // b3 CAM-02 缺 credential_env（实现者已测 cameras[0].username; 本轮换字段）
    {
        std::string cams = b.cameras;
        check(erase_field_line(&cams, "CAM-02", "credential_env"),
              "b3a CAM-02 credential_env line removed");
        auto r = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
        check(!r.ok && has_error_path(r, "cameras[1].credential_env"),
              "b3b CAM-02 missing credential_env -> cameras[1].credential_env");
    }
}

}  // namespace

int main() {
    Baseline b = baseline();
    if (!b.loaded) {
        check(false, "baseline config readable");
        std::printf("== extension fixtures ==\n== %d passed, %d failed ==\n", g_pass, g_total - g_pass);
        return g_pass == g_total ? 0 : 1;
    }
    test_badges(b);
    test_registry_matrix(b);
    test_integrations(b);
    test_required_field_negatives(b);
    std::printf("== extension fixtures ==\n== %d passed, %d failed ==\n", g_pass, g_total - g_pass);
    bool ok = (g_pass == g_total);
    std::printf("RESULT: %s passed=%d failed=%d\n", ok ? "PASS" : "FAIL", g_pass, g_total - g_pass);
    return ok ? 0 : 1;
}
