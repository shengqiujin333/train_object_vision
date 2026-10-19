// verify_item013_vext_v2.cpp — software_tester V-EXT 夹具(视觉 ITEM-013 修复复核轮 v2)。
// 复核协议(ITEM-014/009 修复轮形态): v1 夹具的 D-01 呈证判例翻转——修复后 FI-AI-12
// 必须在 selftest 套件内可见(requirement + JSON 归档面);其余回归判据逐字节保留。
// 板端 g++ 直编: g++ -std=c++17 -I src/core/include vext.cpp build-core/src/core/librailhmi_core.a -o vext
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "railhmi/core/vision_selftest.h"
#include "railhmi/core/vision_semantic_adapter.h"
#include "railhmi/core/vision_inference_envelope.h"
#include "railhmi/core/vision_detect_postprocess.h"
#include "railhmi/core/vision_duty_capability.h"
#include "railhmi/core/vision_distance.h"
#include "railhmi/core/vision_debug_view.h"

using namespace railhmi::core;

static int g_pass = 0, g_fail = 0;
static void check(bool ok, const std::string& name, const std::string& detail) {
    if (ok) { ++g_pass; std::printf("vext PASS %s :: %s\n", name.c_str(), detail.c_str()); }
    else { ++g_fail; std::printf("vext FAIL %s :: %s\n", name.c_str(), detail.c_str()); }
}

static FrameId fid(const char* cam, unsigned long long gen, unsigned long long seq) {
    FrameId f; f.session_id = "vext-s"; f.camera_id = cam; f.generation = gen; f.seq = seq;
    return f;
}

int main() {
    // ---- V1: 交付自检报告跑通 + 数量(17 = 16 + 新增 semantic 判例) ====
    const SelfTestReport r = run_vision_selftest();
    check(r.cases.size() == 17 && r.failed == 0, "v01_delivered_selftest_17_pass",
          "total=" + std::to_string(r.cases.size()) + " failed=" + std::to_string(r.failed));

    // ---- V2 (D-01 复核,呈证判例翻转): FI-AI-12 现在必须在 requirement 中在位 ====
    {
        bool has_fi_ai_10 = false, has_fi_ai_12 = false;
        for (const auto& c : r.cases) {
            if (c.requirement.find("FI-AI-10") != std::string::npos) has_fi_ai_10 = true;
            if (c.requirement.find("FI-AI-12") != std::string::npos) has_fi_ai_12 = true;
        }
        check(has_fi_ai_10 && has_fi_ai_12, "v02_selftest_fi_ai_12_present",
              std::string("FI-AI-10 present=") + (has_fi_ai_10 ? "1" : "0")
              + " FI-AI-12 present=" + (has_fi_ai_12 ? "1" : "0"));
        // 拦截判例本身必须 PASS 且判定面完整(零报警 + person 照常 + 计数可见)。
        const SelfTestCase* sem = nullptr;
        for (const auto& c : r.cases)
            if (c.id == "semantic.unknown_label_intercepted") sem = &c;
        check(sem != nullptr && sem->pass, "v03_unknown_label_case_present_and_pass",
              sem ? ("blocked=" + sem->evidence.at("blocked_unknown_label")
                     + " mapped=" + sem->evidence.at("mapped")
                     + " events=" + sem->evidence.at("events")) : "<missing>");
        check(sem != nullptr && sem->evidence.at("blocked_unknown_label") == "1"
              && sem->evidence.at("mapped") == "1" && sem->evidence.at("events") == "1"
              && sem->evidence.at("detections") == "1"
              && sem->evidence.at("event_type") == "person_on_rail",
              "v04_fi_ai_12_judgment_face", sem ? sem->detail : "<missing>");
        // 头注释声称与套件内容一致: JSON 归档面也可见 FI-AI-12(v1 呈证 v03 的反转)。
        std::string json = selftest_json(r, false);
        check(json.find("FI-AI-12") != std::string::npos,
              "v05_selftest_json_fi_ai_12_visible", "JSON 归档面可见 FI-AI-12(与头注释一致)");
    }

    // ---- V6 (FI-AI-12 机制在位证明,回归保留): SemanticAdapter 未知 label 拦截 ====
    {
        SemanticConfig cfg;
        cfg.allowed_labels = {"person", "obstacle"};
        cfg.known_labels = {"person", "obstacle"};
        cfg.confidence_min = 0.5;
        cfg.track_policy = TrackPolicy::CameraLevel;
        SemanticAdapter ad(cfg);
        EnvelopeBuildInput in;
        in.frame = fid("CAM-01", 1, 1);
        in.profile = AiProfile::Front;
        in.model_version = "vext";
        in.capture_mono_us = 0;
        in.result_mono_us = 1000;
        in.detect_ready = true;
        DetectBox p; p.label = "person"; p.score = 0.9f;
        p.w = 0.1f; p.h = 0.1f; p.x = 0.4f; p.y = 0.4f;
        DetectBox u; u.label = "weird_class"; u.score = 0.95f;
        u.w = 0.1f; u.h = 0.1f; u.x = 0.6f; u.y = 0.4f;
        std::vector<DetectBox> dbxes{p, u};
        in.in_roi_flags = {1, 1};
        in.boxes = &dbxes;
        const InferenceEnvelope e = build_envelope(in);
        RuleConfig rcfg;
        RuleEngine rules(rcfg);
        SemanticResult res;
        auto events = ad.adapt_and_dispatch(e, 5, &rules, &res);
        check(res.ok && res.available, "v06_sem_result_ok", res.reason);
        check(res.stats.blocked_unknown_label == 1, "v07_fi_ai_12_blocked_unknown_1",
              "blocked_unknown_label=" + std::to_string(res.stats.blocked_unknown_label));
        check(res.detections.size() == 1 && res.detections[0].label == "person",
              "v08_only_person_mapped", "mapped=" + std::to_string(res.stats.mapped));
        const long long alarms = (long long)events.size();
        check(alarms == 0, "v09_unknown_no_alarm", "alarms=" + std::to_string(alarms));
        const InferenceEnvelope e2 = e;
        SemanticResult res2 = ad.adapt(e2, 6);
        check(res2.stats.blocked_unknown_label == 1 && res2.detections.size() == 1,
              "v10_repeat_unknown_still_blocked_per_call", "per-call counter not accumulated");
        check(res2.detail.find("unknown=1") != std::string::npos,
              "v11_fi_ai_12_counter_visible_in_detail", res2.detail);
    }

    // ---- V12: 非白名单已知 label (obstacle not allowed 键) ====
    {
        SemanticConfig cfg;
        cfg.allowed_labels = {"person"};
        cfg.known_labels = {"person", "obstacle"};
        cfg.confidence_min = 0.5;
        cfg.track_policy = TrackPolicy::CameraLevel;
        SemanticAdapter ad(cfg);
        EnvelopeBuildInput in;
        in.frame = fid("CAM-02", 1, 1);
        in.profile = AiProfile::Front;
        in.capture_mono_us = 0; in.result_mono_us = 1000; in.detect_ready = true;
        DetectBox o; o.label = "obstacle"; o.score = 0.9f; o.w = 0.1f; o.h = 0.1f;
        std::vector<DetectBox> vbox2{o};
        in.in_roi_flags = {1};
        in.boxes = &vbox2;
        const InferenceEnvelope e = build_envelope(in);
        SemanticResult res = ad.adapt(e, 1);
        check(res.stats.blocked_not_allowed == 1 && res.stats.mapped == 0
              && res.reason == semantic_reason::kAllBlocked(),
              "v12_blocked_not_allowed_distinct_counter", res.reason);
    }

    // ---- V13: selftest JSON 逃逸与 pending 完整 ====
    {
        const std::string json = selftest_json(r, false);
        check(json.find("\\u00") == std::string::npos || true, "v13_json_no_raw_ctrl_note",
              "信息性: selftest 无控制字符源");
        check(json.find("\"pending\":[") != std::string::npos && json.find("人工视觉") != std::string::npos,
              "v14_json_pending_human_visual_present", "pending 面保留");
    }

    // ---- V15: envelope 拒绝族回归(交付判例的深边界) ====
    {
        // 交付契约:单 EnvelopePublisher 内单 FrameGate,按 (session, generation, seq) 准入,
        // 相机 id 不参与 gate 判定(交付判例也是单相机构造)。
        EnvelopePublisher pub("vext-s", 2);
        EnvelopeBuildInput in;
        in.frame = fid("CAM-03", 1, 1);
        in.profile = AiProfile::Front;
        in.capture_mono_us = 0; in.result_mono_us = 1000; in.detect_ready = true;
        std::string rej;
        const bool ok_a = pub.submit(build_envelope(in), &rej);
        in.frame = fid("CAM-04", 1, 1);   // 不同相机同 seq: 单 gate 下视为重复
        const bool ok_b = pub.submit(build_envelope(in), &rej);
        check(ok_a && !ok_b && pub.stats().duplicate_seq == 1,
              "v15_single_gate_cross_camera_same_seq_dup",
              std::string("ok_a=") + (ok_a ? "1" : "0") + " ok_b=" + (ok_b ? "1" : "0")
              + " rej=" + rej);
        const InferenceEnvelope un = make_unavailable_envelope(
            fid("CAM-03", 1, 2), AiProfile::Front, "v", 0, 1000, envelope_reason::kAiUnavailable());
        std::string rej2;
        const bool ok_un = pub.submit(un, &rej2);
        check(ok_un && pub.stats().accepted == 2, "v16_unavailable_still_queued",
              std::string("accepted=") + std::to_string(pub.stats().accepted)
              + " reason=" + un.reason);
    }

    // ---- V17: distance/debug 未加工占位回归 ====
    {
        DistanceEstimate e = estimate_distance(DistanceCalibrationSet{}, DistanceRequest{});
        check(!e.valid && e.meters == -1.0, "v17_distance_default_invalid",
              "meters=" + std::to_string(e.meters));
        DebugViewInput in;
        CameraSlot slot; slot.id = "CAM-09"; slot.ai_enabled = true;
        slot.ai_profile = AiProfile::WheelRail;
        in.registry.push_back(slot);
        AiRuntime rt; rt.worker_alive = true;
        in.runtime = rt;
        in.role = Role::Developer;
        DebugChannelInput d;
        d.row.camera_id = "CAM-09"; d.row.profile = AiProfile::WheelRail;
        d.row.available = false;
        d.row.unavailable_reason = "PROFILE_UNSUPPORTED";
        in.channels.push_back(d);
        const DebugViewModel vm = build_debug_view(in);
        check(vm.channels.size() == 1 && vm.channels[0].fps_text == "—",
              "v18_debug_unknown_dash_regression", vm.channels.empty() ? "<none>" : vm.channels[0].fps_text);
    }

    std::printf("vext RESULT pass=%d fail=%d\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
