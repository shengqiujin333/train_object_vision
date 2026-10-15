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

class RailhmiReplayModel : public QObject {
    Q_OBJECT
public:
    explicit RailhmiReplayModel(QObject* parent = nullptr);

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
};
