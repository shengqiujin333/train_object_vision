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
    for (const auto& a : t.alarms) alarms.append(static_cast<qlonglong>(a.ts_ms));
    m["alarms"] = alarms;
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
