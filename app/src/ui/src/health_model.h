#pragma once

// railhmi UI: 健康/结构化日志/自恢复桥接（ITEM-024）。
// 健康评估、日志查询、恢复进度委托 railhmi-core；QML 只做展示。注入为 railhmiHealth。

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <vector>

#include "railhmi/core/health.h"
#include "railhmi/core/recovery.h"

class RailhmiHealthModel : public QObject {
    Q_OBJECT
public:
    explicit RailhmiHealthModel(QObject* parent = nullptr);

    void seed(const railhmi::core::HealthSample& sample, const railhmi::core::HealthThresholds& th,
              const std::vector<railhmi::core::LogRecord>& logs,
              const railhmi::core::RecoveryState& recovery);

    Q_INVOKABLE QVariantMap health() const;
    Q_INVOKABLE QVariantMap recovery() const;
    Q_INVOKABLE QVariantList logs(const QString& level, const QString& source, const QString& keyword) const;
    Q_INVOKABLE QString logJson(int index) const;
    Q_INVOKABLE int logCount() const;

signals:
    void changed();

private:
    railhmi::core::HealthSample sample_;
    railhmi::core::HealthThresholds th_;
    std::vector<railhmi::core::LogRecord> logs_;
    railhmi::core::RecoveryState recovery_;
};
