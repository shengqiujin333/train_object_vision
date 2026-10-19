// railhmi-core 无相机自检测试（视觉推理 ITEM-013）。
// 覆盖：自检用例全过、用例 id 唯一且覆盖所需域、pending 标注待执行项、JSON 结构与确定性、
// 关键判例证据在场、JSON 不含设备地址/凭据。

#include <set>
#include <string>
#include <vector>

#include "test_util.h"

#include "railhmi/core/vision_selftest.h"

namespace {

using namespace railhmi::core;

bool contains(const std::string& hay, const std::string& needle) {
    return hay.find(needle) != std::string::npos;
}

const SelfTestCase* find_case(const SelfTestReport& r, const std::string& id) {
    for (const auto& c : r.cases)
        if (c.id == id) return &c;
    return nullptr;
}

}  // namespace

void run_vision_selftest_tests() {
    using namespace railhmi_test;

    const SelfTestReport r = run_vision_selftest();

    // ---- 全部判例通过 + 覆盖域 ----
    {
        check(r.failed == 0 && r.passed == static_cast<int>(r.cases.size()) && !r.cases.empty(),
              "selftest_all_cases_pass",
              "passed=" + std::to_string(r.passed) + " failed=" + std::to_string(r.failed));
        check(r.cases.size() >= 12, "selftest_case_count", std::to_string(r.cases.size()));
        check(r.no_device, "selftest_no_device_flag", "无相机/无 RKNN 运行");
        check(r.schema == "railhmi.vision.selftest.v1", "selftest_schema", r.schema);

        std::set<std::string> ids;
        std::set<std::string> areas;
        for (const auto& c : r.cases) {
            ids.insert(c.id);
            areas.insert(c.area);
        }
        check(ids.size() == r.cases.size(), "selftest_ids_unique",
              std::to_string(ids.size()) + "/" + std::to_string(r.cases.size()));
        for (const char* need : {"model", "preprocess", "detect", "seg", "two_stage", "envelope",
                                 "availability", "debug"})
            check(areas.count(need) == 1, std::string("selftest_area_") + need, need);

        bool all_have_requirement = true;
        for (const auto& c : r.cases)
            if (c.requirement.empty()) all_have_requirement = false;
        check(all_have_requirement, "selftest_cases_reference_requirement", "每判例标注 TC/FI");

        // TC-T-SELF-01 要求覆盖 FI-AI-01/02/04/07/10/12（FI-AI-12 = 未知 label 拦截）。
        for (const char* fi : {"FI-AI-01", "FI-AI-02", "FI-AI-04", "FI-AI-07", "FI-AI-10",
                               "FI-AI-12"}) {
            bool covered = false;
            for (const auto& c : r.cases)
                if (contains(c.requirement, fi)) covered = true;
            check(covered, std::string("selftest_covers_") + fi, fi);
        }
    }

    // ---- 待执行项明确标注（TC-T-SELF-03） ----
    {
        check(!r.pending.empty(), "selftest_pending_not_empty",
              std::to_string(r.pending.size()) + " item(s)");
        bool has_precision = false;
        bool has_visual = false;
        for (const auto& p : r.pending) {
            if (contains(p, "精度") || contains(p, "检出率")) has_precision = true;
            if (contains(p, "人工视觉")) has_visual = true;
        }
        check(has_precision, "selftest_pending_accuracy", "真机精度/检出率标注待执行");
        check(has_visual, "selftest_pending_human_visual", "人工视觉确认标注待执行");
    }

    // ---- 关键判例的证据在场 ----
    {
        const SelfTestCase* box = find_case(r, "detect.synthetic_box");
        check(box && box->pass && box->evidence.count("expected_x") == 1 &&
                  box->evidence.at("boxes") == "1",
              "selftest_detect_box_evidence", box ? box->evidence.at("x") : "");

        const SelfTestCase* seg = find_case(r, "seg.synthetic_mask");
        check(seg && seg->pass && seg->evidence.count("positive") == 1 &&
                  seg->evidence.at("mask_w") == "640",
              "selftest_seg_mask_evidence", seg ? seg->evidence.at("positive") : "");

        const SelfTestCase* pre = find_case(r, "preprocess.synthetic_letterbox");
        check(pre && pre->pass && pre->evidence.at("pad_y") == "140",
              "selftest_preprocess_letterbox_evidence", pre ? pre->evidence.at("pad_y") : "");

        const SelfTestCase* env = find_case(r, "envelope.stale_duplicate_out_of_order");
        check(env && env->pass && env->evidence.at("stale_generation") == "1" &&
                  env->evidence.at("duplicate_seq") == "1" &&
                  env->evidence.at("out_of_order_seq") == "1",
              "selftest_envelope_counters", env ? env->evidence.at("accepted") : "");

        const SelfTestCase* ai = find_case(r, "availability.ai_stopped");
        check(ai && ai->pass && ai->evidence.at("status_text") == "AI 不可用",
              "selftest_ai_unavailable_text", ai ? ai->evidence.at("status_text") : "");

        const SelfTestCase* no_mask = find_case(r, "two_stage.mask_missing_unavailable");
        check(no_mask && no_mask->pass && no_mask->evidence.at("available") == "false" &&
                  no_mask->evidence.at("alarm_hits") == "0",
              "selftest_two_stage_no_mask", no_mask ? no_mask->evidence.at("reason") : "");

        const SelfTestCase* sem = find_case(r, "semantic.unknown_label_intercepted");
        check(sem && sem->pass && sem->evidence.at("blocked_unknown_label") == "1" &&
                  sem->evidence.at("mapped") == "1" && sem->evidence.at("events") == "1",
              "selftest_unknown_label_intercepted",
              sem ? ("blocked=" + sem->evidence.at("blocked_unknown_label")) : "");

        const SelfTestCase* empty = find_case(r, "preprocess.empty_frame");
        check(empty && empty->pass && !empty->evidence.at("reason").empty(),
              "selftest_empty_frame_reason", empty ? empty->evidence.at("reason") : "");
    }

    // ---- JSON 结构 + 确定性 ----
    {
        const std::string j1 = selftest_json(r, false);
        const std::string j2 = selftest_json(run_vision_selftest(), false);
        check(j1 == j2, "selftest_json_deterministic", "两次运行（排除 timestamp）逐字相同");
        check(contains(j1, "\"schema\":\"railhmi.vision.selftest.v1\"") &&
                  contains(j1, "\"no_device\":true") &&
                  contains(j1, "\"summary\":{") && contains(j1, "\"cases\":["),
              "selftest_json_shape", j1.substr(0, 120));
        check(contains(j1, "\"status\":\"PASS\"") && !contains(j1, "\"status\":\"FAIL\""),
              "selftest_json_all_pass", "JSON 全 PASS");
        check(contains(j1, "真机精度") && contains(j1, "人工视觉"),
              "selftest_json_pending_markers", "pending 在 JSON 中可见");
        check(!contains(j1, "192.168.") && !contains(j1, "rtsp://") &&
                  !contains(j1, "TRAIN_"),
              "selftest_json_no_device_or_secret", "不含地址/URL/凭据变量名");

        const std::string jt = selftest_json(r, true);
        check(contains(jt, "\"timestamp_ms\":"), "selftest_json_timestamp_optional",
              "归档模式含时间戳");
        check(jt.find("\"timestamp_ms\":") != std::string::npos,
              "selftest_json_timestamp_present", "timestamp 存在");
    }

    // ---- 用例 id 列表与报告一致 ----
    {
        const std::vector<std::string> ids = selftest_case_ids();
        check(ids.size() == r.cases.size(), "selftest_case_ids_match",
              std::to_string(ids.size()));
        bool same_order = true;
        for (std::size_t i = 0; i < ids.size() && i < r.cases.size(); ++i)
            if (ids[i] != r.cases[i].id) same_order = false;
        check(same_order, "selftest_case_ids_stable_order", "--list 与报告顺序一致");
    }
}
