#pragma once

// railhmi-core: 历史回放模型（ITEM-020 / INTERACTION §5.3、VISUAL §8.3、DESKTOP §10 三联同步）。
// 时间轴（连续段/报警区段/缺口/游标/关键帧）、四个跳转（-30s/报警时刻/+30s/+60s）、
// 前方报警默认左/前/右同时间轴同步与偏差阈值提示、事件详情侧栏、
// NVR 来源不可用时显式标注并回退本机片段。纯逻辑，无 Qt/SQLite。

#include <string>
#include <vector>

#include "railhmi/core/alarm_center.h"
#include "railhmi/core/persistence_model.h"

namespace railhmi {
namespace core {

// 时间轴区段。
struct TimelineSegment {
    long long t0_ms = 0;
    long long t1_ms = 0;
    SegmentKind kind = SegmentKind::Continuous;   // Continuous=连续录像, Critical=报警保护
    std::string label;                            // 连续录像 / 报警保护片段
};

struct TimelineGap {
    long long t0_ms = 0;
    long long t1_ms = 0;
    std::string reason;
};

struct TimelineMarker {
    long long ts_ms = 0;
    std::string kind;   // "alarm" / "keyframe"
    std::string label;
};

struct ReplayTimeline {
    std::string camera_id;
    long long day_start_ms = 0;
    long long day_end_ms = 0;
    std::vector<TimelineSegment> segments;
    std::vector<TimelineGap> gaps;
    std::vector<TimelineMarker> alarms;
    std::vector<TimelineMarker> keyframes;
    long long duration_ms() const { return day_end_ms - day_start_ms; }
    bool has_alarm_segment() const;
    bool has_gap() const;
};

// 构建某相机某日的时间轴（段/缺口裁剪到当日窗口；报警区段显著；关键帧按间隔）。
ReplayTimeline build_timeline(const std::string& camera_id, long long day_start_ms,
                              const std::vector<RecordingSegmentRow>& segments,
                              const std::vector<RecordingGapRow>& gaps,
                              const std::vector<long long>& alarm_ts,
                              int keyframe_interval_ms);

// 四个跳转点（基于报警时刻）。
struct JumpTargets {
    long long minus30_ms = 0;
    long long alarm_ms = 0;
    long long plus30_ms = 0;
    long long plus60_ms = 0;
};
JumpTargets jump_targets(long long alarm_ts);

// 播放游标。
struct PlaybackState {
    long long pos_ms = 0;
    bool playing = false;
    double speed = 1.0;
};
void seek(PlaybackState* s, long long pos_ms, long long day_start_ms, long long day_end_ms);
void jump_to(PlaybackState* s, long long target_ms);

// 三联同步（左/前/右）。
std::vector<std::string> front_alarm_sync_group();   // 顺序：左侧 CAM-02 / 前方 CAM-01 / 右侧 CAM-03
struct SyncResult {
    long long max_deviation_ms = 0;
    bool in_sync = true;
    bool has_prompt = false;                 // 超阈值 -> 显示“同步偏差”提示
    std::string prompt;
};
SyncResult sync_status(const std::vector<long long>& positions_ms, int threshold_ms);

// 回放来源（NVR 不可用时显式标注并回退本机片段）。
enum class ReplaySource { LocalClips, Nvr, Unavailable };
const char* to_string(ReplaySource s);
struct ReplaySourceStatus {
    ReplaySource source = ReplaySource::Unavailable;
    std::string label;
    bool usable = false;
    std::string note;                        // 例如 “NVR 录像不可用，回放本机片段”
};
ReplaySourceStatus replay_source_status(bool nvr_available, bool local_clips_available);

// 事件详情侧栏。
struct ReplayEventDetail {
    std::string time_text;
    std::string type_label;
    std::string level_label;
    std::string distance_text;
    std::string confidence_text;
    std::string ack_label;
    std::string note;
};
ReplayEventDetail build_event_detail(const AlarmRow& row, double distance_m, double confidence);

}  // namespace core
}  // namespace railhmi
