// Verifier-owned ext fixture for ITEM-018 alarm center (software_verification).
// Linked against the delivered librailhmi_core.a built by the verifier's own
// board_build.py TestRun round; asserts acceptance boundaries of the alarm
// center model that the implementer's tests/probe do not cover:
//   * half-open time window [from, to) boundary semantics
//   * day_start negative floor; parse_date edge rejections + leap day
//   * stable_sort tie behavior; L1-first regardless of input order
//   * duplicate id ack flips every matching row; re-ack no recount;
//     acknowledge(to=Unacked) revert; unknown id no-op
//   * level > 3 clamps to L3; label three-state identity
//   * format_time_text non-positive -> em dash
//   * camera+ack / camera+level filter combos
//   * options dedup (camera/type/level), names parallel ids
//   * unknown type string -> Device fallback (typed label)
//   * query_alarms does not mutate the source vector
//   * build_ack_log empty/multi id, default result
// Pure logic, no Qt/SQLite — like the delivered probe.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/alarm_center.h"

using namespace railhmi::core;

namespace {

int g_passed = 0, g_failed = 0;
std::vector<std::string> g_notes;

void check(const std::string& name, bool ok) {
    (ok ? g_passed : g_failed) += 1;
    if (!ok) g_notes.push_back("FAIL: " + name);
}

EventRow ev(long long id, int level, const char* type, const char* cam, long long ts,
            const char* ack, const char* desc, const char* snapshot) {
    EventRow e;
    e.id = id;
    e.level = level;
    e.type = type;
    e.source_cam = cam;
    e.ts_wall_ms = ts;
    e.ack_state = ack;
    e.description = desc;
    e.snapshot_ref = snapshot;
    return e;
}

// --- 1. time window & date parsing ----------------------------------------
void t_window() {
    const long long t0 = parse_date_utc_ms("2026-10-16");
    AlarmRow a = make_alarm_row(ev(10, 1, "person_on_rail", "CAM-01", t0 + 2000, "unacked", "x", ""),
                                "前方", RecordAvailability::Available);
    AlarmRow b = make_alarm_row(ev(11, 2, "door_ajar", "CAM-06", t0 + 10000, "unacked", "y", ""),
                                "司机室", RecordAvailability::Available);
    AlarmFilter f;
    f.ts_from_ms = t0 + 2000;   // inclusive lower bound
    f.ts_to_ms = t0 + 10000;    // exclusive upper bound
    const std::vector<AlarmRow> got = query_alarms({a, b}, f);
    check("window [from,to) half-open: from in, to out",
          got.size() == 1 && got[0].id == 10);
    AlarmFilter f2;
    f2.ts_from_ms = t0 + 3000;
    f2.ts_to_ms = t0 + 9000;
    check("window strictly inside excludes both boundary rows",
          query_alarms({a, b}, f2).empty());
}

void t_date_edges() {
    check("month 13 rejected", parse_date_utc_ms("2026-13-01") == -1);
    check("month 0 rejected", parse_date_utc_ms("2026-00-10") == -1);
    check("day 32 rejected", parse_date_utc_ms("2026-10-32") == -1);
    check("day 0 rejected", parse_date_utc_ms("2026-10-00") == -1);
    check("malformed rejected", parse_date_utc_ms("2026-10-16x") == -1);
    check("short string rejected", parse_date_utc_ms("bad") == -1);
    check("leap day accepted", parse_date_utc_ms("2024-02-29") > 0);
    // observed lenient month-day bound (d<=31 only): 2023-02-29 normalizes to
    // 2023-03-01 rather than being rejected; not an acceptance criterion in
    // design/PRD, registered as observation O-1 in VERIFY-ITEM-018.md.
    check("non-leap 2-29 accepted + normalized to Mar 1 (observed leniency)",
          parse_date_utc_ms("2023-02-29") == parse_date_utc_ms("2023-03-01"));
    check("negative ts floors to previous day", day_start_utc_ms(-1) == -86400000LL);
    check("negative beyond boundary floors again",
          day_start_utc_ms(-86400000LL - 1) == -2 * 86400000LL);
    check("positive day start", day_start_utc_ms(parse_date_utc_ms("2026-10-16") + 5000)
          == parse_date_utc_ms("2026-10-16"));
}

// --- 2. default sort -------------------------------------------------------
void t_sort() {
    const long long t = parse_date_utc_ms("2026-10-16") + 5000;
    AlarmRow a = make_alarm_row(ev(1, 2, "door_ajar", "CAM-06", t, "unacked", "a", ""), "司机室",
                                RecordAvailability::Available);
    AlarmRow b = make_alarm_row(ev(2, 2, "front_obstacle", "CAM-01", t, "unacked", "b", ""), "前方",
                                RecordAvailability::Available);
    AlarmRow c = make_alarm_row(ev(3, 1, "wheel_derail", "CAM-05", t - 1, "unacked", "c", ""), "车底",
                                RecordAvailability::Available);
    std::vector<AlarmRow> rows = {b, a, c};
    sort_alarms_default(&rows);
    check("L1 first regardless of input order", rows[0].id == 3);
    check("equal (level,time) keys keep insertion order", rows[1].id == 2 && rows[2].id == 1);
    // time desc within same level
    AlarmRow d = make_alarm_row(ev(4, 1, "person_on_rail", "CAM-01", t, "unacked", "d", ""), "前方",
                                RecordAvailability::Available);
    std::vector<AlarmRow> rows2 = {c, d};
    sort_alarms_default(&rows2);
    check("same level earlier ts first (time desc)", rows2[0].id == 4 && rows2[1].id == 3);
}

// --- 3. acknowledge edges --------------------------------------------------
void t_ack() {
    AlarmRow base = make_alarm_row(ev(1, 1, "person_on_rail", "CAM-01", 1000, "unacked", "x", "s"),
                                   "前方", RecordAvailability::Available);
    std::vector<AlarmRow> rows = {base, base, base};   // duplicate ids
    AckResult r1 = acknowledge(&rows, {1}, AckState::Acked);
    check("duplicate ids: every matching row flips", r1.changed == 3);
    check("acked_ids counts duplicates", r1.acked_ids.size() == 3);
    AckResult r2 = acknowledge(&rows, {1, 1, 1, 77}, AckState::Acked);
    check("re-ack: no recount", r2.changed == 0 && r2.acked_ids.empty());
    AckResult r3 = acknowledge(&rows, {1}, AckState::Unacked);
    check("ack to Unacked reverts", r3.changed == 3 && rows[0].ack == AckState::Unacked);
    check("revert relabels", rows[0].ack_label == std::string("未确认"));
    AckResult r4 = acknowledge(&rows, {42}, AckState::Acked);
    check("unknown id: no change, no id", r4.changed == 0 && r4.acked_ids.empty());
}

// --- 4. row derivation edges ----------------------------------------------
void t_row_derive() {
    AlarmRow r = make_alarm_row(ev(5, 9, "device", "CAM-03", 1, "acked", "z", ""), "右侧",
                                RecordAvailability::Incomplete);
    check("level > 3 clamps to L3", r.level == AlarmLevel::L3);
    check("acked label", r.ack_label == std::string("已确认"));
    check("incomplete label", record_availability_label(RecordAvailability::Incomplete)
          == std::string("录像不完整"));
    check("unavailable label", record_availability_label(RecordAvailability::Unavailable)
          == std::string("录像不可用"));
    check("record to_string token",
          std::string(to_string(RecordAvailability::Incomplete)) == "incomplete");
    AlarmRow u = make_alarm_row(ev(9, 2, "totally_unknown_type", "CAM-04", 1, "unacked", "u", ""),
                                "车顶", RecordAvailability::Available);
    check("unknown type string -> Device fallback", u.type == AlarmType::Device);
    check("device fallback label", u.type_label == std::string("设备故障"));
    AlarmRow nsnap = make_alarm_row(ev(11, 1, "person_on_rail", "CAM-01", 1, "unacked", "n", ""),
                                    "前方", RecordAvailability::Available);
    check("empty snapshot_ref -> snapshot_available false", !nsnap.snapshot_available);
    check("em dash for ts<=0", format_time_text(0) == std::string("—")
          && format_time_text(-5) == std::string("—"));
}

// --- 5. filter combos & options -------------------------------------------
void t_filters_options() {
    const long long t0 = parse_date_utc_ms("2026-10-16");
    AlarmRow a = make_alarm_row(ev(1, 1, "person_on_rail", "CAM-01", t0 + 1000, "unacked", "p", ""),
                                "前方", RecordAvailability::Available);
    AlarmRow b = make_alarm_row(ev(2, 3, "side_camera_device", "CAM-02", t0 + 2000, "acked", "s", ""),
                                "左侧", RecordAvailability::Unavailable);
    AlarmFilter f;
    f.has_camera = true;
    f.camera_id = "CAM-02";
    f.has_ack = true;
    f.ack = AckState::Acked;
    const std::vector<AlarmRow> got = query_alarms({a, b}, f);
    check("camera+ack combo hit", got.size() == 1 && got[0].id == 2);
    AlarmFilter g;
    g.has_camera = true;
    g.camera_id = "CAM-02";
    g.has_level = true;
    g.level = AlarmLevel::L1;
    check("camera+level combo empty", query_alarms({a, b}, g).empty());
    AlarmFilter h;
    h.has_type = true;
    h.type = parse_alarm_type("person_on_rail");
    check("type token round trip", h.type == AlarmType::PersonOnRail
          && query_alarms({a, b}, h).size() == 1);
    check("empty list trivially empty", alarm_list_empty({}));
    check("non-empty list not empty", !alarm_list_empty({a}));

    AlarmRow a2 = make_alarm_row(ev(3, 1, "wheel_derail", "CAM-01", t0, "unacked", "w", ""),
                                 "前方", RecordAvailability::Available);
    const AlarmFilterOptions o = build_filter_options({a, a2, b});
    check("camera ids deduped", o.camera_ids.size() == 2);
    check("names parallel ids", o.camera_names.size() == o.camera_ids.size());
    int seen = 0;
    for (AlarmType t2 : o.types) if (t2 == AlarmType::PersonOnRail || t2 == AlarmType::WheelDerail)
        ++seen;
    check("distinct types counted", seen == 2);
    check("no Device in options of these rows", std::find(o.types.begin(), o.types.end(),
          AlarmType::Device) == o.types.end());
    bool hasL1 = false, hasL3 = false;
    for (AlarmLevel l : o.levels) { hasL1 = hasL1 || l == AlarmLevel::L1;
                                    hasL3 = hasL3 || l == AlarmLevel::L3; }
    check("levels deduped to L1+L3", o.levels.size() == 2 && hasL1 && hasL3);
}

// --- 6. non-mutation & log edges ------------------------------------------
void t_purity_log() {
    const long long t0 = parse_date_utc_ms("2026-10-16");
    AlarmRow a = make_alarm_row(ev(1, 3, "side_camera_device", "CAM-02", t0, "unacked", "s", ""),
                                "左侧", RecordAvailability::Unavailable);
    AlarmRow b = make_alarm_row(ev(2, 1, "person_on_rail", "CAM-01", t0, "unacked", "p", ""),
                                "前方", RecordAvailability::Available);
    std::vector<AlarmRow> src = {a, b};
    const std::vector<AlarmRow> got = query_alarms(src, AlarmFilter{});
    check("query returns sorted view", got.size() == 2 && got[0].id == 2 && got[1].id == 1);
    check("source vector order unmodified", src[0].id == 1 && src[1].id == 2);

    MaintenanceLogRow log = build_ack_log({7}, "tech", 5, 6);
    check("default result ok", log.result == std::string("ok"));
    log = build_ack_log({}, "tech", 5, 6);
    check("empty id list -> count=0, empty target",
          log.target.empty() && log.detail == std::string("count=0"));
    log = build_ack_log({1, 2, 3, 4}, "tech", 5, 6, "ok");
    check("multi-id join order preserved", log.target == std::string("1,2,3,4"));
    log = build_ack_log({1000000000000LL}, "op", 1, 2);
    check("large id stringified", log.target == std::string("1000000000000"));
}

void t_parse_fixture_sanity() {
    check("parse_alarm_type known token", parse_alarm_type("pantograph_abnormal")
          == AlarmType::PantographAbnormal);
    AlarmLevel l = AlarmLevel::L3;
    check("parse level L2", parse_alarm_level("L2", &l) && l == AlarmLevel::L2);
    check("parse level digit", parse_alarm_level("3", &l) && l == AlarmLevel::L3);
    check("parse level bad", !parse_alarm_level("L9", &l));
    AckState s = AckState::Acked;
    check("parse ack good", parse_ack_state("unacked", &s) && s == AckState::Unacked);
    check("parse ack bad", !parse_ack_state("maybe", &s));
}

}  // namespace

int main() {
    std::printf("== verify ITEM-018 ext (alarm-center acceptance fixtures) ==\n");
    t_window();
    t_date_edges();
    t_sort();
    t_ack();
    t_row_derive();
    t_filters_options();
    t_purity_log();
    t_parse_fixture_sanity();
    std::printf("RESULT: %s passed=%d failed=%d\n",
                g_failed == 0 ? "PASS" : "FAIL", g_passed, g_failed);
    for (const auto& n : g_notes) std::printf("  %s\n", n.c_str());
    return g_failed == 0 ? 0 : 1;
}
