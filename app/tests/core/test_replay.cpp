// railhmi-core 历史回放模型测试（ITEM-020 / INTERACTION §5.3、VISUAL §8.3、PLY-005）。
// 覆盖：时间轴（连续/报警/缺口/关键帧/裁剪）、四个跳转、游标 seek/jump、三联同步与偏差阈值、
// 回放来源（NVR 不可用回退本机片段）、事件详情侧栏。

#include "test_util.h"

#include <string>
#include <vector>

#include "railhmi/core/replay.h"

namespace {

using namespace railhmi::core;

const long long kDayStart = parse_date_utc_ms("2026-10-16");

void test_timeline() {
    std::vector<RecordingSegmentRow> segs;
    RecordingSegmentRow a; a.id = 1; a.cam = "CAM-01"; a.t0_ms = kDayStart + 10000; a.t1_ms = kDayStart + 20000;
    a.kind = SegmentKind::Continuous;
    RecordingSegmentRow b; b.id = 2; b.cam = "CAM-01"; b.t0_ms = kDayStart + 60000; b.t1_ms = kDayStart + 150000;
    b.kind = SegmentKind::Critical;
    RecordingSegmentRow other; other.id = 3; other.cam = "CAM-02"; other.t0_ms = kDayStart; other.t1_ms = kDayStart + 1000;
    other.kind = SegmentKind::Continuous;
    segs = {a, b, other};

    std::vector<RecordingGapRow> gaps;
    RecordingGapRow g; g.cam = "CAM-01"; g.t0_ms = kDayStart + 30000; g.t1_ms = kDayStart + 45000;
    g.reason = "recording_interrupted";
    RecordingGapRow g2; g2.cam = "CAM-02"; g2.t0_ms = kDayStart; g2.t1_ms = kDayStart + 100;
    gaps = {g, g2};

    const ReplayTimeline t = build_timeline("CAM-01", kDayStart, segs, gaps,
                                            {kDayStart + 70000, kDayStart + 130000}, 60000);
    railhmi_test::check(t.segments.size() == 2, "timeline only CAM-01 segments");
    railhmi_test::check(t.segments[0].t0_ms == kDayStart + 10000 && t.segments[0].kind == SegmentKind::Continuous,
                        "continuous segment");
    railhmi_test::check(t.segments[1].kind == SegmentKind::Critical && t.has_alarm_segment(),
                        "critical/alarm segment prominent");
    railhmi_test::check(t.gaps.size() == 1 && t.gaps[0].reason == "recording_interrupted", "gap per camera");
    railhmi_test::check(t.has_gap(), "has_gap");
    railhmi_test::check(t.alarms.size() == 2, "two alarm markers");
    railhmi_test::check(!t.keyframes.empty(), "keyframe markers present");
    railhmi_test::check(t.duration_ms() == 86400000LL, "day window duration");
}

void test_jumps() {
    const long long alarm = kDayStart + 500000;
    const JumpTargets j = jump_targets(alarm);
    railhmi_test::check(j.minus30_ms == alarm - 30000, "jump -30s");
    railhmi_test::check(j.alarm_ms == alarm, "jump alarm moment");
    railhmi_test::check(j.plus30_ms == alarm + 30000, "jump +30s");
    railhmi_test::check(j.plus60_ms == alarm + 60000, "jump +60s");

    PlaybackState s;
    seek(&s, kDayStart + 100, kDayStart, kDayStart + 1000);
    railhmi_test::check(s.pos_ms == kDayStart + 100, "seek within window");
    seek(&s, kDayStart - 5000, kDayStart, kDayStart + 1000);
    railhmi_test::check(s.pos_ms == kDayStart, "seek clamps low");
    seek(&s, kDayStart + 99999, kDayStart, kDayStart + 1000);
    railhmi_test::check(s.pos_ms == kDayStart + 1000, "seek clamps high");
    jump_to(&s, j.plus30_ms);
    railhmi_test::check(s.pos_ms == alarm + 30000, "jump_to sets position");
}

void test_sync() {
    const std::vector<std::string> grp = front_alarm_sync_group();
    railhmi_test::check(grp.size() == 3 && grp[0] == "CAM-02" && grp[1] == "CAM-01" && grp[2] == "CAM-03",
                        "sync group left/front/right");

    const SyncResult ok = sync_status({1000, 1100, 1200}, 300);
    railhmi_test::check(ok.in_sync && !ok.has_prompt && ok.max_deviation_ms == 200, "in sync within threshold");

    const SyncResult bad = sync_status({1000, 1100, 2000}, 300);
    railhmi_test::check(!bad.in_sync && bad.has_prompt && bad.max_deviation_ms == 1000,
                        "deviation over threshold -> prompt");
    railhmi_test::check(bad.prompt.find("同步偏差") != std::string::npos, "prompt text");
}

void test_source() {
    const ReplaySourceStatus nvr = replay_source_status(true, true);
    railhmi_test::check(nvr.source == ReplaySource::Nvr && nvr.usable, "nvr available");
    const ReplaySourceStatus local = replay_source_status(false, true);
    railhmi_test::check(local.source == ReplaySource::LocalClips && local.usable &&
                            local.note.find("NVR 录像不可用") != std::string::npos,
                        "nvr unavailable -> local clips with explicit note");
    const ReplaySourceStatus none = replay_source_status(false, false);
    railhmi_test::check(none.source == ReplaySource::Unavailable && !none.usable, "no source unavailable");
}

void test_event_detail() {
    EventRow e;
    e.id = 1; e.level = 1; e.type = "person_on_rail"; e.source_cam = "CAM-01";
    e.ts_wall_ms = kDayStart + 5000; e.description = "轨道人员"; e.ack_state = "acked";
    const AlarmRow row = make_alarm_row(e, "前方", RecordAvailability::Available);
    const ReplayEventDetail d = build_event_detail(row, 12.34, 0.87);
    railhmi_test::check(d.type_label == std::string("前方轨道人员"), "detail type");
    railhmi_test::check(d.level_label == std::string("一级"), "detail level");
    railhmi_test::check(d.distance_text == std::string("12.3 m"), "detail distance");
    railhmi_test::check(d.confidence_text == std::string("87%"), "detail confidence");
    railhmi_test::check(d.ack_label == std::string("已确认"), "detail ack");
    railhmi_test::check(!d.time_text.empty(), "detail time");
}

}  // namespace

void run_replay_tests() {
    test_timeline();
    test_jumps();
    test_sync();
    test_source();
    test_event_detail();
}
