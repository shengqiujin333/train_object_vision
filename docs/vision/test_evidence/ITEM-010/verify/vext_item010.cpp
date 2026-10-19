// ITEM-010 验证轮 vext —— 软件测试自有扩展夹具（板端 g++ 直编，链接 librailhmi_core.a）。
// 覆盖交付自测（core-tests 85 项 + probe selftest 17 项）未覆盖的边界：
//   A) apply() 自动求值路径（未先 evaluate 直接 apply）
//   B) 阈值边界（score==score_min 恰好过；override==0 走声明阈值而非恒拒）
//   C) 平分 tie（两个等分声明标签 —— 首个高优先序保持）
//   D) 统计台账精确账（evaluations/predictions/unknown_results/rejected 逐项）
//   E) NotRaised/Closed 的 to_detection 映射（normal 之外的第二状态）
//   F) descriptor load_status 透传（MODEL_MISSING/RKNN_RUNTIME_UNAVAILABLE/空 -> NOT_READY）
//   G) 声明 output_count=0 语义（跳过输出核验；但类分数布局检查随之跳过）
//   H) 调度集成：温度守卫降载与职责路并存时互不污染；waits() 不含职责路；
//      无帧职责路归 idle 不归 unavailable；budget 降载后职责路仍不占预算
#include "railhmi/core/vision_duty_capability.h"
#include "railhmi/core/vision_worker_schedule.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace railhmi::core;

static int g_fail = 0;

static void check(bool ok, const std::string& name, const std::string& detail = "") {
    const std::string d = detail.empty() ? std::string() : (" :: " + detail);
    std::printf("vext %s %s%s\n", ok ? "PASS" : "FAIL", name.c_str(), d.c_str());
    if (!ok) ++g_fail;
}

static TensorDesc in_tensor(int h, int w, TensorType type = TensorType::Int8) {
    TensorDesc t;
    t.dir = TensorDir::Input;
    t.index = 0;
    t.name = "images";
    t.n_dims = 4;
    t.dims = {1, static_cast<uint32_t>(h), static_cast<uint32_t>(w), 3};
    t.type = type;
    t.fmt = TensorFormat::Nhwc;
    t.quant = type == TensorType::Float32 ? QuantType::None : QuantType::Affine;
    t.zp = type == TensorType::Float32 ? 0 : -128;
    t.scale = 0.00392157f;
    return t;
}

static TensorDesc score_out() {
    TensorDesc t;
    t.dir = TensorDir::Output;
    t.name = "score";
    t.n_dims = 4;
    t.dims = {1, 1, 1, 1};
    t.type = TensorType::Float32;
    t.fmt = TensorFormat::Nchw;
    t.quant = QuantType::None;
    t.n_elems = 1;
    return t;
}

static ModelDescriptor desc_ok(int outputs) {
    ModelDescriptor d;
    d.path = "fixture-duty.rknn";
    d.file_name = "fixture-duty.rknn";
    d.file_present = true;
    d.load_status = "ok";
    d.runtime_api_version = "fixture";
    d.runtime_drv_version = "fixture";
    d.inputs.push_back(in_tensor(640, 640));
    for (int i = 0; i < outputs; ++i) d.outputs.push_back(score_out());
    return d;
}

static DutyDeclaration decl_pant(float score_min = 0.5f) {
    DutyDeclaration d;
    d.duty = DutyKind::Pantograph;
    d.declared_model_name = "pantograph-3state";
    d.declared_by = "vext";
    d.input_h = 640;
    d.input_w = 640;
    d.output_count = 3;
    d.score_min = score_min;
    d.labels = {{"pantograph_not_raised", DutyState::NotRaised},
                {"pantograph_normal", DutyState::Normal},
                {"pantograph_abnormal", DutyState::Abnormal}};
    return d;
}

static DutyDeclaration decl_wheel() {
    DutyDeclaration d;
    d.duty = DutyKind::WheelRail;
    d.declared_model_name = "wheel-rail-2state";
    d.declared_by = "vext";
    d.input_h = 640;
    d.input_w = 640;
    d.output_count = 2;
    d.labels = {{"wheel_normal", DutyState::Normal}, {"wheel_abnormal", DutyState::Abnormal}};
    return d;
}

int main() {
    // ---- A) apply() 自动求值路径 ----
    {
        DutyAdapter ad(DutyKind::Pantograph, "CAM-04");
        ad.set_declaration(decl_pant());
        ad.set_descriptor(desc_ok(3));
        // 未先调用 evaluate() 直接 apply —— 首次 apply 内部必须自动求值。
        const DutyAvailability a = ad.apply({{"pantograph_normal", 0.9f}});
        check(a.available && a.state == DutyState::Normal && ad.stats().evaluations >= 1,
              "g01_apply_without_explicit_eval_auto_evaluates", a.reason);
        check(ad.stats().predictions == 1, "g02_apply_counts_prediction",
              std::to_string(ad.stats().predictions));

        // 未声明时 apply 同样自动求值并拒绝。
        DutyAdapter raw(DutyKind::WheelRail, "CAM-05");
        const DutyAvailability b = raw.apply({{"wheel_abnormal", 0.99f}});
        check(!b.available && b.state == DutyState::Unknown && raw.stats().rejected_unavailable == 1,
              "g03_apply_no_declaration_rejects", b.reason);
    }

    // ---- B) 阈值边界 ----
    {
        DutyDeclaration d = decl_pant(0.5f);
        // score == score_min 恰好达阈（p.score < threshold 拒，等值不拒）。
        DutyLabelResult r = map_duty_prediction(d, {{"pantograph_normal", 0.5f}});
        check(r.state == DutyState::Normal, "g04_threshold_inclusive_at_exact_min",
              std::to_string(r.used_score));
        // 恰低于阈值一位（0.499999）。
        r = map_duty_prediction(d, {{"pantograph_normal", 0.499999f}});
        check(r.state == DutyState::Unknown && r.below_threshold == 1, "g05_threshold_just_below", "");

        // override == 0：threshold_override >= 0 为真 -> 阈值 0，任何非负分都过。
        r = map_duty_prediction(d, {{"pantograph_normal", 0.01f}}, 0.0f);
        check(r.state == DutyState::Normal, "g06_override_zero_is_valid_threshold", "0.01 passes");

        // override < 0：回落声明阈值。
        r = map_duty_prediction(d, {{"pantograph_normal", 0.3f}}, -1.0f);
        check(r.state == DutyState::Unknown && r.below_threshold == 1, "g07_override_negative_falls_back",
              "");
    }

    // ---- C) 平分 tie：首个达到最高分的标签保持（> 严格比较，后到等分不覆盖）----
    {
        DutyDeclaration d = decl_pant(0.5f);
        const DutyLabelResult r = map_duty_prediction(
            d, {{"pantograph_abnormal", 0.8f}, {"pantograph_not_raised", 0.8f}});
        check(r.state == DutyState::Abnormal && r.used_label == "pantograph_abnormal",
              "g08_tie_keeps_first_label", r.used_label);
    }

    // ---- D) 统计台账精确账 ----
    {
        DutyAdapter ad(DutyKind::Pantograph, "CAM-04");
        ad.set_declaration(decl_pant());
        ad.set_descriptor(desc_ok(3));
        DutyAvailability a = ad.evaluate();
        a = ad.apply({{"person", 0.9f}, {"pantograph_normal", 0.2f}});   // 1 blocked + 1 below
        // unknown_results 账目：evaluate(READY 仍 unknown) +1，apply 结果 unknown 再 +1 = 2。
        check(ad.stats().evaluations == 1 && ad.stats().predictions == 1 &&
                  ad.stats().unknown_results == 2 && ad.stats().blocked_unknown_label == 1 &&
                  ad.stats().below_threshold == 1 && ad.stats().rejected_unavailable == 0,
              "g10_stats_exact_ledger",
              "ev=" + std::to_string(ad.stats().evaluations) + " pr=" +
                  std::to_string(ad.stats().predictions) + " un=" +
                  std::to_string(ad.stats().unknown_results) + " bl=" +
                  std::to_string(ad.stats().blocked_unknown_label) + " be=" +
                  std::to_string(ad.stats().below_threshold));
        a = ad.apply({{"pantograph_abnormal", 0.9f}});   // 结果非 unknown：账目不再增
        check(ad.stats().evaluations == 1 && ad.stats().predictions == 2 &&
                  ad.stats().unknown_results == 2,
              "g11_stats_second_apply_no_extra_eval", "");
        // 重复 evaluate 重新求值：READY 结构面重算，语义状态回落缺省 unknown
        //（apply 的状态是"最近一次推理"视图，evaluate 是结构可用性视图）。
        ad.evaluate();
        check(ad.stats().evaluations == 2 && ad.state() == DutyState::Unknown &&
                  ad.availability().reason == duty_reason::kReady(),
              "g12_re_eval_resets_semantic_state_to_unknown", "");
    }

    // ---- E) NotRaised / Closed 的 Detection 映射（normal 之外的合法状态）----
    {
        DutyAdapter ad(DutyKind::Pantograph, "CAM-04");
        ad.set_declaration(decl_pant());
        ad.set_descriptor(desc_ok(3));
        ad.evaluate();
        ad.apply({{"pantograph_not_raised", 0.7f}});
        Detection d;
        check(ad.to_detection(1, &d) && d.pantograph == PantographState::NotRaised &&
                  d.profile == AiProfile::Pantograph,
              "g13_not_raised_detection_mapping", std::to_string(static_cast<int>(d.pantograph)));

        DutyDeclaration door = decl_pant();
        door.duty = DutyKind::CabDoor;
        door.declared_model_name = "cab-door-2state";
        door.output_count = 2;
        door.labels = {{"door_closed", DutyState::Closed}, {"door_ajar", DutyState::Ajar}};
        ModelDescriptor d2 = desc_ok(2);
        DutyAdapter ad2(DutyKind::CabDoor, "CAM-06");
        ad2.set_declaration(door);
        ad2.set_descriptor(d2);
        ad2.evaluate();
        ad2.apply({{"door_closed", 0.66f}});
        Detection dd;
        check(ad2.to_detection(2, &dd) && dd.door == DoorState::Closed,
              "g14_closed_detection_mapping", "");
        // normal 状态的 wheel 不置 wheel_abnormal（回归锚）。
        DutyAdapter w(DutyKind::WheelRail, "CAM-05");
        w.set_declaration(decl_wheel());
        w.set_descriptor(d2);
        w.evaluate();
        w.apply({{"wheel_normal", 0.9f}});
        Detection wd;
        check(w.to_detection(3, &wd) && !wd.wheel_abnormal, "g15_wheel_normal_not_abnormal", "");
    }

    // ---- F) descriptor load_status 透传 ----
    {
        DutyEvidenceInput in;
        in.duty = DutyKind::WheelRail;
        in.declaration_provided = true;
        in.declaration = decl_wheel();
        in.descriptor_present = true;

        in.descriptor = desc_ok(2);
        in.descriptor.load_status = "RKNN_RUNTIME_UNAVAILABLE";
        DutyAvailability a = evaluate_duty(in);
        check(!a.available && a.reason == "RKNN_RUNTIME_UNAVAILABLE" && a.state == DutyState::Unknown,
              "g16_load_status_passthrough_runtime_unavailable", a.reason);

        in.descriptor = desc_ok(2);
        in.descriptor.load_status = "";
        DutyAvailability b = evaluate_duty(in);
        check(!b.available && b.reason == "NOT_READY", "g17_empty_load_status_not_ready", b.reason);
    }

    // ---- G) 声明 output_count=0 语义（跳过输出核验，仍要求声明完整）----
    {
        DutyEvidenceInput in;
        in.duty = DutyKind::Pantograph;
        in.declaration_provided = true;
        in.declaration = decl_pant();
        in.declaration.output_count = 0;
        in.descriptor_present = true;
        in.descriptor = desc_ok(3);
        DutyAvailability a = evaluate_duty(in);
        check(a.available && a.reason == duty_reason::kReady(),
              "g18_output_count_zero_skips_output_checks", a.reason);
        // 但布局检查也随之跳过：255x80x80 的检测头输出在 output_count=0 时不判 mismatch
        //（声明方显式放弃了输出数契约）—— 行为锚定，防止未来静默改变。
        in.descriptor = desc_ok(0);
        in.descriptor.outputs.push_back([] {
            TensorDesc t = score_out();
            t.dims = {1, 255, 80, 80};
            t.n_elems = 255u * 80u * 80u;
            return t;
        }());
        DutyAvailability b = evaluate_duty(in);
        check(b.available, "g19_output_count_zero_no_layout_check", b.reason);
    }

    // ---- H) 调度集成：温度守卫降载与职责路并存、waits 不含职责路、无帧职责路归 idle ----
    {
        AiSchedulerConfig cfg;
        cfg.npu_budget = 2;
        cfg.temp_limit_c = 30.0;   // 触发守卫：budget 2 -> 1
        AiLaneScheduler sch(cfg);
        ThermalSample hot = injected_thermal_sample(90.0, 1000);

        std::vector<AiLaneDemand> demand(4);
        demand[0].camera_id = "CAM-01";
        demand[0].profile = AiProfile::Front;
        demand[0].has_frame = true;
        demand[1].camera_id = "CAM-04";
        demand[1].profile = AiProfile::Pantograph;
        demand[1].has_frame = true;
        demand[1].duty_ready = false;
        demand[2].camera_id = "CAM-05";
        demand[2].profile = AiProfile::WheelRail;
        demand[2].has_frame = true;
        demand[2].duty_ready = false;
        demand[3].camera_id = "CAM-06";
        demand[3].profile = AiProfile::CabDoor;
        demand[3].has_frame = false;   // 无帧职责路
        demand[3].duty_ready = false;

        const AiScheduleRound r = sch.plan(demand, hot);
        check(r.temperature_guard && r.budget_effective == 1, "g20_guard_halves_budget",
              "effective=" + std::to_string(r.budget_effective));
        check(r.run.size() == 1 && r.run[0] == "CAM-01", "g21_guard_run_front_only", "");
        check(r.unavailable.size() == 2, "g22_duty_lanes_unavailable_even_under_guard",
              "unavail=" + std::to_string(r.unavailable.size()));
        check(r.idle.size() == 1 && r.idle[0] == "CAM-06", "g23_no_frame_duty_lane_is_idle",
              "idle=" + std::to_string(r.idle.size()));
        check(r.skipped.empty(), "g24_guard_no_pseudo_starvation", "");
        // 职责路不进 seen_order_：waits() 只有 front，max_wait_rounds 不被职责路污染。
        check(sch.waits().size() == 1 && sch.waits().count("CAM-01") == 1,
              "g25_waits_exclude_duty_lanes", "waits=" + std::to_string(sch.waits().size()));

        // 下一轮 front 再跑：等待轮数按 front 自己的 last_run 计算，职责路不参与。
        demand[0].has_frame = true;
        const AiScheduleRound r2 = sch.plan(demand, hot);
        check(r2.run.size() == 1 && r2.run[0] == "CAM-01" && r2.skipped.empty(),
              "g26_second_round_stable", "");
        check(sch.max_wait_rounds() <= 1, "g27_max_wait_not_polluted",
              std::to_string(sch.max_wait_rounds()));

        // 温度恢复（低于 limit 30）后职责路仍不占预算：budget=2、仅 front 就绪。
        ThermalSample cool = injected_thermal_sample(25.0, 2000);
        const AiScheduleRound r3 = sch.plan(demand, cool);
        check(!r3.temperature_guard && r3.budget_effective == 2 && r3.run.size() == 1 &&
                  r3.run[0] == "CAM-01" && r3.unavailable.size() == 2 && r3.skipped.empty(),
              "g28_recovered_budget_duty_still_outside", "run=" + std::to_string(r3.run.size()));

        // 决策 JSON：unavailable 数组可观察。
        const std::string j = schedule_round_json(r3);
        check(j.find("\"unavailable\":[") != std::string::npos &&
                  j.find("\"skipped\":[]") != std::string::npos,
              "g29_round_json_unavailable_visible", "");
    }

    const bool ok = g_fail == 0;
    std::printf("vext RESULT: %s failures=%d\n", ok ? "PASS" : "FAIL", g_fail);
    return ok ? 0 : 1;
}
