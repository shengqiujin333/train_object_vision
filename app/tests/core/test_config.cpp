// railhmi-core 配置与状态契约测试（ITEM-004 / TC-CORE-01）。
// 合法配置逐项断言；非法配置断言报出**具体字段路径**且不 ok。

#include "test_util.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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
    bool removed = false;
    bool first = true;
    while (std::getline(in, line)) {
        if (!removed && line.find(needle) != std::string::npos) {
            removed = true;
            continue;
        }
        if (!first) out << "\n";
        out << line;
        first = false;
    }
    if (!first) out << "\n";
    *s = out.str();
    return removed;
}

bool has_error_path(const ConfigLoadResult& r, const std::string& needle) {
    for (const auto& e : r.errors) {
        if (e.path.find(needle) != std::string::npos) return true;
    }
    return false;
}

std::string join_errors(const ConfigLoadResult& r) {
    std::string s;
    for (const auto& e : r.errors) s += " [" + e.str() + "]";
    return s;
}

struct Baseline {
    std::string cameras, app, thresholds, roi;
    bool loaded = false;
    std::string dir_error;
};

Baseline load_baseline_strings() {
    Baseline b;
    const std::string dir = RAILHMI_CONFIG_DIR;
    b.cameras = read_file(dir + "/cameras.yaml");
    b.app = read_file(dir + "/app.yaml");
    b.thresholds = read_file(dir + "/thresholds.yaml");
    b.roi = read_file(dir + "/roi.yaml");
    b.loaded = !b.cameras.empty() && !b.app.empty() && !b.thresholds.empty() && !b.roi.empty();
    if (!b.loaded) b.dir_error = "could not read config files under " + dir;
    return b;
}

void test_baseline_ok(const Baseline& b) {
    ConfigLoadResult r = load_config_from_strings(b.cameras, b.app, b.thresholds, b.roi);
    railhmi_test::check(r.ok, "baseline config loads without errors", join_errors(r));
    railhmi_test::check(r.settings.cameras.size() == 6, "baseline has 6 cameras",
                        std::to_string(r.settings.cameras.size()));

    const auto* cam01 = r.settings.cameras.empty() ? nullptr : &r.settings.cameras[0];
    if (cam01) {
        railhmi_test::check(cam01->id == "CAM-01" && cam01->position == CameraPosition::Primary,
                            "CAM-01 id/position=primary");
        railhmi_test::check(cam01->ai_enabled && cam01->ai_profile == AiProfile::Front,
                            "CAM-01 ai_enabled=true, ai_profile=front");
    }
    auto find_cam = [&](const std::string& id) -> const CameraConfig* {
        for (const auto& c : r.settings.cameras) if (c.id == id) return &c;
        return nullptr;
    };
    const CameraConfig* c2 = find_cam("CAM-02");
    const CameraConfig* c3 = find_cam("CAM-03");
    const CameraConfig* c4 = find_cam("CAM-04");
    const CameraConfig* c5 = find_cam("CAM-05");
    const CameraConfig* c6 = find_cam("CAM-06");
    railhmi_test::check(c2 && !c2->ai_enabled && c2->ai_profile == AiProfile::None,
                        "CAM-02 ai_enabled=false, ai_profile=none");
    railhmi_test::check(c3 && !c3->ai_enabled && c3->ai_profile == AiProfile::None,
                        "CAM-03 ai_enabled=false, ai_profile=none");
    railhmi_test::check(c4 && c4->ai_profile == AiProfile::Pantograph,
                        "CAM-04 ai_profile=pantograph");
    railhmi_test::check(c5 && c5->ai_profile == AiProfile::WheelRail,
                        "CAM-05 ai_profile=wheel_rail");
    railhmi_test::check(c6 && c6->ai_profile == AiProfile::CabDoor,
                        "CAM-06 ai_profile=cab_door");

    railhmi_test::check(r.settings.app.render_path == RenderPath::Cpu, "app.render_path=cpu");
    railhmi_test::check(r.settings.app.mode == ImagingMode::Visible, "app.mode=visible");
    railhmi_test::check(!mode_ir_connected(), "IR is not connected this phase");
    railhmi_test::check(r.settings.app.recording_source == RecordingSource::Both,
                        "app.recording_source=both");
    railhmi_test::check(r.settings.app.resolution_w == 1920 && r.settings.app.resolution_h == 1080,
                        "app.resolution=1920x1080");

    railhmi_test::check(r.settings.thresholds.confirm_frames == 3 &&
                            r.settings.thresholds.clip_post_seconds == 60,
                        "thresholds values parsed");

    railhmi_test::check(r.settings.roi.size() == 4, "baseline has 4 roi entries",
                        std::to_string(r.settings.roi.size()));

    // integration_state: NVR 已登记但缺 protocol/channel_mapping -> unconfigured
    railhmi_test::check(r.settings.app.integrations.size() == 1, "one integration (NVR)");
    if (!r.settings.app.integrations.empty()) {
        const IntegrationConfig& nvr = r.settings.app.integrations[0];
        railhmi_test::check(nvr.id == "NVR" && nvr.registered, "NVR registered");
        railhmi_test::check(initial_integration_state(nvr) == IntegrationState::Unconfigured,
                            "NVR initial integration_state=unconfigured");
        railhmi_test::check(std::string(to_string(initial_integration_state(nvr))) == "unconfigured",
                            "integration_state string is 'unconfigured'");
    }

    // applied_defaults: CAM-02 缺 tile_aspect -> 记入默认值（不静默）
    bool tile_default = false;
    for (const auto& d : r.settings.applied_defaults)
        if (d.find("cameras[1].tile_aspect") != std::string::npos) tile_default = true;
    railhmi_test::check(tile_default, "missing optional tile_aspect recorded in applied_defaults");
}

void test_negative(const Baseline& b) {
    {   // 缺失必填字段 -> 报具体字段 cameras[0].username
        std::string cams = b.cameras;
        erase_line_containing(&cams, "username: admin");
        ConfigLoadResult r = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
        railhmi_test::check(!r.ok && has_error_path(r, "cameras[0].username"),
                            "missing required field reports cameras[0].username", join_errors(r));
    }
    {   // 非法 transport -> cameras[0].transport
        std::string cams = b.cameras;
        replace_first(&cams, "transport: udp", "transport: tcp");
        ConfigLoadResult r = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
        railhmi_test::check(!r.ok && has_error_path(r, "cameras[0].transport"),
                            "invalid transport reports cameras[0].transport", join_errors(r));
    }
    {   // 非法 ai_profile -> cameras[0].ai_profile
        std::string cams = b.cameras;
        replace_first(&cams, "ai_profile: front", "ai_profile: bogus");
        ConfigLoadResult r = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
        railhmi_test::check(!r.ok && has_error_path(r, "cameras[0].ai_profile"),
                            "invalid ai_profile reports cameras[0].ai_profile", join_errors(r));
    }
    {   // ai_enabled=true 但 ai_profile=none -> 一致性错误
        std::string cams = b.cameras;
        replace_first(&cams, "ai_profile: front", "ai_profile: none");
        ConfigLoadResult r = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
        railhmi_test::check(!r.ok && has_error_path(r, "cameras[0].ai_profile"),
                            "ai_enabled/profile mismatch reports cameras[0].ai_profile",
                            join_errors(r));
    }
    {   // 缺失 thresholds 字段 -> thresholds.confirm_frames
        std::string th = b.thresholds;
        erase_line_containing(&th, "confirm_frames: 3");
        ConfigLoadResult r = load_config_from_strings(b.cameras, b.app, th, b.roi);
        railhmi_test::check(!r.ok && has_error_path(r, "thresholds.confirm_frames"),
                            "missing thresholds field reports thresholds.confirm_frames",
                            join_errors(r));
    }
    {   // 非法 render_path -> app.render_path
        std::string app = b.app;
        replace_first(&app, "render_path: cpu", "render_path: vulkan");
        ConfigLoadResult r = load_config_from_strings(b.cameras, app, b.thresholds, b.roi);
        railhmi_test::check(!r.ok && has_error_path(r, "app.render_path"),
                            "invalid render_path reports app.render_path", join_errors(r));
    }
    {   // 非法 mode -> app.mode
        std::string app = b.app;
        replace_first(&app, "mode: visible", "mode: thermal");
        ConfigLoadResult r = load_config_from_strings(b.cameras, app, b.thresholds, b.roi);
        railhmi_test::check(!r.ok && has_error_path(r, "app.mode"),
                            "invalid mode reports app.mode", join_errors(r));
    }
    {   // roi 引用未知相机 -> roi[0].camera
        std::string roi = b.roi;
        replace_first(&roi, "camera: CAM-01", "camera: CAM-99");
        ConfigLoadResult r = load_config_from_strings(b.cameras, b.app, b.thresholds, roi);
        railhmi_test::check(!r.ok && has_error_path(r, "roi[0].camera"),
                            "unknown roi camera reports roi[0].camera", join_errors(r));
    }
    {   // roi 越界 -> roi[0].w
        std::string roi = b.roi;
        replace_first(&roi, "w: 0.84", "w: 1.5");
        ConfigLoadResult r = load_config_from_strings(b.cameras, b.app, b.thresholds, roi);
        railhmi_test::check(!r.ok && has_error_path(r, "roi[0].w"),
                            "out-of-range roi w reports roi[0].w", join_errors(r));
    }
    {   // 缺失文件 -> 报文件名
        ConfigLoadResult r = load_config_dir("/tmp/railhmi_config_does_not_exist");
        railhmi_test::check(!r.ok && has_error_path(r, "cameras.yaml"),
                            "missing config file reports file name", join_errors(r));
    }
}

}  // namespace

void run_config_tests() {
    Baseline b = load_baseline_strings();
    if (!b.loaded) {
        railhmi_test::check(false, "baseline config files readable", b.dir_error);
        return;
    }
    test_baseline_ok(b);
    test_negative(b);
}
