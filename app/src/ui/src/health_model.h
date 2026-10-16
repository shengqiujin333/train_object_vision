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
    // 演示自恢复进度（仿真）：重置为进行中，并逐步推进直至完成（D-02/D-04）。
    Q_INVOKABLE void resetRecovery();
    Q_INVOKABLE void advanceRecovery();
    Q_INVOKABLE QVariantList logs(const QString& level, const QString& source, const QString& keyword) const;
    Q_INVOKABLE QString logJson(int index) const;
    Q_INVOKABLE int logCount() const;

signals:
    void changed();

private:
    QVariantMap recoveryMap(const railhmi::core::RecoveryState& st) const;

    railhmi::core::HealthSample sample_;
    railhmi::core::HealthThresholds th_;
    std::vector<railhmi::core::LogRecord> logs_;
    railhmi::core::RecoveryState recovery_;
    railhmi::core::RecoveryTracker demo_tracker_;
    bool demo_active_ = false;
    long long demo_clock_ = 1000;
};
