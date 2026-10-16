#include "railhmi/core/recovery.h"

#include <algorithm>

namespace railhmi {
namespace core {

const char* to_string(RecoveryStep s) {
    switch (s) {
        case RecoveryStep::LoadConfig: return "load_config";
        case RecoveryStep::OpenDatabase: return "open_database";
        case RecoveryStep::StartRecording: return "start_recording";
        case RecoveryStep::StartStreaming: return "start_streaming";
        case RecoveryStep::StartAi: return "start_ai";
        case RecoveryStep::PtzHome: return "ptz_home";
        case RecoveryStep::FrontPreset: return "front_preset";
        case RecoveryStep::FrontAiReady: return "front_ai_ready";
        case RecoveryStep::Count: break;
    }
    return "unknown";
}

const char* recovery_step_label(RecoveryStep s) {
    switch (s) {
        case RecoveryStep::LoadConfig: return "加载配置";
        case RecoveryStep::OpenDatabase: return "打开数据库";
        case RecoveryStep::StartRecording: return "启动录像/存储";
        case RecoveryStep::StartStreaming: return "启动相机取流";
        case RecoveryStep::StartAi: return "启动 AI";
        case RecoveryStep::PtzHome: return "云台归位";
        case RecoveryStep::FrontPreset: return "调前方预置位";
        case RecoveryStep::FrontAiReady: return "前方 AI READY";
        case RecoveryStep::Count: break;
    }
    return "未知";
}

const char* to_string(StepStatus s) {
    switch (s) {
        case StepStatus::Pending: return "pending";
        case StepStatus::Running: return "running";
        case StepStatus::Ok: return "ok";
        case StepStatus::Degraded: return "degraded";
        case StepStatus::Failed: return "failed";
    }
    return "pending";
}

const char* step_status_label(StepStatus s) {
    switch (s) {
        case StepStatus::Pending: return "待执行";
        case StepStatus::Running: return "进行中";
        case StepStatus::Ok: return "完成";
        case StepStatus::Degraded: return "降级";
        case StepStatus::Failed: return "失败";
    }
    return "待执行";
}

bool recovery_step_critical(RecoveryStep s) {
    return s == RecoveryStep::LoadConfig || s == RecoveryStep::OpenDatabase;
}

int RecoveryState::progress_pct() const {
    if (steps.empty()) return 0;
    int done = 0;
    for (const auto& s : steps)
        if (s.status == StepStatus::Ok || s.status == StepStatus::Degraded || s.status == StepStatus::Failed) ++done;
    return done * 100 / static_cast<int>(steps.size());
}

std::string RecoveryState::text() const {
    if (!started) return "未开始";
    if (completed) return "恢复完成" + std::string(degraded_cameras > 0 ? "（部分降级）" : "");
    int done = 0;
    for (const auto& s : steps)
        if (s.status != StepStatus::Pending && s.status != StepStatus::Running) ++done;
    return "恢复中 " + std::to_string(done) + "/" + std::to_string(steps.size());
}

RecoveryTracker::RecoveryTracker() {
    for (int i = 0; i < static_cast<int>(RecoveryStep::Count); ++i) {
        StepState s;
        s.step = static_cast<RecoveryStep>(i);
        st_.steps.push_back(s);
    }
}

StepState* RecoveryTracker::find(RecoveryStep s) {
    for (auto& st : st_.steps)
        if (st.step == s) return &st;
    return nullptr;
}

void RecoveryTracker::recompute() {
    bool alive = true;
    for (const auto& s : st_.steps) {
        if (recovery_step_critical(s.step) && s.status == StepStatus::Failed) alive = false;
    }
    st_.alive = alive;
}

void RecoveryTracker::start(long long now_ms) {
    st_ = RecoveryState();
    for (int i = 0; i < static_cast<int>(RecoveryStep::Count); ++i) {
        StepState s;
        s.step = static_cast<RecoveryStep>(i);
        st_.steps.push_back(s);
    }
    st_.started = true;
    st_.started_ms = now_ms;
    st_.alive = true;
}

void RecoveryTracker::complete_step(RecoveryStep s, bool ok, const std::string& detail) {
    StepState* st = find(s);
    if (!st) return;
    if (ok) {
        st->status = StepStatus::Ok;
    } else {
        st->status = recovery_step_critical(s) ? StepStatus::Failed : StepStatus::Degraded;
    }
    st->detail = detail;
    recompute();
}

void RecoveryTracker::camera_result(bool ok, bool critical, const std::string& detail) {
    StepState* st = find(RecoveryStep::StartStreaming);
    if (!st) return;
    if (ok) {
        if (st->status == StepStatus::Pending || st->status == StepStatus::Running)
            st->status = StepStatus::Ok;
    } else if (critical) {
        st->status = StepStatus::Failed;
    } else {
        if (st->status != StepStatus::Failed) st->status = StepStatus::Degraded;
        ++st_.degraded_cameras;
    }
    if (!detail.empty()) st->detail = detail;
    recompute();
}

void RecoveryTracker::finish(long long now_ms) {
    st_.completed = true;
    st_.finished_ms = now_ms;
}

}  // namespace core
}  // namespace railhmi
