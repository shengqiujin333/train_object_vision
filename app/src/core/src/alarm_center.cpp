#include "railhmi/core/alarm_center.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

namespace railhmi {
namespace core {

const char* to_string(RecordAvailability v) {
    switch (v) {
        case RecordAvailability::Available: return "available";
        case RecordAvailability::Incomplete: return "incomplete";
        case RecordAvailability::Unavailable: return "unavailable";
    }
    return "unavailable";
}

const char* record_availability_label(RecordAvailability v) {
    switch (v) {
        case RecordAvailability::Available: return "可回放";
        case RecordAvailability::Incomplete: return "录像不完整";
        case RecordAvailability::Unavailable: return "录像不可用";
    }
    return "录像不可用";
}

const char* alarm_type_label(AlarmType t) {
    switch (t) {
        case AlarmType::PersonOnRail: return "前方轨道人员";
        case AlarmType::FrontObstacle: return "前方障碍物";
        case AlarmType::WheelDerail: return "轮轨异常";
        case AlarmType::PantographAbnormal: return "受电弓异常";
        case AlarmType::DoorAjar: return "车门未关严";
        case AlarmType::FrontCameraDevice: return "前方主摄设备";
        case AlarmType::SideCameraDevice: return "侧视摄像头设备";
        case AlarmType::Device: return "设备故障";
    }
    return "设备故障";
}

const char* alarm_level_label(AlarmLevel l) {
    switch (l) {
        case AlarmLevel::L1: return "一级";
        case AlarmLevel::L2: return "二级";
        case AlarmLevel::L3: return "三级";
    }
    return "三级";
}

const char* ack_state_label(AckState a) { return a == AckState::Acked ? "已确认" : "未确认"; }

AlarmType parse_alarm_type(const std::string& s) {
    const AlarmType all[8] = {AlarmType::PersonOnRail, AlarmType::FrontObstacle, AlarmType::WheelDerail,
                              AlarmType::PantographAbnormal, AlarmType::DoorAjar, AlarmType::FrontCameraDevice,
                              AlarmType::SideCameraDevice, AlarmType::Device};
    for (AlarmType t : all)
        if (s == to_string(t)) return t;
    return AlarmType::Device;
}

bool parse_alarm_level(const std::string& s, AlarmLevel* out) {
    if (s == "L1" || s == "1") { *out = AlarmLevel::L1; return true; }
    if (s == "L2" || s == "2") { *out = AlarmLevel::L2; return true; }
    if (s == "L3" || s == "3") { *out = AlarmLevel::L3; return true; }
    return false;
}

bool parse_ack_state(const std::string& s, AckState* out) {
    if (s == "acked") { *out = AckState::Acked; return true; }
    if (s == "unacked") { *out = AckState::Unacked; return true; }
    return false;
}

namespace {
// Howard Hinnant civil_from_days：UTC 毫秒 -> 年月日时分秒。
void civil_from_ms(long long ms, int* y, int* mo, int* d, int* h, int* mi, int* s) {
    long long secs = ms / 1000;
    if (ms < 0 && ms % 1000 != 0) --secs;
    long long days = secs / 86400;
    long long rem = secs % 86400;
    if (rem < 0) { rem += 86400; --days; }
    *h = static_cast<int>(rem / 3600);
    *mi = static_cast<int>((rem % 3600) / 60);
    *s = static_cast<int>(rem % 60);
    long long z = days + 719468;
    const long long era = (z >= 0 ? z : z - 146096) / 146097;
    const unsigned long long doe = static_cast<unsigned long long>(z - era * 146097);
    const unsigned long long yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    long long yy = static_cast<long long>(yoe) + era * 400;
    const unsigned long long doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned long long mp = (5 * doy + 2) / 153;
    const unsigned long long dd = doy - (153 * mp + 2) / 5 + 1;
    const unsigned long long mm = mp < 10 ? mp + 3 : mp - 9;
    yy += (mm <= 2);
    *y = static_cast<int>(yy);
    *mo = static_cast<int>(mm);
    *d = static_cast<int>(dd);
}
}  // namespace

std::string format_time_text(long long ts_wall_ms) {
    if (ts_wall_ms <= 0) return "—";
    int y, mo, d, h, mi, s;
    civil_from_ms(ts_wall_ms, &y, &mo, &d, &h, &mi, &s);
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d", y, mo, d, h, mi, s);
    return std::string(buf);
}

long long parse_date_utc_ms(const std::string& date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') return -1;
    for (int i : {0, 1, 2, 3, 5, 6, 8, 9})
        if (date[i] < '0' || date[i] > '9') return -1;
    const int y = std::atoi(date.substr(0, 4).c_str());
    const int m = std::atoi(date.substr(5, 2).c_str());
    const int d = std::atoi(date.substr(8, 2).c_str());
    if (m < 1 || m > 12 || d < 1 || d > 31) return -1;
    // days_from_civil
    const long long yy = y - (m <= 2);
    const long long era = (yy >= 0 ? yy : yy - 399) / 400;
    const unsigned long long yoe = static_cast<unsigned long long>(yy - era * 400);
    const unsigned long long doy = (153 * (m > 2 ? m - 3 : m + 9) + 2) / 5 + d - 1;
    const unsigned long long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const long long days = era * 146097 + static_cast<long long>(doe) - 719468;
    return days * 86400000LL;
}

long long day_start_utc_ms(long long ts_wall_ms) {
    long long secs = ts_wall_ms / 1000;
    if (ts_wall_ms < 0 && ts_wall_ms % 1000 != 0) --secs;
    long long days = secs / 86400;
    if (secs % 86400 < 0) --days;
    return days * 86400000LL;
}

AlarmRow make_alarm_row(const EventRow& e, const std::string& camera_name, RecordAvailability rec) {
    AlarmRow r;
    r.id = e.id;
    r.level = (e.level <= 1) ? AlarmLevel::L1 : (e.level == 2 ? AlarmLevel::L2 : AlarmLevel::L3);
    r.type = parse_alarm_type(e.type);
    r.type_label = alarm_type_label(r.type);
    r.ts_wall_ms = e.ts_wall_ms;
    r.time_text = format_time_text(e.ts_wall_ms);
    r.camera_id = e.source_cam;
    r.camera_name = camera_name;
    r.description = e.description;
    r.ack = (e.ack_state == "acked") ? AckState::Acked : AckState::Unacked;
    r.ack_label = ack_state_label(r.ack);
    r.record = rec;
    r.record_label = record_availability_label(rec);
    r.snapshot_available = !e.snapshot_ref.empty();
    r.snapshot_ref = e.snapshot_ref;
    return r;
}

bool alarm_matches(const AlarmRow& r, const AlarmFilter& f) {
    if (f.ts_from_ms > 0 && r.ts_wall_ms < f.ts_from_ms) return false;
    if (f.ts_to_ms > 0 && r.ts_wall_ms >= f.ts_to_ms) return false;
    if (f.has_camera && r.camera_id != f.camera_id) return false;
    if (f.has_type && r.type != f.type) return false;
    if (f.has_level && r.level != f.level) return false;
    if (f.has_ack && r.ack != f.ack) return false;
    return true;
}

void sort_alarms_default(std::vector<AlarmRow>* rows) {
    std::stable_sort(rows->begin(), rows->end(), [](const AlarmRow& a, const AlarmRow& b) {
        const int la = static_cast<int>(a.level);
        const int lb = static_cast<int>(b.level);
        if (la != lb) return la < lb;            // L1 < L2 < L3 数值，一级最先
        return a.ts_wall_ms > b.ts_wall_ms;      // 同级时间降序
    });
}

std::vector<AlarmRow> query_alarms(const std::vector<AlarmRow>& rows, const AlarmFilter& f) {
    std::vector<AlarmRow> out;
    for (const auto& r : rows)
        if (alarm_matches(r, f)) out.push_back(r);
    sort_alarms_default(&out);
    return out;
}

bool alarm_list_empty(const std::vector<AlarmRow>& rows) { return rows.empty(); }

AlarmFilterOptions build_filter_options(const std::vector<AlarmRow>& rows) {
    AlarmFilterOptions o;
    for (const auto& r : rows) {
        if (std::find(o.camera_ids.begin(), o.camera_ids.end(), r.camera_id) == o.camera_ids.end()) {
            o.camera_ids.push_back(r.camera_id);
            o.camera_names.push_back(r.camera_name);
        }
        if (std::find(o.types.begin(), o.types.end(), r.type) == o.types.end()) o.types.push_back(r.type);
        if (std::find(o.levels.begin(), o.levels.end(), r.level) == o.levels.end()) o.levels.push_back(r.level);
    }
    std::sort(o.levels.begin(), o.levels.end(),
              [](AlarmLevel a, AlarmLevel b) { return static_cast<int>(a) < static_cast<int>(b); });
    return o;
}

AckResult acknowledge(std::vector<AlarmRow>* rows, const std::vector<long long>& ids, AckState to) {
    AckResult res;
    for (auto& r : *rows) {
        if (std::find(ids.begin(), ids.end(), r.id) == ids.end()) continue;
        if (r.ack == to) continue;
        r.ack = to;
        r.ack_label = ack_state_label(to);
        ++res.changed;
        res.acked_ids.push_back(r.id);
    }
    return res;
}

MaintenanceLogRow build_ack_log(const std::vector<long long>& ids, const std::string& user,
                                long long ts_wall_ms, long long ts_mono_us,
                                const std::string& result) {
    MaintenanceLogRow row;
    row.ts_wall_ms = ts_wall_ms;
    row.ts_mono_us = ts_mono_us;
    row.user = user;
    row.action = "ack_alarm";
    for (std::size_t i = 0; i < ids.size(); ++i) {
        if (i) row.target += ",";
        row.target += std::to_string(ids[i]);
    }
    row.result = result;
    row.detail = "count=" + std::to_string(ids.size());
    return row;
}

}  // namespace core
}  // namespace railhmi
