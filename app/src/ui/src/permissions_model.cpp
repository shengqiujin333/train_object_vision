#include "permissions_model.h"

#include "railhmi/core/maintenance.h"   // roll_logs_by_day

using railhmi::core::MaintenanceLogRow;
using railhmi::core::Permission;
using railhmi::core::PermissionsConfig;
using railhmi::core::Role;
using railhmi::core::SensitiveAction;

RailhmiPermissionsModel::RailhmiPermissionsModel(QObject* parent) : QObject(parent) {}

void RailhmiPermissionsModel::seed(Role role, const PermissionsConfig& cfg, const QString& user) {
    role_ = role;
    cfg_ = cfg;
    user_ = user;
    emit changed();
}

QString RailhmiPermissionsModel::roleName() const {
    return QString::fromLatin1(railhmi::core::to_string(role_));
}

QVariantList RailhmiPermissionsModel::matrix() const {
    QVariantList out;
    for (const auto& r : railhmi::core::permission_matrix(cfg_)) {
        QVariantMap m;
        m["permission"] = QString::fromLatin1(railhmi::core::to_string(r.permission));
        m["label"] = QString::fromUtf8(r.label.c_str());
        m["driver"] = r.driver;
        m["maintainer"] = r.maintainer;
        m["developer"] = r.developer;
        m["admin"] = r.admin;
        out.append(m);
    }
    return out;
}

bool RailhmiPermissionsModel::canExportRecording() const {
    return railhmi::core::role_has_permission(role_, Permission::ExportRecording, cfg_);
}
bool RailhmiPermissionsModel::canExportSnapshot() const {
    return railhmi::core::role_has_permission(role_, Permission::ExportSnapshot, cfg_);
}
bool RailhmiPermissionsModel::canModifyDeviceConfig() const {
    return railhmi::core::role_has_permission(role_, Permission::ModifyDeviceConfig, cfg_);
}
bool RailhmiPermissionsModel::canModifySystemConfig() const {
    return railhmi::core::role_has_permission(role_, Permission::ModifySystemConfig, cfg_);
}

QVariantMap RailhmiPermissionsModel::actionMap(const SensitiveAction& a, const MaintenanceLogRow& log) {
    QVariantMap m;
    m["kind"] = QString::fromStdString(a.kind);
    m["target"] = QString::fromStdString(a.target);
    m["detail"] = QString::fromUtf8(a.detail.c_str());
    m["allowed"] = a.allowed;
    m["requiresConfirm"] = a.requires_confirm;
    m["confirmed"] = a.confirmed;
    m["executed"] = a.executed;
    m["result"] = QString::fromStdString(a.result);
    QVariantMap lm;
    lm["action"] = QString::fromStdString(log.action);
    lm["target"] = QString::fromStdString(log.target);
    lm["user"] = QString::fromStdString(log.user);
    lm["result"] = QString::fromStdString(log.result);
    lm["detail"] = QString::fromUtf8(log.detail.c_str());
    m["log"] = lm;
    return m;
}

QVariantMap RailhmiPermissionsModel::exportRecording(const QString& camera_id, double from_ms, double to_ms,
                                                     bool confirmed) {
    const SensitiveAction a = railhmi::core::export_recording(
        role_, cfg_, camera_id.toStdString(), static_cast<long long>(from_ms), static_cast<long long>(to_ms),
        confirmed);
    MaintenanceLogRow log;
    if (a.executed) {
        clock_ms_ += 1000;
        log = railhmi::core::build_action_log(a, user_.toStdString(), clock_ms_, 0);
        logs_.push_back(log);
        if (logs_.size() > 1000) logs_.erase(logs_.begin());
        emit changed();
    }
    return actionMap(a, log);
}

QVariantMap RailhmiPermissionsModel::modifyConfig(const QString& key, const QString& value, bool confirmed,
                                                  bool system_scope) {
    const SensitiveAction a = railhmi::core::modify_config(role_, cfg_, key.toStdString(),
                                                           value.toStdString(), confirmed, system_scope);
    MaintenanceLogRow log;
    if (a.executed) {
        clock_ms_ += 1000;
        log = railhmi::core::build_action_log(a, user_.toStdString(), clock_ms_, 0);
        logs_.push_back(log);
        if (logs_.size() > 1000) logs_.erase(logs_.begin());
        emit changed();
    }
    return actionMap(a, log);
}

QVariantList RailhmiPermissionsModel::recentLogs(int n) const {
    const std::vector<MaintenanceLogRow> rolled = railhmi::core::roll_logs_by_day(logs_, 7);
    QVariantList out;
    const int start = (n > 0 && static_cast<int>(rolled.size()) > n) ? static_cast<int>(rolled.size()) - n : 0;
    for (int i = start; i < static_cast<int>(rolled.size()); ++i) {
        QVariantMap lm;
        lm["action"] = QString::fromStdString(rolled[i].action);
        lm["target"] = QString::fromStdString(rolled[i].target);
        lm["user"] = QString::fromStdString(rolled[i].user);
        lm["result"] = QString::fromStdString(rolled[i].result);
        lm["detail"] = QString::fromUtf8(rolled[i].detail.c_str());
        out.append(lm);
    }
    return out;
}
