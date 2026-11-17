// railhmi-core 报警中心模型测试（ITEM-018 / INTERACTION §5.2、VISUAL §8.2）。
// 覆盖：字段派生、默认排序（级别降序→时间降序）、六维筛选与空态、确认/批量确认不改删事件、
// 维护日志条目、录像可用性标注。

#include "test_util.h"

#include <string>
#include <vector>

#include "railhmi/core/alarm_center.h"

namespace {

using namespace railhmi::core;

const long long kDay = 86400000LL;
const long long kT0 = parse_date_utc_ms("2026-10-16");   // 当日 00:00 UTC

EventRow make_event(long long id, int level, const char* type, const char* cam, long long ts,
                    const char* ack, const char* desc, bool snap) {
    EventRow e;
    e.id = id;
    e.level = level;
    e.type = type;
    e.source_cam = cam;
    e.ts_wall_ms = ts;
    e.description = desc;
    e.ack_state = ack;
    e.snapshot_ref = snap ? "snap/1.bmp" : "";
    return e;
}

std::vector<AlarmRow> sample_rows() {
    std::vector<AlarmRow> v;
    v.push_back(make_alarm_row(make_event(1, 1, "person_on_rail", "CAM-01", kT0 + 5000, "unacked", "轨道人员", true),
                               "前方", RecordAvailability::Available));
    v.push_back(make_alarm_row(make_event(2, 3, "side_camera_device", "CAM-02", kT0 + 3000, "unacked", "侧视离线", false),
                               "左侧", RecordAvailability::Unavailable));
    v.push_back(make_alarm_row(make_event(3, 2, "pantograph_abnormal", "CAM-04", kT0 + 9000, "unacked", "受电弓异常", true),
                               "车顶", RecordAvailability::Incomplete));
    v.push_back(make_alarm_row(make_event(4, 1, "wheel_derail", "CAM-05", kT0 + 1000, "acked", "轮轨异常", true),
                               "车底", RecordAvailability::Available));
    return v;
}

void test_row_fields() {
    const AlarmRow r = sample_rows()[0];
    railhmi_test::check(r.level == AlarmLevel::L1 && r.type == AlarmType::PersonOnRail, "level/type derived");
    railhmi_test::check(r.type_label == std::string("前方轨道人员"), "type label");
    railhmi_test::check(r.ack == AckState::Unacked && r.ack_label == std::string("未确认"), "ack label");
    railhmi_test::check(r.record == RecordAvailability::Available && r.record_label == std::string("可回放"),
                        "record label available");
    railhmi_test::check(r.snapshot_available, "snapshot available flag");
    railhmi_test::check(r.time_text.rfind("2026-10-16", 0) == 0, "time text date");
    const AlarmRow u = sample_rows()[1];
    railhmi_test::check(u.record == RecordAvailability::Unavailable &&
                            u.record_label == std::string("录像不可用"),
                        "recording unavailable labeled (PLY-008)");
}

void test_default_sort() {
    std::vector<AlarmRow> rows = sample_rows();
    sort_alarms_default(&rows);
    // L1 first; within L1 time desc (ts 5000 before 1000); then L2; then L3.
    railhmi_test::check(rows[0].id == 1 && rows[1].id == 4, "L1 first, time desc within level");
    railhmi_test::check(rows[2].id == 3, "then L2");
    railhmi_test::check(rows[3].id == 2, "then L3");
}

void test_filters() {
    const std::vector<AlarmRow> rows = sample_rows();

    AlarmFilter l1;
    l1.has_level = true;
    l1.level = AlarmLevel::L1;
    const std::vector<AlarmRow> only_l1 = query_alarms(rows, l1);
    railhmi_test::check(only_l1.size() == 2, "filter by level L1 -> 2");

    AlarmFilter cam;
    cam.has_camera = true;
    cam.camera_id = "CAM-04";
    railhmi_test::check(query_alarms(rows, cam).size() == 1, "filter by camera");

    AlarmFilter ack;
    ack.has_ack = true;
    ack.ack = AckState::Unacked;
    railhmi_test::check(query_alarms(rows, ack).size() == 3, "filter unacked -> 3");

    AlarmFilter type;
    type.has_type = true;
    type.type = AlarmType::WheelDerail;
    railhmi_test::check(query_alarms(rows, type).size() == 1, "filter by type");

    AlarmFilter win;
    win.ts_from_ms = kT0 + 2000;
    win.ts_to_ms = kT0 + 6000;
    const std::vector<AlarmRow> inwin = query_alarms(rows, win);
    railhmi_test::check(inwin.size() == 2, "filter by time window");

    // 组合筛选 -> 空态
    AlarmFilter combo;
    combo.has_level = true;
    combo.level = AlarmLevel::L3;
    combo.has_camera = true;
    combo.camera_id = "CAM-01";
    const std::vector<AlarmRow> empty = query_alarms(rows, combo);
    railhmi_test::check(alarm_list_empty(empty), "combined filter with no match -> empty state");

    // 组合筛选 -> 命中
    AlarmFilter combo2;
    combo2.has_level = true;
    combo2.level = AlarmLevel::L1;
    combo2.has_ack = true;
    combo2.ack = AckState::Unacked;
    railhmi_test::check(query_alarms(rows, combo2).size() == 1, "combined filter hit");
}

void test_batch_ack() {
    std::vector<AlarmRow> rows = sample_rows();
    const AckResult res = acknowledge(&rows, {1, 2, 3});
    railhmi_test::check(res.changed == 3, "batch ack changed 3");
    railhmi_test::check(res.acked_ids.size() == 3, "acked ids returned");
    int acked = 0;
    for (const auto& r : rows) if (r.ack == AckState::Acked) ++acked;
    railhmi_test::check(acked == 4, "3 newly acked + 1 pre-acked = 4 (events not deleted)");
    railhmi_test::check(rows.size() == 4, "ack does not delete rows");

    const AckResult again = acknowledge(&rows, {1, 2});
    railhmi_test::check(again.changed == 0, "already-acked not recounted");

    const MaintenanceLogRow log = build_ack_log({1, 2, 3}, "maintainer", kT0 + 100000, 12345);
    railhmi_test::check(log.action == std::string("ack_alarm"), "log action");
    railhmi_test::check(log.target == std::string("1,2,3"), "log target ids");
    railhmi_test::check(log.detail == std::string("count=3"), "log detail count");
    railhmi_test::check(log.user == std::string("maintainer"), "log user");
}

void test_filter_options() {
    const AlarmFilterOptions o = build_filter_options(sample_rows());
    railhmi_test::check(o.camera_ids.size() == 4, "4 cameras in options");
    railhmi_test::check(o.types.size() == 4, "4 types in options");
    railhmi_test::check(o.levels.size() == 3 && o.levels[0] == AlarmLevel::L1, "3 levels sorted L1 first");
}

void test_time_helpers() {
    railhmi_test::check(format_time_text(parse_date_utc_ms("2026-10-16")) == std::string("2026-10-16 00:00:00"),
                        "date round trip");
    railhmi_test::check(day_start_utc_ms(kT0 + 5000) == kT0, "day start");
    railhmi_test::check(parse_date_utc_ms("bad") == -1, "bad date rejected");
}

// 现场固定类别：全部类型 / 摄像头 / 门磁 / NVR / 主机。
// chip 与事件集无关（core 只按事件本身派生类别），筛选必须真的委托 core。
void test_fixed_categories() {
    std::vector<AlarmRow> rows = sample_rows();
    rows.push_back(make_alarm_row(
        make_event(5, 2, "door_ajar", "DOOR-T1", kT0 + 10000, "unacked", "车门未关严", false),
        "门磁", RecordAvailability::Unavailable));
    rows.push_back(make_alarm_row(
        make_event(6, 2, "device", "NVR-1", kT0 + 11000, "unacked", "录像机异常", false),
        "录像机", RecordAvailability::Unavailable));
    rows.push_back(make_alarm_row(
        make_event(7, 3, "device", "HOST", kT0 + 12000, "unacked", "主机故障", false),
        "主机", RecordAvailability::Unavailable));

    railhmi_test::check(std::string(alarm_category(rows[0])) == "camera",
                        "camera-derived alarm -> camera category");
    railhmi_test::check(std::string(alarm_category(rows[4])) == "door",
                        "door_ajar -> door category even with camera-ish source id");
    railhmi_test::check(std::string(alarm_category(rows[5])) == "nvr",
                        "NVR source -> nvr category");
    railhmi_test::check(std::string(alarm_category(rows[6])) == "host",
                        "device fault -> host category");

    std::size_t n = 0;
    const char* const* tokens = alarm_category_tokens(&n);
    railhmi_test::check(n == 4 && std::string(tokens[0]) == "camera" &&
                            std::string(tokens[3]) == "host",
                        "exactly four fixed category tokens");
    railhmi_test::check(std::string(alarm_category_label("nvr")) == "NVR" &&
                            std::string(alarm_category_label("door")) == "门磁" &&
                            std::string(alarm_category_label("host")) == "主机",
                        "fixed category labels");
    railhmi_test::check(!parse_alarm_category("all") && !parse_alarm_category("bogus") &&
                            parse_alarm_category("camera"),
                        "unknown category token rejected, all is not a category");

    AlarmFilter f;
    f.has_category = true;
    f.category = "door";
    const std::vector<AlarmRow> door = query_alarms(rows, f);
    railhmi_test::check(door.size() == 1 && door[0].id == 5, "door category filter hit exactly one row");
    f.category = "nvr";
    railhmi_test::check(query_alarms(rows, f).size() == 1, "nvr category filter hit exactly one row");
    f.category = "host";
    railhmi_test::check(query_alarms(rows, f).size() == 1, "host category filter hit exactly one row");
    f.category = "camera";
    railhmi_test::check(query_alarms(rows, f).size() == 4, "camera category filter hit camera rows");
    f.has_category = false;
    railhmi_test::check(query_alarms(rows, f).size() == rows.size(),
                        "all-types clears category condition");
}

}  // namespace

void run_alarm_center_tests() {
    test_row_fields();
    test_default_sort();
    test_filters();
    test_batch_ack();
    test_filter_options();
    test_time_helpers();
    test_fixed_categories();
}
