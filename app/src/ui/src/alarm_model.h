#pragma once

// railhmi UI: 报警中心桥接（ITEM-018）。
// 过滤/默认排序/确认/维护日志全部委托 railhmi-core（单一真相），QML 只做展示与交互。
// 由 main.cpp 注入为上下文属性 railhmiAlarms。

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <map>
#include <string>
#include <vector>

#include "railhmi/core/alarm_center.h"
#include "railhmi/core/persistence_model.h"

class RailhmiAlarmModel : public QObject {
    Q_OBJECT
public:
    explicit RailhmiAlarmModel(QObject* parent = nullptr);

    // 灌入事件（来自持久化 events 表 + 相机名映射 + 录像可用性）。
    void seedEvents(const std::vector<railhmi::core::EventRow>& events,
                    const std::map<std::string, std::string>& camera_names,
                    const std::map<std::string, railhmi::core::RecordAvailability>& record);

    // 六维筛选 + 默认排序，返回展示行。
    Q_INVOKABLE QVariantList query(const QString& level, const QString& type, const QString& camera,
                                   const QString& ack, const QString& from_date, const QString& to_date) const;
    // 筛选可选项：{cameras:[{id,name}], types:[{token,label}], levels:[{token,label}]}
    Q_INVOKABLE QVariantMap options() const;
    // 批量确认：只改 ack_state 不删事件；返回变化数。写入维护日志（lastLogText 可查）。
    Q_INVOKABLE int ack(const QVariantList& ids);
    Q_INVOKABLE int rowCount() const { return static_cast<int>(rows_.size()); }

    QString lastLogText() const { return QString::fromStdString(last_log_); }

signals:
    void changed();

private:
    QVariantMap rowToVariant(const railhmi::core::AlarmRow& r) const;

    std::vector<railhmi::core::AlarmRow> rows_;
    std::map<std::string, std::string> cam_names_;
    std::string last_log_;
};
