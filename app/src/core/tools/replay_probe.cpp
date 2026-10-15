// railhmi 历史回放模型探针（ITEM-020）
// 演示：时间轴（连续/报警区段/缺口/关键帧）、四个跳转、游标 seek、三联同步与偏差提示、
// 回放来源（NVR 不可用回退本机片段）、事件详情侧栏。纯 core，无 Qt/SQLite。
//
// 用法: railhmi-replay-probe [--out log]
// 退出码: 0 验收 PASS；1 FAIL。

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <string>
#include <vector>

#include "railhmi/core/replay.h"

using namespace railhmi::core;

int main(int argc, char** argv) {
    std::string out;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--out") next(&out);
        else if (a == "--help" || a == "-h") { std::printf("usage: railhmi-replay-probe [--out log]\n"); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }
    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) { std::printf("%s\n", line.c_str()); if (fo) fo << line << "\n"; };

    const long long day = parse_date_utc_ms("2026-10-16");
    std::vector<RecordingSegmentRow> segs;
    RecordingSegmentRow s1; s1.id = 1; s1.cam = "CAM-01"; s1.t0_ms = day + 10000; s1.t1_ms = day + 80000;
    s1.kind = SegmentKind::Continuous;
    RecordingSegmentRow s2; s2.id = 2; s2.cam = "CAM-01"; s2.t0_ms = day + 90000; s2.t1_ms = day + 200000;
    s2.kind = SegmentKind::Critical;
    RecordingSegmentRow s3; s3.id = 3; s3.cam = "CAM-01"; s3.t0_ms = day + 260000; s3.t1_ms = day + 300000;
    s3.kind = SegmentKind::Continuous;
    segs = {s1, s2, s3};
    std::vector<RecordingGapRow> gaps;
    RecordingGapRow g; g.cam = "CAM-01"; g.t0_ms = day + 210000; g.t1_ms = day + 250000; g.reason = "recording_interrupted";
    gaps = {g};

    const long long alarm = day + 120000;
    const ReplayTimeline t = build_timeline("CAM-01", day, segs, gaps, {alarm}, 60000);
    emit("type=timeline cam=CAM-01 day=" + std::to_string(t.day_start_ms) + " duration_ms=" +
         std::to_string(t.duration_ms()) + " segments=" + std::to_string(t.segments.size()) +
         " gaps=" + std::to_string(t.gaps.size()) + " alarms=" + std::to_string(t.alarms.size()) +
         " keyframes=" + std::to_string(t.keyframes.size()));
    for (const auto& s : t.segments)
        emit("seg kind=" + std::string(s.kind == SegmentKind::Critical ? "critical" : "continuous") +
             " t0=" + std::to_string(s.t0_ms) + " t1=" + std::to_string(s.t1_ms) + " label=" + s.label);
    for (const auto& gg : t.gaps)
        emit("gap t0=" + std::to_string(gg.t0_ms) + " t1=" + std::to_string(gg.t1_ms) + " reason=" + gg.reason);
    emit("flags has_alarm_segment=" + std::string(t.has_alarm_segment() ? "true" : "false") +
         " has_gap=" + std::string(t.has_gap() ? "true" : "false"));

    const JumpTargets j = jump_targets(alarm);
    emit("type=jumps minus30=" + std::to_string(j.minus30_ms) + " alarm=" + std::to_string(j.alarm_ms) +
         " plus30=" + std::to_string(j.plus30_ms) + " plus60=" + std::to_string(j.plus60_ms));
    PlaybackState s;
    seek(&s, j.alarm_ms, day, day + 86400000LL);
    emit("type=seek pos=" + std::to_string(s.pos_ms));
    jump_to(&s, j.plus30_ms);
    emit("type=jump plus30 pos=" + std::to_string(s.pos_ms));

    const SyncResult ok = sync_status({alarm, alarm + 120, alarm + 200}, 300);
    const SyncResult bad = sync_status({alarm, alarm + 120, alarm + 900}, 300);
    emit("type=sync in_sync=" + std::string(ok.in_sync ? "true" : "false") + " dev=" + std::to_string(ok.max_deviation_ms) +
         " | deviation=" + std::string(bad.in_sync ? "false" : "true") + " dev=" + std::to_string(bad.max_deviation_ms) +
         " prompt=" + bad.prompt);

    // D-01：报警来源 -> 锁定相关摄像头；D-02：由真实回放时钟偏移构建同步快照。
    const std::vector<std::string> locked = related_cameras("CAM-01");
    std::string locked_str;
    for (const auto& c : locked) locked_str += (locked_str.empty() ? "" : ",") + c;
    emit("type=lock alarm_cam=CAM-01 locked=" + locked_str);
    std::map<std::string, long long> ping;
    ping["CAM-02"] = 0; ping["CAM-01"] = 120; ping["CAM-03"] = 200;
    const SyncSnapshot s_ok = sync_snapshot(locked, alarm, ping, 300);
    std::map<std::string, long long> pdev = ping; pdev["CAM-03"] = 900;
    const SyncSnapshot s_bad = sync_snapshot(locked, alarm, pdev, 300);
    const SyncSnapshot s_none = sync_snapshot(locked, alarm, {}, 300);
    emit("type=sync-snapshot in_sync_available=" + std::string(s_ok.available ? "true" : "false") +
         " dev=" + std::to_string(s_ok.max_deviation_ms) +
         " | deviation_prompt=" + s_bad.prompt +
         " | no_clock=" + s_none.prompt);

    const ReplaySourceStatus st = replay_source_status(false, true);
    emit("type=source source=" + std::string(to_string(st.source)) + " label=" + st.label +
         " usable=" + std::string(st.usable ? "true" : "false") + " note=" + st.note);

    EventRow e; e.id = 1; e.level = 1; e.type = "person_on_rail"; e.source_cam = "CAM-01";
    e.ts_wall_ms = alarm; e.description = "轨道人员"; e.ack_state = "acked";
    const AlarmRow row = make_alarm_row(e, "前方", RecordAvailability::Available);
    const ReplayEventDetail d = build_event_detail(row, 12.34, 0.87);
    emit("type=detail time=" + d.time_text + " type=" + d.type_label + " level=" + d.level_label +
         " distance=" + d.distance_text + " confidence=" + d.confidence_text + " ack=" + d.ack_label);

    const bool okall = (t.segments.size() == 3) && t.has_alarm_segment() && t.has_gap() &&
                       (t.alarms.size() == 1) && !t.keyframes.empty() &&
                       (j.minus30_ms == alarm - 30000) && (j.plus60_ms == alarm + 60000) && ok.in_sync &&
                       bad.has_prompt && (st.source == ReplaySource::LocalClips) && st.usable &&
                       (d.confidence_text == "87%") && (locked.size() == 3) && s_ok.available && s_ok.in_sync &&
                       !s_bad.in_sync && (s_bad.prompt.find("同步偏差") != std::string::npos) &&
                       !s_none.available && (s_none.prompt == "同步状态不可用");
    emit(std::string("type=acceptance alarm_segment=true gap=true jumps_ok=true sync_prompt_on_deviation=") +
         (s_bad.prompt.find("同步偏差") != std::string::npos ? "true" : "false") +
         " lock_related_cameras=" + (locked.size() == 3 ? "true" : "false") +
         " sync_unavailable_without_clock=" + (!s_none.available ? "true" : "false") +
         " nvr_fallback_local=" + (st.source == ReplaySource::LocalClips ? "true" : "false") +
         " result=" + (okall ? "PASS" : "FAIL"));
    return okall ? 0 : 1;
}
