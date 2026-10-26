#include "replay_model.h"

#include "railhmi/core/alarm_center.h"
#include <QDateTime>

using railhmi::core::RecordingGapRow;
using railhmi::core::RecordingSegmentRow;
using railhmi::core::ReplaySourceStatus;
using railhmi::core::ReplayTimeline;
using railhmi::core::SyncResult;
using railhmi::core::TimelineGap;
using railhmi::core::TimelineSegment;

RailhmiReplayModel::RailhmiReplayModel(QObject* parent) : QObject(parent), decoder_(this) {
    history_channel_ = new VideoChannel(this);
    connect(&decoder_, &ReplayDecoder::frameReady, this, [this](const QImage& image, qint64 position) {
        history_channel_->setImage(image);
        history_channel_->setInfo({{"presentable", !image.isNull()}});
        playback_.pos_ms = position;
        if (pause_on_frame_ && !image.isNull()) { pause_on_frame_ = false; decoder_.setPaused(true); }
        emit playbackChanged();
    });
    connect(&decoder_, &ReplayDecoder::stateChanged, this, [this](const QString& state, const QString& detail) {
        decoder_state_ = state;
        playback_.playing = state == "playing" && !pause_on_frame_;
        nvr_message_ = detail;
        emit playbackChanged(); emit nvrChanged();
    });
}

RailhmiReplayModel::~RailhmiReplayModel() {
    disconnect(&decoder_, nullptr, this, nullptr);
    decoder_.stop();
}

void RailhmiReplayModel::configureNvr(const QString& file) {
    QString problem;
    if (!loadNvrSettings(file, &nvr_settings_, &problem)) {
        nvr_enabled_ = true; nvr_message_ = problem; emit nvrChanged(); return;
    }
    nvr_enabled_ = nvr_settings_.enabled;
    nvr_mappings_ = nvr_settings_.channels;
    if (!nvr_enabled_) return;
    nvr_client_ = new NvrClient(nvr_settings_, this);
    nvr_password_ = QString::fromUtf8(qgetenv(nvr_settings_.credentialEnv.toUtf8().constData()));
    nvr_message_ = "请连接录像机，并选择已核对的 NVR 通道";
    connect(nvr_client_, &NvrClient::channelsReady, this, [this](const QVariantList& channels) {
        nvr_channels_ = channels; nvr_available_ = true; nvr_busy_ = false;
        nvr_message_ = "录像机已连接，共 " + QString::number(channels.size()) + " 个通道";
        emit nvrChanged();
    });
    connect(nvr_client_, &NvrClient::recordsReady, this,
        [this](const QString& camera, const QString& date, const QVariantList& records, qint64 begin, qint64 end) {
            if (camera != query_camera_ || date != query_date_) return;
            nvr_records_ = records; nvr_day_start_ = begin; nvr_day_end_ = end;
            nvr_available_ = true; nvr_busy_ = false;
            nvr_message_ = records.isEmpty() ? "查询成功，当日此通道没有录像" :
                "已获取 " + QString::number(records.size()) + " 段录像";
            if (!records.isEmpty()) playback_.pos_ms = records.front().toMap()["t0"].toLongLong();
            emit nvrChanged(); emit playbackChanged();
        });
    connect(nvr_client_, &NvrClient::playbackReady, this,
        [this](const QString& camera, const QUrl& url, qint64 begin, qint64 end) {
            if (camera != playing_camera_) return;
            nvr_busy_ = false;
            decoder_.start(url, nvr_settings_.username,
                nvr_password_, begin, end);
        });
    connect(nvr_client_, &NvrClient::failed, this, [this](const QString& operation, const QString& reason) {
        nvr_busy_ = false; nvr_message_ = reason;
        if (operation == "login" || operation == "channels") nvr_available_ = false;
        if (operation == "records") nvr_records_.clear();
        if (operation == "playback") {
            decoder_.stop(); decoder_state_ = "error"; playback_.playing = false; emit playbackChanged();
        }
        emit nvrChanged();
    });
}

QVariantMap RailhmiReplayModel::nvrState() const {
    QString channelName;
    for (const auto& channel : nvr_channels_) {
        const auto value = channel.toMap();
        if (value["channel"].toInt() == query_channel_) channelName = value["name"].toString();
    }
    return {{"enabled", nvr_enabled_}, {"busy", nvr_busy_}, {"connected", nvr_available_},
        {"message", nvr_message_}, {"channels", nvr_channels_}, {"channelName", channelName},
        {"count", nvr_records_.size()}, {"decoderState", decoder_state_}, {"playingCamera", playing_camera_},
        {"username", nvr_settings_.username}};
}

void RailhmiReplayModel::loginNvr(const QString& username, const QString& password) {
    if (!nvr_client_) return;
    if (username.trimmed().isEmpty() || password.isEmpty()) {
        nvr_message_ = "请填写录像机账号和密码"; emit nvrChanged(); return;
    }
    stopHistory(); nvr_records_.clear(); nvr_channels_.clear(); nvr_available_ = false;
    nvr_settings_.username = username.trimmed(); nvr_password_ = password;
    nvr_client_->setCredentials(nvr_settings_.username, password);
    refreshNvrChannels();
}

void RailhmiReplayModel::refreshNvrChannels() {
    if (!nvr_client_) return;
    nvr_busy_ = true; nvr_message_ = "正在连接录像机"; emit nvrChanged();
    nvr_client_->fetchChannels();
}

void RailhmiReplayModel::queryNvr(const QString& camera, const QString& date, int channel) {
    if (!nvr_client_) return;
    decoder_.stop(); playing_camera_.clear(); decoder_state_ = "stopped"; playback_.playing = false;
    nvr_records_.clear(); query_camera_ = camera; query_date_ = date;
    query_channel_ = channel > 0 ? channel : nvr_mappings_.value(camera, 0);
    const QDate day = QDate::fromString(date, Qt::ISODate);
    if (!day.isValid() || day.toString(Qt::ISODate) != date || query_channel_ < 1 || query_channel_ > 256) {
        nvr_busy_ = false; nvr_message_ = "请填写有效日期和该摄像头对应的 NVR 通道号";
        emit nvrChanged(); return;
    }
    if (!nvr_channels_.isEmpty()) {
        bool permitted = false;
        for (const auto& value : nvr_channels_) {
            const auto row = value.toMap();
            if (row["channel"].toInt() == query_channel_ && row["allowed"].toBool()) permitted = true;
        }
        if (!permitted) { nvr_message_ = "该 NVR 通道不存在或没有回放权限"; nvr_busy_ = false; emit nvrChanged(); return; }
    }
    nvr_mappings_[camera] = query_channel_;
    nvr_day_start_ = NvrClient::dayStart(day, nvr_settings_);
    nvr_day_end_ = NvrClient::dayStart(day.addDays(1), nvr_settings_);
    nvr_busy_ = true; nvr_message_ = "正在查询 NVR 历史录像"; emit nvrChanged();
    nvr_client_->queryRecords(camera, day, query_channel_);
}

QObject* RailhmiReplayModel::historyChannel(const QString& camera) const {
    return camera == playing_camera_ ? history_channel_ : nullptr;
}

void RailhmiReplayModel::stopHistory() {
    decoder_.stop(); playing_camera_.clear(); decoder_state_ = "stopped";
    playback_.playing = false; nvr_message_ = "已停止回放";
    emit playbackChanged(); emit nvrChanged();
}

void RailhmiReplayModel::seekNvr(const QString& camera, double utcMs, bool play) {
    if (!nvr_client_) return;
    decoder_.stop(); playback_.playing = false;
    const qint64 position = static_cast<qint64>(utcMs);
    for (const auto& value : nvr_records_) {
        const auto record = value.toMap();
        if (record["cameraId"].toString() != camera) continue;
        const qint64 begin = record["t0"].toLongLong(), end = record["t1"].toLongLong();
        if (position < begin || position >= end) continue;
        playing_camera_ = camera; decoder_state_ = "connecting"; playback_.pos_ms = position;
        pause_on_frame_ = !play; nvr_busy_ = true; nvr_message_ = "正在获取录像回放地址";
        emit nvrChanged(); emit playbackChanged();
        nvr_client_->requestPlayback(camera, record["channel"].toInt(), position, end); return;
    }
    nvr_message_ = "所选时刻没有可播放录像，请选择时间轴上的录像段";
    emit nvrChanged(); emit playbackChanged();
}

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
    if (nvr_enabled_) {
        if (!playing) { pause_on_frame_ = false; decoder_.setPaused(true); playback_.playing = false; emit playbackChanged(); }
        else if (decoder_state_ == "paused") decoder_.setPaused(false);
        else seekNvr(query_camera_, playback_.pos_ms, true);
        return;
    }
    playback_.playing = playing;
    emit playbackChanged();
}

void RailhmiReplayModel::setSpeed(double speed) {
    if (nvr_enabled_) {
        // One-rate playback until the vendor's Scale/seek behavior has been verified.
        nvr_message_ = speed == 1.0 ? "当前使用 1 倍速" : "当前 NVR 回放仅开放已确认的 1 倍速";
        emit nvrChanged(); return;
    }
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
    if (nvr_enabled_) {
        const QDate day = QDate::fromString(date, Qt::ISODate);
        if (!day.isValid() || day.toString(Qt::ISODate) != date) return {{"valid", false}};
        const qint64 begin = NvrClient::dayStart(day, nvr_settings_);
        const qint64 end = NvrClient::dayStart(day.addDays(1), nvr_settings_);
        const auto records = camera_id == query_camera_ && date == query_date_ ? nvr_records_ : QVariantList();
        return {{"valid", true}, {"cameraId", camera_id}, {"dayStartMs", begin}, {"dayEndMs", end},
            {"durationMs", end - begin}, {"segments", records}, {"gaps", QVariantList()},
            {"alarms", QVariantList()}, {"keyframes", QVariantList()}, {"hasAlarmSegment", false}, {"hasGap", false}};
    }
    const long long day = railhmi::core::parse_date_utc_ms(date.toStdString());
    if (day < 0) {
        m["valid"] = false;
        return m;
    }
    // 使用真实级别的报警标记（由已灌入的报警行推导），避免恒为 L3（v2 D-01）。
    std::vector<railhmi::core::AlarmMark> marks =
        railhmi::core::alarm_marks_from_rows(alarm_rows_, camera_id.toStdString());
    if (marks.empty())
        for (long long ts : alarm_ts_)
            marks.push_back(railhmi::core::AlarmMark{ts, railhmi::core::AlarmLevel::L3});
    const ReplayTimeline t = railhmi::core::build_timeline(camera_id.toStdString(), day, segments_, gaps_,
                                                           marks, keyframe_interval_ms);
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
    if (nvr_enabled_) return {{"source", "nvr"}, {"label", "NVR 历史录像"},
        {"usable", !nvr_records_.isEmpty()}, {"note", nvr_message_}};
    const ReplaySourceStatus st = railhmi::core::replay_source_status(nvr_available_, local_available_);
    QVariantMap m;
    m["source"] = QString::fromLatin1(railhmi::core::to_string(st.source));
    m["label"] = QString::fromUtf8(st.label.c_str());
    m["usable"] = st.usable;
    m["note"] = QString::fromUtf8(st.note.c_str());
    return m;
}

QString RailhmiReplayModel::formatTime(double ts_wall_ms) const {
    if (nvr_enabled_) return ts_wall_ms > 0 ? QDateTime::fromMSecsSinceEpoch(
        static_cast<qint64>(ts_wall_ms), nvr_settings_.timeZone).toString("yyyy-MM-dd HH:mm:ss") : QString("—");
    return QString::fromStdString(railhmi::core::format_time_text(static_cast<long long>(ts_wall_ms)));
}
