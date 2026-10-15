#include "railhmi/core/replay.h"

#include <algorithm>
#include <cstdio>

namespace railhmi {
namespace core {

bool ReplayTimeline::has_alarm_segment() const {
    for (const auto& s : segments)
        if (s.kind == SegmentKind::Critical) return true;
    return false;
}
bool ReplayTimeline::has_gap() const { return !gaps.empty(); }

ReplayTimeline build_timeline(const std::string& camera_id, long long day_start_ms,
                              const std::vector<RecordingSegmentRow>& segments,
                              const std::vector<RecordingGapRow>& gaps,
                              const std::vector<long long>& alarm_ts,
                              int keyframe_interval_ms) {
    ReplayTimeline t;
    t.camera_id = camera_id;
    t.day_start_ms = day_start_ms;
    t.day_end_ms = day_start_ms + 86400000LL;

    for (const auto& s : segments) {
        if (s.cam != camera_id) continue;
        if (s.t1_ms <= t.day_start_ms || s.t0_ms >= t.day_end_ms) continue;
        TimelineSegment ts;
        ts.t0_ms = std::max(s.t0_ms, t.day_start_ms);
        ts.t1_ms = std::min(s.t1_ms, t.day_end_ms);
        ts.kind = s.kind;
        ts.label = (s.kind == SegmentKind::Critical) ? "报警保护片段" : "连续录像";
        t.segments.push_back(ts);
    }
    std::sort(t.segments.begin(), t.segments.end(),
              [](const TimelineSegment& a, const TimelineSegment& b) { return a.t0_ms < b.t0_ms; });

    for (const auto& g : gaps) {
        if (g.cam != camera_id) continue;
        if (g.t1_ms <= t.day_start_ms || g.t0_ms >= t.day_end_ms) continue;
        TimelineGap tg;
        tg.t0_ms = std::max(g.t0_ms, t.day_start_ms);
        tg.t1_ms = std::min(g.t1_ms, t.day_end_ms);
        tg.reason = g.reason;
        t.gaps.push_back(tg);
    }
    std::sort(t.gaps.begin(), t.gaps.end(),
              [](const TimelineGap& a, const TimelineGap& b) { return a.t0_ms < b.t0_ms; });

    for (long long ts : alarm_ts) {
        if (ts < t.day_start_ms || ts >= t.day_end_ms) continue;
        TimelineMarker m;
        m.ts_ms = ts;
        m.kind = "alarm";
        m.label = "报警";
        t.alarms.push_back(m);
    }
    std::sort(t.alarms.begin(), t.alarms.end(),
              [](const TimelineMarker& a, const TimelineMarker& b) { return a.ts_ms < b.ts_ms; });

    if (keyframe_interval_ms > 0) {
        for (long long ts = t.day_start_ms; ts < t.day_end_ms; ts += keyframe_interval_ms) {
            TimelineMarker m;
            m.ts_ms = ts;
            m.kind = "keyframe";
            t.keyframes.push_back(m);
        }
    }
    return t;
}

JumpTargets jump_targets(long long alarm_ts) {
    JumpTargets j;
    j.alarm_ms = alarm_ts;
    j.minus30_ms = alarm_ts - 30000;
    j.plus30_ms = alarm_ts + 30000;
    j.plus60_ms = alarm_ts + 60000;
    return j;
}

void seek(PlaybackState* s, long long pos_ms, long long day_start_ms, long long day_end_ms) {
    if (pos_ms < day_start_ms) pos_ms = day_start_ms;
    if (pos_ms > day_end_ms) pos_ms = day_end_ms;
    s->pos_ms = pos_ms;
}
void jump_to(PlaybackState* s, long long target_ms) { s->pos_ms = target_ms; }

std::vector<std::string> front_alarm_sync_group() { return {"CAM-02", "CAM-01", "CAM-03"}; }

std::vector<std::string> related_cameras(const std::string& alarm_camera) {
    // 前方主摄报警 -> 左/前/右三联；其他来源 -> 仅该路（锁定相关摄像头）。
    if (alarm_camera == "CAM-01") return front_alarm_sync_group();
    if (alarm_camera.empty()) return {};
    return {alarm_camera};
}

SyncResult sync_status(const std::vector<long long>& positions_ms, int threshold_ms) {
    SyncResult r;
    if (positions_ms.empty()) return r;
    long long lo = positions_ms[0], hi = positions_ms[0];
    for (long long p : positions_ms) {
        lo = std::min(lo, p);
        hi = std::max(hi, p);
    }
    r.max_deviation_ms = hi - lo;
    r.in_sync = r.max_deviation_ms <= threshold_ms;
    r.has_prompt = !r.in_sync;
    if (r.has_prompt)
        r.prompt = "同步偏差 " + std::to_string(r.max_deviation_ms) + " ms（阈值 " +
                   std::to_string(threshold_ms) + " ms）";
    return r;
}

SyncSnapshot sync_snapshot(const std::vector<std::string>& locked_cameras, long long cursor_ms,
                           const std::map<std::string, long long>& camera_offsets_ms, int threshold_ms) {
    SyncSnapshot s;
    if (locked_cameras.empty() || camera_offsets_ms.empty()) {
        s.available = false;
        s.prompt = "同步状态不可用";
        return s;
    }
    std::vector<long long> positions;
    for (const auto& cam : locked_cameras) {
        auto it = camera_offsets_ms.find(cam);
        if (it == camera_offsets_ms.end()) {
            s.available = false;   // 缺少某路真实时钟 -> 不可用（不谎报在同步）
            s.prompt = "同步状态不可用";
            return s;
        }
        positions.push_back(cursor_ms + it->second);
    }
    const SyncResult r = sync_status(positions, threshold_ms);
    s.available = true;
    s.in_sync = r.in_sync;
    s.max_deviation_ms = r.max_deviation_ms;
    s.prompt = r.prompt;
    return s;
}

const char* to_string(ReplaySource s) {
    switch (s) {
        case ReplaySource::LocalClips: return "local_clips";
        case ReplaySource::Nvr: return "nvr";
        case ReplaySource::Unavailable: return "unavailable";
    }
    return "unavailable";
}

ReplaySourceStatus replay_source_status(bool nvr_available, bool local_clips_available) {
    ReplaySourceStatus st;
    if (nvr_available) {
        st.source = ReplaySource::Nvr;
        st.label = "NVR 录像";
        st.usable = true;
    } else if (local_clips_available) {
        st.source = ReplaySource::LocalClips;
        st.label = "本机片段";
        st.usable = true;
        st.note = "NVR 录像不可用，回放本机片段";
    } else {
        st.source = ReplaySource::Unavailable;
        st.label = "录像不可用";
        st.usable = false;
        st.note = "录像不可用";
    }
    return st;
}

ReplayEventDetail build_event_detail(const AlarmRow& row, double distance_m, double confidence) {
    ReplayEventDetail d;
    d.time_text = row.time_text;
    d.type_label = row.type_label;
    d.level_label = alarm_level_label(row.level);
    char buf[64];
    if (distance_m >= 0) {
        std::snprintf(buf, sizeof(buf), "%.1f m", distance_m);
        d.distance_text = buf;
    } else {
        d.distance_text = "—";
    }
    if (confidence >= 0) {
        std::snprintf(buf, sizeof(buf), "%d%%", static_cast<int>(confidence * 100 + 0.5));
        d.confidence_text = buf;
    } else {
        d.confidence_text = "—";
    }
    d.ack_label = row.ack_label;
    d.note = row.record_label;   // 录像可用性作为备注/标注
    return d;
}

}  // namespace core
}  // namespace railhmi
