#pragma once

// railhmi-core: 报警中心模型（ITEM-018 / INTERACTION §5.2、VISUAL §8.2）。
// 列表字段：级别/类型/时间/来源摄像头/描述/处理状态/录像可用性；默认排序 级别降序→时间降序；
// 筛选：时间窗（日期/时间段）、摄像头、类型、级别、确认状态；确认/批量确认只改 ack_state 并
// 产出维护日志条目；录像不可用时行内显式标注。纯逻辑，无 Qt/SQLite 依赖。

#include <string>
#include <vector>

#include "railhmi/core/navigation.h"
#include "railhmi/core/persistence_model.h"
#include "railhmi/core/rules.h"

namespace railhmi {
namespace core {

// 录像可用性三态（PLY-008）：可回放 / 不完整 / 不可用。
enum class RecordAvailability { Available, Incomplete, Unavailable };
const char* to_string(RecordAvailability v);
const char* record_availability_label(RecordAvailability v);   // 可回放 / 录像不完整 / 录像不可用

const char* alarm_type_label(AlarmType t);      // 中文类型
const char* alarm_level_label(AlarmLevel l);    // 一级/二级/三级
const char* ack_state_label(AckState a);        // 未确认/已确认

AlarmType parse_alarm_type(const std::string& s);
bool parse_alarm_level(const std::string& s, AlarmLevel* out);
bool parse_ack_state(const std::string& s, AckState* out);

// UTC 毫秒 -> "YYYY-MM-DD HH:MM:SS"（UTC，等宽数字用）。
std::string format_time_text(long long ts_wall_ms);
// "YYYY-MM-DD" -> 当日 00:00:00 UTC 毫秒；失败返回 -1。
long long parse_date_utc_ms(const std::string& yyyy_mm_dd);
// 取某天 [00:00, 次日 00:00) 的 UTC 毫秒窗口。
long long day_start_utc_ms(long long ts_wall_ms);

struct AlarmRow {
    long long id = 0;
    AlarmLevel level = AlarmLevel::L3;
    AlarmType type = AlarmType::Device;
    std::string type_label;
    long long ts_wall_ms = 0;
    std::string time_text;
    std::string camera_id;
    std::string camera_name;
    std::string description;
    AckState ack = AckState::Unacked;
    std::string ack_label;
    RecordAvailability record = RecordAvailability::Unavailable;
    std::string record_label;
    bool snapshot_available = false;
    std::string snapshot_ref;
};

// 由持久化事件行 + 相机名 + 录像可用性派生展示行。
AlarmRow make_alarm_row(const EventRow& e, const std::string& camera_name, RecordAvailability rec);

// 时间窗 [ts_from_ms, ts_to_ms)；0/0 表示不限。
struct AlarmFilter {
    long long ts_from_ms = 0;      // inclusive; 0 => 不限
    long long ts_to_ms = 0;        // exclusive; 0 => 不限
    bool has_camera = false;
    std::string camera_id;
    bool has_type = false;
    AlarmType type = AlarmType::Device;
    bool has_level = false;
    AlarmLevel level = AlarmLevel::L3;
    bool has_ack = false;
    AckState ack = AckState::Unacked;
};

bool alarm_matches(const AlarmRow& r, const AlarmFilter& f);
// 默认排序：级别降序 -> 时间降序（UI-003/ALM-003）。
void sort_alarms_default(std::vector<AlarmRow>* rows);
// 筛选 + 默认排序。
std::vector<AlarmRow> query_alarms(const std::vector<AlarmRow>& rows, const AlarmFilter& f);
bool alarm_list_empty(const std::vector<AlarmRow>& rows);

// 筛选可选项（来自当前事件集）。
struct AlarmFilterOptions {
    std::vector<std::string> camera_ids;
    std::vector<std::string> camera_names;
    std::vector<AlarmType> types;
    std::vector<AlarmLevel> levels;
};
AlarmFilterOptions build_filter_options(const std::vector<AlarmRow>& rows);

// 确认/批量确认：只改 ack_state，返回变化计数与变化 id。
struct AckResult {
    int changed = 0;
    std::vector<long long> acked_ids;
};
AckResult acknowledge(std::vector<AlarmRow>* rows, const std::vector<long long>& ids,
                      AckState to = AckState::Acked);

// 维护/操作日志条目（HLT-007）。
MaintenanceLogRow build_ack_log(const std::vector<long long>& ids, const std::string& user,
                                long long ts_wall_ms, long long ts_mono_us,
                                const std::string& result = "ok");

}  // namespace core
}  // namespace railhmi
