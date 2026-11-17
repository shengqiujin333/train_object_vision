// railhmi-core AI 报警判定测试（2026-11-17）：检测框 -> 是否报警、级别、多帧确认与解除保持。
// 覆盖现场要求的三路（车前方/受电弓/轮轨）以及不产生 AI 报警的两路（车门/无职责）。

#include "test_util.h"

#include <string>
#include <vector>

#include "railhmi/core/ai_alarm.h"

namespace {

using namespace railhmi::core;

EnvelopeBox box(float conf, bool in_roi, int class_id = 0) {
    EnvelopeBox b;
    b.x = 0.4f;
    b.y = 0.4f;
    b.w = 0.2f;
    b.h = 0.2f;
    b.confidence = conf;
    b.class_id = class_id;
    b.label = class_id == 0 ? "person" : "object";
    b.in_roi = in_roi;
    return b;
}

void test_front_requires_roi() {
    const std::vector<EnvelopeBox> inside = {box(0.9f, true)};
    const AiAlarmCandidate hit = evaluate_ai_alarm(AiProfile::Front, inside, 0.5);
    railhmi_test::check(hit.hit, "front: person inside rail mask alarms");
    railhmi_test::check(hit.type == AlarmType::PersonOnRail && hit.level == AlarmLevel::L1,
                        "front: person on rail is L1 person_on_rail");

    const std::vector<EnvelopeBox> outside = {box(0.9f, false)};
    railhmi_test::check(!evaluate_ai_alarm(AiProfile::Front, outside, 0.5).hit,
                        "front: person outside rail mask does not alarm");

    const std::vector<EnvelopeBox> weak = {box(0.2f, true)};
    railhmi_test::check(!evaluate_ai_alarm(AiProfile::Front, weak, 0.5).hit,
                        "front: below confidence threshold does not alarm");

    const std::vector<EnvelopeBox> obstacle = {box(0.8f, true, 2)};
    const AiAlarmCandidate obs = evaluate_ai_alarm(AiProfile::Front, obstacle, 0.5);
    railhmi_test::check(obs.hit && obs.type == AlarmType::FrontObstacle && obs.level == AlarmLevel::L1,
                        "front: non-person obstacle on rail is L1 front_obstacle");
}

void test_reused_profiles() {
    const std::vector<EnvelopeBox> boxes = {box(0.7f, false, 2)};
    const AiAlarmCandidate pan = evaluate_ai_alarm(AiProfile::Pantograph, boxes, 0.5);
    railhmi_test::check(pan.hit && pan.level == AlarmLevel::L2 &&
                            pan.type == AlarmType::PantographAbnormal,
                        "pantograph: reused detector hit is L2 pantograph_abnormal");

    const AiAlarmCandidate wheel = evaluate_ai_alarm(AiProfile::WheelRail, boxes, 0.5);
    railhmi_test::check(wheel.hit && wheel.level == AlarmLevel::L1 &&
                            wheel.type == AlarmType::WheelDerail,
                        "wheel_rail: reused detector hit is L1 wheel_derail");

    railhmi_test::check(!evaluate_ai_alarm(AiProfile::CabDoor, boxes, 0.5).hit,
                        "cab_door: AI never raises door alarms (BLE door sensor owns it)");
    railhmi_test::check(!evaluate_ai_alarm(AiProfile::None, boxes, 0.5).hit,
                        "none: no duty -> no AI alarm");
    railhmi_test::check(!evaluate_ai_alarm(AiProfile::Front, {}, 0.5).hit,
                        "no boxes -> no alarm");
}

void test_tracker_confirm_and_clear() {
    AiAlarmConfig cfg;
    cfg.confirm_frames = 2;
    cfg.clear_hold_ms = 1000;
    cfg.confidence_min = 0.5;
    AiAlarmTracker tracker(cfg);

    const std::vector<EnvelopeBox> hit = {box(0.9f, true)};
    const std::vector<EnvelopeBox> none;

    AiAlarmState st;
    railhmi_test::check(!tracker.update("CAM-01", AiProfile::Front, hit, 1000, &st),
                        "first hit does not transition (needs 2 frames)");
    railhmi_test::check(!st.active, "not active after 1 frame");
    railhmi_test::check(tracker.update("CAM-01", AiProfile::Front, hit, 1100, &st),
                        "second hit transitions to alarm");
    railhmi_test::check(st.active && st.level == AlarmLevel::L1, "active after confirm frames");

    // 目标丢失：保持期内仍报警，超过 clear_hold_ms 才解除。
    railhmi_test::check(!tracker.update("CAM-01", AiProfile::Front, none, 1500, &st),
                        "missing target inside hold window keeps alarm");
    railhmi_test::check(st.active, "still active inside hold window");
    railhmi_test::check(tracker.update("CAM-01", AiProfile::Front, none, 2100, &st),
                        "missing target past hold window clears alarm");
    railhmi_test::check(!st.active, "inactive after hold window");

    // 清空（AI 停止）：红框必须立刻熄灭。
    tracker.update("CAM-01", AiProfile::Front, hit, 3000, &st);
    tracker.update("CAM-01", AiProfile::Front, hit, 3100, &st);
    railhmi_test::check(tracker.active("CAM-01"), "active before clear");
    tracker.clear();
    railhmi_test::check(!tracker.active("CAM-01") && tracker.active_count() == 0,
                        "clear() drops all lanes (no stale red frame)");
}

void test_tracker_per_camera_isolation() {
    AiAlarmConfig cfg;
    cfg.confirm_frames = 1;
    cfg.clear_hold_ms = 0;
    AiAlarmTracker tracker(cfg);
    AiAlarmState st;
    const std::vector<EnvelopeBox> hit = {box(0.9f, false, 2)};
    tracker.update("CAM-04", AiProfile::Pantograph, hit, 1000, &st);
    railhmi_test::check(tracker.active("CAM-04"), "CAM-04 alarms independently");
    railhmi_test::check(!tracker.active("CAM-05"), "CAM-05 unaffected by CAM-04 alarm");
    railhmi_test::check(tracker.active_count() == 1, "active count is per lane");
    railhmi_test::check(std::string(ai_alarm_type_token(st.type)) == "pantograph_abnormal",
                        "event token matches contract");
}

}  // namespace

void run_ai_alarm_tests() {
    test_front_requires_roi();
    test_reused_profiles();
    test_tracker_confirm_and_clear();
    test_tracker_per_camera_isolation();
}
