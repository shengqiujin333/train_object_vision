// railhmi-core 摄像头注册表与检测职责徽标测试（ITEM-012 / 交互 §7.3、视觉 §7.12）。
// 配置驱动槽位与顺序；检测职责徽标仅 P4/P6（司机 P1 不显示）；NVR 集成项登记（通道映射=未配置）。

#include "test_util.h"

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

bool erase_line_containing(std::string* s, const std::string& needle) {
    std::istringstream in(*s);
    std::ostringstream out;
    std::string line;
    bool removed = false, first = true;
    while (std::getline(in, line)) {
        if (!removed && line.find(needle) != std::string::npos) { removed = true; continue; }
        if (!first) out << "\n";
        out << line;
        first = false;
    }
    if (!first) out << "\n";
    *s = out.str();
    return removed;
}

// 删除 [start_marker, end_marker) 之间的行（含 start_marker 行，不含 end_marker 行）
bool erase_block(std::string* s, const std::string& start_marker, const std::string& end_marker) {
    std::istringstream in(*s);
    std::ostringstream out;
    std::string line;
    bool dropping = false, removed = false, first = true;
    while (std::getline(in, line)) {
        if (!dropping && line.find(start_marker) != std::string::npos) { dropping = true; removed = true; continue; }
        if (dropping && line.find(end_marker) != std::string::npos) { dropping = false; }
        if (dropping) continue;
        if (!first) out << "\n";
        out << line;
        first = false;
    }
    if (!first) out << "\n";
    *s = out.str();
    return removed;
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

void test_baseline_registry(const Baseline& b) {
    ConfigLoadResult r = load_config_from_strings(b.cameras, b.app, b.thresholds, b.roi);
    railhmi_test::check(r.ok, "baseline config loads");
    std::vector<CameraSlot> reg = build_registry(r.settings);
    railhmi_test::check(reg.size() == 6, "registry has 6 slots", std::to_string(reg.size()));
    if (reg.size() != 6) return;
    railhmi_test::check(reg[0].id == "CAM-01" && reg[5].id == "CAM-06", "registry order = CAM-01..06");
    railhmi_test::check(reg[0].order == 0 && reg[5].order == 5, "registry order field");
    railhmi_test::check(reg[0].position == CameraPosition::Primary && reg[1].position == CameraPosition::Left &&
                            reg[2].position == CameraPosition::Right && reg[3].position == CameraPosition::Roof &&
                            reg[4].position == CameraPosition::Underfloor && reg[5].position == CameraPosition::Cab,
                        "registry positions");
    railhmi_test::check(reg[0].ai_enabled && reg[0].ai_profile == AiProfile::Front, "CAM-01 ai front");
    railhmi_test::check(!reg[1].ai_enabled && reg[1].ai_profile == AiProfile::None, "CAM-02 no ai");
    railhmi_test::check(reg[0].detection_badge == "前方障碍/人员", "badge front");
    railhmi_test::check(reg[1].detection_badge == "广角·无检测", "badge none (side)");
    railhmi_test::check(reg[3].detection_badge == "受电弓三状态", "badge pantograph");
    railhmi_test::check(reg[4].detection_badge == "轮轨", "badge wheel rail");
    railhmi_test::check(reg[5].detection_badge == "车门", "badge cab door");
}

void test_badge_visibility() {
    railhmi_test::check(!detection_badge_visible(Page::Realtime), "badge hidden on P1 (driver realtime)");
    railhmi_test::check(detection_badge_visible(Page::AllCameras), "badge visible on P4");
    railhmi_test::check(detection_badge_visible(Page::Maintenance), "badge visible on P6");
    railhmi_test::check(!detection_badge_visible(Page::AlarmCenter) &&
                            !detection_badge_visible(Page::Replay) &&
                            !detection_badge_visible(Page::AiDebug),
                        "badge hidden on P2/P3/P5");
}

void test_nvr_integration(const Baseline& b) {
    ConfigLoadResult r = load_config_from_strings(b.cameras, b.app, b.thresholds, b.roi);
    std::vector<IntegrationSlotConfig> ints = build_integrations(r.settings);
    railhmi_test::check(ints.size() == 1, "one integration (NVR)");
    if (ints.empty()) return;
    railhmi_test::check(ints[0].id == "NVR" && ints[0].address == "192.168.1.4", "NVR id/address");
    railhmi_test::check(ints[0].registered, "NVR registered");
    railhmi_test::check(ints[0].state == "unconfigured", "NVR state = unconfigured", ints[0].state);
    railhmi_test::check(ints[0].channel_mapping == "未配置", "NVR channel mapping = 未配置", ints[0].channel_mapping);
    railhmi_test::check(ints[0].protocol == "—", "NVR protocol placeholder —");
}

void test_config_driven(const Baseline& b) {
    {   // 改名
        std::string cams = b.cameras;
        replace_first(&cams, "name: 车头前方", "name: 前向相机");
        ConfigLoadResult r = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
        auto reg = build_registry(r.settings);
        railhmi_test::check(!reg.empty() && reg[0].name == "前向相机", "rename reflected in registry");
    }
    {   // 删除一路（CAM-02）
        std::string cams = b.cameras;
        erase_block(&cams, "  - id: CAM-02", "  - id: CAM-03");
        ConfigLoadResult r = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
        auto reg = build_registry(r.settings);
        railhmi_test::check(reg.size() == 5, "remove camera -> 5 slots", std::to_string(reg.size()));
        bool has2 = false;
        for (auto& s : reg) if (s.id == "CAM-02") has2 = true;
        railhmi_test::check(!has2, "removed camera absent");
    }
    {   // 新增一路（CAM-07）
        std::string cams = b.cameras;
        cams += "  - id: CAM-07\n    name: 备用\n    position: left\n"
                "    rtsp_url: \"rtsp://192.168.1.170:554/h264/ch1/main/av_stream\"\n"
                "    transport: udp\n    username: admin\n"
                "    credential_env: TRAIN_SIDE_RTSP_CREDENTIAL\n    spectrum: visible\n"
                "    ai_enabled: false\n    ai_profile: none\n    display_fps: 15\n";
        ConfigLoadResult r = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
        auto reg = build_registry(r.settings);
        railhmi_test::check(reg.size() == 7 && reg[6].id == "CAM-07", "add camera -> 7 slots");
    }
    {   // 仅改配置的顺序（两路倒序）-> 注册表顺序跟随配置
        std::string two =
            "cameras:\n"
            "  - id: CAM-02\n    name: 左\n    position: left\n"
            "    rtsp_url: \"rtsp://192.168.1.160:554/h264/ch1/main/av_stream\"\n"
            "    transport: udp\n    username: admin\n"
            "    credential_env: TRAIN_SIDE_RTSP_CREDENTIAL\n    spectrum: visible\n"
            "    ai_enabled: false\n    ai_profile: none\n    display_fps: 15\n"
            "  - id: CAM-01\n    name: 前\n    position: primary\n"
            "    rtsp_url: \"rtsp://192.168.1.65:554/cam/realmonitor?channel=1&subtype=0\"\n"
            "    transport: udp\n    username: admin\n"
            "    credential_env: TRAIN_FRONT_RTSP_CREDENTIAL\n    spectrum: visible\n"
            "    ai_enabled: true\n    ai_profile: front\n    display_fps: 25\n";
        ConfigLoadResult r = load_config_from_strings(two, b.app, b.thresholds, "roi: []\n");
        railhmi_test::check(r.ok, "two-camera config loads");
        auto reg = build_registry(r.settings);
        railhmi_test::check(reg.size() == 2 && reg[0].id == "CAM-02" && reg[1].id == "CAM-01",
                            "registry order follows config order");
    }
    {   // 删除必填字段 -> 启动报出具体缺失项
        std::string cams = b.cameras;
        erase_line_containing(&cams, "rtsp_url:");
        ConfigLoadResult r = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
        bool hit = false;
        for (auto& e : r.errors) if (e.path.find("cameras[0].rtsp_url") != std::string::npos) hit = true;
        railhmi_test::check(!r.ok && hit, "missing required field reports cameras[0].rtsp_url");
    }
}

}  // namespace

void run_registry_tests() {
    Baseline b = baseline();
    if (!b.loaded) {
        railhmi_test::check(false, "registry baseline config readable");
        return;
    }
    test_baseline_registry(b);
    test_badge_visibility();
    test_nvr_integration(b);
    test_config_driven(b);
}
