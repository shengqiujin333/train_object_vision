#pragma once

// railhmi UI: 历史回放桥接（ITEM-020）。
// 时间轴/跳转/三联同步/来源状态全部委托 railhmi-core（单一真相），QML 只做展示。
// 由 main.cpp 注入为上下文属性 railhmiReplay。

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <vector>

#include "railhmi/core/persistence_model.h"
#include "railhmi/core/replay.h"

class RailhmiReplayModel : public QObject {
    Q_OBJECT
public:
    explicit RailhmiReplayModel(QObject* parent = nullptr);

    void seed(const std::vector<railhmi::core::RecordingSegmentRow>& segments,
              const std::vector<railhmi::core::RecordingGapRow>& gaps,
              const std::vector<long long>& alarm_ts, bool nvr_available, bool local_available);

    // 某相机某日时间轴（date = YYYY-MM-DD）。
    Q_INVOKABLE QVariantMap timeline(const QString& camera_id, const QString& date,
                                     int keyframe_interval_ms = 60000) const;
    // 四个跳转点。
    Q_INVOKABLE QVariantMap jumpTargets(double alarm_ts) const;
    // 三联同步状态（positions 为各路的墙钟毫秒）。
    Q_INVOKABLE QVariantMap syncStatus(const QVariantList& positions, int threshold_ms) const;
    // 回放来源状态。
    Q_INVOKABLE QVariantMap sourceStatus() const;
    // 墙钟毫秒 -> 文本。
    Q_INVOKABLE QString formatTime(double ts_wall_ms) const;
    Q_INVOKABLE int syncThreshold() const { return sync_threshold_ms_; }
    void setSyncThreshold(int ms) { sync_threshold_ms_ = ms; }

private:
    std::vector<railhmi::core::RecordingSegmentRow> segments_;
    std::vector<railhmi::core::RecordingGapRow> gaps_;
    std::vector<long long> alarm_ts_;
    bool nvr_available_ = false;
    bool local_available_ = false;
    int sync_threshold_ms_ = 300;
};
