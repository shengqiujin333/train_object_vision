#include "alarm_model.h"

#include "railhmi/core/rules.h"

using railhmi::core::AckState;
using railhmi::core::AlarmFilter;
using railhmi::core::AlarmLevel;
using railhmi::core::AlarmRow;
using railhmi::core::AlarmType;
using railhmi::core::EventRow;
using railhmi::core::RecordAvailability;

RailhmiAlarmModel::RailhmiAlarmModel(QObject* parent) : QObject(parent) {}

void RailhmiAlarmModel::seedEvents(const std::vector<EventRow>& events,
                                   const std::map<std::string, std::string>& camera_names,
                                   const std::map<std::string, RecordAvailability>& record) {
    rows_.clear();
    cam_names_ = camera_names;
    for (const auto& e : events) {
        std::string name = e.source_cam;
        auto it = camera_names.find(e.source_cam);
        if (it != camera_names.end()) name = it->second;
        RecordAvailability rec = RecordAvailability::Unavailable;
        auto ri = record.find(e.source_cam);
        if (ri != record.end()) rec = ri->second;
        else if (e.record_available) rec = RecordAvailability::Available;
        rows_.push_back(railhmi::core::make_alarm_row(e, name, rec));
    }
    emit changed();
}

QVariantMap RailhmiAlarmModel::rowToVariant(const AlarmRow& r) const {
    QVariantMap m;
    m["id"] = static_cast<qlonglong>(r.id);
    m["level"] = QString::fromLatin1(railhmi::core::to_string(r.level));
    m["levelLabel"] = QString::fromUtf8(railhmi::core::alarm_level_label(r.level));
    m["type"] = QString::fromLatin1(railhmi::core::to_string(r.type));
    m["typeLabel"] = QString::fromUtf8(r.type_label.c_str());
    m["time"] = QString::fromStdString(r.time_text);
    m["tsWallMs"] = static_cast<qlonglong>(r.ts_wall_ms);
    m["cameraId"] = QString::fromStdString(r.camera_id);
    m["cameraName"] = QString::fromUtf8(r.camera_name.c_str());
    m["description"] = QString::fromUtf8(r.description.c_str());
    m["ack"] = QString::fromLatin1(railhmi::core::to_string(r.ack));
    m["ackLabel"] = QString::fromUtf8(r.ack_label.c_str());
    m["record"] = QString::fromLatin1(railhmi::core::to_string(r.record));
    m["recordLabel"] = QString::fromUtf8(r.record_label.c_str());
    m["recordOk"] = (r.record == RecordAvailability::Available);
    m["snapshot"] = r.snapshot_available;
    m["snapshotRef"] = QString::fromStdString(r.snapshot_ref);
    return m;
}

QVariantList RailhmiAlarmModel::query(const QString& level, const QString& type, const QString& camera,
                                      const QString& ack, const QString& from_date,
                                      const QString& to_date) const {
    AlarmFilter f;
    AlarmLevel lv;
    if (!level.isEmpty() && railhmi::core::parse_alarm_level(level.toStdString(), &lv)) {
        f.has_level = true;
        f.level = lv;
    }
    if (!type.isEmpty()) {
        f.has_type = true;
        f.type = railhmi::core::parse_alarm_type(type.toStdString());
    }
    if (!camera.isEmpty()) {
        f.has_camera = true;
        f.camera_id = camera.toStdString();
    }
    AckState a;
    if (!ack.isEmpty() && railhmi::core::parse_ack_state(ack.toStdString(), &a)) {
        f.has_ack = true;
        f.ack = a;
    }
    const long long from = railhmi::core::parse_date_utc_ms(from_date.toStdString());
    if (from >= 0) f.ts_from_ms = from;
    const long long to0 = railhmi::core::parse_date_utc_ms(to_date.toStdString());
    if (to0 >= 0) f.ts_to_ms = to0 + 86400000LL;   // 含当日

    QVariantList out;
    for (const auto& r : railhmi::core::query_alarms(rows_, f)) out.append(rowToVariant(r));
    return out;
}

QVariantMap RailhmiAlarmModel::options() const {
    const railhmi::core::AlarmFilterOptions o = railhmi::core::build_filter_options(rows_);
    QVariantMap m;
    QVariantList cams;
    for (std::size_t i = 0; i < o.camera_ids.size(); ++i) {
        QVariantMap c;
        c["id"] = QString::fromStdString(o.camera_ids[i]);
        c["name"] = QString::fromUtf8(i < o.camera_names.size() ? o.camera_names[i].c_str() : "");
        cams.append(c);
    }
    m["cameras"] = cams;
    QVariantList types;
    for (AlarmType t : o.types) {
        QVariantMap c;
        c["token"] = QString::fromLatin1(railhmi::core::to_string(t));
        c["label"] = QString::fromUtf8(railhmi::core::alarm_type_label(t));
        types.append(c);
    }
    m["types"] = types;
    QVariantList levels;
    for (AlarmLevel l : o.levels) {
        QVariantMap c;
        c["token"] = QString::fromLatin1(railhmi::core::to_string(l));
        c["label"] = QString::fromUtf8(railhmi::core::alarm_level_label(l));
        levels.append(c);
    }
    m["levels"] = levels;
    return m;
}

int RailhmiAlarmModel::ack(const QVariantList& ids) {
    std::vector<long long> v;
    for (const auto& id : ids) v.push_back(id.toLongLong());
    const railhmi::core::AckResult res = railhmi::core::acknowledge(&rows_, v, AckState::Acked);
    const railhmi::core::MaintenanceLogRow log =
        railhmi::core::build_ack_log(res.acked_ids, "operator", 0, 0);
    last_log_ = log.action + " target=" + log.target + " " + log.detail;
    if (res.changed > 0) emit changed();
    return res.changed;
}
