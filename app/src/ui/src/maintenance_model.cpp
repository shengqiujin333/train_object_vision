#include "maintenance_model.h"

using railhmi::core::HealthSnapshot;
using railhmi::core::MaintenanceLogRow;
using railhmi::core::PtzAction;
using railhmi::core::PtzCheck;
using railhmi::core::PtzState;
using railhmi::core::PtzTolerance;
using railhmi::core::RecordingStatus;

RailhmiMaintenanceModel::RailhmiMaintenanceModel(QObject* parent) : QObject(parent) {}

void RailhmiMaintenanceModel::seed(const PtzState& current, const PtzTolerance& tol,
                                   bool front_ai_available, const HealthSnapshot& health,
                                   const RecordingStatus& recording, const QString& user) {
    ptz_ = current;
    tol_ = tol;
    front_ai_available_ = front_ai_available;
    health_ = health;
    recording_ = recording;
    user_ = user;
    emit changed();
}

QVariantMap RailhmiMaintenanceModel::checkMap(const PtzCheck& c, bool front_ai_available) const {
    QVariantMap m;
    m["result"] = QString::fromLatin1(railhmi::core::to_string(c.result));
    m["resultLabel"] = QString::fromUtf8(railhmi::core::ptz_result_label(c.result));
    m["ok"] = c.ok;
    m["text"] = QString::fromUtf8(c.text.c_str());
    m["panDelta"] = c.pan_delta;
    m["tiltDelta"] = c.tilt_delta;
    m["zoomDelta"] = c.zoom_delta;
    m["frontAiReady"] = railhmi::core::front_ai_ready(c.ok, front_ai_available);
    return m;
}

QVariantMap RailhmiMaintenanceModel::logMap(const MaintenanceLogRow& r) const {
    QVariantMap m;
    m["tsWallMs"] = static_cast<qlonglong>(r.ts_wall_ms);
    m["user"] = QString::fromStdString(r.user);
    m["action"] = QString::fromStdString(r.action);
    m["target"] = QString::fromStdString(r.target);
    m["result"] = QString::fromStdString(r.result);
    m["detail"] = QString::fromUtf8(r.detail.c_str());
    return m;
}

QVariantMap RailhmiMaintenanceModel::ptzState() const {
    QVariantMap m;
    m["panActual"] = ptz_.pan_actual;
    m["tiltActual"] = ptz_.tilt_actual;
    m["zoomActual"] = ptz_.zoom_actual;
    m["panTarget"] = ptz_.pan_target;
    m["tiltTarget"] = ptz_.tilt_target;
    m["zoomTarget"] = ptz_.zoom_target;
    const QVariantMap check = checkMap(railhmi::core::ptz_check(ptz_, tol_), front_ai_available_);
    m["check"] = check;
    return m;
}

QVariantMap RailhmiMaintenanceModel::ptzAction(const QString& kind) {
    const PtzState before = ptz_;
    PtzState after = ptz_;
    PtzAction action;
    // 二次确认后执行：目标归零/前方预置位；实际值带小残差（确定性，容差内）。
    after.pan_target = 0.0;
    after.tilt_target = 0.0;
    after.zoom_target = 1.0;
    after.pan_actual = 0.2;
    after.tilt_actual = -0.3;
    after.zoom_actual = 1.05;
    after.verified = true;
    if (kind == "preset_front") {
        after.zoom_actual = 1.06;
        action = railhmi::core::ptz_recall_front_preset(before, after, tol_);
    } else {
        action = railhmi::core::ptz_home(before, after, tol_);
    }
    ptz_ = after;

    clock_ms_ += 1000;
    const MaintenanceLogRow log = railhmi::core::build_ptz_log(action, user_.toStdString(), clock_ms_, 0);
    logs_.push_back(log);
    if (logs_.size() > 500) logs_.erase(logs_.begin());

    QVariantMap m;
    m["kind"] = QString::fromStdString(action.kind);
    m["label"] = QString::fromUtf8(action.label.c_str());
    m["requiresConfirm"] = action.requires_confirm;
    m["applied"] = action.applied;
    m["panActual"] = ptz_.pan_actual;
    m["tiltActual"] = ptz_.tilt_actual;
    m["zoomActual"] = ptz_.zoom_actual;
    m["check"] = checkMap(action.check, front_ai_available_);
    m["log"] = logMap(log);
    emit changed();
    return m;
}

QVariantMap RailhmiMaintenanceModel::recording() const {
    QVariantMap m;
    m["running"] = recording_.running;
    m["usedPct"] = recording_.used_pct;
    m["freeText"] = QString::fromStdString(recording_.free_text);
    m["stateText"] = QString::fromUtf8(recording_.state_text.c_str());
    return m;
}

QVariantMap RailhmiMaintenanceModel::health() const {
    QVariantMap m;
    m["npu"] = health_.npu_load;
    m["cpu"] = health_.cpu;
    m["tempC"] = health_.temp_c;
    m["netErrors"] = static_cast<qlonglong>(health_.net_errors);
    m["text"] = QString::fromUtf8(railhmi::core::health_text(health_).c_str());
    return m;
}

QVariantList RailhmiMaintenanceModel::recentLogs(int n) const {
    QVariantList out;
    const std::vector<MaintenanceLogRow> rolled = railhmi::core::roll_logs_by_day(logs_, 7);
    const int start = (n > 0 && static_cast<int>(rolled.size()) > n) ? static_cast<int>(rolled.size()) - n : 0;
    for (int i = start; i < static_cast<int>(rolled.size()); ++i) out.append(logMap(rolled[i]));
    return out;
}

QVariantMap RailhmiMaintenanceModel::reconnect(const QString& camera_id) {
    const std::string cam = camera_id.toStdString();
    int& count = reconnect_counts_[cam];
    ++count;
    clock_ms_ += 1000;
    const MaintenanceLogRow log = railhmi::core::build_maintenance_log(
        "reconnect", cam, "ok", "手动重连", user_.toStdString(), clock_ms_, 0);
    logs_.push_back(log);
    if (logs_.size() > 1000) logs_.erase(logs_.begin());
    emit changed();
    QVariantMap m;
    m["cameraId"] = camera_id;
    m["reconnectCount"] = count;
    m["stateLabel"] = QString::fromUtf8("重连中");
    m["log"] = logMap(log);
    return m;
}

QVariantList RailhmiMaintenanceModel::logsByDay(int max_days) const {
    const std::vector<MaintenanceLogRow> rolled = railhmi::core::roll_logs_by_day(logs_, max_days);
    QVariantList out;
    std::string cur;
    QVariantList entries;
    auto flush = [&]() {
        if (cur.empty()) return;
        QVariantMap day;
        day["day"] = QString::fromStdString(cur);
        day["count"] = static_cast<int>(entries.size());
        day["entries"] = entries;
        out.append(day);
        entries = QVariantList();
    };
    for (const auto& r : rolled) {
        const std::string d = railhmi::core::log_day_key(r.ts_wall_ms);
        if (d != cur) {
            flush();
            cur = d;
        }
        entries.append(logMap(r));
    }
    flush();
    return out;
}

int RailhmiMaintenanceModel::logDayCount() const {
    const std::vector<MaintenanceLogRow> rolled = railhmi::core::roll_logs_by_day(logs_, 0);
    std::vector<std::string> days;
    for (const auto& r : rolled) {
        const std::string d = railhmi::core::log_day_key(r.ts_wall_ms);
        if (days.empty() || days.back() != d) days.push_back(d);
    }
    return static_cast<int>(days.size());
}
