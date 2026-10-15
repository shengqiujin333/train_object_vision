#include "railhmi/core/simulation.h"

#include "railhmi/core/retention.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace railhmi {
namespace core {

const char* to_string(FaultKind k) {
    switch (k) {
        case FaultKind::StreamCut: return "stream_cut";
        case FaultKind::Freeze: return "freeze";
        case FaultKind::AiExit: return "ai_exit";
        case FaultKind::DiskLow: return "disk_low";
        case FaultKind::PtzOutOfTolerance: return "ptz_out_of_tolerance";
        case FaultKind::RecordingInterruption: return "recording_interruption";
    }
    return "?";
}

const char* fault_id(FaultKind k) {
    switch (k) {
        case FaultKind::StreamCut: return "FI-01";
        case FaultKind::Freeze: return "FI-02";
        case FaultKind::AiExit: return "FI-03";
        case FaultKind::DiskLow: return "FI-04";
        case FaultKind::PtzOutOfTolerance: return "FI-05";
        case FaultKind::RecordingInterruption: return "FI-10";
    }
    return "FI-??";
}

bool parse_fault(const std::string& token, FaultKind* out) {
    std::string s;
    for (char c : token) s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    if (s.rfind("fi-", 0) == 0) s = s.substr(3);
    if (s == "01" || s == "1" || s == "stream_cut" || s == "streamcut") { *out = FaultKind::StreamCut; return true; }
    if (s == "02" || s == "2" || s == "freeze") { *out = FaultKind::Freeze; return true; }
    if (s == "03" || s == "3" || s == "ai_exit" || s == "aiexit") { *out = FaultKind::AiExit; return true; }
    if (s == "04" || s == "4" || s == "disk_low" || s == "disklow") { *out = FaultKind::DiskLow; return true; }
    if (s == "05" || s == "5" || s == "ptz" || s == "ptz_out_of_tolerance") { *out = FaultKind::PtzOutOfTolerance; return true; }
    if (s == "10" || s == "recording_interruption" || s == "recording") { *out = FaultKind::RecordingInterruption; return true; }
    return false;
}

std::string format_observation(const Observation& o) {
    std::ostringstream s;
    s << "t=" << o.t_ms << " type=" << o.type;
    if (!o.detail.empty()) s << " " << o.detail;
    return s.str();
}

namespace {

bool has_fault(const SimulationConfig& cfg, FaultKind k) {
    return std::find(cfg.faults.begin(), cfg.faults.end(), k) != cfg.faults.end();
}

// 故障在 t 时刻是否处于激活区间 [onset, recovery)；recovery<=onset 表示不解除。
bool fault_active(const SimulationConfig& cfg, FaultKind k, long long t) {
    if (!has_fault(cfg, k)) return false;
    if (t < cfg.fault_onset_ms) return false;
    if (cfg.fault_recovery_ms > cfg.fault_onset_ms && t >= cfg.fault_recovery_ms) return false;
    return true;
}

Detection make_detection(const SimulationConfig& cfg, long long t) {
    Detection d;
    d.camera_id = cfg.camera_id;
    d.profile = cfg.profile;
    d.t_ms = t;
    d.track_id = 1;
    d.in_roi = true;
    d.confidence = 0.9;
    d.distance_m = 8.0;
    switch (cfg.profile) {
        case AiProfile::Front: d.label = "person"; break;
        case AiProfile::Pantograph: d.pantograph = PantographState::Abnormal; break;
        case AiProfile::WheelRail: d.wheel_abnormal = true; break;
        case AiProfile::CabDoor: d.door = DoorState::Ajar; break;
        case AiProfile::None: break;
    }
    return d;
}

}  // namespace

SimulationRunner::SimulationRunner(SimulationConfig cfg) : cfg_(std::move(cfg)) {}

std::vector<Observation> SimulationRunner::run() const {
    std::vector<Observation> out;
    auto emit = [&](long long t, const std::string& type, const std::string& detail) {
        out.push_back(Observation{t, type, detail});
    };

    std::string fl;
    for (FaultKind k : cfg_.faults) {
        if (!fl.empty()) fl += ",";
        fl += fault_id(k);
    }
    emit(0, "injection",
         "camera=" + cfg_.camera_id + " profile=" + std::string(to_string(cfg_.profile)) +
             " faults=" + (fl.empty() ? "none" : fl) + " onset_ms=" + std::to_string(cfg_.fault_onset_ms) +
             " recovery_ms=" + std::to_string(cfg_.fault_recovery_ms) +
             " duration_ms=" + std::to_string(cfg_.duration_ms) + " step_ms=" + std::to_string(cfg_.step_ms));

    StreamStateMachine machine(cfg_.camera_id, cfg_.stream);
    RuleEngine rules(cfg_.rules);
    std::vector<RecordingGapRow> gaps;
    const QualityFlags qok;
    const Detection det = make_detection(cfg_, 0);

    machine.start(0);
    long long alarm_count = 0;

    for (long long t = 0; t <= cfg_.duration_ms; t += cfg_.step_ms) {
        const bool cut = fault_active(cfg_, FaultKind::StreamCut, t);
        const bool frz = !cut && fault_active(cfg_, FaultKind::Freeze, t);
        const bool ai_ok = !fault_active(cfg_, FaultKind::AiExit, t);

        if (cut) {
            machine.on_source_error(t, "FI-01 stream cut");
        } else if (frz) {
            machine.tick(t);  // 停发新帧
        } else {
            machine.on_frame(t, qok);
            machine.tick(t);
            if (cfg_.feed_detections && ai_ok) {
                Detection d = det;
                d.t_ms = t;
                for (const auto& ev : rules.on_detection(d)) {
                    ++alarm_count;
                    emit(t, "alarm", std::string("type=") + to_string(ev.type) + " level=" +
                                         to_string(ev.level) + " cam=" + ev.source_cam +
                                         " track_id=" + std::to_string(ev.track_id));
                }
            }
        }

        emit(t, "stream_state", std::string("state=") + to_string(machine.state()) +
                                    " reconnect_count=" + std::to_string(machine.reconnect_count()) +
                                    " presentable=" + (machine.may_present_as_live(t) ? "true" : "false") +
                                    " cam=" + cfg_.camera_id);
        if (!ai_ok) emit(t, "ai", std::string("status=unavailable cam=") + cfg_.camera_id);
        else emit(t, "ai", std::string("status=ok cam=") + cfg_.camera_id);

        long long free_bytes = cfg_.disk_free_bytes;
        if (fault_active(cfg_, FaultKind::DiskLow, t)) free_bytes = cfg_.disk_low_threshold_bytes - 1;
        const bool low = free_bytes < cfg_.disk_low_threshold_bytes;
        emit(t, "storage", "free_bytes=" + std::to_string(free_bytes) + " total_bytes=" +
                               std::to_string(cfg_.disk_total_bytes) + " low=" + (low ? "true" : "false") +
                               " record_available=" + (low ? "false" : "true"));

        if (fault_active(cfg_, FaultKind::PtzOutOfTolerance, t)) {
            emit(t, "ptz", "tolerance_ok=false target_zoom=" + std::to_string(cfg_.ptz_target_zoom) +
                               " actual_zoom=" + std::to_string(cfg_.ptz_actual_zoom));
        } else {
            emit(t, "ptz", std::string("tolerance_ok=true target_zoom=") +
                               std::to_string(cfg_.ptz_target_zoom) + " actual_zoom=" +
                               std::to_string(cfg_.ptz_actual_zoom));
        }

        if (fault_active(cfg_, FaultKind::RecordingInterruption, t)) {
            add_gap(gaps, cfg_.camera_id, t, t + cfg_.step_ms, "FI-10 recording interruption");
            emit(t, "gap", "cam=" + cfg_.camera_id + " t0_ms=" + std::to_string(t) + " t1_ms=" +
                               std::to_string(t + cfg_.step_ms) + " gap_count=" + std::to_string(gaps.size()));
        }
    }

    emit(cfg_.duration_ms, "summary",
         "final_state=" + std::string(to_string(machine.state())) +
             " reconnect_count=" + std::to_string(machine.reconnect_count()) +
             " alarms=" + std::to_string(alarm_count) +
             " gaps=" + std::to_string(gaps.size()) +
             " events_recorded=" + std::to_string(rules.events().size()));
    return out;
}

}  // namespace core
}  // namespace railhmi
