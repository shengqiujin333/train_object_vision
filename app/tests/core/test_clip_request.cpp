// railhmi-core 保护片段请求/幂等键/可用性测试（视频 ITEM-012 纯逻辑）。

#include "test_util.h"

#include "railhmi/core/clip_request.h"

namespace {

using namespace railhmi::core;

void test_window_computation() {
    const ClipRequest r = make_clip_request("r1", 5, "CAM-01", 100000, 30, 60);
    railhmi_test::check(r.t0_wall_ms == 70000, "t0 = trigger-30s");
    railhmi_test::check(r.t1_wall_ms == 160000, "t1 = trigger+60s");
    railhmi_test::check(r.duration_ms() == 90000, "duration = pre+post");
    const ClipRequest z = make_clip_request("r0", -1, "CAM-02", 500, 0, 0);
    railhmi_test::check(z.t0_wall_ms == 500 && z.t1_wall_ms == 500, "zero window");
}

void test_same_params() {
    const ClipRequest a = make_clip_request("x", 1, "CAM-01", 1000, 30, 60);
    const ClipRequest b = make_clip_request("x", 1, "CAM-01", 1000, 30, 60);
    const ClipRequest c = make_clip_request("x", 1, "CAM-01", 1000, 30, 61);
    railhmi_test::check(clip_request_same_params(a, b), "same params");
    railhmi_test::check(!clip_request_same_params(a, c), "different post -> not same");
}

void test_registry_idempotency() {
    ClipRequestRegistry reg;
    const ClipRequest a = make_clip_request("evt-1-CAM-01", 1, "CAM-01", 1000, 30, 60);
    railhmi_test::check(reg.submit(a) == ClipRequestOutcome::Added, "first added");
    railhmi_test::check(reg.submit(a) == ClipRequestOutcome::Duplicate, "repeat duplicate");
    const ClipRequest b = make_clip_request("evt-1-CAM-01", 1, "CAM-01", 1000, 31, 60);
    railhmi_test::check(reg.submit(b) == ClipRequestOutcome::Conflict, "different -> conflict");
    railhmi_test::check(reg.size() == 1, "conflict does not overwrite job");
    railhmi_test::check(reg.known("evt-1-CAM-01"), "known");
    railhmi_test::check(!reg.known("nope"), "unknown");
    const ClipRequest* found = reg.find("evt-1-CAM-01");
    railhmi_test::check(found && found->t0_wall_ms == a.t0_wall_ms, "find returns original");
}

void test_availability_roundtrip() {
    for (auto a : {ClipAvailability::Complete, ClipAvailability::Incomplete, ClipAvailability::Unavailable}) {
        ClipAvailability parsed;
        railhmi_test::check(parse_clip_availability(to_string(a), &parsed) && parsed == a, "availability roundtrip");
    }
    ClipAvailability dummy;
    railhmi_test::check(!parse_clip_availability("bogus", &dummy), "availability reject unknown");
}

void test_result_defaults() {
    ClipResult r;
    railhmi_test::check(r.availability == ClipAvailability::Unavailable, "default unavailable");
    railhmi_test::check(!r.published && !r.has_gap && r.size_bytes == 0, "default flags");
}

}  // namespace

void run_clip_request_tests() {
    test_window_computation();
    test_same_params();
    test_registry_idempotency();
    test_availability_roundtrip();
    test_result_defaults();
}
