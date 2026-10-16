// Verifier-owned ext fixture for ITEM-020 replay model (software_verification, v4).
// v4 targets the UI-review v2 FIX round (stamp 20261016-ITEM020-uifix-r2):
// keeps ALL v3 checks as regression and adds the alarm_marks_from_rows contract
// the deliverable does not cover from the verifier side (review
// REVIEW-ITEM-020-v2.md D-01 — levels must flow bridge->UI end to end):
//   * alarm_marks_from_rows: real level carried per row; camera filter; empty
//     camera_id -> ALL rows (no rows would break the bridge fallback contract);
//     rows of any level (L1/L2/L3); day-window filtering still happens downstream
//     in build_timeline
//   * marks path end to end: alarm_marks_from_rows -> build_timeline(AlarmMark
//     overload) -> segment level AND marker level == the real row level for
//     L1/L2/L3 probes at the same window
//   * level priority inside a critical window: first matching mark (by vector
//     order) wins (documented by level_in's loop)
//   * the bridge fallback contract: ts-only overload marks L3 — retained so the
//     "no rows fallback" branch is covered if ever reached
//   * per-camera marks: another camera's rows (CAM-04) do NOT color CAM-01's
//     critical segment when marks are camera-filtered first
// Pure logic + stdlib, no Qt — like the delivered probe.
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/replay.h"

#ifndef RAILHMI_CONFIG_DIR
#define RAILHMI_CONFIG_DIR "config"
#endif

using namespace railhmi::core;

namespace {

int g_passed = 0, g_failed = 0;
std::vector<std::string> g_notes;

void check(const std::string& name, bool ok) {
    (ok ? g_passed : g_failed) += 1;
    if (!ok) g_notes.push_back("FAIL: " + name);
}

const long long kDayMs = 86400000LL;
const long long kDay = parse_date_utc_ms("2026-10-16");

RecordingSegmentRow seg(long long id, const char* cam, long long a, long long b, SegmentKind k) {
    RecordingSegmentRow r;
    r.id = id; r.cam = cam; r.t0_ms = a; r.t1_ms = b; r.kind = k; r.complete = true;
    return r;
}

RecordingGapRow gap(const char* cam, long long a, long long b, const char* reason) {
    RecordingGapRow g;
    g.cam = cam; g.t0_ms = a; g.t1_ms = b; g.reason = reason;
    return g;
}

// --- 1. day parsing / formatting ---------------------------------------------
void t_day_parse_format() {
    check("parse_date 2026-10-16 == 1792108800000", kDay == 1792108800000LL);
    check("next day binds to +86400000", parse_date_utc_ms("2026-10-17") == kDay + kDayMs);
    check("reject short/long lengths", parse_date_utc_ms("2026-10-6") == -1 &&
          parse_date_utc_ms("202-10-16") == -1 && parse_date_utc_ms("2026-10-166") == -1);
    check("reject non-digit", parse_date_utc_ms("202a-10-16") == -1);
    check("reject month 13 / month 00", parse_date_utc_ms("2026-13-10") == -1 &&
          parse_date_utc_ms("2026-00-10") == -1);
    check("reject day 32 / day 00", parse_date_utc_ms("2026-10-32") == -1 &&
          parse_date_utc_ms("2026-10-00") == -1);
    check("format day start", format_time_text(kDay) == "2026-10-16 00:00:00");
    check("format +2min", format_time_text(kDay + 120000) == "2026-10-16 00:02:00");
    check("format ts<=0 -> dash", format_time_text(0) == "—" && format_time_text(-5) == "—");
    check("format last ms of previous day", format_time_text(kDay - 1) == "2026-10-15 23:59:59");
}

// --- 2. timeline clipping / sorting / filters (v2 regression) -----------------
void t_timeline() {
    std::vector<RecordingSegmentRow> s = {
        seg(1, "CAM-01", kDay - 10000, kDay + 20000, SegmentKind::Continuous),
        seg(2, "CAM-01", kDay + 80000000, kDay + 90000000, SegmentKind::Critical),
        seg(3, "CAM-01", kDay + 300000, kDay + 400000, SegmentKind::Continuous),
        seg(4, "CAM-01", kDay - 20000, kDay - 1000, SegmentKind::Continuous),
        seg(5, "CAM-01", kDay + 86500000, kDay + 87000000, SegmentKind::Continuous),
        seg(6, "CAM-02", kDay, kDay + 1000, SegmentKind::Critical),
        seg(7, "CAM-01", kDay, kDay, SegmentKind::Continuous)
    };
    std::vector<RecordingGapRow> g = {
        gap("CAM-01", kDay - 1000, kDay + 5000, "recording_interrupted"),
        gap("CAM-01", kDay + 86450000, kDay + 87000000, "fi10"),
        gap("CAM-02", kDay, kDay + 100, "fi10")
    };
    const ReplayTimeline t = build_timeline("CAM-01", kDay, s, g,
                                            {kDay - 1, kDay, kDay + 86400000, kDay + 5000},
                                            86400000);
    check("size 3 (before/after/zero-length/other-cam excluded)", t.segments.size() == 3);
    check("straddling start clipped to day_start", !t.segments.empty() &&
          t.segments[0].t0_ms == kDay && t.segments[0].t1_ms == kDay + 20000);
    check("unsorted input sorted by t0 (middle segment second)", t.segments.size() > 1 &&
          t.segments[1].t0_ms == kDay + 300000);
    check("straddling end clipped to day_end (critical survived)", t.segments.size() > 2 &&
          t.segments[2].t0_ms == kDay + 80000000 &&
          t.segments[2].t1_ms == kDay + 86400000 && t.segments[2].kind == SegmentKind::Critical);
    check("start-straddling gap clipped", t.gaps.size() == 1 &&
          t.gaps[0].t0_ms == kDay && t.gaps[0].t1_ms == kDay + 5000 &&
          t.gaps[0].reason == "recording_interrupted");
    check("alarm markers: day-edge ts kept inside, dropped at day_end and before",
          t.alarms.size() == 2 && t.alarms[0].ts_ms == kDay && t.alarms[1].ts_ms == kDay + 5000);
    check("alarm markers not camera-filtered (doc as implemented)",
          t.alarms[0].kind == "alarm" && t.alarms[1].label == "报警");
    check("keyframe interval == day length -> exactly 1 at day_start",
          t.keyframes.size() == 1 && t.keyframes[0].ts_ms == kDay);
    check("day window duration", t.duration_ms() == kDayMs && t.day_end_ms == kDay + kDayMs);
    check("has_alarm_segment/has_gap flags", t.has_alarm_segment() && t.has_gap());

    std::vector<RecordingSegmentRow> segs1 = {seg(1, "CAM-01", kDay, kDay + 1000,
                                                  SegmentKind::Continuous)};
    const std::vector<RecordingSegmentRow> no_segs;
    const std::vector<RecordingGapRow> none_gaps;
    const std::vector<long long> none_ts;
    const std::vector<AlarmMark> none_marks;
    const ReplayTimeline t2 = build_timeline("CAM-01", kDay, segs1,
                                             none_gaps, none_ts, 0);
    check("no critical -> has_alarm_segment false", !t2.has_alarm_segment());
    check("no gaps -> has_gap false", !t2.has_gap());
    check("keyframe interval 0 / negative -> none", t2.keyframes.empty());
    const ReplayTimeline t3 = build_timeline("CAM-01", kDay, no_segs, none_gaps,
                                             none_ts, -5);
    check("keyframe interval negative -> none", t3.keyframes.empty());
    std::vector<RecordingSegmentRow> segs2 = {seg(1, "CAM-01", kDay + 100, kDay + 200,
                                                  SegmentKind::Continuous)};
    const ReplayTimeline t4 = build_timeline("CAM-01", kDay, segs2,
                                             none_gaps, none_ts, 86400000);
    check("no critical row -> has_alarm_segment false (mixed)", !t4.has_alarm_segment());
}

// --- 2b. D-04 level propagation (v3 regression) --------------------------------
void t_level_propagation() {
    const std::vector<RecordingGapRow> none_gaps;
    // L1 alarm inside the critical window -> critical segment derives level 1;
    // L2 alarm outside the window only colors its own marker.
    const std::vector<RecordingSegmentRow> s = {
        seg(1, "CAM-01", kDay + 32000, kDay + 70000, SegmentKind::Critical),
        seg(2, "CAM-01", kDay, kDay + 10000, SegmentKind::Continuous)
    };
    std::vector<AlarmMark> marks = {
        {kDay + 65000, AlarmLevel::L1},   // inside critical window
        {kDay + 6500, AlarmLevel::L2}     // inside continuous window only
    };
    const ReplayTimeline t = build_timeline("CAM-01", kDay, s, none_gaps, marks, 0);
    check("v3 D-04 critical segment adopts real L1 level",
          t.segments.size() == 2 && t.segments[1].kind == SegmentKind::Critical &&
          t.segments[1].level == 1);
    check("v3 D-04 continuous segment level stays 0",
          t.segments[0].kind == SegmentKind::Continuous && t.segments[0].level == 0);
    check("v3 D-04 alarm markers sorted by ts keep their own levels",
          t.alarms.size() == 2 && t.alarms[0].ts_ms == kDay + 6500 && t.alarms[0].level == 2 &&
          t.alarms[1].ts_ms == kDay + 65000 && t.alarms[1].level == 1);

    // critical straddling day_end is clipped to day_end and keeps the level of the
    // L2 alarm inside its window; L1 alarm at day edge (ts == t0) also counts as in
    std::vector<RecordingSegmentRow> segc = {seg(1, "CAM-01", kDay + 10000, kDay + kDayMs,
                                                 SegmentKind::Critical)};
    std::vector<AlarmMark> markc = {{kDay, AlarmLevel::L1}, {kDay + 20000, AlarmLevel::L2}};
    const ReplayTimeline tc = build_timeline("CAM-01", kDay, segc, none_gaps, markc, 0);
    check("v3 D-04 clipped critical keeps first in-window alarm level (L2; day-edge marker not covered)",
          tc.segments.size() == 1 && tc.segments[0].t0_ms == kDay + 10000 &&
          tc.segments[0].t1_ms == kDay + kDayMs && tc.segments[0].level == 2);

    // empty alarms -> level stays 0 (renderers fall back to neutral)
    std::vector<RecordingSegmentRow> seg0 = {seg(1, "CAM-01", kDay, kDay + 1000,
                                                 SegmentKind::Critical)};
    const std::vector<AlarmMark> no_marks;
    const ReplayTimeline t0 = build_timeline("CAM-01", kDay, seg0,
                                             none_gaps, no_marks, 0);
    check("v3 D-04 critical without alarm -> level 0 unknown",
          t0.segments.size() == 1 && t0.segments[0].level == 0);

    // ts-only convenience overload keeps default L3 marks (bridge fallback contract)
    const std::vector<long long> ts_only = {kDay};
    const ReplayTimeline tl3 = build_timeline("CAM-01", kDay, seg0,
                                              none_gaps, ts_only, 0);
    check("v3 D-04 ts-only overload marks L3 (+ segment derives 3)",
          tl3.segments[0].level == 3 && tl3.alarms[0].level == 3);
}

// --- 2c. D-01 thumbnail refs (v3 regression) -----------------------------------
void t_keyframe_thumb() {
    std::vector<RecordingSegmentRow> segs = {seg(1, "CAM-01", kDay, kDay + 1000,
                                                 SegmentKind::Continuous)};
    std::vector<long long> alarm_ts = {kDay};
    const ReplayTimeline t = build_timeline("CAM-01", kDay, segs, {}, alarm_ts,
                                            3600000);
    check("v3 D-01 hourly keyframes count = 24", t.keyframes.size() == 24);
    check("v3 D-01 thumb ref camera-scoped",
          !t.keyframes.empty() && t.keyframes[0].thumb == "thumb/CAM-01/1792108800000.bmp");
    check("v3 D-01 thumb ts unambiguously maps to marker ts",
          t.keyframes[10].ts_ms == kDay + 36000000LL &&
          t.keyframes[10].thumb == "thumb/CAM-01/" +
              std::to_string(kDay + 36000000LL) + ".bmp");
    check("v3 D-01 kind separated alarm vs keyframe",
          !t.keyframes.empty() && t.keyframes[0].kind == "keyframe" &&
          t.alarms[0].kind == "alarm" && t.alarms[0].thumb.empty());
}

// --- 2d. v4 NEW: alarm_marks_from_rows contract --------------------------------
AlarmRow row_of(long long id, long long level, const char* cam, const char* name,
                long long ts) {
    EventRow e;
    e.id = id; e.level = static_cast<int>(level); e.type = "person_on_rail";
    e.source_cam = cam; e.ts_wall_ms = ts; e.ack_state = "unacked";
    return make_alarm_row(e, name, RecordAvailability::Available);
}

void t_marks_from_rows() {
    // hand-built rows: AlarmLevel values 1..3 must round-trip through the rows
    std::vector<AlarmRow> rows = {
        row_of(1, 1, "CAM-01", "前方", kDay + 65000),    // front L1
        row_of(2, 2, "CAM-04", "车顶", kDay + 9000),     // roof L2
        row_of(3, 3, "CAM-01", "前方", kDay + 500000)    // front L3
    };
    const std::vector<AlarmMark> front = alarm_marks_from_rows(rows, "CAM-01");
    check("v4 marks: camera filter keeps front rows only (2)",
          front.size() == 2);
    check("v4 marks: first front mark carries real L1 (row.level -> AlarmMark.level)",
          front[0].ts_ms == kDay + 65000 && front[0].level == AlarmLevel::L1);
    check("v4 marks: second front mark carries real L3",
          front[1].ts_ms == kDay + 500000 && front[1].level == AlarmLevel::L3);
    const std::vector<AlarmMark> roof = alarm_marks_from_rows(rows, "CAM-04");
    check("v4 marks: roof mark carries real L2",
          roof.size() == 1 && roof[0].level == AlarmLevel::L2 &&
          roof[0].ts_ms == kDay + 9000);
    const std::vector<AlarmMark> all = alarm_marks_from_rows(rows, "");
    check("v4 marks: empty camera_id -> ALL rows (no rows would starve the bridge)",
          all.size() == 3);
    const std::vector<AlarmMark> none = alarm_marks_from_rows({}, "CAM-01");
    check("v4 marks: no rows -> empty marks (bridge then faces fallback decision)",
          none.empty());
    // unknown camera filter -> no marks (no silent widening)
    check("v4 marks: unknown camera filter -> empty (no silent widening)",
          alarm_marks_from_rows(rows, "CAM-XX").empty());

    // end to end: marks from rows -> timeline; segment AND marker level equal the
    // real row level for L1/L2/L3 via the exact bridge path
    const std::vector<RecordingGapRow> none_gaps;
    std::vector<RecordingSegmentRow> crit = {seg(1, "CAM-01", kDay + 60000,
                                                 kDay + 70000, SegmentKind::Critical)};
    for (int lvl = 1; lvl <= 3; ++lvl) {
        std::vector<AlarmRow> one = {row_of(10, lvl, "CAM-01", "前方", kDay + 65000)};
        const std::vector<AlarmMark> m1 = alarm_marks_from_rows(one, "CAM-01");
        const ReplayTimeline tt = build_timeline("CAM-01", kDay, crit, none_gaps, m1, 0);
        char name[96];
        std::snprintf(name, sizeof(name),
                      "v4 e2e L%d: marks-from-rows -> segment level %d == marker level %d",
                      lvl, lvl, lvl);
        check(name, tt.segments.size() == 1 && tt.segments[0].level == lvl &&
                    tt.alarms.size() == 1 && tt.alarms[0].level == lvl);
    }

    // per-camera marks must NOT color another camera's window:
    // with only front rows filtered, the roof critical segment stays level 0 when
    // building CAM-04's timeline from the front filter (and vice versa)
    std::vector<RecordingSegmentRow> roof_crit = {seg(2, "CAM-04", kDay + 8000,
                                                      kDay + 11000, SegmentKind::Critical)};
    const ReplayTimeline tfront = build_timeline("CAM-04", kDay, roof_crit,
                                                 none_gaps, front, 0);
    check("v4 per-camera: front-filtered marks leave CAM-04 critical level 0",
          tfront.segments.size() == 1 && tfront.segments[0].level == 0);
    const ReplayTimeline troof = build_timeline("CAM-01", kDay, crit,
                                                none_gaps, roof, 0);
    check("v4 per-camera: roof-filtered marks leave CAM-01 critical level 0",
          troof.segments.size() == 1 && troof.segments[0].level == 0);

    // level priority in a critical window: first matching mark (vector order) wins
    std::vector<AlarmMark> two_in = {{kDay + 65000, AlarmLevel::L1},
                                     {kDay + 66000, AlarmLevel::L2}};
    const ReplayTimeline tprio = build_timeline("CAM-01", kDay, crit, none_gaps, two_in, 0);
    check("v4 window priority: first matching mark's level wins (L1)",
          tprio.segments.size() == 1 && tprio.segments[0].level == 1);
    std::vector<AlarmMark> two_in_r = {{kDay + 65000, AlarmLevel::L3},
                                       {kDay + 66000, AlarmLevel::L2}};
    const ReplayTimeline tprio2 = build_timeline("CAM-01", kDay, crit, none_gaps,
                                                 two_in_r, 0);
    check("v4 window priority: order (not level) decides, L3 also propagates",
          tprio2.segments.size() == 1 && tprio2.segments[0].level == 3);
    // mark at the window edge ts == t1 counts as inside (inclusive both ends)
    std::vector<AlarmMark> edge = {{kDay + 70000, AlarmLevel::L2}};
    const ReplayTimeline tedge = build_timeline("CAM-01", kDay, crit, none_gaps, edge, 0);
    check("v4 window inclusive at t1 edge", tedge.segments.size() == 1 &&
          tedge.segments[0].level == 2);
}

// --- 3. jumps / seek / jump_to -------------------------------------------------
void t_jumps_seek() {
    const long long alarm = kDay + 500000;
    const JumpTargets j = jump_targets(alarm);
    check("jump offsets exact -30s", j.minus30_ms == alarm - 30000);
    check("jump alarm exact", j.alarm_ms == alarm);
    check("jump +30s exact", j.plus30_ms == alarm + 30000);
    check("jump +60s exact", j.plus60_ms == alarm + 60000);

    PlaybackState s;
    seek(&s, kDay + 86400000, kDay, kDay + 86400000);
    check("seek at exact day_end allowed (<= clamp)", s.pos_ms == kDay + 86400000);
    seek(&s, kDay + 87000000, kDay, kDay + 86400000);
    check("seek beyond day_end clamps to day_end", s.pos_ms == kDay + 86400000);
    seek(&s, kDay - 1, kDay, kDay + 86400000);
    check("seek before day_start clamps to day_start", s.pos_ms == kDay);
    jump_to(&s, kDay + 999999999);
    check("jump_to does not clamp (contract as implemented)",
          s.pos_ms == kDay + 999999999);
}

// --- 4. sync edges (v1 regression) ------------------------------------------------
void t_sync() {
    const SyncResult e = sync_status({}, 300);
    check("empty positions -> defaults (dev 0, in sync, no prompt)",
          e.max_deviation_ms == 0 && e.in_sync && !e.has_prompt && e.prompt.empty());
    const SyncResult one = sync_status({kDay + 42}, 300);
    check("single position -> dev 0", one.max_deviation_ms == 0 && one.in_sync);
    const SyncResult z = sync_status({5, 5}, 0);
    check("threshold 0 & dev 0 -> in_sync", z.in_sync && !z.has_prompt && z.max_deviation_ms == 0);
    const SyncResult z1 = sync_status({5, 6}, 0);
    check("threshold 0 & dev 1 -> prompt", !z1.in_sync && z1.has_prompt &&
          z1.prompt == "同步偏差 1 ms（阈值 0 ms）");
    const SyncResult b = sync_status({0, 300}, 300);
    check("dev == threshold -> inclusive in_sync", b.in_sync && !b.has_prompt &&
          b.max_deviation_ms == 300);
    const SyncResult b1 = sync_status({0, 301}, 300);
    check("dev == threshold+1 -> prompt with exact text",
          !b1.in_sync && b1.has_prompt && b1.prompt == "同步偏差 301 ms（阈值 300 ms）");
    const SyncResult mx = sync_status({900, 100, 300}, 300);
    check("deviation computed as hi - lo across three", mx.max_deviation_ms == 800 &&
          mx.has_prompt && mx.prompt == "同步偏差 800 ms（阈值 300 ms）");
}

// --- 5. D-01 fix: related_cameras ------------------------------------------------
void t_related_cameras() {
    const std::vector<std::string> front = related_cameras("CAM-01");
    check("front alarm locks exactly the sync triple in order CAM-02,CAM-01,CAM-03",
          front.size() == 3 && front[0] == "CAM-02" && front[1] == "CAM-01" &&
          front[2] == "CAM-03");
    check("front triple == front_alarm_sync_group", front == front_alarm_sync_group());
    const std::vector<std::string> left = related_cameras("CAM-02");
    check("non-front alarm locks only its own camera (left)",
          left.size() == 1 && left[0] == "CAM-02");
    const std::vector<std::string> roof = related_cameras("CAM-04");
    check("roof camera alarm locks only CAM-04 (not the front triple)",
          roof.size() == 1 && roof[0] == "CAM-04");
    check("unknown camera id still locks single (no crash / no empty)",
          related_cameras("CAM-XX").size() == 1 &&
          related_cameras("CAM-XX")[0] == "CAM-XX");
    check("empty camera -> no lock", related_cameras("").empty());
}

// --- 6. D-02 fix: sync_snapshot ---------------------------------------------------
void t_sync_snapshot() {
    const std::vector<std::string> locked = related_cameras("CAM-01");
    // empty offsets -> unavailable with exact UI text; no false "in sync"
    const SyncSnapshot none = sync_snapshot(locked, kDay + 5000, {}, 300);
    check("empty offsets -> unavailable, not in_sync, exact text",
          !none.available && !none.in_sync && none.max_deviation_ms == 0 &&
          none.prompt == "同步状态不可用");
    // locked set empty -> unavailable too
    const SyncSnapshot noLock = sync_snapshot({}, kDay + 5000, {{ "CAM-01", 0 }}, 300);
    check("empty locked set -> unavailable", !noLock.available &&
          noLock.prompt == "同步状态不可用");
    // missing one locked camera -> unavailable (no silent guess)
    std::map<std::string, long long> partial;
    partial["CAM-02"] = 0;
    partial["CAM-01"] = 10;
    const SyncSnapshot miss = sync_snapshot(locked, kDay + 5000, partial, 300);
    check("missing CAM-03 offset -> unavailable", !miss.available &&
          miss.prompt == "同步状态不可用");
    // positions are cursor + offset: dev independent of the absolute cursor
    std::map<std::string, long long> ok;
    ok["CAM-02"] = 0;
    ok["CAM-01"] = 120;
    ok["CAM-03"] = 200;
    const SyncSnapshot a = sync_snapshot(locked, kDay + 5000, ok, 300);
    check("within-threshold snapshot: available, in sync, dev 200, no prompt",
          a.available && a.in_sync && a.max_deviation_ms == 200 && a.prompt.empty());
    const SyncSnapshot b = sync_snapshot(locked, kDay + 777777, ok, 300);
    check("deviation independent of absolute cursor", b.available && b.in_sync &&
          b.max_deviation_ms == 200);
    // inclusive threshold boundary through the snapshot path
    std::map<std::string, long long> edge = ok;
    edge["CAM-03"] = 300;
    const SyncSnapshot at = sync_snapshot(locked, 1000, edge, 300);
    check("dev == threshold stays in sync (inclusive)", at.available && at.in_sync &&
          at.max_deviation_ms == 300 && at.prompt.empty());
    std::map<std::string, long long> over = ok;
    over["CAM-03"] = 301;
    const SyncSnapshot o1 = sync_snapshot(locked, 1000, over, 300);
    check("dev == threshold+1 -> prompt with exact text", o1.available && !o1.in_sync &&
          o1.max_deviation_ms == 301 && o1.prompt == "同步偏差 301 ms（阈值 300 ms）");
    // big deviation -> probe-matching full prompt text
    std::map<std::string, long long> dev = ok;
    dev["CAM-03"] = 900;
    const SyncSnapshot big = sync_snapshot(locked, kDay + 5000, dev, 300);
    check("dev 900 -> exact probe prompt text",
          big.available && !big.in_sync && big.max_deviation_ms == 900 &&
          big.prompt == "同步偏差 900 ms（阈值 300 ms）");
    // single-camera lock (non-front alarm) with its own offset
    const std::vector<std::string> one = related_cameras("CAM-05");
    std::map<std::string, long long> off5;
    off5["CAM-05"] = 60;
    const SyncSnapshot s1 = sync_snapshot(one, kDay + 2000, off5, 300);
    check("single-camera snapshot in sync (only offset no dev)",
          s1.available && s1.in_sync && s1.max_deviation_ms == 0);
    // single-camera lock WITHOUT its offset -> unavailable (no fallback to triple)
    std::map<std::string, long long> other;
    other["CAM-02"] = 0;
    const SyncSnapshot s2 = sync_snapshot(one, kDay + 2000, other, 300);
    check("single-camera lock without its offset -> unavailable", !s2.available &&
          s2.prompt == "同步状态不可用");
}

// --- 7. source status (v1 regression) ----------------------------------------------
void t_source() {
    const ReplaySourceStatus nvr = replay_source_status(true, false);
    check("nvr-only -> Nvr usable, empty note",
          nvr.source == ReplaySource::Nvr && nvr.usable && nvr.note.empty() &&
          nvr.label == "NVR 录像");
    const ReplaySourceStatus both = replay_source_status(true, true);
    check("both -> NVR wins", both.source == ReplaySource::Nvr && both.usable);
    const ReplaySourceStatus local = replay_source_status(false, true);
    check("local fallback note exact", local.source == ReplaySource::LocalClips &&
          local.usable && local.label == "本机片段" &&
          local.note == "NVR 录像不可用，回放本机片段");
    const ReplaySourceStatus none = replay_source_status(false, false);
    check("none -> Unavailable with explicit note",
          none.source == ReplaySource::Unavailable && !none.usable &&
          none.label == "录像不可用" && none.note == "录像不可用");
    check("to_string roundtrip", std::string(to_string(ReplaySource::LocalClips)) == "local_clips" &&
          std::string(to_string(ReplaySource::Nvr)) == "nvr" &&
          std::string(to_string(ReplaySource::Unavailable)) == "unavailable");
}

// --- 8. event detail edges (v1 regression + D-01 detail-from-row chain) -------------
void t_detail() {
    EventRow e;
    e.id = 7; e.level = 2; e.type = "person_on_rail"; e.source_cam = "CAM-01";
    e.ts_wall_ms = kDay + 125000; e.description = "轨道人员"; e.ack_state = "unacked";
    const AlarmRow row2 = make_alarm_row(e, "前方", RecordAvailability::Incomplete);
    const ReplayEventDetail d2 = build_event_detail(row2, 3.14159, 0.8749);
    check("L2 label", d2.level_label == "二级");
    check("unacked label", d2.ack_label == "未确认");
    check("distance %.1f rounding", d2.distance_text == "3.1 m");
    check("confidence floor rounding 87", d2.confidence_text == "87%");
    // D-07 fix contract change: default note no longer aliases the availability
    // label that moved to record_label (was note=="录像不完整" pre-fix)
    check("default note empty; availability label carried by record_label (D-07 fix)",
          d2.note.empty() && d2.record_label == "录像不完整" && !d2.record_ok);

    const ReplayEventDetail d3 = build_event_detail(make_alarm_row(e, "前方", RecordAvailability::Available),
                                                    -1.0, 0.875);
    check("distance sentinel -> dash", d3.distance_text == "—");
    check("default note empty; record_label 可回放 record_ok=true (D-07 fix)",
          d3.note.empty() && d3.record_label == "可回放" && d3.record_ok);
    check("row carries selection identity fields (D-01 detail chain)",
          row2.camera_id == "CAM-01" && row2.time_text.size() == 19 &&
          row2.ts_wall_ms == kDay + 125000 && row2.camera_name == "前方");
    const ReplayEventDetail d4 = build_event_detail(make_alarm_row(e, "前方", RecordAvailability::Unavailable),
                                                    -1.0, -1.0);
    check("confidence sentinel -> dash", d4.confidence_text == "—");
    check("confidence half-up rounding 88", build_event_detail(
              make_alarm_row(e, "前方", RecordAvailability::Unavailable), 12.34, 0.875)
              .confidence_text == "88%");

    e.level = 3;
    const ReplayEventDetail dl3 = build_event_detail(make_alarm_row(e, "前方", RecordAvailability::Available),
                                                     15.96, 0.0);
    check("L3 label", dl3.level_label == "三级");
    check("confidence 0 -> 0%", dl3.confidence_text == "0%");

    e.ack_state = "acked";
    check("acked label", build_event_detail(make_alarm_row(e, "前方", RecordAvailability::Available),
                                            1.0, 0.9).ack_label == "已确认");
}

// --- 9. pantograph row -> distinct detail (selection must change detail) -------------
void t_detail_distinct_types() {
    EventRow roof;
    roof.id = 3; roof.level = 2; roof.type = "pantograph_abnormal"; roof.source_cam = "CAM-04";
    roof.ts_wall_ms = kDay + 9000; roof.description = "受电弓异常"; roof.ack_state = "unacked";
    const AlarmRow r3 = make_alarm_row(roof, "车顶", RecordAvailability::Incomplete);
    const ReplayEventDetail d3 = build_event_detail(r3, -1.0, 0.91);
    check("roof row type label distinct", d3.type_label == "受电弓异常");
    check("roof row distance sentinel -> dash (no number faked)",
          d3.distance_text == "—");
    check("roof row confidence 91%", d3.confidence_text == "91%");
    check("roof row unacked", d3.ack_label == "未确认");
    const AlarmRow front = make_alarm_row(
        EventRow{1, 1, "person_on_rail", "CAM-01", "", kDay + 5000, 0, 0.0, -1.0, "",
                 "", "轨道人员", "acked", false, ""},
        "前方", RecordAvailability::Available);
    const ReplayEventDetail d1 = build_event_detail(front, 12.34, 0.87);
    check("front row distinct from roof row (selection changes detail)",
          d1.type_label == "前方轨道人员" && d1.level_label == "一级" &&
          d1.ack_label == "已确认" && d1.distance_text == "12.3 m");
}

// --- 10. config: replay threshold parse / validate (replay_model consumes) -----------
void t_config_threshold() {
    const std::string dir = RAILHMI_CONFIG_DIR;
    const auto read = [&](const char* name) {
        std::ifstream in(dir + "/" + name, std::ios::binary);
        std::ostringstream ss;
        ss << in.rdbuf();
        return ss.str();
    };
    const ConfigLoadResult base = load_config_from_strings(
        read("cameras.yaml"), read("app.yaml"), read("thresholds.yaml"), read("roi.yaml"));
    check("baseline config ok", base.ok);
    check("replay_sync_threshold_ms parsed = 300",
          base.settings.thresholds.replay_sync_threshold_ms == 300);

    // remove the key -> error mentions the exact field path; no silent default
    std::string th = read("thresholds.yaml");
    std::istringstream in(th);
    std::string line, out;
    while (std::getline(in, line))
        if (line.find("replay_sync_threshold_ms") == std::string::npos)
            out += line + "\n";
    const ConfigLoadResult miss = load_config_from_strings(read("cameras.yaml"),
        read("app.yaml"), out, read("roi.yaml"));
    bool miss_path = false;
    for (const auto& e2 : miss.errors)
        if (e2.path.find("thresholds.replay_sync_threshold_ms") != std::string::npos) miss_path = true;
    check("missing replay key -> explicit field error, not ok", !miss.ok && miss_path);

    // negative value rejected
    std::string th2 = read("thresholds.yaml");
    {
        const std::string from = "replay_sync_threshold_ms: 300";
        const auto pos = th2.find(from);
        if (pos != std::string::npos) th2.replace(pos, from.size(), "replay_sync_threshold_ms: -1");
    }
    const ConfigLoadResult neg = load_config_from_strings(read("cameras.yaml"),
        read("app.yaml"), th2, read("roi.yaml"));
    bool neg_path = false;
    for (const auto& e2 : neg.errors)
        if (e2.path.find("thresholds.replay_sync_threshold_ms") != std::string::npos &&
            e2.message.find(">= 0") != std::string::npos) neg_path = true;
    check("negative replay threshold -> error must be >= 0", !neg.ok && neg_path);

    // cross-check: threshold parsed from config drives sync_snapshot semantics
    const int ms = base.settings.thresholds.replay_sync_threshold_ms;
    const std::vector<std::string> locked = related_cameras("CAM-01");
    std::map<std::string, long long> at, over;
    at["CAM-02"] = 0; at["CAM-01"] = 0; at["CAM-03"] = ms;
    over = at; over["CAM-03"] = ms + 1;
    check("config threshold drives inclusive snapshot boundary",
          sync_snapshot(locked, 1000, at, ms).in_sync &&
          !sync_snapshot(locked, 1000, over, ms).in_sync);
}


// --- 11. D-07 v3: note param independent of record availability; record_ok 三态 ----
void t_detail_note_v3() {
    EventRow e;
    e.id = 9; e.level = 2; e.type = "person_on_rail"; e.source_cam = "CAM-01";
    e.ts_wall_ms = kDay + 15000; e.description = "轨道人员"; e.ack_state = "acked";
    const AlarmRow avail = make_alarm_row(e, "前方", RecordAvailability::Available);
    const ReplayEventDetail d1 = build_event_detail(avail, 5.0, 0.9, "维护备注甲");
    check("v3 D-07 explicit note kept verbatim", d1.note == "维护备注甲");
    check("v3 D-07 record fields independent of note (available)",
          d1.record_label == "可回放" && d1.record_ok);

    const AlarmRow inc = make_alarm_row(e, "前方", RecordAvailability::Incomplete);
    const ReplayEventDetail d2 = build_event_detail(inc, 5.0, 0.9, "维护备注乙");
    check("v3 D-07 incomplete record_ok=false with explicit note",
          d2.record_ok == false && d2.record_label == "录像不完整" &&
          d2.note == "维护备注乙");
    // default overload keeps alias-free empty note (not the record label)
    const ReplayEventDetail d2b = build_event_detail(inc, 5.0, 0.9);
    check("v3 D-07 default note empty while record label set",
          d2b.note.empty() && d2b.record_label == "录像不完整" && !d2b.record_ok);

    const AlarmRow un = make_alarm_row(e, "前方", RecordAvailability::Unavailable);
    const ReplayEventDetail d3 = build_event_detail(un, 5.0, 0.9, "维护备注丙");
    check("v3 D-07 unavailable record_ok=false with explicit note",
          d3.record_ok == false && d3.record_label == "录像不可用" &&
          d3.note == "维护备注丙");

    // empty explicit note falls back to dash display contract handled in UI; here
    // assert the note param itself is what lands in the struct (no silent swap)
    const ReplayEventDetail d4 = build_event_detail(avail, 5.0, 0.9, "");
    check("v3 D-07 empty note round-trips as empty (UI renders dash)",
          d4.note.empty() && d4.record_label == "可回放");
}

}  // namespace

int main() {
    t_day_parse_format();
    t_timeline();
    t_level_propagation();
    t_keyframe_thumb();
    t_marks_from_rows();
    t_jumps_seek();
    t_sync();
    t_related_cameras();
    t_sync_snapshot();
    t_source();
    t_detail();
    t_detail_distinct_types();
    t_config_threshold();
    t_detail_note_v3();

    for (const auto& n : g_notes) std::printf("%s\n", n.c_str());
    const bool ok = g_failed == 0;
    std::printf("== %d passed, %d failed ==\n", g_passed, g_failed);
    std::printf("RESULT: %s passed=%d failed=%d\n", ok ? "PASS" : "FAIL", g_passed, g_failed);
    return ok ? 0 : 1;
}
