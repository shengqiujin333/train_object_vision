// software_verification extended fixtures for ITEM-009 (verifier-owned).
// Beyond implementer first-party cover (test_simulation.cpp): window containment,
// FI-01-over-FI-02 precedence when both injected, FI-02-only full progression
// (frozen->reconnecting->connecting->offline->live), gap-merge envelope for FI-10,
// FI-04/FI-05 exact on/off partitioning over the fault window, FI-03 no-recovery
// (recovery<=onset)，determinism on a different config, format contract, and
// parse_fault alias/negative matrix. Links librailhmi_core.a only (no Qt/GStreamer).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "railhmi/core/simulation.h"

using namespace railhmi::core;

static int fx_pass = 0, fx_fail = 0;
static void fxc(bool cond, const char* msg) {
    if (cond) { ++fx_pass; std::printf("[PASS] %s\n", msg); }
    else      { ++fx_fail; std::printf("[FAIL] %s\n", msg); }
}

namespace {

SimulationConfig base_ext() {
    SimulationConfig c;
    c.camera_id = "CAM-99";
    c.profile = AiProfile::Front;
    c.duration_ms = 4000;
    c.step_ms = 250;
    c.fault_onset_ms = 1000;
    c.fault_recovery_ms = 3000;
    StreamThresholds s;
    s.first_frame_timeout_ms = 500;
    s.freeze_timeout_ms = 500;
    s.reconnect_threshold = 2;
    s.reconnect_backoff_ms = 200;
    c.stream = s;
    RuleConfig r;
    r.confirm_frames = 1;
    r.confirm_window_ms = 100;
    r.confidence_min = 0.5;
    r.suppress_window_ms = 500;
    c.rules = r;
    return c;
}

int count_obs(const std::vector<Observation>& o, const std::string& type,
              const std::string& substr = std::string()) {
    int n = 0;
    for (const auto& x : o) {
        if (x.type != type) continue;
        if (!substr.empty() && x.detail.find(substr) == std::string::npos) continue;
        ++n;
    }
    return n;
}

int count_obs(const std::vector<Observation>& o, const std::string& type,
              const char* substr, long long t_min, long long t_max) {
    int n = 0;
    for (const auto& x : o) {
        if (x.type != type) continue;
        if (x.t_ms < t_min || x.t_ms > t_max) continue;
        if (substr && *substr && x.detail.find(substr) == std::string::npos) continue;
        ++n;
    }
    return n;
}

bool has_obs(const std::vector<Observation>& o, const std::string& type,
             const std::string& substr, long long t_min, long long t_max) {
    for (const auto& x : o) {
        if (x.type != type) continue;
        if (x.t_ms < t_min || x.t_ms > t_max) continue;
        if (x.detail.find(substr) != std::string::npos) return true;
    }
    return false;
}

bool has_state(const std::vector<Observation>& o, const std::string& state,
               long long t_min, long long t_max) {
    for (const auto& x : o) {
        if (x.type != "stream_state") continue;
        if (x.t_ms < t_min || x.t_ms > t_max) continue;
        if (x.detail.find("state=" + state) == 0) return true;
    }
    return false;
}

bool live_and_presentable(const std::vector<Observation>& o, long long t_min, long long t_max) {
    for (const auto& x : o) {
        if (x.type != "stream_state") continue;
        if (x.t_ms < t_min || x.t_ms > t_max) continue;
        if (x.detail.find("state=live reconnect_count=0 presentable=true") == 0) return true;
    }
    return false;
}

std::string summary_of(const std::vector<Observation>& o) {
    for (const auto& x : o) if (x.type == "summary") return x.detail;
    return std::string();
}

// ---------------- 1. parse_fault alias / negative matrix ----------------
void test_parse_matrix() {
    FaultKind k;
    fxc(parse_fault("01", &k) && k == FaultKind::StreamCut &&
        parse_fault("1", &k) && k == FaultKind::StreamCut &&
        parse_fault("fi-01", &k) && k == FaultKind::StreamCut &&
        parse_fault("FI-01", &k) && k == FaultKind::StreamCut &&
        parse_fault("stream_cut", &k) && k == FaultKind::StreamCut &&
        parse_fault("streamcut", &k) && k == FaultKind::StreamCut,
        "fx parse: FI-01 numeric/fi-/case/alias forms");
    fxc(parse_fault("02", &k) && k == FaultKind::Freeze &&
        parse_fault("2", &k) && k == FaultKind::Freeze &&
        parse_fault("freeze", &k) && k == FaultKind::Freeze,
        "fx parse: FI-02 forms");
    fxc(parse_fault("03", &k) && k == FaultKind::AiExit &&
        parse_fault("ai_exit", &k) && k == FaultKind::AiExit &&
        parse_fault("aiexit", &k) && k == FaultKind::AiExit,
        "fx parse: FI-03 forms");
    fxc(parse_fault("04", &k) && k == FaultKind::DiskLow &&
        parse_fault("disk_low", &k) && k == FaultKind::DiskLow &&
        parse_fault("disklow", &k) && k == FaultKind::DiskLow,
        "fx parse: FI-04 forms");
    fxc(parse_fault("05", &k) && k == FaultKind::PtzOutOfTolerance &&
        parse_fault("ptz", &k) && k == FaultKind::PtzOutOfTolerance &&
        parse_fault("ptz_out_of_tolerance", &k) && k == FaultKind::PtzOutOfTolerance,
        "fx parse: FI-05 forms");
    fxc(parse_fault("10", &k) && k == FaultKind::RecordingInterruption &&
        parse_fault("recording_interruption", &k) && k == FaultKind::RecordingInterruption &&
        parse_fault("recording", &k) && k == FaultKind::RecordingInterruption &&
        parse_fault("FI-10", &k) && k == FaultKind::RecordingInterruption,
        "fx parse: FI-10 forms");
    fxc(!parse_fault("", &k) && !parse_fault("0", &k) && !parse_fault("99", &k) &&
        !parse_fault("fi-", &k) && !parse_fault("fi-02x", &k) &&
        !parse_fault("stream_cuts", &k) && !parse_fault("rec", &k),
        "fx parse: near-miss and empty tokens rejected");
}

// ---------------- 2. FI-01 window containment ----------------
void test_fi01_window() {
    SimulationConfig c = base_ext();
    c.faults = {FaultKind::StreamCut};
    const std::vector<Observation> o = SimulationRunner(c).run();
    fxc(live_and_presentable(o, 0, 750),
        "fx FI-01: live+presentable before onset");
    fxc(has_state(o, "reconnecting", 1000, 1000),
        "fx FI-01: reconnecting at onset (count 1 < threshold 2)");
    fxc(has_state(o, "offline", 1250, 2750),
        "fx FI-01: offline at second source error (threshold 2)");
    fxc(count_obs(o, "stream_state", "presentable=false", 1000LL, 2750LL) == 8,
        "fx FI-01: not presentable during whole fault window (8 ticks)");
    fxc(live_and_presentable(o, 3000, 4000),
        "fx FI-01: recovers to live at recovery boundary");
    fxc(summary_of(o).find("final_state=live") != std::string::npos,
        "fx FI-01: summary final_state=live");
    // 报警只在窗口外产生（窗口内无帧）
    int alarms_in_window = 0;
    for (const auto& x : o)
        if (x.type == "alarm" && x.t_ms >= 1000 && x.t_ms < 3000) ++alarms_in_window;
    fxc(alarms_in_window == 0, "fx FI-01: no alarm emitted inside fault window");
    std::string s = summary_of(o);
    const std::size_t p = s.find("alarms=");
    int alarms_summary = p == std::string::npos ? -1 : std::atoi(s.c_str() + p + 7);
    fxc(alarms_summary == count_obs(o, "alarm", ""),
        "fx FI-01: summary alarms count == observed alarm lines");
    fxc(summary_of(o).find("gaps=0") != std::string::npos,
        "fx FI-01: no gaps when FI-10 not injected");
}

// ---------------- 3. FI-01 over FI-02 precedence ----------------
void test_precedence_cut_over_freeze() {
    SimulationConfig c = base_ext();
    c.faults = {FaultKind::StreamCut, FaultKind::Freeze};
    const std::vector<Observation> o = SimulationRunner(c).run();
    fxc(count_obs(o, "stream_state", "state=frozen") == 0,
        "fx precedence: combined FI-01+FI-02 never enters frozen (cut wins per implementation)");
    fxc(has_obs(o, "injection", "faults=FI-01,FI-02", 0, 0),
        "fx precedence: both faults recorded in injection line");
    fxc(has_state(o, "offline", 1250, 2750) &&
        live_and_presentable(o, 3000, 4000),
        "fx precedence: cut path still escalates and recovers");
}

// ---------------- 4. FI-02 alone: freeze progression through states ----------------
void test_fi02_progression() {
    SimulationConfig c = base_ext();
    c.faults = {FaultKind::Freeze};
    const std::vector<Observation> o = SimulationRunner(c).run();
    // 时序: t=1000 冻结激活但仍 live(距末帧 250<500); t=1250 FROZEN; t=1750
    // frozen 超时 -> reconnect(1); t=2000 backoff(200) 满 -> connecting; t=2500
    // 首帧超时(500) -> reconnect(2)=阈值 -> OFFLINE; t=3000 恢复 -> live
    fxc(has_state(o, "live", 750, 750),
        "fx FI-02: live until onset tick");
    fxc(has_state(o, "frozen", 1250, 1500),
        "fx FI-02: frozen reached one step after last frame (+freeze_timeout)");
    fxc(has_obs(o, "stream_state", "presentable=false", 1250, 2750),
        "fx FI-02: frozen/offline frame not presentable as live");
    fxc(has_state(o, "reconnecting", 1750, 1750),
        "fx FI-02: frozen timeout -> reconnecting (count 1)");
    fxc(has_state(o, "connecting", 2000, 2250),
        "fx FI-02: reconnecting backoff -> connecting");
    fxc(has_state(o, "offline", 2500, 2750),
        "fx FI-02: second reconnect escalation -> offline");
    fxc(live_and_presentable(o, 3000, 4000),
        "fx FI-02: recovery at boundary -> live");
    fxc(summary_of(o).find("final_state=live") != std::string::npos,
        "fx FI-02: summary final_state=live");
}

// ---------------- 5. FI-10 gap merge envelope ----------------
void test_fi10_merge_envelope() {
    SimulationConfig c = base_ext();
    c.faults = {FaultKind::RecordingInterruption};
    const std::vector<Observation> o = SimulationRunner(c).run();
    // 窗口 [1000,3000)，step 250 -> 8 次相邻 add，应合并为单一缺口 [1000,3000]
    int gap_lines = 0;
    long long max_t1 = -1, min_t0 = 1LL << 62;
    int gaps_other_than_1 = 0;
    bool window_ok = true;
    for (const auto& x : o) {
        if (x.type != "gap") continue;
        ++gap_lines;
        if (x.t_ms < 1000 || x.t_ms >= 3000) window_ok = false;
        const std::size_t p1 = x.detail.find("t0_ms=");
        const std::size_t p2 = x.detail.find(" t1_ms=");
        const std::size_t p3 = x.detail.find(" gap_count=");
        if (p1 == std::string::npos || p2 == std::string::npos || p3 == std::string::npos)
            { window_ok = false; continue; }
        const long long t0 = std::atoll(x.detail.c_str() + p1 + 6);
        const long long t1 = std::atoll(x.detail.c_str() + p2 + 7);
        const long long gc = std::atoll(x.detail.c_str() + p3 + 11);
        if (t0 < min_t0) min_t0 = t0;
        if (t1 > max_t1) max_t1 = t1;
        if (gc != 1) ++gaps_other_than_1;
    }
    fxc(gap_lines == 8, "fx FI-10: one line per in-window tick (8)");
    fxc(window_ok, "fx FI-10: gap lines only inside fault window");
    fxc(gaps_other_than_1 == 0, "fx FI-10: merged count stays 1 across adjacent adds");
    fxc(min_t0 == 1000 && max_t1 == 3000,
        "fx FI-10: merged gap spans [onset, recovery)");
    fxc(summary_of(o).find("gaps=1") != std::string::npos,
        "fx FI-10: summary gaps=1");
    fxc(has_obs(o, "storage", "low=false", 0, 4000),
        "fx FI-10: storage unaffected when disk fault not injected");
}

// ---------------- 6. FI-04 storage partitioning ----------------
void test_fi04_partition() {
    SimulationConfig c = base_ext();
    c.faults = {FaultKind::DiskLow};
    const std::vector<Observation> o = SimulationRunner(c).run();
    fxc(count_obs(o, "storage", "low=false record_available=true", 0LL, 750LL) == 4,
        "fx FI-04: low=false before onset (4 ticks)");
    fxc(count_obs(o, "storage", "low=true record_available=false", 1000LL, 2750LL) == 8,
        "fx FI-04: low=true + record_available=false across window (8 ticks)");
    fxc(count_obs(o, "storage", "low=false record_available=true", 3000LL, 4000LL) == 5,
        "fx FI-04: recovers low=false at boundary (5 ticks)");
    fxc(count_obs(o, "storage", "low=", 0LL, 4000LL) == 17,
        "fx FI-04: one storage observation per tick, none missing");
}

// ---------------- 7. FI-05 partitioning ----------------
void test_fi05_partition() {
    SimulationConfig c = base_ext();
    c.faults = {FaultKind::PtzOutOfTolerance};
    const std::vector<Observation> o = SimulationRunner(c).run();
    fxc(count_obs(o, "ptz", "tolerance_ok=false", 1000LL, 2750LL) == 8,
        "fx FI-05: tolerance_ok=false exactly inside window (8)");
    fxc(count_obs(o, "ptz", "tolerance_ok=true", 0LL, 750LL) == 4 &&
        count_obs(o, "ptz", "tolerance_ok=true", 3000LL, 4000LL) == 5,
        "fx FI-05: tolerance_ok=true before onset and after recovery");
    fxc(has_obs(o, "ptz", "target_zoom=10.000000 actual_zoom=10.000000", 0, 4000),
        "fx FI-05: target/actual values recorded alongside tolerance result");
}

// ---------------- 8. FI-03 no-recovery (recovery <= onset) ----------------
void test_fi03_norecovery() {
    SimulationConfig c = base_ext();
    c.faults = {FaultKind::AiExit};
    c.fault_onset_ms = 0;
    c.fault_recovery_ms = 0;  // recovery<=onset: 全程不解除
    const std::vector<Observation> o = SimulationRunner(c).run();
    fxc(count_obs(o, "ai", "status=unavailable") == 17,
        "fx FI-03: ai unavailable for all 17 ticks (recovery<=onset never lifts)");
    fxc(count_obs(o, "alarm", "") == 0 && summary_of(o).find("alarms=0") != std::string::npos,
        "fx FI-03: no alarm lines and summary alarms=0");
    fxc(count_obs(o, "stream_state", "state=live") == 17,
        "fx FI-03: stream unaffected by AI exit (live all ticks)");
    fxc(count_obs(o, "storage", "low=false") == 17 &&
        count_obs(o, "ptz", "tolerance_ok=true") == 17,
        "fx FI-03: other channels keep reporting healthy");
}

// ---------------- 9. determinism on a different config ----------------
void test_determinism_other_config() {
    SimulationConfig c = base_ext();
    c.duration_ms = 1000;
    c.step_ms = 100;
    c.fault_onset_ms = 300;
    c.fault_recovery_ms = 700;
    c.faults = {FaultKind::StreamCut, FaultKind::Freeze, FaultKind::DiskLow,
                FaultKind::PtzOutOfTolerance, FaultKind::RecordingInterruption};
    const std::vector<Observation> o1 = SimulationRunner(c).run();
    const std::vector<Observation> o2 = SimulationRunner(c).run();
    bool same = o1.size() == o2.size();
    for (std::size_t i = 0; same && i < o1.size(); ++i)
        same = format_observation(o1[i]) == format_observation(o2[i]);
    fxc(same, "fx determinism: two cold instances byte-identical (step 100, 5 faults)");
    fxc(!o1.empty() && format_observation(o1.front()).find("faults=FI-01,FI-02,FI-04,FI-05,FI-10")
            != std::string::npos,
        "fx determinism: injection line lists FI ids in config order");
}

// ---------------- 10. format contract ----------------
void test_format_contract() {
    SimulationConfig c = base_ext();
    c.faults = {FaultKind::StreamCut};
    const std::vector<Observation> o = SimulationRunner(c).run();
    bool shape_ok = true;
    for (const auto& x : o) {
        const std::string line = format_observation(x);
        if (line.rfind("t=", 0) != 0) shape_ok = false;
        if (line.find(" type=") == std::string::npos) shape_ok = false;
    }
    fxc(shape_ok && !o.empty(), "fx format: every line is 't=<ms> type=<t> ...'");
    fxc(format_observation(o.front()).find("camera=CAM-99") != std::string::npos &&
        format_observation(o.front()).find("profile=front") != std::string::npos &&
        format_observation(o.front()).find("onset_ms=1000") != std::string::npos &&
        format_observation(o.front()).find("recovery_ms=3000") != std::string::npos &&
        format_observation(o.front()).find("duration_ms=4000") != std::string::npos &&
        format_observation(o.front()).find("step_ms=250") != std::string::npos,
        "fx format: injection line records all inject parameters");
    fxc(format_observation(o.back()).rfind("t=4000 type=summary final_state=", 0) == 0,
        "fx format: last observation is summary at t=duration");
}

}  // namespace

int main() {
    std::printf("== railhmi ITEM-009 verifier ext fixtures ==\n");
    test_parse_matrix();
    test_fi01_window();
    test_precedence_cut_over_freeze();
    test_fi02_progression();
    test_fi10_merge_envelope();
    test_fi04_partition();
    test_fi05_partition();
    test_fi03_norecovery();
    test_determinism_other_config();
    test_format_contract();
    std::printf("== %d passed, %d failed ==\n", fx_pass, fx_fail);
    std::printf("RESULT: %s passed=%d failed=%d\n", fx_fail == 0 ? "PASS" : "FAIL", fx_pass, fx_fail);
    return fx_fail == 0 ? 0 : 1;
}
