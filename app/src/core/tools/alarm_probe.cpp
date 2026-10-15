// railhmi 报警中心模型探针（ITEM-018）
// 用样例事件演示：字段派生、默认排序（级别降序→时间降序）、六维筛选与空态、
// 批量确认（只改 ack_state 不删事件）与维护日志条目、录像不可用行内标注。纯 core，无 Qt/SQLite。
//
// 用法: railhmi-alarm-probe [--out log]
// 退出码: 0 验收 PASS；1 FAIL。

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/alarm_center.h"

using namespace railhmi::core;

int main(int argc, char** argv) {
    std::string out;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--out") next(&out);
        else if (a == "--help" || a == "-h") { std::printf("usage: railhmi-alarm-probe [--out log]\n"); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }
    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) { std::printf("%s\n", line.c_str()); if (fo) fo << line << "\n"; };

    const long long t0 = parse_date_utc_ms("2026-10-16");
    auto ev = [&](long long id, int lvl, const char* type, const char* cam, long long ts, const char* ack,
                  const char* desc, const char* snap) {
        EventRow e;
        e.id = id; e.level = lvl; e.type = type; e.source_cam = cam; e.ts_wall_ms = ts;
        e.ack_state = ack; e.description = desc; e.snapshot_ref = snap;
        return e;
    };
    std::vector<AlarmRow> rows;
    rows.push_back(make_alarm_row(ev(1, 1, "person_on_rail", "CAM-01", t0 + 5000, "unacked", "轨道人员", "s1"), "前方", RecordAvailability::Available));
    rows.push_back(make_alarm_row(ev(2, 3, "side_camera_device", "CAM-02", t0 + 3000, "unacked", "侧视离线", ""), "左侧", RecordAvailability::Unavailable));
    rows.push_back(make_alarm_row(ev(3, 2, "pantograph_abnormal", "CAM-04", t0 + 9000, "unacked", "受电弓异常", "s3"), "车顶", RecordAvailability::Incomplete));
    rows.push_back(make_alarm_row(ev(4, 1, "wheel_derail", "CAM-05", t0 + 1000, "acked", "轮轨异常", "s4"), "车底", RecordAvailability::Available));

    emit("type=list rows=" + std::to_string(rows.size()) + " order=level_desc,time_desc");
    std::vector<AlarmRow> sorted = rows;
    sort_alarms_default(&sorted);
    for (const auto& r : sorted)
        emit("row id=" + std::to_string(r.id) + " level=" + to_string(r.level) + " type=" + to_string(r.type) +
             " time=" + r.time_text + " cam=" + r.camera_id + "(" + r.camera_name + ")" +
             " ack=" + to_string(r.ack) + " record=" + to_string(r.record) + " snapshot=" +
             (r.snapshot_available ? "yes" : "no") + " desc=" + r.description +
             (r.record == RecordAvailability::Available ? "" : " [" + r.record_label + "]"));

    AlarmFilter f1; f1.has_level = true; f1.level = AlarmLevel::L1;
    AlarmFilter f2; f2.has_ack = true; f2.ack = AckState::Unacked;
    AlarmFilter f3; f3.ts_from_ms = t0 + 2000; f3.ts_to_ms = t0 + 6000;
    AlarmFilter f4; f4.has_level = true; f4.level = AlarmLevel::L3; f4.has_camera = true; f4.camera_id = "CAM-01";
    emit("type=filter name=level_L1 count=" + std::to_string(query_alarms(rows, f1).size()));
    emit("type=filter name=unacked count=" + std::to_string(query_alarms(rows, f2).size()));
    emit("type=filter name=window count=" + std::to_string(query_alarms(rows, f3).size()));
    emit("type=filter name=combo_empty count=" + std::to_string(query_alarms(rows, f4).size()) +
         " empty=" + (alarm_list_empty(query_alarms(rows, f4)) ? "true" : "false"));

    const AckResult res = acknowledge(&rows, {1, 2, 3});
    emit("type=ack changed=" + std::to_string(res.changed));
    for (const auto& r : rows)
        emit("row id=" + std::to_string(r.id) + " ack=" + to_string(r.ack) + " ack_label=" + r.ack_label);
    const MaintenanceLogRow log = build_ack_log(res.acked_ids, "maintainer", t0 + 100000, 555);
    emit("type=maint-log action=" + log.action + " target=" + log.target + " user=" + log.user +
         " detail=" + log.detail);

    const AlarmFilterOptions opts = build_filter_options(rows);
    emit("type=options cameras=" + std::to_string(opts.camera_ids.size()) + " types=" +
         std::to_string(opts.types.size()) + " levels=" + std::to_string(opts.levels.size()));

    const bool ok = (res.changed == 3) && (query_alarms(rows, f1).size() == 2) &&
                    (query_alarms(rows, f2).size() == 0) && alarm_list_empty(query_alarms(rows, f4)) &&
                    (log.target == "1,2,3") && (rows.size() == 4);
    emit(std::string("type=acceptance batch_ack_changed=") + std::to_string(res.changed) +
         " events_preserved=" + (rows.size() == 4 ? "true" : "false") +
         " empty_state_ok=" + (alarm_list_empty(query_alarms(rows, f4)) ? "true" : "false") +
         " maint_log=" + log.target + " result=" + (ok ? "PASS" : "FAIL"));
    return ok ? 0 : 1;
}
