// Verifier-owned ext fixture for ITEM-020 replay model (software_verification, v2).
// v2 targets the D-01/D-02 FIX round: keeps all v1 logic-edge checks as regression
// and adds fix-round boundaries the deliverable does not cover:
//   * related_cameras: front triple order (CAM-02,CAM-01,CAM-03), non-front single
//     lock, unknown camera single lock, empty camera -> no lock
//   * sync_snapshot: positions are cursor+offset (independent of cursor value),
//     unlocked/partial offsets -> unavailable with exact UI text, single-camera
//     snapshot, inclusive threshold boundary through snapshot, empty offsets,
//     exact prompt texts
//   plus v1 coverage retained: date parse/format edges, cross-day timeline
//   clipping/sorting/filtering, jump/seek clamping, sync_status edges (empty,
//   single, threshold 0, inclusive boundary, exact prompt), source combinations,
//   detail sentinels/rounding/labels, replay_sync_threshold_ms config
//   parse/validation path (missing key, negative value).
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

// --- 2. timeline clipping / sorting / filters --------------------------------
void t_timeline() {
    std::vector<RecordingSegmentRow> s = {
        seg(1, "CAM-01", kDay - 10000, kDay + 20000, SegmentKind::Continuous),      // straddles start
        seg(2, "CAM-01", kDay + 80000000, kDay + 90000000, SegmentKind::Critical),  // straddles end
        seg(3, "CAM-01", kDay + 300000, kDay + 400000, SegmentKind::Continuous),    // middle, unsorted
        seg(4, "CAM-01", kDay - 20000, kDay - 1000, SegmentKind::Continuous),       // fully before
        seg(5, "CAM-01", kDay + 86500000, kDay + 87000000, SegmentKind::Continuous),// fully after
        seg(6, "CAM-02", kDay, kDay + 1000, SegmentKind::Critical),                 // other camera
        seg(7, "CAM-01", kDay, kDay, SegmentKind::Continuous)                       // zero length
    };
    std::vector<RecordingGapRow> g = {
        gap("CAM-01", kDay - 1000, kDay + 5000, "recording_interrupted"),           // straddles start
        gap("CAM-01", kDay + 86450000, kDay + 87000000, "fi10"),                    // fully after
        gap("CAM-02", kDay, kDay + 100, "fi10")                                     // other camera
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

    const ReplayTimeline t2 = build_timeline("CAM-01", kDay, {seg(1, "CAM-01", kDay, kDay + 1000,
                                                              SegmentKind::Continuous)},
                                             {}, {}, 0);
    check("no critical -> has_alarm_segment false", !t2.has_alarm_segment());
    check("no gaps -> has_gap false", !t2.has_gap());
    check("keyframe interval 0 / negative -> none", t2.keyframes.empty());
    const ReplayTimeline t3 = build_timeline("CAM-01", kDay, {}, {}, {}, -5);
    check("keyframe interval negative -> none", t3.keyframes.empty());
    const ReplayTimeline t4 = build_timeline("CAM-01", kDay, {seg(1, "CAM-01", kDay + 100, kDay + 200,
                                                              SegmentKind::Continuous)},
                                             {}, {}, 86400000);
    check("no critical row -> has_alarm_segment false (mixed)", !t4.has_alarm_segment());
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
    check("note == record availability label (incomplete)",
          d2.note == "录像不完整");

    const ReplayEventDetail d3 = build_event_detail(make_alarm_row(e, "前方", RecordAvailability::Available),
                                                    -1.0, 0.875);
    check("distance sentinel -> dash", d3.distance_text == "—");
    check("note == 可回放 (available)", d3.note == "可回放");
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

}  // namespace

int main() {
    t_day_parse_format();
    t_timeline();
    t_jumps_seek();
    t_sync();
    t_related_cameras();
    t_sync_snapshot();
    t_source();
    t_detail();
    t_detail_distinct_types();
    t_config_threshold();

    for (const auto& n : g_notes) std::printf("%s\n", n.c_str());
    const bool ok = g_failed == 0;
    std::printf("== %d passed, %d failed ==\n", g_passed, g_failed);
    std::printf("RESULT: %s passed=%d failed=%d\n", ok ? "PASS" : "FAIL", g_passed, g_failed);
    return ok ? 0 : 1;
}
