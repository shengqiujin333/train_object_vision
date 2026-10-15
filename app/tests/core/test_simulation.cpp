// railhmi-core 模拟输入与故障注入测试（ITEM-009 / TC-SIM-01、TEST_DESIGN FI-01..05/FI-10）。
// 无需相机：注入故障后断言进入对应状态、可恢复，且注入参数与观测写入结构化日志。

#include "test_util.h"

#include <string>
#include <vector>

#include "railhmi/core/simulation.h"

namespace {

using namespace railhmi::core;

bool has_obs(const std::vector<Observation>& o, const std::string& type, const std::string& substr) {
    for (const auto& x : o) {
        if (x.type == type && x.detail.find(substr) != std::string::npos) return true;
    }
    return false;
}

std::string summary(const std::vector<Observation>& o) {
    for (const auto& x : o) if (x.type == "summary") return x.detail;
    return std::string();
}

int alarm_count_after(const std::vector<Observation>& o, long long t_ms) {
    int n = 0;
    for (const auto& x : o) if (x.type == "alarm" && x.t_ms >= t_ms) ++n;
    return n;
}

StreamThresholds fast_stream() {
    StreamThresholds s;
    s.first_frame_timeout_ms = 500;
    s.freeze_timeout_ms = 500;
    s.reconnect_threshold = 2;
    s.reconnect_backoff_ms = 200;
    return s;
}

SimulationConfig base_config() {
    SimulationConfig c;
    c.camera_id = "CAM-01";
    c.profile = AiProfile::Front;
    c.duration_ms = 6000;
    c.step_ms = 500;
    c.fault_onset_ms = 1000;
    c.fault_recovery_ms = 4500;
    c.stream = fast_stream();
    RuleConfig r;
    r.confirm_frames = 1;
    r.confirm_window_ms = 100;
    r.confidence_min = 0.5;
    r.suppress_window_ms = 500;
    c.rules = r;
    return c;
}

void test_enum() {
    railhmi_test::check(std::string(fault_id(FaultKind::StreamCut)) == "FI-01", "FI-01 id");
    railhmi_test::check(std::string(fault_id(FaultKind::RecordingInterruption)) == "FI-10", "FI-10 id");
    railhmi_test::check(std::string(to_string(FaultKind::PtzOutOfTolerance)) == "ptz_out_of_tolerance",
                        "fault to_string");
    FaultKind k;
    railhmi_test::check(parse_fault("01", &k) && k == FaultKind::StreamCut, "parse '01'");
    railhmi_test::check(parse_fault("fi-10", &k) && k == FaultKind::RecordingInterruption, "parse 'fi-10'");
    railhmi_test::check(parse_fault("freeze", &k) && k == FaultKind::Freeze, "parse 'freeze'");
    railhmi_test::check(!parse_fault("99", &k), "parse invalid fault rejected");
}

void test_injection_log() {
    SimulationConfig c = base_config();
    c.faults = {FaultKind::StreamCut, FaultKind::DiskLow};
    SimulationRunner r(c);
    auto obs = r.run();
    railhmi_test::check(has_obs(obs, "injection", "faults=FI-01,FI-04"), "injection params logged");
    railhmi_test::check(has_obs(obs, "injection", "camera=CAM-01"), "injection camera logged");
    railhmi_test::check(!format_observation(obs.front()).empty(), "format_observation non-empty");
}

void test_fi01_stream_cut() {
    SimulationConfig c = base_config();
    c.faults = {FaultKind::StreamCut};
    SimulationRunner r(c);
    auto obs = r.run();
    railhmi_test::check(has_obs(obs, "stream_state", "state=reconnecting"), "FI-01 -> reconnecting");
    railhmi_test::check(has_obs(obs, "stream_state", "state=offline"), "FI-01 -> offline escalation");
    railhmi_test::check(has_obs(obs, "stream_state", "state=live"), "FI-01 recovers to live");
    railhmi_test::check(summary(obs).find("final_state=live") != std::string::npos,
                        "FI-01 final state live", summary(obs));
}

void test_fi02_freeze() {
    SimulationConfig c = base_config();
    c.faults = {FaultKind::Freeze};
    SimulationRunner r(c);
    auto obs = r.run();
    railhmi_test::check(has_obs(obs, "stream_state", "state=frozen"), "FI-02 -> frozen");
    railhmi_test::check(has_obs(obs, "stream_state", "state=live"), "FI-02 recovers to live");
}

void test_fi03_ai_exit() {
    SimulationConfig c = base_config();
    c.faults = {FaultKind::AiExit};
    c.fault_onset_ms = 0;
    c.fault_recovery_ms = 0;  // 全程 AI 不可用
    SimulationRunner r(c);
    auto obs = r.run();
    railhmi_test::check(has_obs(obs, "ai", "status=unavailable"), "FI-03 -> ai unavailable");
    railhmi_test::check(alarm_count_after(obs, 0) == 0, "FI-03 no detections -> no alarms");
    railhmi_test::check(summary(obs).find("alarms=0") != std::string::npos, "FI-03 summary alarms=0",
                        summary(obs));
}

void test_fi04_disk_low() {
    SimulationConfig c = base_config();
    c.faults = {FaultKind::DiskLow};
    SimulationRunner r(c);
    auto obs = r.run();
    railhmi_test::check(has_obs(obs, "storage", "low=true"), "FI-04 -> storage low");
    railhmi_test::check(has_obs(obs, "storage", "record_available=false"), "FI-04 record_available=false");
    railhmi_test::check(has_obs(obs, "storage", "low=false"), "FI-04 recovers (low=false after)");
}

void test_fi05_ptz() {
    SimulationConfig c = base_config();
    c.faults = {FaultKind::PtzOutOfTolerance};
    SimulationRunner r(c);
    auto obs = r.run();
    railhmi_test::check(has_obs(obs, "ptz", "tolerance_ok=false"), "FI-05 -> ptz tolerance fail");
    railhmi_test::check(has_obs(obs, "ptz", "tolerance_ok=true"), "FI-05 recovers tolerance");
}

void test_fi10_recording_interruption() {
    SimulationConfig c = base_config();
    c.faults = {FaultKind::RecordingInterruption};
    SimulationRunner r(c);
    auto obs = r.run();
    railhmi_test::check(has_obs(obs, "gap", "cam=CAM-01"), "FI-10 -> recording gap logged");
    railhmi_test::check(summary(obs).find("gaps=") != std::string::npos &&
                            summary(obs).find("gaps=0") == std::string::npos,
                        "FI-10 summary has gaps", summary(obs));
}

void test_alarms_without_fault() {
    SimulationConfig c = base_config();
    c.faults = {};
    SimulationRunner r(c);
    auto obs = r.run();
    railhmi_test::check(has_obs(obs, "alarm", "type=person_on_rail"), "no fault -> front alarm produced");
    railhmi_test::check(summary(obs).find("alarms=1") != std::string::npos, "no fault summary alarms=1",
                        summary(obs));
}

void test_deterministic_reset() {
    SimulationConfig c = base_config();
    c.faults = {FaultKind::StreamCut, FaultKind::RecordingInterruption};
    SimulationRunner r1(c);
    SimulationRunner r2(c);  // 重新实例化 = 重置
    auto o1 = r1.run();
    auto o2 = r2.run();
    bool same = o1.size() == o2.size();
    for (std::size_t i = 0; same && i < o1.size(); ++i) {
        if (format_observation(o1[i]) != format_observation(o2[i])) same = false;
    }
    railhmi_test::check(same, "simulation is deterministic/resettable");
}

void test_profiles() {
    {   // pantograph profile -> abnormal alarm
        SimulationConfig c = base_config();
        c.profile = AiProfile::Pantograph;
        c.faults = {};
        SimulationRunner r(c);
        auto obs = r.run();
        railhmi_test::check(has_obs(obs, "alarm", "type=pantograph_abnormal level=L2"),
                            "pantograph injection -> L2 alarm");
    }
    {   // none profile -> no alarms
        SimulationConfig c = base_config();
        c.profile = AiProfile::None;
        c.faults = {};
        SimulationRunner r(c);
        auto obs = r.run();
        railhmi_test::check(summary(obs).find("alarms=0") != std::string::npos,
                            "none profile -> no alarms", summary(obs));
    }
}

}  // namespace

void run_simulation_tests() {
    test_enum();
    test_injection_log();
    test_fi01_stream_cut();
    test_fi02_freeze();
    test_fi03_ai_exit();
    test_fi04_disk_low();
    test_fi05_ptz();
    test_fi10_recording_interruption();
    test_alarms_without_fault();
    test_deterministic_reset();
    test_profiles();
}
