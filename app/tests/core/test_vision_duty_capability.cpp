// railhmi-core 其它职责可用性语义测试（视觉推理 ITEM-010 纯逻辑）。
// 覆盖 TC-T-MIS-01..04：无专用模型 -> unavailable + reason + unknown；unknown 永不映射 normal；
// 提供并核验声明模型后才 ready 且按声明标签映射状态；仅 ai_profile=none 永不推断。

#include <string>
#include <vector>

#include "test_util.h"

#include "railhmi/core/vision_duty_capability.h"

namespace {

using namespace railhmi::core;

TensorDesc input_tensor(int h, int w, TensorType type = TensorType::Int8, TensorFormat fmt = TensorFormat::Nhwc) {
    TensorDesc t;
    t.dir = TensorDir::Input;
    t.index = 0;
    t.name = "images";
    t.n_dims = 4;
    t.dims = {1, static_cast<uint32_t>(h), static_cast<uint32_t>(w), 3};
    if (fmt == TensorFormat::Nchw) t.dims = {1, 3, static_cast<uint32_t>(h), static_cast<uint32_t>(w)};
    t.type = type;
    t.fmt = fmt;
    t.quant = type == TensorType::Float32 ? QuantType::None : QuantType::Affine;
    t.zp = type == TensorType::Float32 ? 0 : -128;
    t.scale = 0.00392157f;
    return t;
}

TensorDesc output_tensor(int c, int h, int w) {
    TensorDesc t;
    t.dir = TensorDir::Output;
    t.index = 0;
    t.name = "out";
    t.n_dims = 4;
    t.dims = {1, static_cast<uint32_t>(c), static_cast<uint32_t>(h), static_cast<uint32_t>(w)};
    t.type = TensorType::Float32;
    t.fmt = TensorFormat::Nchw;
    t.quant = QuantType::None;
    t.n_elems = static_cast<uint32_t>(c) * static_cast<uint32_t>(h) * static_cast<uint32_t>(w);
    return t;
}

// 职责解码支持的布局：每个输出 1 个元素（类别分数）。
TensorDesc score_output() {
    TensorDesc t;
    t.dir = TensorDir::Output;
    t.index = 0;
    t.name = "score";
    t.n_dims = 4;
    t.dims = {1, 1, 1, 1};
    t.type = TensorType::Float32;
    t.fmt = TensorFormat::Nchw;
    t.quant = QuantType::None;
    t.n_elems = 1;
    return t;
}

// 夹具：与声明匹配的“受电弓三态分类”描述（1 输入 640x640 int8 + 4 输出）。
ModelDescriptor desc_pantograph_ok() {
    ModelDescriptor d;
    d.path = "fixture-pantograph.rknn";
    d.file_name = "fixture-pantograph.rknn";
    d.file_present = true;
    d.load_status = "ok";
    d.runtime_api_version = "fixture";
    d.runtime_drv_version = "fixture";
    d.inputs.push_back(input_tensor(640, 640));
    for (int i = 0; i < 3; ++i) d.outputs.push_back(score_output());
    return d;
}

// 真实 yolov5s 形态（3 输出 255 通道）——用于“登记名不等于能力/结构不匹配”。
ModelDescriptor desc_yolov5s_like() {
    ModelDescriptor d;
    d.path = "yolov5s.rknn";
    d.file_name = "yolov5s.rknn";
    d.file_present = true;
    d.load_status = "ok";
    d.inputs.push_back(input_tensor(640, 640));
    d.outputs.push_back(output_tensor(255, 80, 80));
    d.outputs.push_back(output_tensor(255, 40, 40));
    d.outputs.push_back(output_tensor(255, 20, 20));
    return d;
}

DutyDeclaration decl_pantograph() {
    DutyDeclaration d;
    d.duty = DutyKind::Pantograph;
    d.declared_model_name = "pantograph-3state";
    d.declared_by = "unit-test";
    d.input_count = 1;
    d.input_c = 3;
    d.input_h = 640;
    d.input_w = 640;
    d.output_count = 3;
    d.require_quantized_input = true;
    d.labels = {{"pantograph_not_raised", DutyState::NotRaised},
                {"pantograph_normal", DutyState::Normal},
                {"pantograph_abnormal", DutyState::Abnormal}};
    d.score_min = 0.5f;
    return d;
}

DutyDeclaration decl_door() {
    DutyDeclaration d;
    d.duty = DutyKind::CabDoor;
    d.declared_model_name = "cab-door-2state";
    d.input_h = 640;
    d.input_w = 640;
    d.output_count = 2;
    d.labels = {{"door_closed", DutyState::Closed}, {"door_ajar", DutyState::Ajar}};
    return d;
}

DutyDeclaration decl_wheel() {
    DutyDeclaration d;
    d.duty = DutyKind::WheelRail;
    d.declared_model_name = "wheel-rail-2state";
    d.input_h = 640;
    d.input_w = 640;
    d.output_count = 2;
    d.labels = {{"wheel_normal", DutyState::Normal}, {"wheel_abnormal", DutyState::Abnormal}};
    return d;
}

DutyEvidenceInput evidence(DutyKind duty, bool registered, const std::string& name) {
    DutyEvidenceInput in;
    in.duty = duty;
    in.registered_in_config = registered;
    in.registered_model_name = name;
    return in;
}

}  // namespace

void run_vision_duty_capability_tests() {
    using namespace railhmi_test;

    // ---- 词表与转换（unknown 是缺省，不是 normal） ----
    {
        check(std::string(to_string(DutyKind::Pantograph)) == "pantograph" &&
                  std::string(to_string(DutyKind::WheelRail)) == "wheel_rail" &&
                  std::string(to_string(DutyKind::CabDoor)) == "cab_door",
              "duty_kind_tokens", "pantograph/wheel_rail/cab_door");
        DutyKind k = DutyKind::Pantograph;
        check(parse_duty_kind("wheel_rail", &k) && k == DutyKind::WheelRail, "duty_parse_kind",
              "wheel_rail");
        check(!parse_duty_kind("front", &k), "duty_parse_kind_reject_front", "front 不是 duty");

        DutyState s = DutyState::Normal;
        check(std::string(to_string(DutyState::Unknown)) == "unknown" &&
                  parse_duty_state("unknown", &s) && s == DutyState::Unknown,
              "duty_state_unknown_token", "unknown");
        check(std::string(duty_state_display_zh(DutyState::Unknown)) == "未知",
              "duty_unknown_display_not_normal", duty_state_display_zh(DutyState::Unknown));
        check(std::string(duty_state_display_zh(DutyState::Normal)) == "正常", "duty_normal_display",
              duty_state_display_zh(DutyState::Normal));
        check(!parse_duty_state("normal ", &s), "duty_state_parse_strict", "严格匹配，不 trim");
    }

    // ---- profile <-> duty ----
    {
        DutyKind k = DutyKind::Pantograph;
        check(duty_of_profile(AiProfile::Pantograph, &k) && k == DutyKind::Pantograph,
              "duty_of_profile_pantograph", "pantograph");
        check(duty_of_profile(AiProfile::WheelRail, &k) && k == DutyKind::WheelRail,
              "duty_of_profile_wheel", "wheel_rail");
        check(duty_of_profile(AiProfile::CabDoor, &k) && k == DutyKind::CabDoor,
              "duty_of_profile_door", "cab_door");
        check(!duty_of_profile(AiProfile::Front, &k) && !duty_of_profile(AiProfile::None, &k),
              "duty_of_profile_excludes_front_none", "front/none 不属于职责");
        check(profile_of_duty(DutyKind::CabDoor) == AiProfile::CabDoor, "profile_of_duty_door",
              "cab_door");
    }

    // ---- 状态词表按职责校验 ----
    {
        check(duty_state_allowed(DutyKind::Pantograph, DutyState::NotRaised) &&
                  duty_state_allowed(DutyKind::Pantograph, DutyState::Normal) &&
                  duty_state_allowed(DutyKind::Pantograph, DutyState::Abnormal),
              "duty_pantograph_states", "not_raised/normal/abnormal");
        check(!duty_state_allowed(DutyKind::Pantograph, DutyState::Ajar) &&
                  !duty_state_allowed(DutyKind::Pantograph, DutyState::Closed),
              "duty_pantograph_rejects_door_states", "closed/ajar 越界");
        check(duty_state_allowed(DutyKind::CabDoor, DutyState::Closed) &&
                  duty_state_allowed(DutyKind::CabDoor, DutyState::Ajar),
              "duty_door_states", "closed/ajar");
        check(!duty_state_allowed(DutyKind::CabDoor, DutyState::NotRaised),
              "duty_door_rejects_pantograph_state", "not_raised 越界");
        check(duty_state_allowed(DutyKind::WheelRail, DutyState::Normal) &&
                  duty_state_allowed(DutyKind::WheelRail, DutyState::Abnormal) &&
                  !duty_state_allowed(DutyKind::WheelRail, DutyState::Closed),
              "duty_wheel_states", "normal/abnormal");
        check(duty_state_allowed(DutyKind::Pantograph, DutyState::Unknown) &&
                  duty_state_allowed(DutyKind::CabDoor, DutyState::Unknown),
              "duty_unknown_always_allowed", "unknown 是合法缺省");
    }

    // ---- 与 rules.h 状态互转 ----
    {
        PantographState ps = PantographState::Unknown;
        check(to_pantograph_state(DutyState::Abnormal, &ps) && ps == PantographState::Abnormal,
              "duty_to_pantograph_state", "abnormal");
        check(!to_pantograph_state(DutyState::Ajar, &ps), "duty_to_pantograph_rejects_ajar",
              "ajar 不是受电弓状态");
        DoorState ds = DoorState::Unknown;
        check(to_door_state(DutyState::Ajar, &ds) && ds == DoorState::Ajar, "duty_to_door_state",
              "ajar");
        check(!to_door_state(DutyState::Normal, &ds), "duty_to_door_rejects_normal",
              "normal 不是车门状态");
    }

    // ---- 声明完整性 ----
    {
        DutyDeclaration d = decl_pantograph();
        check(d.complete(), "duty_declaration_complete", "1 in / 3 class-score out + 标签表");
        DutyDeclaration bad = d;
        bad.declared_model_name.clear();
        check(!bad.complete(), "duty_declaration_requires_model_name", "模型名必填");
        bad = d;
        bad.input_h = 0;
        check(!bad.complete(), "duty_declaration_requires_input_shape", "输入形状必填");
        bad = d;
        bad.labels.clear();
        check(!bad.complete(), "duty_declaration_requires_labels", "标签表必填");
        bad = d;
        bad.output_count = 0;   // 只验证标签映射本身的合法性
        bad.labels.push_back({"pantograph_unknown", DutyState::Unknown});
        check(!bad.complete(), "duty_declaration_rejects_unknown_mapping",
              "标签不得映射到 unknown");
        bad = d;
        bad.output_count = 0;
        bad.labels.push_back({"door_ajar", DutyState::Ajar});
        check(!bad.complete(), "duty_declaration_rejects_cross_duty_state",
              "受电弓声明不得使用车门状态");
        bad = d;
        bad.output_count = -1;
        check(!bad.complete(), "duty_declaration_rejects_negative_outputs", "output_count 非负");
        bad = d;
        bad.output_count = 2;   // 3 个标签必须对应 3 个类分数输出
        check(!bad.complete(), "duty_declaration_outputs_must_match_labels",
              "输出数必须等于标签数");
    }

    // ---- 求值：无声明（TC-T-MIS-01/02） ----
    {
        DutyAvailability a = evaluate_duty(evidence(DutyKind::Pantograph, false, ""));
        check(!a.available && a.reason == duty_reason::kProfileUnsupported() &&
                  a.state == DutyState::Unknown && a.semantics_unknown(),
              "duty_no_model_unavailable_unknown", a.reason + "/unknown");
        check(a.evidence_source == "none", "duty_no_model_evidence_source_none", a.evidence_source);
        check(a.detail.find("no dedicated model capability") != std::string::npos,
              "duty_no_model_detail", a.detail);

        // 配置登记了模型名，但不构成能力。
        DutyAvailability b = evaluate_duty(evidence(DutyKind::CabDoor, true, "yolov5s"));
        check(!b.available && b.reason == duty_reason::kProfileUnsupported() &&
                  b.state == DutyState::Unknown,
              "duty_config_registration_not_capability", b.reason);
        check(b.config_registered && b.registered_model_name == "yolov5s",
              "duty_config_registration_recorded", "仅记录");
        check(b.detail.find("registration is not evidence") != std::string::npos,
              "duty_config_registration_detail", b.detail);
    }

    // ---- 求值：声明不完整 / 无描述 / 加载失败 ----
    {
        DutyEvidenceInput in = evidence(DutyKind::Pantograph, true, "yolov5s");
        in.declaration_provided = true;
        DutyDeclaration bad = decl_pantograph();
        bad.labels.clear();
        in.declaration = bad;
        DutyAvailability a = evaluate_duty(in);
        check(!a.available && a.reason == duty_reason::kModelMismatch() &&
                  a.state == DutyState::Unknown,
              "duty_incomplete_declaration_mismatch", a.detail);

        in.declaration = decl_pantograph();
        DutyAvailability b = evaluate_duty(in);
        check(!b.available && b.reason == duty_reason::kModelMissing(),
              "duty_declaration_without_descriptor_missing", b.reason);

        in.descriptor_present = true;
        in.descriptor = desc_pantograph_ok();
        in.descriptor.load_status = "MODEL_LOAD_FAILED";
        DutyAvailability c = evaluate_duty(in);
        check(!c.available && c.reason == "MODEL_LOAD_FAILED" && c.state == DutyState::Unknown,
              "duty_descriptor_load_failed", c.reason);
    }

    // ---- 求值：结构不匹配（MODEL_MISMATCH） ----
    {
        DutyEvidenceInput in = evidence(DutyKind::Pantograph, true, "yolov5s");
        in.declaration_provided = true;
        in.declaration = decl_pantograph();
        in.descriptor_present = true;

        // 真实 yolov5s 形态：输出个数 3 与声明一致，但每个输出不是 1 元素（无解码器）。
        in.descriptor = desc_yolov5s_like();
        DutyAvailability a = evaluate_duty(in);
        check(!a.available && a.reason == duty_reason::kModelMismatch() &&
                  a.state == DutyState::Unknown,
              "duty_registered_yolov5s_is_mismatch", a.detail);
        bool has_layout_mismatch = false;
        for (const auto& m : a.mismatched)
            if (m == "unsupported_decode_layout") has_layout_mismatch = true;
        check(has_layout_mismatch, "duty_mismatch_reports_decode_layout",
              "mismatched 含 unsupported_decode_layout");

        // 输出个数不符（声明 3 类 vs 描述 2 输出）。
        ModelDescriptor d2 = desc_pantograph_ok();
        d2.outputs.resize(2);
        in.descriptor = d2;
        DutyAvailability a2 = evaluate_duty(in);
        bool has_output_mismatch = false;
        for (const auto& m : a2.mismatched)
            if (m.find("output_count") != std::string::npos) has_output_mismatch = true;
        check(!a2.available && a2.reason == duty_reason::kModelMismatch() && has_output_mismatch,
              "duty_mismatch_reports_output_count", a2.detail);

        // 输入尺寸不符。
        ModelDescriptor d = desc_pantograph_ok();
        d.inputs[0] = input_tensor(320, 320);
        in.descriptor = d;
        DutyAvailability b = evaluate_duty(in);
        bool has_hw = false;
        for (const auto& m : b.mismatched)
            if (m.find("input_hw") != std::string::npos) has_hw = true;
        check(!b.available && b.reason == duty_reason::kModelMismatch() && has_hw,
              "duty_mismatch_input_shape", b.detail);

        // 未量化输入。
        d = desc_pantograph_ok();
        d.inputs[0] = input_tensor(640, 640, TensorType::Float32);
        in.descriptor = d;
        DutyAvailability c = evaluate_duty(in);
        bool has_q = false;
        for (const auto& m : c.mismatched)
            if (m == "input_not_quantized") has_q = true;
        check(!c.available && has_q, "duty_mismatch_not_quantized", c.detail);

        // 输入个数不符。
        d = desc_pantograph_ok();
        d.inputs.push_back(input_tensor(640, 640));
        in.descriptor = d;
        DutyAvailability e = evaluate_duty(in);
        bool has_cnt = false;
        for (const auto& m : e.mismatched)
            if (m.find("input_count") != std::string::npos) has_cnt = true;
        check(!e.available && has_cnt, "duty_mismatch_input_count", e.detail);
    }

    // ---- 求值：全部匹配 -> READY，但状态仍为 unknown（TC-T-MIS-03 前半） ----
    {
        DutyEvidenceInput in = evidence(DutyKind::Pantograph, true, "pantograph-3state");
        in.declaration_provided = true;
        in.declaration = decl_pantograph();
        in.descriptor_present = true;
        in.descriptor = desc_pantograph_ok();
        DutyAvailability a = evaluate_duty(in);
        check(a.available && a.reason == duty_reason::kReady(), "duty_declared_verified_ready", a.reason);
        check(a.state == DutyState::Unknown && a.semantics_unknown(),
              "duty_ready_is_not_normal", "READY != normal");
        check(a.evidence_source == "declaration+descriptor", "duty_ready_evidence_source",
              a.evidence_source);
        bool has_input_q = false;
        for (const auto& m : a.matched)
            if (m == "input_quantized") has_input_q = true;
        check(has_input_q, "duty_ready_reports_matched_items", "matched 含 input_quantized");
    }

    // ---- 标签 -> 状态映射（不越过 unknown 默认） ----
    {
        DutyDeclaration d = decl_pantograph();
        std::vector<DutyPrediction> preds = {{"pantograph_normal", 0.9f}};
        DutyLabelResult r = map_duty_prediction(d, preds);
        check(r.state == DutyState::Normal && r.used_label == "pantograph_normal",
              "duty_map_declared_label", r.used_label);

        r = map_duty_prediction(d, {{"person", 0.99f}});
        check(r.state == DutyState::Unknown && r.blocked_unknown_label == 1,
              "duty_map_unknown_label_blocked", "未声明标签不猜语义");

        r = map_duty_prediction(d, {{"pantograph_abnormal", 0.3f}});
        check(r.state == DutyState::Unknown && r.below_threshold == 1,
              "duty_map_below_threshold_unknown", "阈值以下不产生判定");

        r = map_duty_prediction(d, {});
        check(r.state == DutyState::Unknown && r.considered == 0, "duty_map_empty_unknown", "空输入");

        r = map_duty_prediction(d, {{"pantograph_normal", 0.6f}, {"pantograph_abnormal", 0.8f}});
        check(r.state == DutyState::Abnormal && r.used_score > 0.79f, "duty_map_highest_score_wins",
              r.used_label);

        r = map_duty_prediction(d, {{"pantograph_abnormal", 0.4f}}, 0.35f);
        check(r.state == DutyState::Abnormal, "duty_map_threshold_override", "阈值覆盖生效");

        DutyLabelResult door = map_duty_prediction(decl_door(), {{"door_ajar", 0.9f}});
        check(door.state == DutyState::Ajar, "duty_map_door_ajar_not_abnormal", "ajar");
        DutyLabelResult wheel = map_duty_prediction(decl_wheel(), {{"wheel_abnormal", 0.9f}});
        check(wheel.state == DutyState::Abnormal, "duty_map_wheel_abnormal", "abnormal");
    }

    // ---- 适配器：不可用时拒绝推理结果（不产报警） ----
    {
        DutyAdapter ad(DutyKind::CabDoor, "CAM-06");
        ad.set_config_registration(true, "yolov5s");
        DutyAvailability a = ad.evaluate();
        check(!a.available && a.state == DutyState::Unknown, "duty_adapter_unavailable", a.reason);
        DutyAvailability b = ad.apply({{"door_ajar", 0.99f}});
        check(b.state == DutyState::Unknown && ad.stats().rejected_unavailable == 1 &&
                  ad.stats().predictions == 0,
              "duty_adapter_rejects_prediction_when_unavailable", "unknown 保持");
        Detection det;
        check(!ad.to_detection(1000, &det), "duty_adapter_no_detection_when_unavailable",
              "不可用不产生判定");
        check(ad.semantics_unknown(), "duty_adapter_semantics_unknown", "unknown");
    }

    // ---- 适配器：可用时的完整路径 ----
    {
        DutyAdapter ad(DutyKind::Pantograph, "CAM-04");
        ad.set_config_registration(true, "pantograph-3state");
        ad.set_declaration(decl_pantograph());
        ad.set_descriptor(desc_pantograph_ok());
        DutyAvailability a = ad.evaluate();
        check(a.available && a.state == DutyState::Unknown, "duty_adapter_ready_unknown",
              "ready 但暂无推理");

        DutyAvailability b = ad.apply({{"pantograph_normal", 0.2f}});
        check(b.state == DutyState::Unknown && ad.stats().unknown_results >= 1,
              "duty_adapter_low_score_stays_unknown", "低分不产生 normal");

        DutyAvailability c = ad.apply({{"pantograph_abnormal", 0.91f}});
        check(c.state == DutyState::Abnormal, "duty_adapter_abnormal", "abnormal");
        Detection det;
        check(ad.to_detection(1234, &det) && det.profile == AiProfile::Pantograph &&
                  det.pantograph == PantographState::Abnormal && det.track_id == -1 &&
                  det.camera_id == "CAM-04" && det.t_ms == 1234,
              "duty_adapter_detection_pantograph", "Detection 状态映射 + null track");

        DutyAdapter door(DutyKind::CabDoor, "CAM-06");
        door.set_declaration(decl_door());
        door.set_descriptor(desc_pantograph_ok());   // 结构相同（1 in/4 out != 2）-> 不匹配
        check(!door.evaluate().available, "duty_adapter_door_shape_mismatch", "输出个数不同");

        DutyDeclaration dd = decl_door();
        // 构造与声明匹配的描述（2 输出）。
        ModelDescriptor d2 = desc_pantograph_ok();
        d2.outputs.resize(2);
        door.set_descriptor(d2);
        check(door.evaluate().available, "duty_adapter_door_ready", "door ready");
        door.apply({{"door_ajar", 0.8f}});
        Detection dd_det;
        check(door.to_detection(5, &dd_det) && dd_det.door == DoorState::Ajar,
              "duty_adapter_detection_door", "DoorState::Ajar");
        (void)dd;

        DutyAdapter wheel(DutyKind::WheelRail, "CAM-05");
        wheel.set_declaration(decl_wheel());
        wheel.set_descriptor(d2);
        check(wheel.evaluate().available, "duty_adapter_wheel_ready", "wheel ready");
        wheel.apply({{"wheel_abnormal", 0.7f}});
        Detection wdet;
        check(wheel.to_detection(6, &wdet) && wdet.wheel_abnormal,
              "duty_adapter_detection_wheel", "wheel_abnormal=true");
        wheel.apply({{"wheel_normal", 0.7f}});
        Detection wok;
        check(wheel.to_detection(7, &wok) && !wok.wheel_abnormal,
              "duty_adapter_wheel_normal", "wheel_abnormal=false");
    }

    // ---- 项目真实登记（pantograph/wheel_rail/cab_door 均登记 yolov5s 风格模型）----
    {
        const char* names[] = {"pantograph", "wheel_rail", "cab_door"};
        const DutyKind kinds[] = {DutyKind::Pantograph, DutyKind::WheelRail, DutyKind::CabDoor};
        bool all_unknown_unavailable = true;
        for (int i = 0; i < 3; ++i) {
            DutyAvailability a = evaluate_duty(evidence(kinds[i], true, "yolov5s"));
            if (a.available || a.state != DutyState::Unknown) all_unknown_unavailable = false;
            if (a.reason != duty_reason::kProfileUnsupported()) all_unknown_unavailable = false;
            check(a.state == DutyState::Unknown && !a.available, std::string("duty_registered_") +
                                                                    names[i] + "_unknown",
                  a.reason);
        }
        check(all_unknown_unavailable, "duty_all_three_unavailable_unknown",
              "三职责：unavailable + unknown（配置登记不算能力）");
    }

    // ---- JSON 可观察面 ----
    {
        DutyAdapter ad(DutyKind::WheelRail, "CAM-05");
        ad.set_config_registration(true, "yolov8s-seg");
        DutyAvailability a = ad.evaluate();
        const std::string j = duty_availability_json(a);
        check(j.find("\"state\":\"unknown\"") != std::string::npos &&
                  j.find("\"semantics_unknown\":true") != std::string::npos &&
                  j.find("\"available\":false") != std::string::npos &&
                  j.find("PROFILE_UNSUPPORTED") != std::string::npos,
              "duty_json_unknown_fields", j.substr(0, 140));
        check(j.find("\"state_display\":\"未知\"") != std::string::npos,
              "duty_json_display_unknown_not_normal", "显示为未知");

        std::vector<DutyStatusItem> items;
        DutyStatusItem it;
        it.duty = DutyKind::WheelRail;
        it.profile = AiProfile::WheelRail;
        it.camera_id = "CAM-05";
        it.availability = a;
        items.push_back(it);
        const std::string sj = duty_status_json(items);
        check(sj.find("\"duties\":[") != std::string::npos &&
                  sj.find("\"profile\":\"wheel_rail\"") != std::string::npos &&
                  sj.find("\"rejected_unavailable\":0") != std::string::npos,
              "duty_status_json_shape", sj.substr(0, 120));
    }
}
