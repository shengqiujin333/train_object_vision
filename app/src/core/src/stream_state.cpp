#include "railhmi/core/stream_state.h"

namespace railhmi {
namespace core {

const char* to_string(StreamState s) {
    switch (s) {
        case StreamState::Connecting: return "connecting";
        case StreamState::Live: return "live";
        case StreamState::Frozen: return "frozen";
        case StreamState::Offline: return "offline";
        case StreamState::Reconnecting: return "reconnecting";
        case StreamState::Degraded: return "degraded";
    }
    return "?";
}

const char* to_string(StreamEventKind k) {
    switch (k) {
        case StreamEventKind::Started: return "started";
        case StreamEventKind::FirstFrame: return "first_frame";
        case StreamEventKind::Freeze: return "freeze";
        case StreamEventKind::ReconnectAttempt: return "reconnect_attempt";
        case StreamEventKind::Escalated: return "escalated";
        case StreamEventKind::Recovered: return "recovered";
        case StreamEventKind::SourceError: return "source_error";
        case StreamEventKind::QualityDegraded: return "quality_degraded";
    }
    return "?";
}

std::string QualityFlags::describe() const {
    std::string s;
    auto add = [&](const char* n) { if (!s.empty()) s += ","; s += n; };
    if (too_dark) add("too_dark");
    if (too_bright) add("too_bright");
    if (occluded) add("occluded");
    if (blurred) add("blurred");
    return s;
}

QualityFlags classify_quality(const FrameStats& s, const QualityThresholds& t) {
    QualityFlags f;
    if (s.mean_luma >= 0.0) {
        if (s.mean_luma < t.dark_mean) f.too_dark = true;
        if (s.mean_luma > t.bright_mean) f.too_bright = true;
    }
    if (s.stddev_luma >= 0.0 && s.stddev_luma < t.min_stddev) f.occluded = true;
    if (s.lap_var >= 0.0 && s.lap_var < t.blur_lapvar) f.blurred = true;
    return f;
}

StreamStateMachine::StreamStateMachine(std::string camera_id, StreamThresholds thresholds)
    : camera_id_(std::move(camera_id)), th_(thresholds) {}

void StreamStateMachine::emit(StreamEventKind k, long long now_ms, const std::string& detail) {
    StreamEvent e;
    e.camera_id = camera_id_;
    e.kind = k;
    e.t_ms = now_ms;
    e.reconnect_count = reconnect_count_;
    e.detail = detail;
    events_.push_back(std::move(e));
}

void StreamStateMachine::enter(StreamState s, long long now_ms) {
    state_ = s;
    state_since_ms_ = now_ms;
}

void StreamStateMachine::start(long long now_ms) {
    reconnect_count_ = 0;
    enter(StreamState::Connecting, now_ms);
    emit(StreamEventKind::Started, now_ms);
}

void StreamStateMachine::on_frame(long long now_ms, const QualityFlags& quality) {
    const StreamState prev = state_;
    last_frame_ms_ = now_ms;
    reconnect_count_ = 0;
    if (quality.any()) {
        enter(StreamState::Degraded, now_ms);
        emit(StreamEventKind::QualityDegraded, now_ms, quality.describe());
    } else {
        enter(StreamState::Live, now_ms);
    }
    if (prev == StreamState::Connecting) {
        emit(StreamEventKind::FirstFrame, now_ms);
    } else if (prev == StreamState::Reconnecting || prev == StreamState::Frozen ||
               prev == StreamState::Offline) {
        emit(StreamEventKind::Recovered, now_ms);
    }
}

void StreamStateMachine::begin_reconnect(long long now_ms, const std::string& detail) {
    ++reconnect_count_;
    emit(StreamEventKind::ReconnectAttempt, now_ms, detail);
    if (reconnect_count_ >= th_.reconnect_threshold) {
        enter(StreamState::Offline, now_ms);
        emit(StreamEventKind::Escalated, now_ms,
             "reconnect threshold reached (" + std::to_string(reconnect_count_) + ")");
    } else {
        enter(StreamState::Reconnecting, now_ms);
    }
}

void StreamStateMachine::on_source_error(long long now_ms, const std::string& detail) {
    emit(StreamEventKind::SourceError, now_ms, detail);
    begin_reconnect(now_ms, detail);
}

void StreamStateMachine::on_reconnect_attempt(long long now_ms) {
    begin_reconnect(now_ms, "reconnect attempt");
}

void StreamStateMachine::tick(long long now_ms) {
    switch (state_) {
        case StreamState::Connecting:
            if (now_ms - state_since_ms_ >= th_.first_frame_timeout_ms) {
                begin_reconnect(now_ms, "first frame timeout");
            }
            break;
        case StreamState::Reconnecting:
            if (now_ms - state_since_ms_ >= th_.reconnect_backoff_ms) {
                enter(StreamState::Connecting, now_ms);
            }
            break;
        case StreamState::Live:
        case StreamState::Degraded:
            if (last_frame_ms_ >= 0 && now_ms - last_frame_ms_ >= th_.freeze_timeout_ms) {
                enter(StreamState::Frozen, now_ms);
                emit(StreamEventKind::Freeze, now_ms);
            }
            break;
        case StreamState::Frozen:
            if (now_ms - state_since_ms_ >= th_.freeze_timeout_ms) {
                begin_reconnect(now_ms, "frozen -> reconnect");
            }
            break;
        case StreamState::Offline:
            break;
    }
}

bool StreamStateMachine::may_present_as_live(long long now_ms) const {
    if (!is_streaming()) return false;
    if (last_frame_ms_ < 0) return false;
    return (now_ms - last_frame_ms_) < th_.freeze_timeout_ms;
}

std::vector<StreamEvent> StreamStateMachine::drain_events() {
    std::vector<StreamEvent> out;
    out.swap(events_);
    return out;
}

}  // namespace core
}  // namespace railhmi
