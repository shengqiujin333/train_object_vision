// Verifier-owned ext fixture for ITEM-025 software_verification (board-side).
// Subject: implementer round stamp 20261016-ITEM025-r1 (commit 6d1787b).
// Linked against THIS round's fresh build-core static libs. Asserts the
// fault-injection/self-check contract the task text requires, deeper than the
// delivered probes/tests:
//
//   A. fault_catalog(): exactly 6 entries, ids FI-01..FI-05/FI-10, all four
//      fields non-empty; prompt/recovery strings match the catalog of record.
//   B. self_check_list(): >= 7 steps, SC-01..SC-05/SC-10 inject commands parse
//      as valid railhmi-sim-inject arguments, SC-BOOT restarts railhmi
//      (the unit name install.sh enables), every prompt/recovery non-empty.
//   C. parse_fault() token surface used by the SC inject commands and the
//      delivered TestRun CLI (--fi 01,02,03,04,05,10).
//   D. fault_active() window semantics via SimulationRunner: [onset, recovery)
//      half-open; recovery<=onset means never recovers; fault absent = never
//      active regardless of window.
//   E. Per-FI observable state sequences on the FI catalog default config:
//      FI-01 cut -> reconnecting -> offline escalation -> live after recovery;
//      FI-02 freeze -> frozen -> live; FI-03 AI unavailable + zero alarms;
//      FI-04 storage low -> recovers; FI-05 ptz tolerance fail -> recovers;
//      FI-10 gaps recorded and merged.
//   F. Determinism: same config run twice -> byte-identical observations.
//
// RESULT: PASS|FAIL passed=N failed=M (exit 0 iff PASS failed=0).
// Pure logic, no Qt/RKNN, same as delivered probes.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/fault_catalog.h"
#include "railhmi/core/simulation.h"
#include "railhmi/core/retention.h"
#include "railhmi/core/config.h"

using namespace railhmi::core;

namespace {

int g_passed = 0, g_failed = 0;
std::vector<std::string> g_notes;

void check(bool ok, const std::string& name) {
    (ok ? g_passed : g_failed) += 1;
    if (!ok) g_notes.push_back("FAIL: " + name);
}

void check_eq(const std::string& name, const std::string& got, const std::string& want) {
    check(got == want, name + " [" + got + "] want [" + want + "]");
}

bool has_obs(const std::vector<Observation>& o, const std::string& type,
             const std::string& substr) {
    for (const auto& x : o)
        if (x.type == type && x.detail.find(substr) != std::string::npos) return true;
    return false;
}

std::string summary(const std::vector<Observation>& o) {
    for (const auto& x : o)
        if (x.type == "summary") return x.detail;
    return std::string();
}

std::string fmt_all(const std::vector<Observation>& o) {
    std::string s;
    for (const auto& x : o) s += format_observation(x) + "\n";
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
    StreamThresholds s;               // fast thresholds: see escalation quickly
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

// --- A. catalog + self-check list -----------------------------------------
void t_catalog() {
    const std::vector<FaultPrompt> cat = fault_catalog();
    check(cat.size() == 6, "catalog size == 6");
    const std::vector<std::string> ids = {"FI-01", "FI-02", "FI-03", "FI-04", "FI-05", "FI-10"};
    for (const auto& id : ids) {
        bool found = false, fields_ok = false;
        for (const auto& f : cat)
            if (f.id == id) {
                found = true;
                fields_ok = !f.label.empty() && !f.prompt.empty() && !f.recovery.empty();
            }
        check(found, "catalog has " + id);
        check(fields_ok, "catalog " + id + " label/prompt/recovery non-empty");
    }
    // catalog of record: prompts/recovery must be the delivered strings
    std::string label, prompt5, recovery10;
    for (const auto& f : cat) {
        if (f.id == "FI-01") label = f.label;
        if (f.id == "FI-05") prompt5 = f.prompt;
        if (f.id == "FI-10") recovery10 = f.recovery;
    }
    check_eq("FI-01 label", label, "断流");
    check_eq("FI-05 prompt mentions NOT_READY", prompt5,
             "云台卡显示目标值 vs 实际值 + 容差校验失败（超差），前方 AI 进入 NOT_READY");
    check_eq("FI-10 recovery keeps gaps", recovery10, "恢复录像进程/存储；缺口记录保留可检索");

    const std::vector<SelfCheckStep> sc = self_check_list();
    check(sc.size() >= 7, "self-check >= 7 steps");
    bool boot = false;
    for (const auto& s : sc) {
        check(!s.inject.empty() && !s.expected_prompt.empty() && !s.recovery_path.empty(),
              "self-check " + s.id + " fields non-empty");
        if (s.id == "SC-BOOT") {
            boot = true;
            check(s.inject.find("systemctl restart railhmi") != std::string::npos,
                  "SC-BOOT restarts the enabled unit name (railhmi)");
            check(s.recovery_path.find("Restart=always") != std::string::npos,
                  "SC-BOOT recovery cites Restart=always");
        }
        // SC-01..SC-10 inject commands must be real CLI invocations
        if (s.id != "SC-BOOT") {
            check(s.inject.rfind("railhmi-sim-inject --fi ", 0) == 0,
                  "self-check " + s.id + " inject uses railhmi-sim-inject --fi");
            const std::string tok = s.inject.substr(std::string("railhmi-sim-inject --fi ").size(), 2);
            FaultKind k = FaultKind::StreamCut;
            check(parse_fault(tok, &k), "self-check " + s.id + " --fi token parses: " + tok);
        }
    }
    check(boot, "self-check has SC-BOOT");
}

// --- C. parse_fault surface --------------------------------------------------
void t_parse_fault() {
    FaultKind k = FaultKind::StreamCut;
    check(parse_fault("01", &k) && k == FaultKind::StreamCut, "parse '01' -> StreamCut");
    check(parse_fault("1", &k) && k == FaultKind::StreamCut, "parse '1' -> StreamCut");
    check(parse_fault("FI-01", &k) && k == FaultKind::StreamCut, "parse 'FI-01' (case-insensitive)");
    check(parse_fault("stream_cut", &k) && k == FaultKind::StreamCut, "parse 'stream_cut'");
    check(parse_fault("02", &k) && k == FaultKind::Freeze, "parse '02' -> Freeze");
    check(parse_fault("03", &k) && k == FaultKind::AiExit, "parse '03' -> AiExit");
    check(parse_fault("04", &k) && k == FaultKind::DiskLow, "parse '04' -> DiskLow");
    check(parse_fault("05", &k) && k == FaultKind::PtzOutOfTolerance, "parse '05' -> PtzOutOfTolerance");
    check(parse_fault("10", &k) && k == FaultKind::RecordingInterruption, "parse '10' -> RecordingInterruption");
    check(parse_fault("recording_interruption", &k) && k == FaultKind::RecordingInterruption,
          "parse 'recording_interruption'");
    check(!parse_fault("99", &k), "parse '99' rejected");
    check(!parse_fault("", &k), "parse '' rejected");
    check_eq("to_string(ptz)", to_string(FaultKind::PtzOutOfTolerance), "ptz_out_of_tolerance");
    check_eq("fault_id(FI-10)", fault_id(FaultKind::RecordingInterruption), "FI-10");
}

// --- D. fault window semantics ------------------------------------------------
void t_window_semantics() {
    // half-open [onset, recovery): at t==onset active, at t==recovery inactive.
    SimulationConfig c = base_config();
    c.faults = {FaultKind::DiskLow};
    c.fault_onset_ms = 1000;
    c.fault_recovery_ms = 2000;
    c.duration_ms = 3000;
    c.step_ms = 500;
    auto obs = SimulationRunner(c).run();
    bool at_onset = false, at_recovery = false;
    for (const auto& x : obs) {
        if (x.type == "storage") {
            if (x.t_ms == 1000 && x.detail.find("low=true") != std::string::npos) at_onset = true;
            if (x.t_ms == 2000 && x.detail.find("low=false") != std::string::npos) at_recovery = true;
        }
    }
    check(at_onset, "fault active at t==onset (half-open window)");
    check(at_recovery, "fault inactive at t==recovery (half-open window)");

    // recovery <= onset: never recovers
    SimulationConfig n = base_config();
    n.faults = {FaultKind::DiskLow};
    n.fault_onset_ms = 1000;
    n.fault_recovery_ms = 500;  // <= onset
    auto nobs = SimulationRunner(n).run();
    long long low_ticks = 0;
    for (const auto& x : nobs)
        if (x.type == "storage" && x.detail.find("low=true") != std::string::npos) ++low_ticks;
    check(low_ticks > 0, "recovery<=onset: fault active");
    bool ever_recovered = false;
    for (const auto& x : nobs)
        if (x.t_ms > n.fault_onset_ms && x.type == "storage" &&
            x.detail.find("low=false") != std::string::npos) ever_recovered = true;
    check(!ever_recovered, "recovery<=onset: never recovers (documented semantics)");

    // fault absent from list: window ignored entirely
    SimulationConfig a = base_config();
    a.faults = {};  // no faults at all, window still set
    auto aobs = SimulationRunner(a).run();
    check(!has_obs(aobs, "storage", "low=true"), "no fault listed -> never active");
    check(has_obs(aobs, "summary", "alarms="), "no-fault run still emits summary");
}

// --- E. per-FI observable sequences (task: every FI has explicit prompt+recovery
//        evidence; model-layer behavior behind those prompts) -------------------
void t_fi_sequences() {
    {   // FI-01 stream cut: reconnect -> escalate -> recover to live
        SimulationConfig c = base_config();
        c.faults = {FaultKind::StreamCut};
        auto obs = SimulationRunner(c).run();
        check(has_obs(obs, "stream_state", "state=reconnecting"), "FI-01 -> reconnecting");
        check(has_obs(obs, "stream_state", "state=offline"), "FI-01 -> offline escalation");
        check(has_obs(obs, "stream_state", "presentable=false"), "FI-01 frames not presentable as live");
        check(has_obs(obs, "stream_state", "state=live"), "FI-01 recovers to live");
        check(summary(obs).find("final_state=live") != std::string::npos,
              "FI-01 final state live");
    }
    {   // FI-02 freeze: frozen placeholder state -> recovers
        SimulationConfig c = base_config();
        c.faults = {FaultKind::Freeze};
        auto obs = SimulationRunner(c).run();
        check(has_obs(obs, "stream_state", "state=frozen"), "FI-02 -> frozen");
        check(has_obs(obs, "stream_state", "state=live"), "FI-02 recovers to live");
    }
    {   // FI-03 AI exit: unavailable + alarms annotated away (0 alarms)
        SimulationConfig c = base_config();
        c.faults = {FaultKind::AiExit};
        c.fault_onset_ms = 0;
        c.fault_recovery_ms = 0;  // whole run unavailable
        auto obs = SimulationRunner(c).run();
        check(has_obs(obs, "ai", "status=unavailable"), "FI-03 -> ai unavailable");
        check(summary(obs).find("alarms=0") != std::string::npos, "FI-03 no alarms while AI down");
        check(has_obs(obs, "summary", "final_state="), "FI-03 stream unaffected by AI exit");
    }
    {   // FI-04 disk low: storage warn + record_available=false -> recovers
        SimulationConfig c = base_config();
        c.faults = {FaultKind::DiskLow};
        auto obs = SimulationRunner(c).run();
        check(has_obs(obs, "storage", "low=true"), "FI-04 -> storage low");
        check(has_obs(obs, "storage", "record_available=false"), "FI-04 record_available=false");
        check(has_obs(obs, "storage", "low=false"), "FI-04 recovers (low=false)");
    }
    {   // FI-05 ptz out of tolerance -> recovers
        SimulationConfig c = base_config();
        c.faults = {FaultKind::PtzOutOfTolerance};
        auto obs = SimulationRunner(c).run();
        check(has_obs(obs, "ptz", "tolerance_ok=false"), "FI-05 -> tolerance fail");
        check(has_obs(obs, "ptz", "tolerance_ok=true"), "FI-05 recovers tolerance");
    }
    {   // FI-10 recording interruption: gaps recorded + merged per camera
        SimulationConfig c = base_config();
        c.faults = {FaultKind::RecordingInterruption};
        auto obs = SimulationRunner(c).run();
        check(has_obs(obs, "gap", "cam=CAM-01"), "FI-10 -> gap logged");
        check(summary(obs).find("gaps=0") == std::string::npos, "FI-10 summary gaps > 0");
        // consecutive per-step gaps must merge into one contiguous gap record
        std::vector<RecordingGapRow> gaps;
        add_gap(gaps, "CAM-01", 1500, 2000, "FI-10 recording interruption");
        add_gap(gaps, "CAM-01", 2000, 2500, "FI-10 recording interruption");
        check(gaps.size() == 1 && gaps[0].t0_ms == 1500 && gaps[0].t1_ms == 2500,
              "adjacent gaps merge (gap model behind FI-10 display)");
    }
}

// --- F. determinism -----------------------------------------------------------
void t_determinism() {
    SimulationConfig c = base_config();
    c.faults = {FaultKind::StreamCut, FaultKind::Freeze, FaultKind::AiExit,
                FaultKind::DiskLow, FaultKind::PtzOutOfTolerance,
                FaultKind::RecordingInterruption};
    auto o1 = SimulationRunner(c).run();
    auto o2 = SimulationRunner(c).run();  // fresh instance = documented reset
    check(fmt_all(o1) == fmt_all(o2), "same config -> byte-identical observation stream");
}

// --- G. deployment arg contract (fix-r2 D-02) -------------------------------
// parse_config_dir is the runtime surface the systemd unit relies on:
//   ExecStart=/opt/railhmi/bin/railhmi --config /opt/railhmi/config
void t_config_dir_arg() {
    const std::string fb = "/opt/railhmi/config";
    { const char* argv[] = {"railhmi","--config","/data/cfg","--other"};
      check_eq("--config <dir>", parse_config_dir(4, argv, "config"), "/data/cfg"); }
    { const char* argv[] = {"railhmi","--config=/x"};
      check_eq("--config=<dir>", parse_config_dir(2, argv, "config"), "/x"); }
    { const char* argv[] = {"railhmi"};
      check_eq("absent falls back", parse_config_dir(1, argv, fb), fb); }
    { const char* argv[] = {"railhmi","--config"};
      check_eq("no value falls back (never empty)", parse_config_dir(2, argv, fb), fb); }
    { const char* argv[] = {"railhmi","--config",""};
      check_eq("empty value falls back", parse_config_dir(3, argv, fb), fb); }
    { const char* argv[] = {"railhmi","--mode","x","--config",
                            "/opt/railhmi/config"};
      check_eq("unit argument honoured (fix-r2 D-02)",
               parse_config_dir(5, argv, "config"), "/opt/railhmi/config"); }
    check_eq("no argv falls back safely", parse_config_dir(0, nullptr, fb), fb);
}

}  // namespace

int main() {
    std::printf("== verify ITEM-025 ext (fault catalog / self-check / injection) ==\n");
    t_catalog();
    t_parse_fault();
    t_window_semantics();
    t_fi_sequences();
    t_determinism();
    t_config_dir_arg();
    std::printf("RESULT: %s passed=%d failed=%d\n",
                g_failed == 0 ? "PASS" : "FAIL", g_passed, g_failed);
    for (const auto& n : g_notes) std::printf("  %s\n", n.c_str());
    return g_failed == 0 ? 0 : 1;
}
