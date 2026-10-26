#pragma once

// railhmi UI: 历史回放桥接（ITEM-020）。
// 时间轴/跳转/三联同步/来源状态全部委托 railhmi-core（单一真相），QML 只做展示。
// 修复 D-01：从报警卡进入时调用 selectAlarm(cameraId, ts) 锁定相关摄像头并动态生成详情。
// 修复 D-02：syncState(cursor) 由各路真实回放时钟偏移构建；无时钟时返回“同步状态不可用”。
// 由 main.cpp 注入为上下文属性 railhmiReplay。

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <map>
#include <string>
#include <vector>

#include "railhmi/core/alarm_center.h"
#include "railhmi/core/persistence_model.h"
#include "railhmi/core/replay.h"
#include "nvr_client.h"
#include "replay_decoder.h"
#include "video_runtime.h"

class RailhmiReplayModel : public QObject {
    Q_OBJECT
public:
    explicit RailhmiReplayModel(QObject* parent = nullptr);
    ~RailhmiReplayModel() override;
    void configureNvr(const QString& file);
    Q_INVOKABLE bool nvrEnabled() const { return nvr_enabled_; }
    Q_INVOKABLE QVariantMap nvrState() const;
    Q_INVOKABLE int nvrChannel(const QString& camera) const { return nvr_mappings_.value(camera, 0); }
    Q_INVOKABLE void refreshNvrChannels();
    Q_INVOKABLE void loginNvr(const QString& username, const QString& password);
    Q_INVOKABLE void queryNvr(const QString& camera, const QString& date, int channel = 0);
    Q_INVOKABLE void seekNvr(const QString& camera, double utcMs, bool play = true);
    Q_INVOKABLE QObject* historyChannel(const QString& camera) const;
    Q_INVOKABLE void stopHistory();

    void seed(const std::vector<railhmi::core::RecordingSegmentRow>& segments,
              const std::vector<railhmi::core::RecordingGapRow>& gaps,
              const std::vector<long long>& alarm_ts, bool nvr_available, bool local_available);
    // 报警行（用于按选中报警动态生成详情）。
    void seedAlarms(const std::vector<railhmi::core::AlarmRow>& rows,
                    const std::vector<double>& distances, const std::vector<double>& confidences);
    // 各路回放时钟偏移（无真实源时不提供 -> 同步状态不可用）。
    void setCameraOffsets(const std::map<std::string, long long>& offsets_ms);

    // 从报警卡进入：锁定相关摄像头并定位报警时刻。
    Q_INVOKABLE void selectAlarm(const QString& camera_id, double ts_wall_ms);
    Q_INVOKABLE QString selectedCameraId() const { return QString::fromStdString(selected_cam_); }
    Q_INVOKABLE double selectedAlarmTs() const { return static_cast<double>(selected_ts_); }
    Q_INVOKABLE QVariantList relatedCameras() const;
    Q_INVOKABLE QVariantMap selectedDetail() const;
    // 三路同步状态（由真实时钟偏移构建；无时钟 -> available=false）。
    Q_INVOKABLE QVariantMap syncState(double cursor_ms) const;
    // 播放控制（PlaybackState；D-03）。
    Q_INVOKABLE QVariantMap playback() const;
    Q_INVOKABLE void setPlaying(bool playing);
    Q_INVOKABLE void setSpeed(double speed);

    // 某相机某日时间轴（date = YYYY-MM-DD）。
    Q_INVOKABLE QVariantMap timeline(const QString& camera_id, const QString& date,
                                     int keyframe_interval_ms = 60000) const;
    Q_INVOKABLE QVariantMap jumpTargets(double alarm_ts) const;
    Q_INVOKABLE QVariantMap syncStatus(const QVariantList& positions, int threshold_ms) const;
    Q_INVOKABLE QVariantMap sourceStatus() const;
    Q_INVOKABLE QString formatTime(double ts_wall_ms) const;
    Q_INVOKABLE int syncThreshold() const { return sync_threshold_ms_; }
    void setSyncThreshold(int ms) { sync_threshold_ms_ = ms; }

signals:
    void selectionChanged();
    void playbackChanged();
    void nvrChanged();

private:
    const railhmi::core::AlarmRow* find(const std::string& cam, long long ts) const;

    std::vector<railhmi::core::RecordingSegmentRow> segments_;
    std::vector<railhmi::core::RecordingGapRow> gaps_;
    std::vector<long long> alarm_ts_;
    std::vector<railhmi::core::AlarmRow> alarm_rows_;
    std::vector<double> distances_;
    std::vector<double> confidences_;
    std::map<std::string, long long> offsets_;
    std::string selected_cam_;
    long long selected_ts_ = 0;
    bool nvr_available_ = false;
    bool local_available_ = false;
    int sync_threshold_ms_ = 300;
    railhmi::core::PlaybackState playback_;
    NvrClient* nvr_client_ = nullptr;
    NvrSettings nvr_settings_;
    ReplayDecoder decoder_;
    VideoChannel* history_channel_ = nullptr;
    bool nvr_enabled_ = false, nvr_busy_ = false, pause_on_frame_ = false;
    QString nvr_message_, query_camera_, query_date_, playing_camera_, decoder_state_;
    QString nvr_password_;
    QVariantList nvr_channels_, nvr_records_;
    QMap<QString, int> nvr_mappings_;
    qint64 nvr_day_start_ = 0, nvr_day_end_ = 0;
    int query_channel_ = 0;
};
