#include "health_model.h"

using railhmi::core::HealthEval;
using railhmi::core::HealthSample;
using railhmi::core::HealthThresholds;
using railhmi::core::LogFilter;
using railhmi::core::LogRecord;
using railhmi::core::LogSeverity;
using railhmi::core::RecoveryState;

RailhmiHealthModel::RailhmiHealthModel(QObject* parent) : QObject(parent) {}

void RailhmiHealthModel::seed(const HealthSample& sample, const HealthThresholds& th,
                              const std::vector<LogRecord>& logs, const RecoveryState& recovery) {
    sample_ = sample;
    th_ = th;
    logs_ = logs;
    recovery_ = recovery;
    emit changed();
}

QVariantMap RailhmiHealthModel::health() const {
    const HealthEval e = railhmi::core::evaluate_health(sample_, th_);
    QVariantMap m;
    m["level"] = QString::fromLatin1(railhmi::core::to_string(e.level));
    m["levelLabel"] = QString::fromUtf8(railhmi::core::health_level_label(e.level));
    m["text"] = QString::fromUtf8(e.text.c_str());
    m["npu"] = sample_.npu_load;
    m["cpu"] = sample_.cpu;
    m["tempC"] = sample_.temp_c;
    m["netErrors"] = static_cast<qlonglong>(sample_.net_errors);
    QVariantList alerts;
    for (const auto& a : e.alerts) alerts.append(QString::fromUtf8(a.c_str()));
    m["alerts"] = alerts;
    return m;
}

QVariantMap RailhmiHealthModel::recoveryMap(const RecoveryState& st) const {
    QVariantMap m;
    QVariantList steps;
    for (const auto& s : st.steps) {
        QVariantMap sm;
        sm["step"] = QString::fromLatin1(railhmi::core::to_string(s.step));
        sm["label"] = QString::fromUtf8(railhmi::core::recovery_step_label(s.step));
        sm["status"] = QString::fromLatin1(railhmi::core::to_string(s.status));
        sm["statusLabel"] = QString::fromUtf8(railhmi::core::step_status_label(s.status));
        sm["detail"] = QString::fromUtf8(s.detail.c_str());
        steps.append(sm);
    }
    m["steps"] = steps;
    m["started"] = st.started;
    m["completed"] = st.completed;
    m["alive"] = st.alive;
    m["degradedCameras"] = st.degraded_cameras;
    m["progress"] = st.progress_pct();
    m["text"] = QString::fromUtf8(st.text().c_str());
    return m;
}

QVariantMap RailhmiHealthModel::recovery() const {
    return recoveryMap(demo_active_ ? demo_tracker_.state() : recovery_);
}

void RailhmiHealthModel::resetRecovery() {
    demo_tracker_.start(demo_clock_);
    demo_active_ = true;
    emit changed();
}

void RailhmiHealthModel::advanceRecovery() {
    if (!demo_active_) { demo_tracker_.start(demo_clock_); demo_active_ = true; }
    demo_clock_ += 500;
    for (int i = 0; i < static_cast<int>(railhmi::core::RecoveryStep::Count); ++i) {
        const railhmi::core::RecoveryStep s = static_cast<railhmi::core::RecoveryStep>(i);
        const railhmi::core::RecoveryState cur = demo_tracker_.state();
        if (cur.steps[i].status != railhmi::core::StepStatus::Pending &&
            cur.steps[i].status != railhmi::core::StepStatus::Running)
            continue;
        if (s == railhmi::core::RecoveryStep::StartStreaming)
            demo_tracker_.camera_result(false, false, "CAM-02 启动失败（降级）");
        else
            demo_tracker_.complete_step(s, true);
        break;
    }
    // 全部步骤终态 -> 标记完成。
    const railhmi::core::RecoveryState now = demo_tracker_.state();
    bool all_terminal = true;
    for (const auto& s : now.steps)
        if (s.status == railhmi::core::StepStatus::Pending || s.status == railhmi::core::StepStatus::Running)
            all_terminal = false;
    if (all_terminal && !now.completed) demo_tracker_.finish(demo_clock_);
    emit changed();
}

QVariantList RailhmiHealthModel::logs(const QString& level, const QString& source, const QString& keyword) const {
    LogFilter f;
    if (level == "warn") { f.has_min_severity = true; f.min_severity = LogSeverity::Warn; }
    else if (level == "error") { f.has_min_severity = true; f.min_severity = LogSeverity::Error; }
    f.source = source.toStdString();
    f.keyword = keyword.toStdString();
    QVariantList out;
    for (const auto& r : railhmi::core::query_logs(logs_, f)) {
        QVariantMap m;
        m["tsWallMs"] = static_cast<qlonglong>(r.ts_wall_ms);
        m["level"] = QString::fromLatin1(railhmi::core::to_string(r.severity));
        m["levelLabel"] = QString::fromUtf8(railhmi::core::log_severity_label(r.severity));
        m["source"] = QString::fromStdString(r.source);
        m["message"] = QString::fromUtf8(r.message.c_str());
        QString fields;
        for (const auto& kv : r.fields) {
            if (!fields.isEmpty()) fields += " ";
            fields += QString::fromStdString(kv.first) + "=" + QString::fromUtf8(kv.second.c_str());
        }
        m["fields"] = fields;
        out.append(m);
    }
    return out;
}

QString RailhmiHealthModel::logJson(int index) const {
    if (index < 0 || index >= static_cast<int>(logs_.size())) return QString();
    return QString::fromStdString(railhmi::core::log_record_json(logs_[index]));
}

int RailhmiHealthModel::logCount() const { return static_cast<int>(logs_.size()); }
