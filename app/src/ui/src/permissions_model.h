#pragma once

// railhmi UI: 权限与二次确认桥接（ITEM-023）。
// 权限矩阵/门控/敏感动作/维护日志委托 railhmi-core；QML 据此隐藏受限入口。
// 由 main.cpp 注入为 railhmiPermissions。

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <vector>

#include "railhmi/core/permissions.h"

class RailhmiPermissionsModel : public QObject {
    Q_OBJECT
public:
    explicit RailhmiPermissionsModel(QObject* parent = nullptr);

    void seed(railhmi::core::Role role, const railhmi::core::PermissionsConfig& cfg, const QString& user);

    Q_INVOKABLE QVariantList matrix() const;
    Q_INVOKABLE QString roleName() const;
    Q_INVOKABLE bool canExportRecording() const;
    Q_INVOKABLE bool canExportSnapshot() const;
    Q_INVOKABLE bool canModifyDeviceConfig() const;
    Q_INVOKABLE bool canModifySystemConfig() const;
    // 二次确认后执行（confirmed=false 时返回“需二次确认/权限不足”，不执行）。
    Q_INVOKABLE QVariantMap exportRecording(const QString& camera_id, double from_ms, double to_ms,
                                            bool confirmed);
    Q_INVOKABLE QVariantMap modifyConfig(const QString& key, const QString& value, bool confirmed,
                                         bool system_scope);
    Q_INVOKABLE QVariantList recentLogs(int n) const;

signals:
    void changed();

private:
    QVariantMap actionMap(const railhmi::core::SensitiveAction& a, const railhmi::core::MaintenanceLogRow& log);

    railhmi::core::Role role_ = railhmi::core::Role::Driver;
    railhmi::core::PermissionsConfig cfg_;
    std::vector<railhmi::core::MaintenanceLogRow> logs_;
    QString user_ = "operator";
    long long clock_ms_ = 1760000000000LL;
};
