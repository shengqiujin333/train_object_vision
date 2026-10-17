// railhmi-core 录像 provider 状态机与覆盖分类测试（视频 ITEM-013 纯逻辑）。

#include "test_util.h"

#include "railhmi/core/recording_provider.h"

namespace {

using namespace railhmi::core;

ProviderRequest mk(const std::string& cam, long long t0, long long t1) {
    ProviderRequest r;
    r.request_id = "req";
    r.camera_id = cam;
    r.t0_ms = t0;
    r.t1_ms = t1;
    return r;
}

SegmentRef ref(const std::string& cam, long long t0, long long t1, bool usable) {
    SegmentRef s;
    s.camera_id = cam;
    s.t0_ms = t0;
    s.t1_ms = t1;
    s.path = "/tmp/x.ts";
    s.usable = usable;
    return s;
}

void test_status_and_reason() {
    for (auto st : {ProviderStatus::Ok, ProviderStatus::Partial, ProviderStatus::Unavailable, ProviderStatus::Error}) {
        ProviderStatus parsed;
        railhmi_test::check(parse_provider_status(to_string(st), &parsed) && parsed == st, "status roundtrip");
    }
    ProviderStatus dummy;
    railhmi_test::check(!parse_provider_status("bogus", &dummy), "status reject unknown");
    railhmi_test::check(std::string(provider_reason::kUnconfigured) == "UNCONFIGURED", "reason token");
}

void test_deadline() {
    ProviderRequest r = mk("CAM-01", 0, 1000);
    r.deadline_mono_us = 5000;
    railhmi_test::check(!provider_deadline_expired(r, 4000), "before deadline");
    railhmi_test::check(provider_deadline_expired(r, 6000), "after deadline");
    r.deadline_mono_us = 0;
    railhmi_test::check(!provider_deadline_expired(r, 99999999), "no deadline never expires");
}

void test_coverage_union() {
    std::vector<SegmentRef> segs = {ref("CAM-01", 0, 2000, true), ref("CAM-01", 1500, 3000, true)};
    railhmi_test::check(covered_ms_in_window(0, 3000, segs) == 3000, "overlap union");
    std::vector<SegmentRef> gapped = {ref("CAM-01", 0, 1000, true), ref("CAM-01", 2000, 3000, true)};
    railhmi_test::check(covered_ms_in_window(0, 3000, gapped) == 2000, "gap between segments");
    railhmi_test::check(covered_ms_in_window(0, 3000, {ref("CAM-01", 0, 3000, false)}) == 0, "unusable not counted");
    // covered_ms_in_window 不看相机（相机过滤在 classify_segments）：仅可用性参与。
    railhmi_test::check(covered_ms_in_window(0, 3000, {ref("CAM-02", 0, 3000, true)}) == 3000,
                        "coverage ignores camera (filtered by classify)");
}

void test_classify_unconfigured_and_empty() {
    const auto unc = classify_segments(mk("CAM-01", 0, 1000), {}, {}, false);
    railhmi_test::check(unc.status == ProviderStatus::Unavailable &&
                            unc.reason == provider_reason::kUnconfigured,
                        "unconfigured");
    const auto empty = classify_segments(mk("CAM-01", 0, 1000), {}, {}, true);
    railhmi_test::check(empty.status == ProviderStatus::Ok && empty.segments.empty() &&
                            !empty.coverage_complete,
                        "ok empty not playable");
}

void test_classify_complete_partial_unusable() {
    const auto full = classify_segments(mk("CAM-01", 1000, 5000),
                                        {ref("CAM-01", 900, 5200, true)}, {}, true);
    railhmi_test::check(full.status == ProviderStatus::Ok && full.coverage_complete &&
                            full.covered_ms == 4000,
                        "complete");

    const auto part = classify_segments(mk("CAM-01", 1000, 5000),
                                        {ref("CAM-01", 1000, 3000, true)}, {}, true);
    railhmi_test::check(part.status == ProviderStatus::Partial && part.covered_ms == 2000, "partial");

    const auto bad = classify_segments(mk("CAM-01", 1000, 5000),
                                       {ref("CAM-01", 900, 5200, false), ref("CAM-01", 1500, 2500, false)}, {}, true);
    railhmi_test::check(bad.status == ProviderStatus::Unavailable && bad.reason == provider_reason::kNotFound,
                        "all unusable -> not_found");
}

void test_classify_filters_camera_and_gaps() {
    std::vector<SegmentRef> segs = {ref("CAM-01", 1000, 5000, true), ref("CAM-02", 1000, 5000, true)};
    std::vector<RecordingGapRow> gaps;
    RecordingGapRow g1; g1.cam = "CAM-01"; g1.t0_ms = 4000; g1.t1_ms = 6000; g1.reason = "X";
    RecordingGapRow g2; g2.cam = "CAM-02"; g2.t0_ms = 4000; g2.t1_ms = 6000; g2.reason = "Y";
    RecordingGapRow g3; g3.cam = "CAM-01"; g3.t0_ms = 0; g3.t1_ms = 500; g3.reason = "OLD";
    gaps = {g1, g2, g3};
    const auto r = classify_segments(mk("CAM-01", 1000, 5000), segs, gaps, true);
    railhmi_test::check(r.segments.size() == 1 && r.segments[0].camera_id == "CAM-01", "camera filter");
    railhmi_test::check(r.gaps.size() == 1 && r.gaps[0].reason == "X", "gap overlap filter");
}

}  // namespace

void run_recording_provider_tests() {
    test_status_and_reason();
    test_deadline();
    test_coverage_union();
    test_classify_unconfigured_and_empty();
    test_classify_complete_partial_unusable();
    test_classify_filters_camera_and_gaps();
}
