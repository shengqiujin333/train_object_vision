// railhmi-core 推理服务模型测试（ITEM-017 / DESKTOP §8.1/§8.2、INTERACTION §5.5/§7.4）。
// 覆盖：有界丢旧帧、帧间隔降频、优先级调度、NPU 预算、温度守卫（降载不停）、
// 两侧广角不入队、AI 不可用语义与报警标注、AI 调试页每路字段。

#include "test_util.h"

#include <string>
#include <vector>

#include "railhmi/core/inference.h"

namespace {

using namespace railhmi::core;

std::vector<CameraSlot> six_slots() {
    struct Def { const char* id; const char* name; CameraPosition pos; bool ai; AiProfile prof; };
    const Def defs[6] = {
        {"CAM-01", "前方", CameraPosition::Primary, true, AiProfile::Front},
        {"CAM-02", "左侧", CameraPosition::Left, false, AiProfile::None},
        {"CAM-03", "右侧", CameraPosition::Right, false, AiProfile::None},
        {"CAM-04", "车顶", CameraPosition::Roof, true, AiProfile::Pantograph},
        {"CAM-05", "车底", CameraPosition::Underfloor, true, AiProfile::WheelRail},
        {"CAM-06", "司机室", CameraPosition::Cab, true, AiProfile::CabDoor},
    };
    std::vector<CameraSlot> v;
    for (int i = 0; i < 6; ++i) {
        CameraSlot s;
        s.id = defs[i].id;
        s.name = defs[i].name;
        s.position = defs[i].pos;
        s.ai_enabled = defs[i].ai;
        s.ai_profile = defs[i].prof;
        s.order = i;
        v.push_back(s);
    }
    return v;
}

void test_bounded_queue_drop_oldest() {
    BoundedFrameQueue q(2);
    railhmi_test::check(!q.push(FrameRef{"CAM-01", 0, 100}), "first push no drop");
    railhmi_test::check(!q.push(FrameRef{"CAM-01", 1, 200}), "second push no drop");
    railhmi_test::check(q.push(FrameRef{"CAM-01", 2, 300}), "third push drops oldest");
    railhmi_test::check(q.size() == 2 && q.dropped() == 1, "queue depth 2, dropped 1");
    FrameRef f;
    q.pop(&f);
    railhmi_test::check(f.seq == 1, "oldest kept is seq 1");
    q.pop(&f);
    railhmi_test::check(f.seq == 2, "newest is seq 2");
    railhmi_test::check(!q.pop(&f), "queue drained");
}

void test_priority_and_gate() {
    railhmi_test::check(ai_priority(AiProfile::Front) < ai_priority(AiProfile::WheelRail), "front > wheel_rail");
    railhmi_test::check(ai_priority(AiProfile::WheelRail) < ai_priority(AiProfile::Pantograph),
                        "wheel_rail > pantograph");
    railhmi_test::check(ai_priority(AiProfile::Pantograph) < ai_priority(AiProfile::CabDoor),
                        "pantograph > cab_door");
    railhmi_test::check(!ai_eligible(AiProfile::None), "none not eligible (两侧广角不入队)");
    railhmi_test::check(frame_due(0, 5) && frame_due(5, 5) && !frame_due(3, 5), "frame interval gating");
}

std::vector<AiChannelRuntime> runtime_channels() {
    std::vector<AiChannelRuntime> v;
    struct P { const char* id; AiProfile prof; };
    const P ps[6] = {{"CAM-01", AiProfile::Front}, {"CAM-02", AiProfile::None}, {"CAM-03", AiProfile::None},
                     {"CAM-04", AiProfile::Pantograph}, {"CAM-05", AiProfile::WheelRail}, {"CAM-06", AiProfile::CabDoor}};
    for (const auto& p : ps) {
        AiChannelRuntime c;
        c.camera_id = p.id;
        c.profile = p.prof;
        v.push_back(c);
    }
    return v;
}

void test_scheduling_budget() {
    AiSchedulerConfig cfg;   // interval 5, depth 2, budget 4, limit 85
    SchedInput in;
    in.channels = runtime_channels();
    in.temp_c = 60;
    SchedDecision d = plan_inference(in, cfg);
    railhmi_test::check(d.run.size() == 4, "budget 4 -> 4 cameras run");
    railhmi_test::check(d.run[0] == "CAM-01" && d.run[1] == "CAM-05" && d.run[2] == "CAM-04" &&
                            d.run[3] == "CAM-06",
                        "priority order front>wheel_rail>pantograph>cab_door");
    railhmi_test::check(d.skipped.empty(), "no skip under budget");
    bool none_excluded = true;
    for (const auto& id : d.run) if (id == "CAM-02" || id == "CAM-03") none_excluded = false;
    railhmi_test::check(none_excluded, "wide-angle cameras not queued");

    cfg.npu_budget = 2;
    SchedDecision d2 = plan_inference(in, cfg);
    railhmi_test::check(d2.run.size() == 2 && d2.run[0] == "CAM-01" && d2.run[1] == "CAM-05",
                        "budget 2 -> top 2 priority");
    railhmi_test::check(d2.skipped.size() == 2, "rest skipped");
}

void test_temperature_guard() {
    AiSchedulerConfig cfg;   // budget 4
    SchedInput in;
    in.channels = runtime_channels();
    in.temp_c = 90.0;        // > 85
    SchedDecision d = plan_inference(in, cfg);
    railhmi_test::check(d.temperature_guard, "over-temp triggers guard");
    railhmi_test::check(!d.run.empty(), "over-temp degrades but does not silently stop");
    railhmi_test::check(d.run.size() == 2 && d.skipped.size() == 2, "budget halved to 2");
}

void test_worker_down_no_schedule() {
    AiSchedulerConfig cfg;
    SchedInput in;
    in.channels = runtime_channels();
    in.worker_alive = false;
    SchedDecision d = plan_inference(in, cfg);
    railhmi_test::check(d.run.empty(), "worker stopped -> nothing scheduled");
}

void test_ai_unavailable_semantics() {
    AiRuntime rt;
    rt.worker_alive = false;
    rt.channels = runtime_channels();
    railhmi_test::check(ai_status_level(rt, 1000) == StatusLevel::Unavailable, "worker down -> AI Unavailable");
    railhmi_test::check(ai_status_text(rt, 1000) == "AI 不可用", "AI text = AI 不可用");
    railhmi_test::check(ai_alarm_annotation(false) == "AI 不可用", "detection alarms annotated AI 不可用");

    rt.worker_alive = true;   // 无结果
    railhmi_test::check(ai_status_level(rt, 1000) == StatusLevel::Warning, "alive but no result -> warning(降级)");
    for (auto& c : rt.channels) if (ai_eligible(c.profile)) c.last_result_ms = 900;
    railhmi_test::check(ai_status_level(rt, 1000) == StatusLevel::Ok, "fresh results -> ok");
    railhmi_test::check(ai_alarm_annotation(true).empty(), "no annotation when AI available");
}

void test_ai_debug_view() {
    const std::vector<CameraSlot> reg = six_slots();
    AiRuntime rt;
    rt.worker_alive = true;
    rt.channels = runtime_channels();
    for (auto& c : rt.channels) {
        if (!ai_eligible(c.profile)) continue;
        c.model_version = "rknn-2.3.2/test";
        c.fps = 5.0;
        c.latency_ms = 42;
        c.last_result_ms = 9500;
    }
    DetectionResult r;
    r.camera_id = "CAM-01";
    r.profile = AiProfile::Front;
    r.model_version = "rknn-2.3.2/yolov5s";
    r.latency_ms = 38;
    r.max_confidence = 0.87f;
    DetectionBox b;
    b.x = 0.4f; b.y = 0.5f; b.w = 0.1f; b.h = 0.2f; b.confidence = 0.87f; b.label = "person"; b.in_roi = true;
    r.boxes.push_back(b);

    const std::vector<AiDebugChannel> dbg = build_ai_debug(reg, rt, {r}, 10000);
    railhmi_test::check(dbg.size() == 4, "AI debug shows only ai_enabled cameras (4)");
    railhmi_test::check(dbg[0].camera_id == "CAM-01" && dbg[0].available, "CAM-01 available");
    railhmi_test::check(dbg[0].boxes.size() == 1 && dbg[0].boxes[0].in_roi, "CAM-01 shows box in ROI");
    railhmi_test::check(dbg[0].max_confidence > 0.8f, "CAM-01 confidence");
    railhmi_test::check(!dbg[0].model_version.empty(), "model version reported");
    railhmi_test::check(dbg[0].latency_ms == 38, "latency from latest result");
    railhmi_test::check(dbg[0].last_result_age_ms == 500, "last result age");
    railhmi_test::check(dbg[0].profile_label == detection_badge_for(AiProfile::Front), "profile label");

    rt.worker_alive = false;
    const std::vector<AiDebugChannel> down = build_ai_debug(reg, rt, {r}, 10000);
    bool all_unavail = true;
    for (const auto& c : down) if (c.available || c.unavailable_reason != "AI 不可用") all_unavail = false;
    railhmi_test::check(all_unavail, "worker stopped -> every AI channel marked unavailable (not silent)");
}

}  // namespace

void run_inference_tests() {
    test_bounded_queue_drop_oldest();
    test_priority_and_gate();
    test_scheduling_budget();
    test_temperature_guard();
    test_worker_down_no_schedule();
    test_ai_unavailable_semantics();
    test_ai_debug_view();
}
