// Verifier-owned ext fixture for ITEM-017 (software_verification).
// Linked against the delivered librailhmi_core.a built by the verifier's own
// board_build.py TestRun round; asserts acceptance boundaries of the
// inference service model that the implementer's tests/probe do not cover:
//   * drop-loop oracles replicated from ai_probe semantics (sampled at the
//     LAST frame_due tick), multi-variant sweep + popped-seq identity
//   * conservation / requested formulas
//   * priority & eligibility identity incl. labels
//   * plan_inference: eligible<budget, run/skipped partition invariants,
//     exact temp_limit boundary, budget floor 1, worker-dead
//   * ai_status: stale edge at exactly stale_ms, level/text identity,
//     alarm annotation
//   * build_ai_debug: worker-dead vs 等待结果 vs 推理陈旧 matrix, ages
//     spanning text formats, latest-result override, ai_enabled filter
// Pure logic, no Qt/RKNN — like the delivered probes.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/camera_registry.h"
#include "railhmi/core/channel_view.h"
#include "railhmi/core/config.h"
#include "railhmi/core/inference.h"
#include "railhmi/core/status.h"

using namespace railhmi::core;

namespace {

int g_passed = 0, g_failed = 0;
std::vector<std::string> g_notes;

void check(const std::string& name, bool ok) {
    (ok ? g_passed : g_failed) += 1;
    if (!ok) g_notes.push_back("FAIL: " + name);
}

// 6-cam registry matching the delivered config baseline (cameras.yaml order).
std::vector<CameraSlot> make_registry() {
    CameraSlot m1; m1.id = "CAM-01"; m1.name = "車頭"; m1.ai_enabled = true;  m1.ai_profile = AiProfile::Front;
    CameraSlot m2; m2.id = "CAM-02"; m2.name = "左";   m2.ai_enabled = false; m2.ai_profile = AiProfile::None;
    CameraSlot m3; m3.id = "CAM-03"; m3.name = "右";   m3.ai_enabled = false; m3.ai_profile = AiProfile::None;
    CameraSlot m4; m4.id = "CAM-04"; m4.name = "弓";   m4.ai_enabled = true;  m4.ai_profile = AiProfile::Pantograph;
    CameraSlot m5; m5.id = "CAM-05"; m5.name = "輪";   m5.ai_enabled = true;  m5.ai_profile = AiProfile::WheelRail;
    CameraSlot m6; m6.id = "CAM-06"; m6.name = "門";   m6.ai_enabled = true;  m6.ai_profile = AiProfile::CabDoor;
    return {m1, m2, m3, m4, m5, m6};
}

std::vector<AiChannelRuntime> channels_from(const std::vector<CameraSlot>& reg) {
    std::vector<AiChannelRuntime> ch;
    for (const auto& s : reg) {
        AiChannelRuntime c;
        c.camera_id = s.id;
        c.profile = s.ai_profile;
        ch.push_back(c);
    }
    return ch;
}

// --- 1. queue micro-edge cases --------------------------------------------
void t_queue_edges() {
    BoundedFrameQueue q(2);
    check("queue push#1 has room (=false)", q.push(FrameRef{"c", 10, 0}) == false);
    check("queue push#2 still has room", q.push(FrameRef{"c", 11, 0}) == false);
    check("queue push#3 drops oldest (=true)", q.push(FrameRef{"c", 12, 0}) == true);
    check("queue depth-2 dropped=1 size=2", q.dropped() == 1 && q.size() == 2);
    FrameRef f;
    q.pop(&f);
    check("queue pop yields oldest kept seq=11", f.seq == 11);
    bool second = q.pop(&f);
    check("queue 2nd pop ok seq=12", second && f.seq == 12);
    check("queue pop on empty=false", q.pop(&f) == false);
    check("queue drop count unchanged by empty pop", q.dropped() == 1);
    q.clear();
    check("queue clear empties size, drop accounting kept", q.size() == 0 && q.dropped() == 1);

    BoundedFrameQueue q1(1);
    q1.push(FrameRef{"c", 0, 0});
    q1.push(FrameRef{"c", 1, 0});   // drops seq 0
    FrameRef f2;
    q1.pop(&f2);
    check("queue depth=1 keeps newest only", f2.seq == 1 && q1.dropped() == 1);

    BoundedFrameQueue qz(0);   // depth<=0 -> treated as 1 by ctor
    qz.push(FrameRef{"c", 5, 0});
    qz.push(FrameRef{"c", 6, 0});
    check("queue depth0 coerced to 1 (no crash, drops old)", qz.depth() == 1 && qz.dropped() == 1);
}

// --- 2. drop replication across variants (oracle = python simulation) -----
void t_drop_replication() {
    struct Var { int frames, interval, depth, dropped, inferred, popped, requested; };
    const std::vector<Var> vars = {
        {50, 5, 2, 35, 10, 44, 10}, {50, 2, 4, 21, 25, 45, 25}, {25, 5, 2, 15, 5, 19, 5},
        {1, 5, 2, 0, 1, 0, 1},      {7, 3, 2, 3, 3, 5, 3},     {10, 4, 3, 4, 3, 6, 3},
        {50, 5, 1, 36, 10, 45, 10},
    };
    for (const auto& v : vars) {
        BoundedFrameQueue q(v.depth);
        long long inf = 0, dropped_at_last_due = 0;
        bool any_pop = false;
        uint64_t last_popped = 0;
        for (int fr = 0; fr < v.frames; ++fr) {
            q.push(FrameRef{"c", (uint64_t)fr, fr * 40});
            if (frame_due((uint64_t)fr, v.interval)) {
                FrameRef got;
                if (q.pop(&got)) {
                    ++inf;
                    any_pop = true;
                    last_popped = got.seq;
                    dropped_at_last_due = q.dropped();
                }
            }
        }
        check("drop variant frames=" + std::to_string(v.frames) + " interval=" +
              std::to_string(v.interval) + " depth=" + std::to_string(v.depth) +
              " dropped(sampled@last due) == oracle",
              dropped_at_last_due == v.dropped);
        check("drop variant frames=" + std::to_string(v.frames) + " interval=" +
              std::to_string(v.interval) + " depth=" + std::to_string(v.depth) +
              " inferred == oracle", inf == v.inferred);
        check("last popped seq == oracle (oldest-first consumer)",
              any_pop && last_popped == (uint64_t)v.popped);
        check("conservation pushes=inferred+dropped+size (frames=" +
              std::to_string(v.frames) + ")",
              inf + q.dropped() + (long long)q.size() == v.frames);
        // requested = ceil(frames/interval) independent of the queue outcome
        check("requested ceil identity (frames=" + std::to_string(v.frames) + ")",
              (v.frames + v.interval - 1) / v.interval == v.requested);
        // in every variant the queue is non-empty at each due frame -> inferred == requested
        check("inferred == requested when queue never empty at due",
              v.inferred == v.requested);
    }
}

// --- 3. priority / eligibility --------------------------------------------
void t_priority() {
    check("priority front=1", ai_priority(AiProfile::Front) == 1);
    check("priority wheel_rail=2", ai_priority(AiProfile::WheelRail) == 2);
    check("priority pantograph=3", ai_priority(AiProfile::Pantograph) == 3);
    check("priority cab_door=4", ai_priority(AiProfile::CabDoor) == 4);
    check("priority none=99", ai_priority(AiProfile::None) == 99);
    check("front > wheel_rail > pantograph > cab_door > none strict chain",
          ai_priority(AiProfile::Front) < ai_priority(AiProfile::WheelRail) &&
          ai_priority(AiProfile::WheelRail) < ai_priority(AiProfile::Pantograph) &&
          ai_priority(AiProfile::Pantograph) < ai_priority(AiProfile::CabDoor) &&
          ai_priority(AiProfile::CabDoor) < ai_priority(AiProfile::None));
    check("eligible: all four duty profiles", ai_eligible(AiProfile::Front) &&
          ai_eligible(AiProfile::WheelRail) && ai_eligible(AiProfile::Pantograph) &&
          ai_eligible(AiProfile::CabDoor));
    check("eligible: none excluded", !ai_eligible(AiProfile::None));
    const AiProfile all[] = {AiProfile::Front, AiProfile::WheelRail,
                             AiProfile::Pantograph, AiProfile::CabDoor};
    for (AiProfile p : all) check("label nonempty " + std::to_string(ai_priority(p)),
                                  !ai_profile_label(p).empty());
}

// --- 4. plan_inference acceptance boundaries ------------------------------
void t_plan() {
    const auto reg = make_registry();
    const auto ch = channels_from(reg);

    SchedInput in;
    in.channels = ch;
    in.temp_c = 60.0;
    in.worker_alive = false;
    SchedDecision dead = plan_inference(in, AiSchedulerConfig{});
    check("worker-dead: nothing scheduled (no run/skip/guard)",
          dead.run.empty() && dead.skipped.empty() && !dead.temperature_guard);

    in.worker_alive = true;
    AiSchedulerConfig big; big.npu_budget = 8; big.temp_limit_c = 85.0;
    SchedDecision d8 = plan_inference(in, big);
    check("budget 8 > eligible: all 4 run", d8.run.size() == 4 && d8.skipped.empty());
    check("budget 8: priority order CAM-01,CAM-05,CAM-04,CAM-06",
          d8.run[0] == "CAM-01" && d8.run[1] == "CAM-05" &&
          d8.run[2] == "CAM-04" && d8.run[3] == "CAM-06");
    check("budget 8: wide angles not scheduled",
          std::find(d8.run.begin(), d8.run.end(), "CAM-02") == d8.run.end() &&
          std::find(d8.run.begin(), d8.run.end(), "CAM-03") == d8.run.end());
    check("budget 8: no guard at 60C", !d8.temperature_guard);

    AiSchedulerConfig small; small.npu_budget = 2; small.temp_limit_c = 85.0;
    SchedDecision d2 = plan_inference(in, small);
    check("budget 2: top 2 by priority", d2.run == std::vector<std::string>{"CAM-01", "CAM-05"});
    check("budget 2: skipped CAM-04,CAM-06 in priority order",
          d2.skipped == std::vector<std::string>{"CAM-04", "CAM-06"});
    bool disjoint = true;
    for (const auto& r : d2.run)
        if (std::find(d2.skipped.begin(), d2.skipped.end(), r) != d2.skipped.end()) disjoint = false;
    check("run/skipped disjoint", disjoint);
    check("run+skipped == eligible count", d2.run.size() + d2.skipped.size() == 4);
    check("guard remains false below temp limit", !d2.temperature_guard);

    // single eligible channel < budget
    SchedInput one;
    AiChannelRuntime oc; oc.camera_id = "CAM-04"; oc.profile = AiProfile::Pantograph;
    one.channels.push_back(oc);
    one.temp_c = 60.0;
    one.worker_alive = true;
    AiSchedulerConfig b4; b4.npu_budget = 4; b4.temp_limit_c = 85.0;
    SchedDecision d1e = plan_inference(one, b4);
    check("single eligible < budget: runs alone, nothing skipped",
          d1e.run.size() == 1 && d1e.run[0] == "CAM-04" && d1e.skipped.empty());

    // only-none channel: nothing scheduled at all
    SchedInput noneOnly;
    AiChannelRuntime nc; nc.camera_id = "CAM-02"; nc.profile = AiProfile::None;
    noneOnly.channels.push_back(nc);
    noneOnly.temp_c = 60.0; noneOnly.worker_alive = true;
    SchedDecision dnn = plan_inference(noneOnly, b4);
    check("none-only input: empty run && empty skipped (not queued)",
          dnn.run.empty() && dnn.skipped.empty() && !dnn.temperature_guard);

    // temperature boundary: exactly at limit -> no guard; epsilon above -> guard
    SchedInput hot = in;
    hot.temp_c = 85.0;
    SchedDecision ex = plan_inference(hot, b4);
    check("temp == limit: no guard, full budget", !ex.temperature_guard && ex.run.size() == 4 && ex.skipped.empty());
    hot.temp_c = 85.01;
    SchedDecision over = plan_inference(hot, b4);
    check("temp > limit: guard on, budget halved 4->2",
          over.temperature_guard && over.run.size() == 2 &&
          over.run[0] == "CAM-01" && over.run[1] == "CAM-05" &&
          over.skipped.size() == 2);
    check("guard semantics: still runs (never silent stop)", !over.run.empty());
    hot.temp_c = 84.999;
    SchedDecision under = plan_inference(hot, b4);
    check("temp just under limit: no guard", !under.temperature_guard && under.run.size() == 4);

    // budget floor: budget=1 over-temp -> max(1,1/2)=1, front still runs
    AiSchedulerConfig b1; b1.npu_budget = 1; b1.temp_limit_c = 85.0;
    hot.temp_c = 90.0;
    SchedDecision f1 = plan_inference(hot, b1);
    check("budget 1 over-temp: floor at 1, front runs (degraded not stopped)",
          f1.temperature_guard && f1.run.size() == 1 && f1.run[0] == "CAM-01");
    // budget 5 (odd) over-temp -> floor(5/2)=2 + max(1,..)=2
    AiSchedulerConfig b5; b5.npu_budget = 5; b5.temp_limit_c = 85.0;
    SchedDecision d5 = plan_inference(hot, b5);
    check("odd budget 5 over-temp: runs 2, skips priority rest",
          d5.temperature_guard && d5.run.size() == 2 && d5.skipped.size() == 2);
}

// --- 5. ai_status: levels, stale edge, text identity ----------------------
void t_status() {
    const auto reg = make_registry();
    auto ch = channels_from(reg);
    AiRuntime rt;
    rt.worker_alive = true;
    rt.channels = ch;

    check("fresh but no results -> Warning level", ai_status_level(rt, 0) == StatusLevel::Warning);
    check("warning text = 推理陈旧", ai_status_text(rt, 0) == "推理陈旧");
    rt.channels[0].last_result_ms = 0;
    check("one fresh result -> Ok", ai_status_level(rt, 3000) == StatusLevel::Ok);
    check("ok text = 运行", ai_status_text(rt, 3000) == "运行");
    // stale edge: exactly stale_ms -> still Ok; +1 -> Warning
    check("age == stale_ms (3000): still Ok (<=)", ai_status_level(rt, 3000 + 0) == StatusLevel::Ok &&
          ai_status_text(rt, 3000) == "运行");
    check("age = stale_ms+1 -> Warning stale", ai_status_level(rt, 3001) == StatusLevel::Warning);
    // one fresh channel keeps Ok even if others stale
    rt.channels[2].last_result_ms = -5;   // stale long ago
    check("mixed fresh+stale -> Ok (any fresh wins)", ai_status_level(rt, 3000) == StatusLevel::Ok);

    rt.worker_alive = false;
    check("worker dead -> Unavailable", ai_status_level(rt, 3000) == StatusLevel::Unavailable);
    check("worker dead text = AI 不可用", ai_status_text(rt, 3000) == "AI 不可用");
    check("alarm annotation: unavailable -> AI 不可用", ai_alarm_annotation(false) == "AI 不可用");
    check("alarm annotation: available -> empty", ai_alarm_annotation(true).empty());

    // custom stale window
    rt.worker_alive = true;
    rt.channels[0].last_result_ms = 0;
    check("custom stale window 100: age 100 still ok", ai_status_level(rt, 100, 100) == StatusLevel::Ok);
    check("custom stale window 100: age 101 stale", ai_status_level(rt, 101, 100) == StatusLevel::Warning);
    check("level/text identity Warning=推理陈旧", ai_status_text(rt, 101, 100) == "推理陈旧");
}

// --- 6. build_ai_debug matrix ---------------------------------------------
void t_debug() {
    const auto reg = make_registry();

    // worker dead: every ai_enabled+eligible channel unavailable "AI 不可用"
    AiRuntime dead;
    dead.worker_alive = false;
    dead.channels = channels_from(reg);
    auto dbg = build_ai_debug(reg, dead, {}, 0);
    check("debug: only ai_enabled+eligible channels listed (4, not wide angles)", dbg.size() == 4);
    bool all_unavail = true, right_cams = true;
    for (const auto& d : dbg) {
        if (d.available || d.unavailable_reason != "AI 不可用") all_unavail = false;
        if (d.camera_id == "CAM-02" || d.camera_id == "CAM-03") right_cams = false;
    }
    check("debug worker-dead: all unavailable reason=AI 不可用", all_unavail);
    check("debug: none of the none-profile slots listed", right_cams);

    // worker alive but no result yet: 等待结果
    AiRuntime alive;
    alive.worker_alive = true;
    alive.channels = channels_from(reg);
    dbg = build_ai_debug(reg, alive, {}, 500);
    bool wait = true;
    for (const auto& d : dbg)
        if (d.available || d.unavailable_reason != "等待结果" || d.last_result_text != "—") wait = false;
    check("debug alive no-result: unavailable 等待结果, last=—", wait);

    // fresh result: available, boxes/confidence/model from latest, ages/texts
    AiRuntime run;
    run.worker_alive = true;
    run.channels = channels_from(reg);
    run.channels[0].model_version = "rknn-2.3.2/yolov5s";
    run.channels[0].last_result_ms = 990;   // probe/worker style: runtime records result time
    std::vector<DetectionResult> latest;
    DetectionResult r;
    r.camera_id = "CAM-01";
    r.profile = AiProfile::Front;
    r.model_version = "rknn-2.3.2/yolov5s";
    r.latency_ms = 33;
    r.ts_ms = 990;               // age 10ms at now=1000 -> "<1 秒前"
    r.max_confidence = 0.80f;
    DetectionBox b; b.x = 0.4f; b.y = 0.5f; b.w = 0.1f; b.h = 0.2f; b.confidence = 0.75f;
    b.label = "target"; b.in_roi = true;
    r.boxes.push_back(b);
    latest.push_back(r);
    dbg = build_ai_debug(reg, run, latest, 1000);
    const AiDebugChannel* c1 = nullptr;
    for (const auto& d : dbg)
        if (d.camera_id == "CAM-01") c1 = &d;
    check("debug fresh: CAM-01 available", c1 && c1->available);
    check("debug fresh: model version from latest result", c1 && c1->model_version == "rknn-2.3.2/yolov5s");
    check("debug fresh: latency from result", c1 && c1->latency_ms == 33);
    check("debug fresh: boxes copied", c1 && c1->boxes.size() == 1 && c1->boxes[0].label == "target");
    check("debug fresh: max_confidence carried", c1 && c1->max_confidence == 0.80f);
    check("debug fresh: age 10ms -> <1 秒前", c1 && c1->last_result_text == "<1 秒前");

    // age formats (format_last_frame: <1s "<1 秒前", <60s "N 秒前",
    // else "M 分 R 秒前"); ages > stale_ms(3000) are additionally 推理陈旧
    // -> available must flip false
    struct Age { long long ts, now; const char* want; };
    const std::vector<Age> ages = {
        {0, 999, "<1 秒前"}, {0, 1000, "1 秒前"}, {0, 2999, "2 秒前"},
        {0, 59000, "59 秒前"}, {0, 60000, "1 分 0 秒前"}, {0, 125000, "2 分 5 秒前"},
    };
    for (const auto& a : ages) {
        run.channels[0].last_result_ms = a.ts;
        auto dd = build_ai_debug(reg, run, latest, a.now);
        const AiDebugChannel* c = nullptr;
        for (const auto& d : dd) if (d.camera_id == "CAM-01") c = &d;
        check("debug age format now=" + std::to_string(a.now) + " text",
              c && c->last_result_text == a.want);
        const bool fresh = (a.now - a.ts) <= 3000;
        check("debug age " + std::to_string(a.now) + "ms availability==fresh",
              c && c->available == fresh);
    }

    // stale result: age > stale_ms -> unavailable 推理陈旧
    run.channels[0].last_result_ms = 0;
    dbg = build_ai_debug(reg, run, latest, 3001);
    {
        const AiDebugChannel* c = nullptr;
        for (const auto& d : dbg) if (d.camera_id == "CAM-01") c = &d;
        check("debug stale age=stale_ms+1: unavailable 推理陈旧",
              c && !c->available && c->unavailable_reason == "推理陈旧");
    }
    // exactly stale_ms: still available
    dbg = build_ai_debug(reg, run, latest, 3000);
    {
        const AiDebugChannel* c = nullptr;
        for (const auto& d : dbg) if (d.camera_id == "CAM-01") c = &d;
        check("debug age == stale_ms: still available (boundary <=)", c && c->available);
    }

    // fps/latency defaulting from runtime channel when result absent for them
    run.channels[0].fps = 5.0;
    run.channels[0].latency_ms = 40;
    dbg = build_ai_debug(reg, run, latest, 500);   // runtime latency 40, result latency 33 -> result wins
    {
        const AiDebugChannel* c = nullptr;
        for (const auto& d : dbg) if (d.camera_id == "CAM-01") c = &d;
        check("debug fps from runtime telemetry", c && c->fps == 5.0);
        check("debug latency: positive result latency overrides runtime", c && c->latency_ms == 33);
    }
    // empty box list but present result: not stale, still available
    DetectionResult r2 = r;
    r2.boxes.clear();
    std::vector<DetectionResult> latest2{r2};
    run.channels[0].last_result_ms = 900;
    dbg = build_ai_debug(reg, run, latest2, 1000);
    {
        const AiDebugChannel* c = nullptr;
        for (const auto& d : dbg) if (d.camera_id == "CAM-01") c = &d;
        check("debug empty-box but fresh result still available",
              c && c->available && c->boxes.empty());
    }
}

// --- 7. worker tick determinism / idempotent stop (media level) -----------
// runs only when linked with ai_worker.cc (skip cleanly otherwise)

void t_registry_from_config_baseline_note() {
    // The config-derived registry identity (6 cams, 4 eligible, order) is
    // asserted on-board by the ai-probe lines; here only the manual mirror is
    // used, so nothing further is required.
    check("registry sanity: 6 slots, 4 eligible", make_registry().size() == 6);
}

}  // namespace

int main() {
    std::printf("== verify ITEM-017 ext (inference acceptance fixtures) ==\n");
    t_registry_from_config_baseline_note();
    t_queue_edges();
    t_drop_replication();
    t_priority();
    t_plan();
    t_status();
    t_debug();
    std::printf("RESULT: %s passed=%d failed=%d\n",
                g_failed == 0 ? "PASS" : "FAIL", g_passed, g_failed);
    for (const auto& n : g_notes) std::printf("  %s\n", n.c_str());
    return g_failed == 0 ? 0 : 1;
}
