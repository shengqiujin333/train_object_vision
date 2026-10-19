// ITEM-009 验证自有扩展夹具（software_tester.software_verification）。
//
// 判据域为交付单测/探针未覆盖的调度器与温度语义边界（全部为验证侧独立重推导，
// 期望值在写判据前按 vision_worker_schedule.cpp 语义逐项手工推导并离线自查）：
//   A/J: 等待轮数序列的精确轮转形态（budget=2 时轮转配对，非仅均衡统计）
//   B  : 新路中途加入：wait=round 抢占（从未运行路优先）
//   C  : 已见路从有帧转 idle 再恢复：等待计数保留（不因 idle 清零）
//   D  : budget=1 四路下 max_wait 有界（=3）且 rounds 计数
//   E  : 温度物理边界精确值：默认界 [-40,200] 含端点（-40/+40 均接受，c<min 才拒）/
//        200.001 越界 / 定制宽界含端点 / 内部空格与尾部杂非非法族
//   F  : pick_thermal_sample 全非法时保留首个非法 reason
//   G  : 注入样本门语义锚定：仅 isfinite（有限值一律 valid；NaN invalid + kOutOfRange）
//   H  : thermal_sample_fresh 半开窗：age==max_age 恰好新鲜；max_age<=0 不检查
//   I  : plan_inference 守卫下奇数预算下取整（5→2、3→1、1→1 下限）
//   K  : idle 路不参与 wait 统计；注册序不因 idle 重置
//   L  : schedule_round_json 对引号/反斜杠转义
//   M  : budget=1 恒 3 路饥饿的周期 3 轮转序（X,Y,Z 精确预言）
//
// 编译（板端）：g++ -std=c++17 -O1 -pthread -I src/core/include
//              vext_item009.cpp -o vext_item009 build-core/src/core/librailhmi_core.a

#include <cmath>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "railhmi/core/vision_worker_schedule.h"

using namespace railhmi;
using core::AiLaneDemand;
using core::AiLaneScheduler;
using core::AiProfile;
using core::AiScheduleRound;
using core::AiSchedulerConfig;
using core::ThermalLimits;
using core::ThermalSample;

namespace {

int g_fail = 0;

void check(bool ok, const std::string& name, const std::string& detail = "") {
    std::string line = ok ? "vext PASS " : "vext FAIL ";
    line += name;
    if (!detail.empty()) line += " :: " + detail;
    std::printf("%s\n", line.c_str());
    if (!ok) ++g_fail;
}

core::ThermalSample measured(double c, long long mono_ms = 1000) {
    std::vector<std::pair<std::string, std::string>> zones;
    zones.emplace_back("thermal_zone0", std::to_string(static_cast<long long>(c * 1000)));
    return core::pick_thermal_sample(zones, mono_ms);
}

AiLaneDemand lane(const std::string& cam, AiProfile p, bool has = true, std::uint64_t seq = 1) {
    AiLaneDemand d;
    d.camera_id = cam;
    d.profile = p;
    d.has_frame = has;
    d.seq = seq;
    return d;
}

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

std::string run_seq(const std::vector<AiScheduleRound>& rounds) {
    std::string s;
    for (const auto& r : rounds)
        for (const auto& cam : r.run) s += cam + ",";
    return s;
}

}  // namespace

int main() {
    // ---- A/J: budget=2 轮转配对的精确序列 ----
    {
        AiLaneScheduler sch(cfg_budget(2));
        std::vector<AiScheduleRound> rounds;
        for (int i = 0; i < 6; ++i) rounds.push_back(sch.plan(four_lanes(), measured(50.0)));
        // 手工推导（等待降序, 优先级升序, 注册序）：
        // r1: 全部 wait=1 -> 按优先级 run={01,05} skip={04,06}
        // r2: 04/06 wait=2 > 01/05 wait=1 -> run={04,06}
        // r3: 01/05 wait=2 > 04/06 wait=1 -> run={01,05}（此后周期 2 重复）
        const std::string seq = run_seq(rounds);
        check(seq == "CAM-01,CAM-05,CAM-04,CAM-06,CAM-01,CAM-05,CAM-04,CAM-06,CAM-01,CAM-05,"
                     "CAM-04,CAM-06,",
              "A_round_robin_pair_sequence", seq);
        check(rounds[0].skipped == std::vector<std::string>({"CAM-04", "CAM-06"}) &&
                  rounds[1].skipped == std::vector<std::string>({"CAM-01", "CAM-05"}),
              "J_skip_mirror_of_run", "r1 skip=04,06 r2 skip=01,05");
    }

    // ---- B: 新路中途加入 ----
    {
        AiLaneScheduler sch(cfg_budget(1));
        std::vector<AiLaneDemand> d = {lane("CAM-01", AiProfile::Front)};
        check(sch.plan(d, measured(50.0)).run == std::vector<std::string>{"CAM-01"},
              "B_solo_lane_runs", "");
        // 第 2 轮 CAM-07 加入：wait(07)=round=2 > wait(01)=1 -> 07 先行
        d.push_back(lane("CAM-07", AiProfile::CabDoor));
        AiScheduleRound r2 = sch.plan(d, measured(50.0));
        check(r2.run == std::vector<std::string>{"CAM-07"} &&
                  r2.skipped == std::vector<std::string>{"CAM-01"},
              "B_new_lane_preempts_by_wait", "wait(07)=2 > wait(01)=1");
        // 第 3 轮：01 wait=2 > 07 wait=1 -> 01 回归
        AiScheduleRound r3 = sch.plan(d, measured(50.0));
        check(r3.run == std::vector<std::string>{"CAM-01"}, "B_alternates_after_join", "");
        check(sch.wait_rounds("CAM-07") == 1 && sch.wait_rounds("CAM-01") == 0,
              "B_waits_observable", "");
    }

    // ---- C: 有帧 -> idle -> 恢复，等待计数保留 ----
    {
        AiLaneScheduler sch(cfg_budget(1));
        std::vector<AiLaneDemand> d = {lane("CAM-01", AiProfile::Front),
                                       lane("CAM-08", AiProfile::CabDoor)};
        sch.plan(d, measured(50.0));                       // r1: 01 run（优先级）
        d[1].has_frame = false;
        AiScheduleRound r2 = sch.plan(d, measured(50.0));  // r2: 08 idle; 01 wait=1 -> run
        check(r2.run == std::vector<std::string>{"CAM-01"} &&
                  r2.idle == std::vector<std::string>{"CAM-08"},
              "C_lane_goes_idle", "");
        d[1].has_frame = true;
        AiScheduleRound r3 = sch.plan(d, measured(50.0));
        // r3: 08 wait = 3-last_run(1) = 2 > 01 wait = 3-2 = 1 -> 08 run（idle 轮不清等待）
        check(r3.run == std::vector<std::string>{"CAM-08"} &&
                  r3.skipped == std::vector<std::string>{"CAM-01"},
              "C_wait_preserved_across_idle", "08 wait=2 (r3-last_run=3-1)");
        // r3 后：01 last_run=r2 -> wait=3-2=1（刚跑过 r3 的是 08）。
        check(sch.waits().at("CAM-01") == 1 && sch.waits().at("CAM-08") == 0,
              "C_waits_after_run", "01 wait=3-last_run(r2)=1; 08 last_run=r3 -> 0");
    }

    // ---- D: budget=1 四路下 max_wait 有界 ----
    {
        AiLaneScheduler sch(cfg_budget(1));
        long long max_wait_seen = 0;
        for (int i = 0; i < 12; ++i) {
            sch.plan(four_lanes(), measured(50.0));
            max_wait_seen = std::max(max_wait_seen, sch.max_wait_rounds());
        }
        check(max_wait_seen == 3, "D_max_wait_bounded_budget1",
              "max_wait==3 over 12 rounds, saw " + std::to_string(max_wait_seen));
        check(sch.rounds() == 12, "D_rounds_counted", "");
    }

    // ---- E: 温度物理边界（默认界 min=-40 / max=200；拒绝条件 c<min || c>max，含端点） ----
    {
        double c = 0;
        std::string why;
        check(core::parse_thermal_millicelsius("40000", &c, &why) && c == 40.0,
              "E_plus_40C_ok", "+40 在默认界内（min=-40/max=200）");
        check(core::parse_thermal_millicelsius("-40000", &c, &why) && c == -40.0,
              "E_minus_40C_inclusive_default", "恰等于下界：c<min 才拒 -> -40 含端点接受");
        check(core::parse_thermal_millicelsius("-39999", &c, &why) && c > -40.0 && c < -39.9,
              "E_minus_39_999_ok", "恰在 -40 上方");
        ThermalLimits wide;
        wide.min_c = -60.0;
        wide.max_c = 250.0;
        check(core::parse_thermal_millicelsius("-40000", &c, &why, wide) && c == -40.0,
              "E_minus_40_inclusive_wide", "定制界下 -40 恰在界内（c<min 才拒，含端点）");
        check(core::parse_thermal_millicelsius("200000", &c, &why, wide) && c == 200.0,
              "E_200_inclusive_wide", "定制界下 200 恰在界内（含端点）");
        check(!core::parse_thermal_millicelsius("200001", &c, &why) &&
                  why == core::thermal_reason::kOutOfRange(),
              "E_200_001_rejected", "默认 max=200 -> 200.001 越界");
        check(!core::parse_thermal_millicelsius("+ 45000", &c, &why) &&
                  why == core::thermal_reason::kMalformed(),
              "E_internal_space_malformed", "内部空格非法（trim 只剥首尾）");
        check(!core::parse_thermal_millicelsius("45000x", &c, &why) &&
                  why == core::thermal_reason::kMalformed(),
              "E_trailing_junk_malformed", "尾部非数字非法");
    }

    // ---- F: 多 zone 全非法时保留首个非法 reason ----
    {
        std::vector<std::pair<std::string, std::string>> zones = {
            {"thermal_zone0", "zzz"}, {"thermal_zone1", ""}, {"thermal_zone2", "999999"}};
        core::ThermalSample s = core::pick_thermal_sample(zones, 7000);
        check(!s.valid && s.reason == core::thermal_reason::kMalformed() &&
                  s.source == "unavailable" && s.zone_index == -1,
              "F_first_failure_reason_kept", "首个非法=malformed（zone0 zzz）");
        std::vector<std::pair<std::string, std::string>> z2 = {
            {"thermal_zone0", ""}, {"thermal_zone1", "999999"}};
        core::ThermalSample s2 = core::pick_thermal_sample(z2, 7000);
        check(!s2.valid && s2.reason == core::thermal_reason::kMalformed(),
              "F_empty_then_oor_keeps_first_reason", "首失败 reason=malformed（空串）");
    }

    // ---- G: 注入样本门语义（锚定当前契约：注入仅 isfinite 门） ----
    {
        core::ThermalSample s = core::injected_thermal_sample(999.0, 1000);
        check(s.valid && s.injected && !s.measured() &&
                  s.reason == core::thermal_reason::kInjected() &&
                  std::string(core::thermal_state_token(s)) == "INJECTED",
              "G_injected_finite_isfinite_only_gate",
              "当前契约锚定：注入不走物理范围门，999C 仍 valid+INJECTED（measured=false）");
        const double nan_v = std::nan("");
        core::ThermalSample sn = core::injected_thermal_sample(nan_v, 1000);
        check(!sn.valid && sn.reason == core::thermal_reason::kOutOfRange() &&
                  std::string(core::thermal_state_token(sn)) == "UNAVAILABLE",
              "G_injected_nan_invalid", "NaN 注入: valid=false + kOutOfRange + UNAVAILABLE");
        core::ThermalSample s2 = core::injected_thermal_sample(-55.0, 1000);
        check(s2.valid && s2.injected && !s2.measured(),
              "G_injected_cold_valid_but_not_measured", "-55C 注入不冒充实测");
        core::ThermalSample s3 = core::injected_thermal_sample(90.0, 1000);
        check(s3.valid && s3.injected && !s3.measured() &&
                  s3.reason == core::thermal_reason::kInjected(),
              "G_injected_hot_marked", "90C 注入: valid + INJECTED + measured=false");
    }

    // ---- H: fresh 半开窗 ----
    {
        core::ThermalSample s = measured(50.0, 1000);
        std::string why;
        check(core::thermal_sample_fresh(s, 6000, 5000, &why) && why.empty(),
              "H_age_equal_max_fresh", "6000-1000==5000 边界含（> 才 stale）");
        check(!core::thermal_sample_fresh(s, 6001, 5000, &why) &&
                  why == core::thermal_reason::kStale(),
              "H_age_max_plus_1_stale", "");
        check(core::thermal_sample_fresh(s, 600000, 0, &why), "H_zero_age_disables_check",
              "max_age<=0 不检查年龄");
        core::ThermalSample u;
        check(!core::thermal_sample_fresh(u, 6000, 0, &why) &&
                  why == core::thermal_reason::kMissing(),
              "H_invalid_sample_never_fresh", "max_age=0 也不救无效样本");
    }

    // ---- I: 守卫下奇数预算下取整 ----
    {
        AiSchedulerConfig c5 = cfg_budget(5);
        AiLaneScheduler sch(c5);
        std::vector<AiLaneDemand> d;
        for (int i = 0; i < 5; ++i)
            d.push_back(lane("CAM-1" + std::to_string(i), AiProfile::Front));
        AiScheduleRound r = sch.plan(d, core::injected_thermal_sample(90.0, 2000));
        check(r.temperature_guard && r.budget_effective == 2, "I_guard_odd_budget_floors",
              "max(1,5/2)=2");
        check(r.run.size() == 2 && r.skipped.size() == 3, "I_run_skip_split", "");
        AiLaneScheduler sch3(cfg_budget(3));
        AiScheduleRound r3 = sch3.plan(d, core::injected_thermal_sample(90.0, 2000));
        check(r3.temperature_guard && r3.budget_effective == 1, "I_guard_budget3_floors_to_1",
              "max(1,3/2)=1");
        AiLaneScheduler sch1(cfg_budget(1));
        AiScheduleRound r1 = sch1.plan(d, core::injected_thermal_sample(90.0, 2000));
        check(r1.temperature_guard && r1.budget_effective == 1 && r1.run.size() == 1,
              "I_guard_budget1_floors_at_1", "降载下限 1");
    }

    // ---- K: 注册序不因 idle 重置 ----
    {
        AiLaneScheduler sch(cfg_budget(2));
        std::vector<AiLaneDemand> d = {lane("CAM-A", AiProfile::Front),
                                       lane("CAM-B", AiProfile::WheelRail),
                                       lane("CAM-C", AiProfile::Pantograph)};
        sch.plan(d, measured(50.0));  // r1: A,B run（优先级），C skip
        d[1].has_frame = false;       // B idle
        AiScheduleRound r2 = sch.plan(d, measured(50.0));
        // r2: A wait=1, C wait=2 -> C 先; 剩 1 个预算给 A -> run={C,A}
        check(r2.run == std::vector<std::string>({"CAM-C", "CAM-A"}),
              "K_registration_order_stable", run_seq({r2}));
    }

    // ---- L: schedule_round_json 对特殊字符转义 ----
    {
        AiLaneScheduler sch(cfg_budget(1));
        std::vector<AiLaneDemand> d = {lane("CAM\"\\01", AiProfile::Front)};
        AiScheduleRound r = sch.plan(d, measured(50.0));
        const std::string j = core::schedule_round_json(r);
        check(j.find("CAM\\\"\\\\01") != std::string::npos,
              "L_json_escapes_quote_backslash", j.substr(0, 120));
        check(j.front() == '{' && j.back() == '}', "L_json_braced", "");
    }

    // ---- M: budget=1 恒 3 路饥饿的周期 3 轮转序 ----
    {
        AiLaneScheduler sch(cfg_budget(1));
        std::vector<AiLaneDemand> d = {lane("CAM-X", AiProfile::Front),
                                       lane("CAM-Y", AiProfile::WheelRail),
                                       lane("CAM-Z", AiProfile::Pantograph)};
        std::vector<AiScheduleRound> rounds;
        for (int i = 0; i < 6; ++i) rounds.push_back(sch.plan(d, measured(50.0)));
        // 手工推导：
        // r1: 全 wait=1 -> X(优先级1)；r2: Y/Z wait=2 -> Y(2<3)；r3: Z wait=3 -> Z
        // r4: X wait=4-1=3 -> X；r5: Y wait=5-2=3 -> Y；r6: Z wait=6-3=3 -> Z
        const std::string seq = run_seq(rounds);
        check(seq == "CAM-X,CAM-Y,CAM-Z,CAM-X,CAM-Y,CAM-Z,", "M_period3_rotation_exact", seq);
        // max_wait 为记账后口径（本轮已 run 的路 last_run 已更新）-> 稳态峰值可观测 2。
        check(sch.max_wait_rounds() == 2, "M_max_wait_post_run_accounting",
              "记账后口径峰值 2（运行前峰值 3 出现在排序内部，不对外暴露）");
    }

    if (g_fail == 0) std::printf("vext OK (0 failures)\n");
    else std::printf("vext FAILED (%d failures)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
