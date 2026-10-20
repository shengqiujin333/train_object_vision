// railhmi-core AI 调试视图数据与掩码叠加测试（视觉推理 ITEM-012 纯逻辑）。
// 覆盖 TC-T-DBG-01..03：字段齐备/不可用原因显式、掩码叠加仅真实可用时提供且四开关默认关闭、
// 司机角色不可见、未知数值占位非 0。

#include <string>
#include <vector>

#include "test_util.h"

#include "railhmi/core/vision_debug_view.h"

namespace {

using namespace railhmi::core;

CameraSlot slot(const std::string& id, AiProfile p) {
    CameraSlot s;
    s.id = id;
    s.name = "camera-" + id;
    s.ai_enabled = true;
    s.ai_profile = p;
    return s;
}

AiRuntime runtime_for(const std::string& id, AiProfile p, bool alive = true) {
    AiRuntime rt;
    rt.worker_alive = alive;
    AiChannelRuntime ch;
    ch.camera_id = id;
    ch.profile = p;
    ch.last_result_ms = 1000;
    ch.fps = 5.0;
    ch.latency_ms = 140;
    ch.model_version = "rknn-2.3.2/yolov5s";
    rt.channels.push_back(ch);
    return rt;
}

DebugChannelInput channel(const std::string& id, AiProfile p, bool available) {
    DebugChannelInput c;
    c.row.camera_id = id;
    c.row.name = "camera-" + id;
    c.row.profile = p;
    c.row.profile_label = ai_profile_label(p);
    c.row.available = available;
    c.row.model_version = available ? "rknn-2.3.2/yolov5s" : "";
    c.row.fps = available ? 5.0 : 0.0;
    c.row.latency_ms = available ? 140 : 0;
    c.row.last_result_age_ms = available ? 120 : -1;
    c.row.last_result_text = available ? "0 秒前" : "—";
    c.row.max_confidence = available ? 0.87f : 0.0f;
    c.row.distance_text = "—";
    c.row.unavailable_reason = available ? "" : "PROFILE_UNSUPPORTED";
    c.boxes_available = available;
    c.box_count = available ? 1 : 0;
    if (available) {
        DetectionBox b;
        b.x = 0.4f;
        b.y = 0.5f;
        b.w = 0.1f;
        b.h = 0.2f;
        b.confidence = 0.87f;
        b.label = "person";
        b.in_roi = true;
        b.distance_m = -1.0f;
        c.row.boxes.push_back(b);
    }
    return c;
}

DebugViewInput input_for(Role role, const OverlaySwitches& sw) {
    DebugViewInput in;
    in.registry.push_back(slot("CAM-01", AiProfile::Front));
    in.registry.push_back(slot("CAM-04", AiProfile::Pantograph));
    in.runtime = runtime_for("CAM-01", AiProfile::Front);
    AiChannelRuntime p;
    p.camera_id = "CAM-04";
    p.profile = AiProfile::Pantograph;
    p.last_result_ms = 1000;
    in.runtime.channels.push_back(p);
    in.role = role;
    in.switches = sw;
    in.now_ms = 1200;
    in.channels.push_back(channel("CAM-01", AiProfile::Front, true));
    in.channels.push_back(channel("CAM-04", AiProfile::Pantograph, false));
    return in;
}

bool has_zero_text(const std::string& t) {
    return t == "0" || t == "0.0" || t == "0.00" || t == "0 ms" || t == "0.0 fps" ||
           t == "0.0 m";
}

}  // namespace

void run_vision_debug_view_tests() {
    using namespace railhmi_test;

    // ---- 四开关默认关闭 + 解析 ----
    {
        OverlaySwitches sw;
        check(!sw.boxes && !sw.roi && !sw.text && !sw.mask && !sw.any(),
              "debug_switches_default_all_off", "四开关默认关闭");
        check(sw.describe() == "none", "debug_switches_describe_none", sw.describe());

        OverlaySwitches p;
        std::string err;
        check(parse_overlay_switches("boxes,roi", &p), "debug_parse_switches_ok", "boxes,roi");
        check(p.boxes && p.roi && !p.text && !p.mask, "debug_parse_switches_values",
              p.describe());
        check(parse_overlay_switches("", &p) && !p.any(), "debug_parse_switches_empty_all_off",
              "空串 -> 全关");
        check(!parse_overlay_switches("bogus", &p), "debug_parse_switches_unknown_rejected",
              "未知开关名拒绝");
        check(parse_overlay_switches("none", &p) && !p.any(), "debug_parse_switches_none_explicit",
              "none -> 全关");
        check(parse_overlay_switches("off", &p) && !p.any(), "debug_parse_switches_off_explicit",
              "off -> 全关");
        check(parse_overlay_switches("mask,text", &p) && p.mask && p.text,
              "debug_parse_switches_mask_text", p.describe());
        p.set(DebugOverlay::Mask, false);
        check(!p.mask && p.text, "debug_switches_set", "可单独关闭");
    }

    // ---- 开关名 token ----
    {
        check(std::string(to_string(DebugOverlay::Boxes)) == "boxes" &&
                  std::string(to_string(DebugOverlay::Roi)) == "roi" &&
                  std::string(to_string(DebugOverlay::Text)) == "text" &&
                  std::string(to_string(DebugOverlay::Mask)) == "mask",
              "debug_overlay_tokens", "boxes/roi/text/mask");
        DebugOverlay ov = DebugOverlay::Boxes;
        check(parse_debug_overlay("mask", &ov) && ov == DebugOverlay::Mask,
              "debug_parse_overlay_mask", "mask");
        check(!parse_debug_overlay("person", &ov), "debug_parse_overlay_rejects", "person");
        check(debug_overlay_count() == 4, "debug_overlay_count_four", "四开关");
    }

    // ---- 司机同样可查看调试页，显式开关可开启叠加 ----
    {
        OverlaySwitches all;
        all.boxes = all.roi = all.text = all.mask = true;
        DebugViewModel vm = build_debug_view(input_for(Role::Driver, all));
        check(vm.visible, "debug_driver_visible", vm.hidden_reason);
        check(vm.channels.size() == 2, "debug_driver_channels", "下发真实调试视图数据");
        check(vm.overlays_effective, "debug_driver_overlays_enabled", "显式开关开启时产生叠加");
        check(vm.hidden_reason.empty(), "debug_driver_no_hidden_reason",
              vm.hidden_reason);
    }
    {
        OverlaySwitches off;
        for (Role r : {Role::Maintainer, Role::Developer, Role::Admin}) {
            DebugViewModel vm = build_debug_view(input_for(r, off));
            check(vm.visible, std::string("debug_ops_role_visible_") + to_string(r),
                  "维护/研发/管理员可见");
            check(!vm.overlays_effective, std::string("debug_ops_default_overlays_off_") +
                                              to_string(r),
                  "默认无叠加");
        }
    }

    // ---- 字段齐备 + 未知占位（TC-T-DBG-01/03） ----
    {
        OverlaySwitches off;
        DebugViewModel vm = build_debug_view(input_for(Role::Developer, off));
        check(vm.channels.size() == 2, "debug_channels_count", std::to_string(vm.channels.size()));
        const DebugChannelView& ok = vm.channels[0];
        check(ok.row.camera_id == "CAM-01" && ok.row.available, "debug_channel_available",
              ok.row.camera_id);
        check(ok.model_version_text == "rknn-2.3.2/yolov5s", "debug_model_version_text",
              ok.model_version_text);
        check(ok.fps_text == "5.0 fps", "debug_fps_text", ok.fps_text);
        check(ok.latency_text == "140 ms", "debug_latency_text", ok.latency_text);
        check(ok.confidence_text == "0.87", "debug_confidence_text", ok.confidence_text);
        check(ok.box_count_text == "1", "debug_box_count_text", ok.box_count_text);
        check(ok.distance_text == "—", "debug_distance_text_unknown", ok.distance_text);
        check(ok.last_result_text == "0 秒前", "debug_last_result_text", ok.last_result_text);
        check(ok.unavailable_reason.empty(), "debug_available_no_reason", "可用时无原因");

        const DebugChannelView& bad = vm.channels[1];
        check(!bad.row.available && bad.unavailable_reason == "PROFILE_UNSUPPORTED",
              "debug_unavailable_reason_explicit", bad.unavailable_reason);
        check(bad.model_version_text == "—" && bad.fps_text == "—" && bad.latency_text == "—" &&
                  bad.confidence_text == "—" && bad.box_count_text == "—" &&
                  bad.distance_text == "—",
              "debug_unknown_placeholders", "不可用路全部 '—'");
        check(!has_zero_text(bad.model_version_text) && !has_zero_text(bad.fps_text) &&
                  !has_zero_text(bad.latency_text) && !has_zero_text(bad.confidence_text) &&
                  !has_zero_text(bad.box_count_text) && !has_zero_text(bad.distance_text),
              "debug_unknown_not_zero", "未知不格式化为 0");

        const std::string j = debug_view_json(vm);
        check(j.find("\"visible\":true") != std::string::npos &&
                  j.find("\"switches_desc\":\"none\"") != std::string::npos &&
                  j.find("\"overlays_effective\":false") != std::string::npos,
              "debug_json_visible_switches", j.substr(0, 120));
        check(j.find("\"unavailable_reason\":\"PROFILE_UNSUPPORTED\"") != std::string::npos,
              "debug_json_unavailable_reason", "原因在 JSON 中可见");
        check(j.find("\"model_version\":\"—\"") != std::string::npos,
              "debug_json_unknown_placeholder", "未知识别为 —");
        check(j.find("\"mask\":{") == std::string::npos,
              "debug_json_no_mask_when_not_provided", "未提供掩码时无 mask 对象");
    }

    // ---- 掩码叠加：仅真实可用 + 开关开启时提供 ----
    {
        std::vector<std::uint8_t> plane(static_cast<std::size_t>(64) * 48, 0);
        for (std::size_t i = 0; i < plane.size(); ++i)
            if (i % 3 == 0) plane[i] = 1;   // 1/3 像素为正
        const long long expected_positive =
            static_cast<long long>(plane.size() / 3) + (plane.size() % 3 ? 1 : 0);

        // (a) 开关关闭：即使掩码可用也不提供叠加
        {
            OverlaySwitches off;
            DebugViewInput in = input_for(Role::Developer, off);
            in.channels[0].mask.available = true;
            in.channels[0].mask.reason = "READY";
            in.channels[0].mask.width = 64;
            in.channels[0].mask.height = 48;
            in.channels[0].mask.stride_bytes = 64;
            in.channels[0].mask.plane = &plane;
            DebugViewModel vm = build_debug_view(in);
            check(!vm.channels[0].overlay_mask, "debug_mask_switch_off_no_overlay",
                  "开关关闭不提供叠加");
            check(vm.channels[0].mask_available, "debug_mask_available_reported",
                  "可用性仍可观察");
            check(vm.channels[0].mask.plane == nullptr, "debug_mask_switch_off_no_plane",
                  "不下发 plane");
        }
        // (b) 开关开启 + 掩码真实可用 -> 提供零拷贝 plane
        {
            OverlaySwitches sw;
            sw.mask = true;
            DebugViewInput in = input_for(Role::Developer, sw);
            in.channels[0].mask.available = true;
            in.channels[0].mask.reason = "READY";
            in.channels[0].mask.width = 64;
            in.channels[0].mask.height = 48;
            in.channels[0].mask.stride_bytes = 64;
            in.channels[0].mask.plane = &plane;
            DebugViewModel vm = build_debug_view(in);
            const DebugChannelView& c = vm.channels[0];
            check(c.overlay_mask, "debug_mask_overlay_provided", "开关开+可用 -> 提供");
            check(c.mask.plane == &plane, "debug_mask_zero_copy", "零拷贝引用同一平面");
            check(c.mask.width == 64 && c.mask.height == 48 && c.mask.stride_bytes == 64,
                  "debug_mask_dimensions", "尺寸/stride");
            check(c.mask.positive_pixels > 0, "debug_mask_positive_pixels",
                  std::to_string(c.mask.positive_pixels));
            check(c.mask.positive_pixels <= expected_positive + 1,
                  "debug_mask_positive_pixels_bounded",
                  std::to_string(c.mask.positive_pixels));
            const std::string j = debug_view_json(vm);
            check(j.find("\"mask\":{\"width\":64") != std::string::npos &&
                      j.find("\"plane_present\":true") != std::string::npos,
                  "debug_json_mask_payload", j.substr(j.find("\"mask\":{"), 120));
        }
        // (c) 开关开启但掩码不可用 -> 不提供叠加，只给原因
        {
            OverlaySwitches sw;
            sw.mask = true;
            DebugViewInput in = input_for(Role::Developer, sw);
            in.channels[0].mask.available = false;
            in.channels[0].mask.reason = "SEG_UNAVAILABLE";
            DebugViewModel vm = build_debug_view(in);
            check(!vm.channels[0].overlay_mask, "debug_mask_unavailable_no_overlay",
                  "无掩码不叠加");
            check(vm.channels[0].mask_unavailable_reason == "SEG_UNAVAILABLE",
                  "debug_mask_unavailable_reason", vm.channels[0].mask_unavailable_reason);
            const std::string j = debug_view_json(vm);
            check(j.find("\"overlays\":{\"boxes\":false,\"roi\":false,\"text\":false,\"mask\":false}") !=
                      std::string::npos,
                  "debug_json_overlay_flags_false", "叠加标志为 false");
        }
        // (d) 开关开启、声称可用但 plane 尺寸不足 -> 视为不可用
        {
            OverlaySwitches sw;
            sw.mask = true;
            std::vector<std::uint8_t> tiny(10, 1);
            DebugViewInput in = input_for(Role::Developer, sw);
            in.channels[0].mask.available = true;
            in.channels[0].mask.reason = "READY";
            in.channels[0].mask.width = 64;
            in.channels[0].mask.height = 48;
            in.channels[0].mask.stride_bytes = 64;
            in.channels[0].mask.plane = &tiny;
            DebugViewModel vm = build_debug_view(in);
            check(!vm.channels[0].overlay_mask, "debug_mask_size_mismatch_rejected",
                  "plane 尺寸不足 -> 不叠加");
            check(!vm.channels[0].mask_unavailable_reason.empty(),
                  "debug_mask_size_mismatch_reason", vm.channels[0].mask_unavailable_reason);
        }
    }

    // ---- ROI 叠加：无 ROI 配置时不提供 ----
    {
        OverlaySwitches sw;
        sw.roi = true;
        DebugViewInput in = input_for(Role::Developer, sw);
        DebugViewModel vm = build_debug_view(in);
        check(!vm.channels[0].overlay_roi, "debug_roi_absent_no_overlay", "无 ROI 不叠加");
        check(vm.channels[0].roi_unavailable_reason == "未配置 ROI", "debug_roi_absent_reason",
              vm.channels[0].roi_unavailable_reason);

        in.channels[0].has_roi = true;
        in.channels[0].roi.camera = "CAM-01";
        in.channels[0].roi.name = "danger_zone";
        in.channels[0].roi.x = 0.08;
        in.channels[0].roi.y = 0.82;
        in.channels[0].roi.w = 0.84;
        in.channels[0].roi.h = 0.16;
        DebugViewModel vm2 = build_debug_view(in);
        check(vm2.channels[0].overlay_roi && vm2.channels[0].has_roi,
              "debug_roi_overlay_provided", "有 ROI + 开关 -> 提供");
        check(vm2.channels[0].roi.w > 0.83 && vm2.channels[0].roi.w < 0.85,
              "debug_roi_rect_values", "矩形来自配置");
        const std::string j = debug_view_json(vm2);
        check(j.find("\"roi_rect\":{\"x\":0.08") != std::string::npos, "debug_json_roi_rect",
              j.substr(j.find("\"roi_rect\""), 120));
    }

    // ---- 框/文字开关与 visible 组合 ----
    {
        OverlaySwitches sw;
        sw.boxes = true;
        sw.text = true;
        DebugViewModel vm = build_debug_view(input_for(Role::Developer, sw));
        check(vm.overlays_effective, "debug_overlays_effective_when_on", "开关开启且可见");
        check(vm.channels[0].overlay_boxes && vm.channels[0].overlay_text,
              "debug_boxes_text_overlays", "框/文字开关生效");
        check(!vm.channels[0].overlay_mask && !vm.channels[0].overlay_roi,
              "debug_mask_roi_off_by_default", "未开启的开关不产生叠加");
    }

    // ---- 空注册表 ----
    {
        DebugViewInput in;
        in.role = Role::Developer;
        in.now_ms = 1;
        DebugViewModel vm = build_debug_view(in);
        check(vm.visible && vm.channels.empty(), "debug_empty_registry_ok", "无相机时不崩溃");
        const std::string j = debug_view_json(vm);
        check(j.find("\"channels\":[]") != std::string::npos, "debug_json_empty_channels", "空数组");
    }
}
