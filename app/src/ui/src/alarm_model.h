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

    // 运行期真实事件追加（BLE 门磁 door_ajar 等）：不覆盖持久化事件，不删除历史。
    // 事件只在发生跃迁时调用一次；sourceId 用于去重/关联，record 固定为不可用（无录像证据）。
    void addLiveEvent(const QString& type, int level, const QString& sourceId,
                      const QString& sourceName, const QString& description);

    // 筛选 + 默认排序，返回展示行。category 为现场固定类别（camera/door/nvr/host），
    // 空串或 "all" 表示全部类型；非法 token 忽略而不是静默变成空结果。
    Q_INVOKABLE QVariantList query(const QString& level, const QString& type, const QString& camera,
                                   const QString& ack, const QString& from_date, const QString& to_date,
                                   const QString& category = QString()) const;
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
