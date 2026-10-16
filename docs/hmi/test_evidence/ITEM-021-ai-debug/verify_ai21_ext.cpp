// Verifier-owned ext fixture for ITEM-021 (software_verification).
// Linked against the delivered librailhmi_core.a built by the verifier's own
// board_build.py TestRun round; asserts ITEM-021 acceptance boundaries the
// implementer's core-tests/probe do not cover:
//   * distance_text selection: min valid distance across multiple boxes,
//     formatting (12.34 -> "12.3 m"), negative/unknown -> dash, 0.0 valid,
//     large values, guard distance_m=-1 skipped (the contract is "-1=未知")
//   * distance path when result present but boxes empty -> dash (not stale text);
//     no channel result at all -> 等待结果 + dash (distance loop runs regardless,
//     AiChannelRuntime.last_result_ms default -1 marks the channel wait-state)
//   * ai_debug_visible full role×outcome matrix vs navigation role_can_access
//     (Driver false; Maintainer/Developer/Admin true) and vs resolve_navigation
//     for a direct ai_debug request (driver denied, others allowed)
//   * role matrix identity via visible_pages: driver's visible set excludes
//     AiDebug; others include it (page never silently rendered for the driver)
//   * channel view fields consumed by P5 (fpsText/latencyMs/lastResultText/
//     confidenceText/distanceText/modelVersion/boxes/boxes[].distanceM chain):
//     per-camera distance text independence (each channel from its own boxes)
//   * regression of the ITEM-017 debug-matrix semantics the ITEM-021 contract
//     builds on (worker-dead=AI 不可用, 等待结果, 推理陈旧 at >stale boundary)
// Pure logic, no Qt/RKNN — like the delivered probes.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/camera_registry.h"
#include "railhmi/core/config.h"
#include "railhmi/core/inference.h"
#include "railhmi/core/navigation.h"

using namespace railhmi::core;

namespace {

int g_passed = 0, g_failed = 0;
std::vector<std::string> g_notes;

void check(bool ok, const std::string& name) {
    (ok ? g_passed : g_failed) += 1;
    if (!ok) g_notes.push_back("FAIL: " + name);
}

void check_eq(const std::string& name, const std::string& got, const std::string& want) {
    check(got == want, name + " [" + got + "] want [" + want + "]");
}

// 6-cam registry matching the delivered config baseline (cameras.yaml order).
std::vector<CameraSlot> make_registry() {
    CameraSlot m1; m1.id = "CAM-01"; m1.name = "前方"; m1.ai_enabled = true;  m1.ai_profile = AiProfile::Front;
    CameraSlot m2; m2.id = "CAM-02"; m2.name = "左";   m2.ai_enabled = false; m2.ai_profile = AiProfile::None;
    CameraSlot m3; m3.id = "CAM-03"; m3.name = "右";   m3.ai_enabled = false; m3.ai_profile = AiProfile::None;
    CameraSlot m4; m4.id = "CAM-04"; m4.name = "车顶"; m4.ai_enabled = true;  m4.ai_profile = AiProfile::Pantograph;
    CameraSlot m5; m5.id = "CAM-05"; m5.name = "车底"; m5.ai_enabled = true;  m5.ai_profile = AiProfile::WheelRail;
    CameraSlot m6; m6.id = "CAM-06"; m6.name = "司机室"; m6.ai_enabled = true; m6.ai_profile = AiProfile::CabDoor;
    return {m1, m2, m3, m4, m5, m6};
}

std::vector<AiChannelRuntime> channels_from(const std::vector<CameraSlot>& reg) {
    std::vector<AiChannelRuntime> ch;
    for (const auto& s : reg) {
        AiChannelRuntime c;
        c.camera_id = s.id;
        c.profile = s.ai_profile;
        c.last_result_ms = 0;  // default -1 = 无结果 would mark every channel 等待结果
        ch.push_back(c);
    }
    return ch;
}

const AiDebugChannel* find_ch(const std::vector<AiDebugChannel>& dbg, const std::string& id) {
    for (const auto& d : dbg)
        if (d.camera_id == id) return &d;
    return nullptr;
}

// --- 1. distance selection & formatting -----------------------------------
void t_distance() {
    const auto reg = make_registry();
    AiRuntime rt;
    rt.worker_alive = true;
    rt.channels = channels_from(reg);

    // single box 12.34 -> "12.3 m"
    DetectionResult r1;
    r1.camera_id = "CAM-01";
    r1.profile = AiProfile::Front;
    r1.ts_ms = 900;
    DetectionBox b1;
    b1.confidence = 0.9f; b1.label = "person"; b1.in_roi = true;
    b1.distance_m = 12.34f;
    r1.boxes.push_back(b1);
    r1.max_confidence = 0.9f;

    auto dbg = build_ai_debug(reg, rt, {r1}, 1000);
    const AiDebugChannel* c1 = find_ch(dbg, "CAM-01");
    check(c1 != nullptr && c1->available, "distance: CAM-01 present & available");
    check_eq("distance 12.34 formats to 1 decimal (verbosely round-half)", c1 ? c1->distance_text : "", "12.3 m");

    // multiple boxes: min valid distance wins
    DetectionResult r2 = r1;
    r2.camera_id = "CAM-04";
    r2.profile = AiProfile::Pantograph;
    DetectionBox b2 = b1; b2.distance_m = 3.05f;
    DetectionBox b3 = b1; b3.distance_m = 7.00f;
    r2.boxes.push_back(b2);
    r2.boxes.push_back(b3);
    dbg = build_ai_debug(reg, rt, {r1, r2}, 1000);
    const AiDebugChannel* c4 = find_ch(dbg, "CAM-04");
    check(c4 != nullptr, "distance min: CAM-04 present");
    check_eq("distance min over boxes takes the minimum valid", c4 ? c4->distance_text : "", "3.0 m");

    // unknown distance (-1) in all boxes -> dash, still available
    DetectionResult r3 = r1;
    r3.camera_id = "CAM-05";
    r3.profile = AiProfile::WheelRail;
    r3.boxes[0].distance_m = -1.0f;
    dbg = build_ai_debug(reg, rt, {r1, r3}, 1000);
    const AiDebugChannel* c5 = find_ch(dbg, "CAM-05");
    check(c5 != nullptr && c5->available, "distance unknown: CAM-05 still available");
    check_eq("distance unknown -> dash placeholder", c5 ? c5->distance_text : "", "\xE2\x80\x94");

    // mixed valid/invalid: only valid participates in min
    DetectionResult r4 = r1;
    r4.camera_id = "CAM-06";
    r4.profile = AiProfile::CabDoor;
    DetectionBox b4 = b1; b4.distance_m = -1.0f;
    DetectionBox b5 = b1; b5.distance_m = 4.50f;
    r4.boxes.clear();
    r4.boxes.push_back(b4);
    r4.boxes.push_back(b5);
    dbg = build_ai_debug(reg, rt, {r1, r4}, 1000);
    const AiDebugChannel* c6 = find_ch(dbg, "CAM-06");
    check_eq("distance min skips -1 boxes", c6 ? c6->distance_text : "", "4.5 m");

    // zero is a valid distance (not "unknown") -> "0.0 m"
    DetectionResult r5 = r1;
    r5.camera_id = "CAM-05";
    DetectionBox b6 = b1; b6.distance_m = 0.0f;
    r5.boxes[0] = b6;
    dbg = build_ai_debug(reg, rt, {r1, r5}, 1000);
    const AiDebugChannel* c5b = find_ch(dbg, "CAM-05");
    check_eq("distance 0.0 valid -> 0.0 m", c5b ? c5b->distance_text : "", "0.0 m");

    // per-channel independence: CAM-01 keeps its own text, no cross-leak
    const AiDebugChannel* c1b = find_ch(dbg, "CAM-01");
    check_eq("per-channel distance independence CAM-01", c1b ? c1b->distance_text : "", "12.3 m");

    // formatting: 9.96 -> "10.0 m" (round-half via %.1f), 123.45 -> "123.4 m"
    DetectionResult r6 = r1;
    r6.camera_id = "CAM-04";
    r6.boxes[0].distance_m = 9.96f;
    dbg = build_ai_debug(reg, rt, {r1, r6}, 1000);
    check_eq("distance 9.96 rounds to 10.0 m", find_ch(dbg, "CAM-04") ? find_ch(dbg, "CAM-04")->distance_text : "", "10.0 m");

    DetectionResult r7 = r1;
    r7.camera_id = "CAM-04";
    r7.boxes[0].distance_m = 123.45f;
    dbg = build_ai_debug(reg, rt, {r1, r7}, 1000);
    check_eq("distance 123.45 -> 123.4 m", find_ch(dbg, "CAM-04") ? find_ch(dbg, "CAM-04")->distance_text : "", "123.4 m");

    // result present but boxes empty -> dash (and channel stays available)
    DetectionResult r8 = r1;
    r8.camera_id = "CAM-05";
    r8.boxes.clear();
    r8.boxes.shrink_to_fit();
    dbg = build_ai_debug(reg, rt, {r1, r8}, 1000);
    const AiDebugChannel* c5c = find_ch(dbg, "CAM-05");
    check(c5c && c5c->available && c5c->boxes.empty(), "no-box result still available");
    check_eq("no-box result -> distance dash", c5c ? c5c->distance_text : "", "\xE2\x80\x94");

    // no result for the channel at all: channel unavailable 等待结果; the
    // distance loop still runs unconditionally, so unknown -> dash (never stale)
    for (auto& c : rt.channels) c.last_result_ms = -1;  // simulate no result yet
    dbg = build_ai_debug(reg, rt, {}, 1000);
    const AiDebugChannel* c4b = find_ch(dbg, "CAM-04");
    check(c4b && !c4b->available && c4b->unavailable_reason == "等待结果",
          "no-result channel unavailable 等待结果");
    check_eq("no-result channel distance_text dash (not stale text leak)",
             c4b ? c4b->distance_text : "", "\xE2\x80\x94");
    // age vs worker-down: worker-down reason overrides the wait even with no result
    rt.worker_alive = false;
    dbg = build_ai_debug(reg, rt, {}, 1000);
    const AiDebugChannel* c4c = find_ch(dbg, "CAM-04");
    check(c4c && !c4c->available && c4c->unavailable_reason == "AI 不可用",
          "no-result + worker down -> AI 不可用");
    check_eq("no-result + worker down distance dash", c4c ? c4c->distance_text : "", "\xE2\x80\x94");
}

// --- 2. driver isolation matrix --------------------------------------------
void t_visibility() {
    check(!ai_debug_visible(Role::Driver), "ai_debug_visible(Driver)=false");
    check(ai_debug_visible(Role::Maintainer), "ai_debug_visible(Maintainer)=true");
    check(ai_debug_visible(Role::Developer), "ai_debug_visible(Developer)=true");
    check(ai_debug_visible(Role::Admin), "ai_debug_visible(Admin)=true");

    // identity with the navigation permission source
    for (Role r : {Role::Driver, Role::Maintainer, Role::Developer, Role::Admin}) {
        const bool via_nav = role_can_access(r, Page::AiDebug);
        check_eq("ai_debug_visible == role_can_access for role",
                 std::string(ai_debug_visible(r) ? "true" : "false"),
                 std::string(via_nav ? "true" : "false"));
    }

    // direct navigation to ai_debug: driver denied, others allowed
    const auto nav_d = resolve_navigation(Role::Driver, Page::AiDebug);
    check(!nav_d.allowed, "direct ai_debug request denied for driver (resolve_navigation)");
    check(nav_d.requested == Page::AiDebug, "denied result carries requested page");
    for (Role r : {Role::Maintainer, Role::Developer, Role::Admin}) {
        check(resolve_navigation(r, Page::AiDebug).allowed,
              "direct ai_debug allowed for " + std::to_string(static_cast<int>(r)));
    }

    // driver's visible page set excludes AiDebug (no glue rendering path)
    const auto vis = visible_pages(Role::Driver);
    bool has_ai = false;
    for (Page p : vis)
        if (p == Page::AiDebug) has_ai = true;
    check(!has_ai, "driver visible_pages excludes AiDebug");
    check(vis.size() == 3, "driver visible set is exactly 3 pages");
    // others include AiDebug (6 total)
    for (Role r : {Role::Maintainer, Role::Developer, Role::Admin}) {
        check(visible_pages(r).size() == 6,
              "role " + std::to_string(static_cast<int>(r)) + " sees all 6 pages incl AiDebug");
    }
    // default page for developer is AI debug (design v2 §8)
    check(default_page(Role::Developer) == Page::AiDebug, "developer lands on AI debug");
}

// --- 3. ITEM-017 semantics regression (contract base for P5 fields) --------
void t_regression() {
    const auto reg = make_registry();
    AiRuntime rt;
    rt.worker_alive = false;
    rt.channels = channels_from(reg);
    auto dbg = build_ai_debug(reg, rt, {}, 0);
    bool all_unavail = true;
    for (const auto& d : dbg)
        if (d.available || d.unavailable_reason != "AI 不可用") all_unavail = false;
    bool four = (dbg.size() == 4);
    check(four && all_unavail, "worker-down: 4 eligible channels, all AI 不可用 (not silent)");

    rt.worker_alive = true;
    for (auto& c : rt.channels) c.last_result_ms = -1;  // no result yet
    dbg = build_ai_debug(reg, rt, {}, 500);
    bool wait = true;
    for (const auto& d : dbg)
        if (d.available || d.unavailable_reason != "等待结果") wait = false;
    check(wait, "alive no-result: all 等待结果");

    for (auto& c : rt.channels) c.last_result_ms = 0;
    rt.channels[0].last_result_ms = 0;
    dbg = build_ai_debug(reg, rt, {}, 3001);
    const AiDebugChannel* c1 = find_ch(dbg, "CAM-01");
    check(c1 && !c1->available && c1->unavailable_reason == "推理陈旧",
          "stale > stale_ms -> 推理陈旧 overrides available");
    dbg = build_ai_debug(reg, rt, {}, 3000);
    c1 = find_ch(dbg, "CAM-01");
    check(c1 && c1->available, "age == stale_ms boundary still available");
}

}  // namespace

int main() {
    std::printf("== verify ITEM-021 ext (AI debug page & driver isolation fixtures) ==\n");
    t_distance();
    t_visibility();
    t_regression();
    std::printf("RESULT: %s passed=%d failed=%d\n",
                g_failed == 0 ? "PASS" : "FAIL", g_passed, g_failed);
    for (const auto& n : g_notes) std::printf("  %s\n", n.c_str());
    return g_failed == 0 ? 0 : 1;
}
