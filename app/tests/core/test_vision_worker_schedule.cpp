// railhmi-core 推理工作线程调度与温度守卫测试（视觉推理 ITEM-009 纯逻辑）。
// 覆盖 TC-T-DISP-03..06 的可判定部分：温度样本语义（实测/注入/不可用）、
// plan_inference 预算与温度守卫来源、无饥饿公平轮转、决策可观察性。
// 线程/锁行为（infer 不阻塞 submit/snapshot/stop）属 target 层，由板端探针实测，不在本文件声称。

#include <map>
#include <set>
#include <string>
#include <vector>

#include "test_util.h"

#include "railhmi/core/vision_worker_schedule.h"

namespace {

using namespace railhmi::core;

ThermalSample measured(double c, long long mono_ms = 1000) {
    std::vector<std::pair<std::string, std::string>> zones;
    zones.emplace_back("thermal_zone0", std::to_string(static_cast<long long>(c * 1000)));
    return pick_thermal_sample(zones, mono_ms);
}

AiLaneDemand lane(const std::string& cam, AiProfile p, bool has = true, std::uint64_t seq = 1) {
    AiLaneDemand d;
    d.camera_id = cam;
    d.profile = p;
    d.has_frame = has;
    d.seq = seq;
    return d;
}

// 基线四路（顺序 = cameras.yaml 的 AI 路）：front / wheel_rail / pantograph / cab_door。
std::vector<AiLaneDemand> four_lanes() {
    return {lane("CAM-01", AiProfile::Front), lane("CAM-05", AiProfile::WheelRail),
            lane("CAM-04", AiProfile::Pantograph), lane("CAM-06", AiProfile::CabDoor)};
}

AiSchedulerConfig cfg_budget(int budget) {
    AiSchedulerConfig c;
    c.inference_interval = 5;
    c.queue_depth = 2;
    c.npu_budget = budget;
    c.temp_limit_c = 85.0;
    return c;
}

int count_in(const std::vector<std::string>& v, const std::string& s) {
    int n = 0;
    for (const auto& x : v)
        if (x == s) ++n;
    return n;
}

}  // namespace

void run_vision_worker_schedule_tests() {
    using namespace railhmi_test;

    // ---- 温度样本：毫摄氏度解析（实测来源） ----
    {
        double c = 0;
        std::string why;
        check(parse_thermal_millicelsius("45123\n", &c, &why) && c > 45.122 && c < 45.124,
              "thermal_parse_millicelsius", "45123 -> 45.123");
        check(parse_thermal_millicelsius(" 45600 ", &c, &why) && c > 45.599 && c < 45.601,
              "thermal_parse_trim", "surrounding whitespace tolerated");
        check(parse_thermal_millicelsius("-5000", &c, &why) && c < -4.999 && c > -5.001,
              "thermal_parse_negative", "-5000 -> -5.0 within physical range");
        check(parse_thermal_millicelsius("0", &c, &why) && c == 0.0, "thermal_parse_zero", "0 -> 0.0");
        check(!parse_thermal_millicelsius("", &c, &why) && why == thermal_reason::kMalformed(),
              "thermal_parse_empty_malformed", why);
        check(!parse_thermal_millicelsius("abc", &c, &why) && why == thermal_reason::kMalformed(),
              "thermal_parse_text_malformed", why);
        check(!parse_thermal_millicelsius("45.123", &c, &why) && why == thermal_reason::kMalformed(),
              "thermal_parse_decimal_rejected", "sysfs 为整数毫度；小数不猜测");
        check(!parse_thermal_millicelsius("999999", &c, &why) && why == thermal_reason::kOutOfRange(),
              "thermal_parse_high_out_of_range", why);
        check(!parse_thermal_millicelsius("-999999", &c, &why) && why == thermal_reason::kOutOfRange(),
              "thermal_parse_low_out_of_range", why);
    }

    // ---- 多 zone 取最大 + 全非法 ----
    {
        std::vector<std::pair<std::string, std::string>> zones = {
            {"thermal_zone0", "45000\n"}, {"thermal_zone1", "52000\n"}, {"thermal_zone2", "bad"}};
        ThermalSample s = pick_thermal_sample(zones, 7000);
        check(s.valid && s.celsius > 51.999 && s.celsius < 52.001 && s.source == "thermal_zone1" &&
                  !s.injected && s.measured() && s.reason == thermal_reason::kOk(),
              "thermal_pick_max_zone", "52.0 from thermal_zone1");
        std::vector<std::pair<std::string, std::string>> bad = {{"thermal_zone0", "n/a"}};
        ThermalSample sb = pick_thermal_sample(bad, 7000);
        check(!sb.valid && sb.reason == thermal_reason::kMalformed() && sb.source == "unavailable",
              "thermal_pick_all_invalid", sb.reason);
        ThermalSample se = pick_thermal_sample({}, 7000);
        check(!se.valid && se.reason == thermal_reason::kMissing(),
              "thermal_pick_empty_reason", se.reason);
    }

    // ---- 注入样本不得冒充实测 ----
    {
        ThermalSample inj = injected_thermal_sample(90.0, 500);
        check(inj.valid && inj.injected && !inj.measured() && inj.reason == thermal_reason::kInjected(),
              "thermal_injected_marked", "injected=true measured=false");
        check(std::string(thermal_state_token(inj)) == "INJECTED", "thermal_state_injected",
              thermal_state_token(inj));
        ThermalSample m = measured(41.5);
        check(std::string(thermal_state_token(m)) == "MEASURED", "thermal_state_measured",
              thermal_state_token(m));
        const std::string j = thermal_sample_json(inj);
        check(j.find("\"measured\":false") != std::string::npos &&
                  j.find("\"state\":\"INJECTED\"") != std::string::npos,
              "thermal_json_injected_not_measured", j);
    }

    // ---- 样本年龄 ----
    {
        ThermalSample s = measured(50.0, 1000);
        std::string why;
        check(thermal_sample_fresh(s, 3000, 5000, &why), "thermal_fresh_within_age", why);
        check(!thermal_sample_fresh(s, 7000, 5000, &why) && why == thermal_reason::kStale(),
              "thermal_stale_age", why);
        ThermalSample u;
        check(!thermal_sample_fresh(u, 3000, 5000, &why) && why == thermal_reason::kMissing(),
              "thermal_fresh_unknown_invalid", why);
    }

    // ---- 预算：优先级 + 路数（plan_inference 口径） ----
    {
        AiLaneScheduler sch(cfg_budget(4));
        AiScheduleRound r = sch.plan(four_lanes(), measured(50.0));
        check(r.run.size() == 4 && r.skipped.empty() && r.budget_effective == 4,
              "sched_budget_4_runs_all", "run=4 skipped=0");
        check(r.priority_order == std::vector<std::string>{"CAM-01", "CAM-05", "CAM-04", "CAM-06"},
              "sched_priority_order_front_wheel_panto_cab", "front>wheel_rail>pantograph>cab_door");
        check(r.run == r.priority_order, "sched_no_contention_keeps_priority", "无竞争时按优先级");
        check(r.round == 1 && r.budget_configured == 4 && !r.temperature_guard && r.thermal_known,
              "sched_round_fields", "round=1 guard=false known=true");
    }
    {
        AiLaneScheduler sch(cfg_budget(2));
        AiScheduleRound r = sch.plan(four_lanes(), measured(50.0));
        check(r.run.size() == 2 && r.skipped.size() == 2 && r.budget_effective == 2,
              "sched_budget_2_halves", "run=2 skipped=2");
        check(r.run[0] == "CAM-01" && r.run[1] == "CAM-05", "sched_budget_2_keeps_top_priority",
              "front+wheel_rail first");
        check(count_in(r.run, "CAM-01") == 1, "sched_no_duplicate_lane", "每轮每路至多一次");
    }

    // ---- 无饥饿：预算小于路数时轮转（TC-T-DISP-04） ----
    {
        AiLaneScheduler sch(cfg_budget(1));
        std::map<std::string, int> ran;
        for (int i = 0; i < 8; ++i) {
            AiScheduleRound r = sch.plan(four_lanes(), measured(50.0));
            check(r.run.size() == 1 && r.skipped.size() == 3, "sched_budget_1_one_lane_round" + std::to_string(i),
                  "run=1 skipped=3");
            ++ran[r.run[0]];
        }
        check(ran.size() == 4, "sched_no_starvation_all_lanes_ran", "4/4 lanes ran within 8 rounds");
        bool balanced = true;
        for (const auto& kv : ran) balanced = balanced && kv.second == 2;
        check(balanced, "sched_no_starvation_balanced", "each lane ran exactly twice in 8 rounds");
        check(sch.max_wait_rounds() <= 4, "sched_max_wait_bounded",
              "max wait=" + std::to_string(sch.max_wait_rounds()));
        check(sch.rounds() == 8, "sched_rounds_counted", std::to_string(sch.rounds()));
    }
    {
        // 等待轮数可观察：第 1 轮后被跳过的路 wait=1，被运行的路 wait=0。
        AiLaneScheduler sch(cfg_budget(2));
        sch.plan(four_lanes(), measured(50.0));
        check(sch.wait_rounds("CAM-01") == 0 && sch.wait_rounds("CAM-05") == 0,
              "sched_wait_zero_after_run", "运行过的路 wait=0");
        check(sch.wait_rounds("CAM-04") == 1 && sch.wait_rounds("CAM-06") == 1,
              "sched_wait_one_after_skip", "被跳过的路 wait=1");
        check(sch.wait_rounds("CAM-99") == 0, "sched_wait_unknown_lane", "未出现的路不参与");
    }

    // ---- 温度守卫（FI-AI-09 / TC-T-DISP-06 的判定部分） ----
    {
        AiLaneScheduler sch(cfg_budget(4));
        AiScheduleRound r = sch.plan(four_lanes(), injected_thermal_sample(90.0, 2000));
        check(r.temperature_guard && r.budget_effective == 2 && r.run.size() == 2,
              "sched_temp_guard_halves_budget", "90C > 85C -> budget 4->2");
        check(r.thermal.injected && !r.thermal.measured() && r.thermal_known,
              "sched_guard_uses_sample_state", "注入样本标记为 injected，不当实测");
    }
    {
        AiLaneScheduler sch(cfg_budget(4));
        AiScheduleRound r = sch.plan(four_lanes(), measured(85.0));
        check(!r.temperature_guard && r.run.size() == 4, "sched_temp_guard_strict_gt",
              "85.0C 不超过阈值 -> 不降载");
        AiScheduleRound r2 = sch.plan(four_lanes(), measured(85.5));
        check(r2.temperature_guard && r2.run.size() == 2, "sched_temp_guard_above_limit",
              "85.5C -> 降载");
    }
    {
        // 温度未知：不冒充“凉”，也不静默宣称守卫生效。
        AiLaneScheduler sch(cfg_budget(4));
        ThermalSample unknown;  // valid=false
        AiScheduleRound r = sch.plan(four_lanes(), unknown);
        check(!r.thermal_known && !r.temperature_guard && r.run.size() == 4,
              "sched_temp_unknown_not_claimed", "thermal_known=false 且不触发守卫");
    }
    {
        // 守卫下仍无饥饿：budget 减半为 2 时 4 路在两轮内各运行一次。
        AiLaneScheduler sch(cfg_budget(4));
        std::map<std::string, int> ran;
        for (int i = 0; i < 2; ++i) {
            AiScheduleRound r = sch.plan(four_lanes(), injected_thermal_sample(90.0, 2000));
            for (const auto& c : r.run) ++ran[c];
        }
        check(ran.size() == 4, "sched_guard_no_starvation", "两轮内 4 路都运行过");
        bool two_each = true;
        for (const auto& kv : ran) two_each = two_each && kv.second == 1;
        check(two_each, "sched_guard_balanced", "每路恰好 1 次");
    }

    // ---- idle / 非 AI 职责 ----
    {
        AiLaneScheduler sch(cfg_budget(4));
        std::vector<AiLaneDemand> lanes = four_lanes();
        lanes.push_back(lane("CAM-02", AiProfile::None));       // 非 AI 路
        lanes.push_back(lane("CAM-05", AiProfile::WheelRail, false));  // 重复 id + 无帧
        AiScheduleRound r = sch.plan(lanes, measured(45.0));
        check(r.run.size() == 4, "sched_idle_does_not_consume_budget", "只有 4 路有帧");
        check(count_in(r.run, "CAM-02") == 0 && count_in(r.idle, "CAM-02") == 1,
              "sched_none_profile_idle", "ai_profile=none 不排队");
        check(sch.wait_rounds("CAM-02") == 0, "sched_none_not_counted_wait",
              "非 AI 路不参与等待统计");
    }

    // ---- 预算边界 0：不崩溃、全部计入 skipped ----
    {
        AiLaneScheduler sch(cfg_budget(0));
        AiScheduleRound r = sch.plan(four_lanes(), measured(45.0));
        check(r.run.empty() && r.skipped.size() == 4 && r.budget_effective == 0,
              "sched_budget_zero_all_skipped", "budget=0 -> run=0 skipped=4");
    }

    // ---- 职责不可用路不占预算（ITEM-010 对 ITEM-009 交接项的落地） ----
    {
        auto lanes = four_lanes();
        lanes[1].duty_ready = false;   // wheel_rail：无核验模型
        lanes[2].duty_ready = false;   // pantograph
        lanes[3].duty_ready = false;   // cab_door
        AiLaneScheduler sch(cfg_budget(1));
        AiScheduleRound r = sch.plan(lanes, measured(50.0));
        check(r.run.size() == 1 && r.run[0] == "CAM-01", "sched_unavailable_not_budgeted",
              "budget=1 全用于就绪的 front");
        check(r.unavailable.size() == 3 && count_in(r.unavailable, "CAM-05") == 1,
              "sched_unavailable_listed", "三路不可用单独列出");
        check(r.skipped.empty(), "sched_unavailable_not_skipped", "不可用不计入预算跳过");
        check(sch.wait_rounds("CAM-05") == 0, "sched_unavailable_no_wait",
              "不可用路不参与等待统计");
        AiScheduleRound r2 = sch.plan(lanes, measured(50.0));
        check(r2.run.size() == 1 && r2.run[0] == "CAM-01", "sched_unavailable_budget_stable",
              "连续两轮都保持 front 独占预算");
        check(schedule_round_json(r2).find("\"unavailable\":[\"CAM-05\"") != std::string::npos,
              "sched_json_unavailable", "决策 JSON 含 unavailable");
    }
    {
        AiLaneScheduler sch(cfg_budget(2));
        AiScheduleRound r = sch.plan(four_lanes(), measured(50.0));
        check(r.unavailable.empty() && r.run.size() == 2, "sched_all_ready_regression",
              "duty_ready 缺省 true 时行为不变");
    }

    // ---- 确定性：同输入序列 -> 同输出 ----
    {
        AiLaneScheduler a(cfg_budget(2));
        AiLaneScheduler b(cfg_budget(2));
        std::string ja, jb;
        for (int i = 0; i < 3; ++i) {
            ja += schedule_round_json(a.plan(four_lanes(), measured(50.0)));
            jb += schedule_round_json(b.plan(four_lanes(), measured(50.0)));
        }
        check(ja == jb, "sched_deterministic", "两实例同输入 -> 逐字相等");
        const std::string j = schedule_round_json(a.plan(four_lanes(), measured(50.0)));
        check(j.find("\"budget_effective\"") != std::string::npos &&
                  j.find("\"temperature_guard\"") != std::string::npos &&
                  j.find("\"thermal\"") != std::string::npos,
              "sched_json_keys", "决策 JSON 含预算/守卫/温度样本");
    }
}
