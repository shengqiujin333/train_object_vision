#include "replay_model.h"

#include "railhmi/core/alarm_center.h"

using railhmi::core::RecordingGapRow;
using railhmi::core::RecordingSegmentRow;
using railhmi::core::ReplaySourceStatus;
using railhmi::core::ReplayTimeline;
using railhmi::core::SyncResult;
using railhmi::core::TimelineGap;
using railhmi::core::TimelineSegment;

RailhmiReplayModel::RailhmiReplayModel(QObject* parent) : QObject(parent) {}

void RailhmiReplayModel::seed(const std::vector<RecordingSegmentRow>& segments,
                              const std::vector<RecordingGapRow>& gaps,
                              const std::vector<long long>& alarm_ts, bool nvr_available,
                              bool local_available) {
    segments_ = segments;
    gaps_ = gaps;
    alarm_ts_ = alarm_ts;
    nvr_available_ = nvr_available;
    local_available_ = local_available;
}

void RailhmiReplayModel::seedAlarms(const std::vector<railhmi::core::AlarmRow>& rows,
                                   const std::vector<double>& distances,
                                   const std::vector<double>& confidences) {
    alarm_rows_ = rows;
    distances_ = distances;
    confidences_ = confidences;
}

void RailhmiReplayModel::setCameraOffsets(const std::map<std::string, long long>& offsets_ms) {
    offsets_ = offsets_ms;
}

void RailhmiReplayModel::selectAlarm(const QString& camera_id, double ts_wall_ms) {
    selected_cam_ = camera_id.toStdString();
    selected_ts_ = static_cast<long long>(ts_wall_ms);
    emit selectionChanged();
}

QVariantList RailhmiReplayModel::relatedCameras() const {
    std::vector<std::string> locked;
    if (!selected_cam_.empty()) locked = railhmi::core::related_cameras(selected_cam_);
    else locked = railhmi::core::front_alarm_sync_group();
    QVariantList out;
    for (const auto& c : locked) out.append(QString::fromStdString(c));
    return out;
}

const railhmi::core::AlarmRow* RailhmiReplayModel::find(const std::string& cam, long long ts) const {
    const railhmi::core::AlarmRow* best = nullptr;
    long long best_delta = 0;
    for (const auto& r : alarm_rows_) {
        if (!cam.empty() && r.camera_id != cam) continue;
        const long long d = r.ts_wall_ms > ts ? r.ts_wall_ms - ts : ts - r.ts_wall_ms;
        if (!best || d < best_delta) {
            best = &r;
            best_delta = d;
        }
    }
    return best;
}

QVariantMap RailhmiReplayModel::selectedDetail() const {
    QVariantMap m;
    const railhmi::core::AlarmRow* row = find(selected_cam_, selected_ts_);
    if (!row) {
        m["cameraId"] = QString::fromStdString(selected_cam_);
        m["tsWallMs"] = static_cast<qlonglong>(selected_ts_);
        return m;
    }
    double dist = -1.0, conf = -1.0;
    for (std::size_t i = 0; i < alarm_rows_.size(); ++i) {
        if (&alarm_rows_[i] == row) {
            if (i < distances_.size()) dist = distances_[i];
            if (i < confidences_.size()) conf = confidences_[i];
            break;
        }
    }
    const railhmi::core::ReplayEventDetail d = railhmi::core::build_event_detail(*row, dist, conf);
    m["cameraId"] = QString::fromStdString(row->camera_id);
    m["cameraName"] = QString::fromUtf8(row->camera_name.c_str());
    m["tsWallMs"] = static_cast<qlonglong>(row->ts_wall_ms);
    m["timeText"] = QString::fromStdString(d.time_text);
    m["typeLabel"] = QString::fromUtf8(d.type_label.c_str());
    m["levelLabel"] = QString::fromUtf8(d.level_label.c_str());
    m["distanceText"] = QString::fromUtf8(d.distance_text.c_str());
    m["confidenceText"] = QString::fromUtf8(d.confidence_text.c_str());
    m["ackLabel"] = QString::fromUtf8(d.ack_label.c_str());
    m["recordLabel"] = QString::fromUtf8(d.record_label.c_str());   // D-07 黄条
    m["recordOk"] = d.record_ok;
    m["note"] = QString::fromUtf8(d.note.c_str());
    return m;
}

QVariantMap RailhmiReplayModel::playback() const {
    QVariantMap m;
    m["playing"] = playback_.playing;
    m["speed"] = playback_.speed;
    m["posMs"] = static_cast<qlonglong>(playback_.pos_ms);
    return m;
}

void RailhmiReplayModel::setPlaying(bool playing) {
    playback_.playing = playing;
    emit playbackChanged();
}

void RailhmiReplayModel::setSpeed(double speed) {
    playback_.speed = speed > 0 ? speed : 1.0;
    emit playbackChanged();
}

QVariantMap RailhmiReplayModel::syncState(double cursor_ms) const {
    std::vector<std::string> locked;
    if (!selected_cam_.empty()) locked = railhmi::core::related_cameras(selected_cam_);
    else locked = railhmi::core::front_alarm_sync_group();
    const railhmi::core::SyncSnapshot s = railhmi::core::sync_snapshot(
        locked, static_cast<long long>(cursor_ms), offsets_, sync_threshold_ms_);
    QVariantMap m;
    m["available"] = s.available;
    m["inSync"] = s.in_sync;
    m["maxDeviationMs"] = static_cast<qlonglong>(s.max_deviation_ms);
    m["prompt"] = QString::fromUtf8(s.prompt.c_str());
    return m;
}

QVariantMap RailhmiReplayModel::timeline(const QString& camera_id, const QString& date,
                                         int keyframe_interval_ms) const {
    QVariantMap m;
    const long long day = railhmi::core::parse_date_utc_ms(date.toStdString());
    if (day < 0) {
        m["valid"] = false;
        return m;
    }
    const ReplayTimeline t = railhmi::core::build_timeline(camera_id.toStdString(), day, segments_, gaps_,
                                                           alarm_ts_, keyframe_interval_ms);
    m["valid"] = true;
    m["cameraId"] = camera_id;
    m["dayStartMs"] = static_cast<qlonglong>(t.day_start_ms);
    m["dayEndMs"] = static_cast<qlonglong>(t.day_end_ms);
    QVariantList segs;
    for (const TimelineSegment& s : t.segments) {
        QVariantMap sm;
        sm["t0"] = static_cast<qlonglong>(s.t0_ms);
        sm["t1"] = static_cast<qlonglong>(s.t1_ms);
        sm["kind"] = QString::fromLatin1(s.kind == railhmi::core::SegmentKind::Critical ? "critical" : "continuous");
        sm["label"] = QString::fromUtf8(s.label.c_str());
        sm["level"] = s.level;   // 1|2|3；0=未知（D-04）
        segs.append(sm);
    }
    m["segments"] = segs;
    QVariantList gs;
    for (const TimelineGap& g : t.gaps) {
        QVariantMap gm;
        gm["t0"] = static_cast<qlonglong>(g.t0_ms);
        gm["t1"] = static_cast<qlonglong>(g.t1_ms);
        gm["reason"] = QString::fromUtf8(g.reason.c_str());
        gs.append(gm);
    }
    m["gaps"] = gs;
    QVariantList alarms;
    for (const auto& a : t.alarms) {
        QVariantMap am;
        am["ts"] = static_cast<qlonglong>(a.ts_ms);
        am["level"] = a.level;
        alarms.append(am);
    }
    m["alarms"] = alarms;
    QVariantList keyframes;
    for (const auto& k : t.keyframes) {
        QVariantMap km;
        km["ts"] = static_cast<qlonglong>(k.ts_ms);
        km["thumb"] = QString::fromStdString(k.thumb);
        keyframes.append(km);
    }
    m["keyframes"] = keyframes;   // D-01：导出关键帧（含缩略图引用）
    m["hasAlarmSegment"] = t.has_alarm_segment();
    m["hasGap"] = t.has_gap();
    return m;
}

QVariantMap RailhmiReplayModel::jumpTargets(double alarm_ts) const {
    const railhmi::core::JumpTargets j = railhmi::core::jump_targets(static_cast<long long>(alarm_ts));
    QVariantMap m;
    m["minus30"] = static_cast<qlonglong>(j.minus30_ms);
    m["alarm"] = static_cast<qlonglong>(j.alarm_ms);
    m["plus30"] = static_cast<qlonglong>(j.plus30_ms);
    m["plus60"] = static_cast<qlonglong>(j.plus60_ms);
    return m;
}

QVariantMap RailhmiReplayModel::syncStatus(const QVariantList& positions, int threshold_ms) const {
    std::vector<long long> v;
    for (const auto& p : positions) v.push_back(p.toLongLong());
    const SyncResult r = railhmi::core::sync_status(v, threshold_ms);
    QVariantMap m;
    m["inSync"] = r.in_sync;
    m["hasPrompt"] = r.has_prompt;
    m["maxDeviationMs"] = static_cast<qlonglong>(r.max_deviation_ms);
    m["prompt"] = QString::fromUtf8(r.prompt.c_str());
    return m;
}

QVariantMap RailhmiReplayModel::sourceStatus() const {
    const ReplaySourceStatus st = railhmi::core::replay_source_status(nvr_available_, local_available_);
    QVariantMap m;
    m["source"] = QString::fromLatin1(railhmi::core::to_string(st.source));
    m["label"] = QString::fromUtf8(st.label.c_str());
    m["usable"] = st.usable;
    m["note"] = QString::fromUtf8(st.note.c_str());
    return m;
}

QString RailhmiReplayModel::formatTime(double ts_wall_ms) const {
    return QString::fromStdString(railhmi::core::format_time_text(static_cast<long long>(ts_wall_ms)));
}
