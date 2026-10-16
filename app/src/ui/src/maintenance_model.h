#pragma once

// railhmi UI: 设备维护桥接（ITEM-022）。
// 云台容差校验/归位/预置位（二次确认）、维护日志、录像/存储、健康均委托 railhmi-core；
// QML 只做展示与交互。由 main.cpp 注入为 railhmiMaintenance。

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <vector>

#include "railhmi/core/maintenance.h"

class RailhmiMaintenanceModel : public QObject {
    Q_OBJECT
public:
    explicit RailhmiMaintenanceModel(QObject* parent = nullptr);

    void seed(const railhmi::core::PtzState& current, const railhmi::core::PtzTolerance& tol,
              bool front_ai_available, const railhmi::core::HealthSnapshot& health,
              const railhmi::core::RecordingStatus& recording, const QString& user);

    // 当前云台实际/目标值 + 容差校验 + 前方 AI 就绪。
    Q_INVOKABLE QVariantMap ptzState() const;
    // 二次确认后执行归位/预置位（kind: home | preset_front）；返回 目标/实际/容差结果 + 日志条目。
    Q_INVOKABLE QVariantMap ptzAction(const QString& kind);
    Q_INVOKABLE QVariantMap recording() const;
    Q_INVOKABLE QVariantMap health() const;
    Q_INVOKABLE QVariantList recentLogs(int n) const;

signals:
    void changed();

private:
    QVariantMap checkMap(const railhmi::core::PtzCheck& c, bool front_ai_available) const;
    QVariantMap logMap(const railhmi::core::MaintenanceLogRow& r) const;

    railhmi::core::PtzState ptz_;
    railhmi::core::PtzTolerance tol_;
    bool front_ai_available_ = false;
    railhmi::core::HealthSnapshot health_;
    railhmi::core::RecordingStatus recording_;
    std::vector<railhmi::core::MaintenanceLogRow> logs_;
    QString user_ = "operator";
    long long clock_ms_ = 1760000000000LL;
};
