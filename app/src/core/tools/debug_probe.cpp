// railhmi-vision-debug（视觉推理 ITEM-012）
// P5 数据面探针：每路 model_version/available/unavailableReason/fps/latency/最近结果/置信度/
// 距离/boxes/ROI + 掩码叠加数据；四开关默认关闭，司机角色不可见；未知数值占位（非 0）。
//
// 用法:
//   railhmi-vision-debug --selftest [--json out.json]
//   railhmi-vision-debug --fixture fields|unavailable|mask_on|mask_off|no_mask|driver
//   railhmi-vision-debug --config ./config --role developer|maintainer|admin|driver \
//       [--overlays boxes,roi,text,mask] [--mask-demo] [--json out.json]
//
// 退出码: 0 通过/一致；1 判据失败；2 参数错误。

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/vision_debug_view.h"

using namespace railhmi::core;

namespace {

int g_fail = 0;
std::ofstream g_fo;

void emit(const std::string& line) {
    std::printf("%s\n", line.c_str());
    std::fflush(stdout);
    if (g_fo) {
        g_fo << line << "\n";
        g_fo.flush();
    }
}

void check(bool ok, const std::string& name, const std::string& detail = "") {
    emit(std::string("[") + (ok ? "PASS" : "FAIL") + "] " + name +
         (detail.empty() ? "" : (" :: " + detail)));
    if (!ok) ++g_fail;
}

bool is_unknown_text(const std::string& t) { return t == "—"; }

CameraSlot mk_slot(const std::string& id, AiProfile p) {
    CameraSlot s;
    s.id = id;
    s.name = "camera-" + id;
    s.ai_enabled = true;
    s.ai_profile = p;
    return s;
}

struct Fixture {
    DebugViewInput input;
    std::vector<std::uint8_t> mask_plane;
};

Fixture base_fixture(Role role, const OverlaySwitches& sw) {
    Fixture f;
    f.input.registry.push_back(mk_slot("CAM-01", AiProfile::Front));
    f.input.registry.push_back(mk_slot("CAM-04", AiProfile::Pantograph));
    AiRuntime rt;
    rt.worker_alive = true;
    AiChannelRuntime a;
    a.camera_id = "CAM-01";
    a.profile = AiProfile::Front;
    a.last_result_ms = 1000;
    a.fps = 5.0;
    a.latency_ms = 142;
    a.model_version = "rknn-2.3.2/yolov5s+rknn-2.3.2/yolov8s-seg";
    rt.channels.push_back(a);
    AiChannelRuntime b;
    b.camera_id = "CAM-04";
    b.profile = AiProfile::Pantograph;
    rt.channels.push_back(b);
    f.input.runtime = rt;
    f.input.role = role;
    f.input.switches = sw;
    f.input.now_ms = 1200;

    DebugChannelInput ok;
    ok.row.camera_id = "CAM-01";
    ok.row.name = "camera-CAM-01";
    ok.row.profile = AiProfile::Front;
    ok.row.profile_label = ai_profile_label(AiProfile::Front);
    ok.row.available = true;
    ok.row.model_version = "rknn-2.3.2/yolov5s";
    ok.row.fps = 5.0;
    ok.row.latency_ms = 142;
    ok.row.last_result_age_ms = 120;
    ok.row.last_result_text = "0 秒前";
    ok.row.distance_text = "—";
    ok.boxes_available = true;
    ok.box_count = 1;
    DetectionBox box;
    box.x = 0.4f;
    box.y = 0.5f;
    box.w = 0.1f;
    box.h = 0.2f;
    box.confidence = 0.87f;
    box.label = "person";
    box.in_roi = true;
    box.distance_m = -1.0f;
    ok.row.boxes.push_back(box);
    ok.row.max_confidence = 0.87f;
    ok.has_roi = true;
    ok.roi.camera = "CAM-01";
    ok.roi.name = "danger_zone";
    ok.roi.x = 0.08;
    ok.roi.y = 0.82;
    ok.roi.w = 0.84;
    ok.roi.h = 0.16;
    f.input.channels.push_back(ok);

    DebugChannelInput bad;
    bad.row.camera_id = "CAM-04";
    bad.row.name = "camera-CAM-04";
    bad.row.profile = AiProfile::Pantograph;
    bad.row.profile_label = ai_profile_label(AiProfile::Pantograph);
    bad.row.available = false;
    bad.row.last_result_age_ms = -1;
    bad.row.last_result_text = "—";
    bad.row.distance_text = "—";
    bad.row.unavailable_reason = "PROFILE_UNSUPPORTED";
    bad.boxes_available = false;
    f.input.channels.push_back(bad);
    return f;
}

std::string scenario_json(const DebugViewModel& vm) { return debug_view_json(vm); }

int run_fixture(const std::string& name) {
    OverlaySwitches off;
    if (name == "fields") {
        const DebugViewModel vm = build_debug_view(base_fixture(Role::Developer, off).input);
        emit("type=view json=" + scenario_json(vm));
        const auto& c = vm.channels[0];
        const bool ok = vm.visible && c.model_version_text.find("yolov5s") != std::string::npos &&
                        c.fps_text == "5.0 fps" && c.latency_text == "142 ms" &&
                        c.confidence_text == "0.87" && c.box_count_text == "1" &&
                        c.distance_text == "—" && c.last_result_text == "0 秒前";
        emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") +
             " failures=" + std::to_string(ok ? 0 : 1));
        return ok ? 0 : 1;
    }
    if (name == "unavailable") {
        const DebugViewModel vm = build_debug_view(base_fixture(Role::Developer, off).input);
        const auto& c = vm.channels[1];
        const bool ok = !c.row.available && c.unavailable_reason == "PROFILE_UNSUPPORTED" &&
                        is_unknown_text(c.model_version_text) && is_unknown_text(c.fps_text) &&
                        is_unknown_text(c.latency_text) && is_unknown_text(c.confidence_text) &&
                        is_unknown_text(c.box_count_text) && is_unknown_text(c.distance_text);
        emit("type=view json=" + scenario_json(vm));
        emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") +
             " failures=" + std::to_string(ok ? 0 : 1));
        return ok ? 0 : 1;
    }
    if (name == "mask_on" || name == "mask_off" || name == "no_mask") {
        Fixture f = base_fixture(Role::Developer, off);
        if (name == "mask_on") {
            f.input.switches.mask = true;
            f.mask_plane.assign(static_cast<std::size_t>(64) * 48, 0);
            for (std::size_t i = 0; i < f.mask_plane.size(); i += 3) f.mask_plane[i] = 1;
            f.input.channels[0].mask.available = true;
            f.input.channels[0].mask.reason = "READY";
            f.input.channels[0].mask.width = 64;
            f.input.channels[0].mask.height = 48;
            f.input.channels[0].mask.stride_bytes = 64;
            f.input.channels[0].mask.plane = &f.mask_plane;
            const DebugViewModel vm = build_debug_view(f.input);
            const auto& c = vm.channels[0];
            const bool ok = c.overlay_mask && c.mask.plane == &f.mask_plane &&
                            c.mask.positive_pixels > 0 && c.mask.width == 64;
            emit("type=view json=" + scenario_json(vm));
            emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") +
                 " failures=" + std::to_string(ok ? 0 : 1));
            return ok ? 0 : 1;
        }
        if (name == "mask_off") {
            f.mask_plane.assign(static_cast<std::size_t>(64) * 48, 1);
            f.input.channels[0].mask.available = true;
            f.input.channels[0].mask.reason = "READY";
            f.input.channels[0].mask.width = 64;
            f.input.channels[0].mask.height = 48;
            f.input.channels[0].mask.stride_bytes = 64;
            f.input.channels[0].mask.plane = &f.mask_plane;
            const DebugViewModel vm = build_debug_view(f.input);
            const auto& c = vm.channels[0];
            const bool ok = !c.overlay_mask && c.mask.plane == nullptr && c.mask_available;
            emit("type=view json=" + scenario_json(vm));
            emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") +
                 " failures=" + std::to_string(ok ? 0 : 1));
            return ok ? 0 : 1;
        }
        // no_mask：开关开但无掩码
        f.input.switches.mask = true;
        f.input.channels[0].mask.available = false;
        f.input.channels[0].mask.reason = "SEG_UNAVAILABLE";
        const DebugViewModel vm = build_debug_view(f.input);
        const auto& c = vm.channels[0];
        const bool ok = !c.overlay_mask && c.mask_unavailable_reason == "SEG_UNAVAILABLE";
        emit("type=view json=" + scenario_json(vm));
        emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") +
             " failures=" + std::to_string(ok ? 0 : 1));
        return ok ? 0 : 1;
    }
    if (name == "driver") {
        OverlaySwitches all;
        all.boxes = all.roi = all.text = all.mask = true;
        const DebugViewModel vm = build_debug_view(base_fixture(Role::Driver, all).input);
        const bool ok = !vm.visible && vm.channels.empty() && !vm.overlays_effective &&
                        !vm.hidden_reason.empty();
        emit("type=view json=" + scenario_json(vm));
        emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") +
             " failures=" + std::to_string(ok ? 0 : 1));
        return ok ? 0 : 1;
    }
    std::fprintf(stderr, "unknown fixture: %s\n", name.c_str());
    return 2;
}

int run_selftest() {
    emit("type=selftest note=纯逻辑夹具（不加载 RKNN/不连相机）；四开关默认关闭、司机不可见、未知占位");

    OverlaySwitches off;
    check(!off.any(), "switches_default_off", off.describe());
    OverlaySwitches parsed;
    check(parse_overlay_switches("boxes,mask", &parsed) && parsed.boxes && parsed.mask &&
              !parsed.roi && !parsed.text,
          "switches_parse", parsed.describe());
    check(!parse_overlay_switches("nope", &parsed), "switches_parse_rejects", "未知开关名");

    const DebugViewModel dev = build_debug_view(base_fixture(Role::Developer, off).input);
    check(dev.visible && dev.channels.size() == 2, "developer_visible_two_channels",
          std::to_string(dev.channels.size()));
    check(!dev.overlays_effective, "default_no_overlays", "默认无叠加");
    check(dev.channels[0].model_version_text.find("yolov5s") != std::string::npos &&
              dev.channels[0].fps_text == "5.0 fps" &&
              dev.channels[0].latency_text == "142 ms" &&
              dev.channels[0].confidence_text == "0.87",
          "fields_present_and_formatted", dev.channels[0].fps_text);

    const auto& bad = dev.channels[1];
    check(!bad.row.available && bad.unavailable_reason == "PROFILE_UNSUPPORTED",
          "unavailable_reason_explicit", bad.unavailable_reason);
    check(is_unknown_text(bad.model_version_text) && is_unknown_text(bad.fps_text) &&
              is_unknown_text(bad.latency_text) && is_unknown_text(bad.confidence_text) &&
              is_unknown_text(bad.box_count_text) && is_unknown_text(bad.distance_text),
          "unknown_placeholders_not_zero", "全部 '—'");
    check(bad.model_version_text != "0" && bad.fps_text != "0.0 fps" && bad.latency_text != "0 ms",
          "unknown_not_zero_literal", "不是 0 的任何形式");

    OverlaySwitches all;
    all.boxes = all.roi = all.text = all.mask = true;
    const DebugViewModel drv = build_debug_view(base_fixture(Role::Driver, all).input);
    check(!drv.visible && drv.channels.empty() && !drv.overlays_effective,
          "driver_hidden_no_overlays", drv.hidden_reason);
    const DebugViewModel maint = build_debug_view(base_fixture(Role::Maintainer, off).input);
    check(maint.visible, "maintainer_visible", "维护可见");

    // 掩码：开关关闭 -> 不提供；开关开启 + 可用 -> 提供；开启但不可用 -> 只给原因
    Fixture f = base_fixture(Role::Developer, off);
    f.mask_plane.assign(static_cast<std::size_t>(64) * 48, 1);
    f.input.channels[0].mask.available = true;
    f.input.channels[0].mask.reason = "READY";
    f.input.channels[0].mask.width = 64;
    f.input.channels[0].mask.height = 48;
    f.input.channels[0].mask.stride_bytes = 64;
    f.input.channels[0].mask.plane = &f.mask_plane;
    const DebugViewModel m_off = build_debug_view(f.input);
    check(!m_off.channels[0].overlay_mask && m_off.channels[0].mask_available,
          "mask_switch_off_not_provided", "开关关闭即使可用也不叠加");

    f.input.switches.mask = true;
    const DebugViewModel m_on = build_debug_view(f.input);
    check(m_on.channels[0].overlay_mask &&
              m_on.channels[0].mask.plane == &f.mask_plane &&
              m_on.channels[0].mask.positive_pixels > 0,
          "mask_overlay_when_available", "零拷贝 + 正像素统计");

    Fixture nm = base_fixture(Role::Developer, all);
    nm.input.channels[0].mask.available = false;
    nm.input.channels[0].mask.reason = "SEG_UNAVAILABLE";
    const DebugViewModel m_none = build_debug_view(nm.input);
    check(!m_none.channels[0].overlay_mask &&
              m_none.channels[0].mask_unavailable_reason == "SEG_UNAVAILABLE",
          "mask_unavailable_no_overlay", m_none.channels[0].mask_unavailable_reason);
    check(m_none.channels[0].overlay_roi && m_none.channels[0].overlay_boxes,
          "roi_boxes_overlay_flags", "ROI/框开关生效");

    const std::string j = debug_view_json(dev);
    check(j.find("\"visible\":true") != std::string::npos &&
              j.find("\"switches_desc\":\"none\"") != std::string::npos &&
              j.find("\"unavailable_reason\":\"PROFILE_UNSUPPORTED\"") != std::string::npos &&
              j.find("\"model_version\":\"—\"") != std::string::npos,
          "json_fields_and_placeholders", j.substr(0, 120));
    check(j.find("\"mask\":{") == std::string::npos, "json_no_mask_when_not_provided",
          "未提供掩码时无 mask 对象");

    const bool ok = g_fail == 0;
    emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") + " failures=" + std::to_string(g_fail));
    return ok ? 0 : 1;
}

// 真实配置：用 cameras.yaml + roi.yaml 生成 P5 数据面（无推理结果 -> 全部不可用/未知）。
int run_config(const std::string& dir, Role role, const OverlaySwitches& sw, bool mask_demo,
               bool do_json, const std::string& json_path) {
    ConfigLoadResult cfg = load_config_dir(dir);
    if (!cfg.ok) {
        emit("type=error config invalid: " + dir);
        for (const auto& e : cfg.errors) emit("  " + e.str());
        return 1;
    }
    DebugViewInput in;
    in.registry = build_registry(cfg.settings);
    in.role = role;
    in.switches = sw;
    in.now_ms = 5000;
    in.runtime.worker_alive = true;
    std::vector<std::uint8_t> demo_mask;
    if (mask_demo) demo_mask.assign(static_cast<std::size_t>(1280) * 720, 0);
    for (const auto& s : in.registry) {
        if (!s.ai_enabled || !ai_eligible(s.ai_profile)) continue;
        AiChannelRuntime ch;
        ch.camera_id = s.id;
        ch.profile = s.ai_profile;
        in.runtime.channels.push_back(ch);
        DebugChannelInput c;
        c.row.camera_id = s.id;
        c.row.name = s.name;
        c.row.profile = s.ai_profile;
        c.row.profile_label = ai_profile_label(s.ai_profile);
        c.row.available = false;
        c.row.unavailable_reason = "等待结果";
        c.row.last_result_age_ms = -1;
        c.row.last_result_text = "—";
        c.row.distance_text = "—";
        const RoiConfig* roi = find_roi(cfg.settings.roi, s.id, "danger_zone");
        if (roi) {
            c.has_roi = true;
            c.roi = *roi;
        }
        if (mask_demo && s.id == "CAM-01") {
            for (std::size_t i = 0; i < demo_mask.size(); i += 7) demo_mask[i] = 1;
            c.mask.available = true;
            c.mask.reason = "DEMO_SYNTHETIC";
            c.mask.width = 1280;
            c.mask.height = 720;
            c.mask.stride_bytes = 1280;
            c.mask.plane = &demo_mask;
        }
        in.channels.push_back(c);
    }
    const DebugViewModel vm = build_debug_view(in);
    emit("type=config dir=" + dir + " role=" + to_string(role) +
         " overlays=" + sw.describe());
    emit("type=view json=" + debug_view_json(vm));
    check(vm.visible == ai_debug_visible(role), "config_role_visibility",
          std::string("visible=") + (vm.visible ? "true" : "false"));
    if (!vm.visible) {
        check(vm.channels.empty() && !vm.overlays_effective, "config_hidden_no_data",
              vm.hidden_reason);
    } else {
        bool all_unknown_ok = true;
        for (const auto& c : vm.channels) {
            if (c.row.available) continue;
            if (!is_unknown_text(c.model_version_text) || !is_unknown_text(c.fps_text) ||
                !is_unknown_text(c.latency_text) || !is_unknown_text(c.confidence_text) ||
                !is_unknown_text(c.distance_text))
                all_unknown_ok = false;
            if (c.unavailable_reason.empty()) all_unknown_ok = false;
        }
        check(all_unknown_ok, "config_unknown_and_reason", "无结果路：原因在、数值未知");
        const bool mask_expected = sw.mask && mask_demo;
        bool mask_ok = true;
        for (const auto& c : vm.channels) {
            if (c.row.camera_id == "CAM-01" && c.overlay_mask != mask_expected) mask_ok = false;
            if (c.row.camera_id != "CAM-01" && c.overlay_mask) mask_ok = false;
        }
        check(mask_ok, "config_mask_overlay_gate",
              std::string("mask_demo=") + (mask_demo ? "true" : "false"));
        bool roi_ok = true;
        for (const auto& c : vm.channels)
            if (c.overlay_roi != (sw.roi && c.has_roi)) roi_ok = false;
        check(roi_ok, "config_roi_overlay_gate", std::string("roi_switch=") +
                                                    (sw.roi ? "on" : "off"));
    }
    if (do_json && !json_path.empty()) {
        std::ofstream j(json_path, std::ios::trunc);
        if (j)
            j << "{\"view\":" << debug_view_json(vm) << ",\"failures\":" << g_fail
              << ",\"result\":\"" << (g_fail == 0 ? "PASS" : "FAIL") << "\"}\n";
    }
    const bool ok = g_fail == 0;
    emit(std::string("RESULT: ") + (ok ? "PASS" : "FAIL") + " failures=" + std::to_string(g_fail));
    return ok ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string out;
    std::string json;
    std::string fixture;
    std::string config;
    std::string overlays;
    std::string role_str = "developer";
    bool mask_demo = false;
    bool selftest = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) {
            if (i + 1 < argc) *d = argv[++i];
        };
        if (a == "--config") next(&config);
        else if (a == "--role") next(&role_str);
        else if (a == "--overlays") next(&overlays);
        else if (a == "--mask-demo") mask_demo = true;
        else if (a == "--fixture") next(&fixture);
        else if (a == "--selftest") selftest = true;
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--help" || a == "-h") {
            std::printf(
                "usage: railhmi-vision-debug --selftest [--out LOG] [--json JSON]\n"
                "       railhmi-vision-debug --fixture fields|unavailable|mask_on|mask_off|"
                "no_mask|driver\n"
                "       railhmi-vision-debug --config DIR [--role driver|maintainer|developer|admin] "
                "[--overlays boxes,roi,text,mask] [--mask-demo] [--json FILE]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    g_fo.open(out, std::ios::trunc);
    int rc = 0;
    if (!fixture.empty()) {
        rc = run_fixture(fixture);
    } else if (!config.empty()) {
        Role role = Role::Developer;
        if (!parse_role(role_str, &role)) {
            std::fprintf(stderr, "bad --role: %s\n", role_str.c_str());
            return 2;
        }
        OverlaySwitches sw;
        if (!parse_overlay_switches(overlays, &sw)) {
            std::fprintf(stderr, "bad --overlays: %s\n", overlays.c_str());
            return 2;
        }
        rc = run_config(config, role, sw, mask_demo, !json.empty(), json);
    } else {
        (void)selftest;
        rc = run_selftest();
    }
    if (!json.empty() && fixture.empty() && config.empty()) {
        std::ofstream j(json, std::ios::app);
        if (j) j << "{\"tool\":\"railhmi-vision-debug\",\"failures\":" << g_fail
                 << ",\"result\":\"" << (g_fail == 0 ? "PASS" : "FAIL") << "\"}\n";
    }
    return rc;
}
