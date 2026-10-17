#include "railhmi/core/recording_provider.h"

#include <cstdio>
#include <vector>

using namespace railhmi;
using core::SegmentRef;
using core::ProviderRequest;
using core::RecordingGapRow;
using core::SegmentQueryResult;

static int fails = 0;
static void check(bool ok, const char* name) {
    std::printf("vext %s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++fails;
}

static core::ProviderStatus st(const SegmentQueryResult& r) { return r.status; }

static SegmentRef mkref(const char* cam, long long t0, long long t1, bool usable) {
    SegmentRef s;
    s.camera_id = cam;
    s.t0_ms = t0;
    s.t1_ms = t1;
    s.path = "/tmp/x.ts";
    s.usable = usable;
    return s;
}
static ProviderRequest mkreq(const char* cam, long long t0, long long t1) {
    ProviderRequest r;
    r.request_id = "vr";
    r.camera_id = cam;
    r.t0_ms = t0;
    r.t1_ms = t1;
    return r;
}

int main() {
    using namespace railhmi::core;
    // --- covered_ms_in_window 数学 ---
    {   // 相邻片段 [0,1000)+[1000,2000) 不重叠但紧邻 -> 并集 2000
        std::vector<SegmentRef> v = {mkref("A", 0, 1000, true), mkref("A", 1000, 2000, true)};
        check(covered_ms_in_window(0, 2000, v) == 2000, "v01_adjacent_union_2000");
    }
    {   // 触碰 (第三段 [2000,3000) 与窗口前两段不重叠) 端闭结合 scan
        std::vector<SegmentRef> v = {mkref("A", 0, 1000, true), mkref("A", 2000, 3000, true)};
        check(covered_ms_in_window(0, 3000, v) == 2000, "v02_touching_union_gap_1000");
    }
    {   // 三段嵌套重叠取最大右端
        std::vector<SegmentRef> v = {mkref("A", 0, 1000, true), mkref("A", 500, 1500, true),
                                     mkref("A", 900, 2500, true)};
        check(covered_ms_in_window(0, 3000, v) == 2500, "v03_chain_union_2500");
    }
    {   // 部分越窗片段被裁剪到窗口
        std::vector<SegmentRef> v = {mkref("A", -500, 500, true)};
        check(covered_ms_in_window(0, 3000, v) == 500, "v04_clipped_to_window_500");
    }
    {   // 完全越窗片段不计
        std::vector<SegmentRef> v = {mkref("A", 4000, 5000, true)};
        check(covered_ms_in_window(0, 3000, v) == 0, "v05_outside_not_counted");
    }
    {   // 零/负窗口 -> 0
        std::vector<SegmentRef> v = {mkref("A", 0, 1000, true)};
        check(covered_ms_in_window(5, 5, v) == 0, "v06_empty_window_zero");
        check(covered_ms_in_window(10, 5, v) == 0, "v07_negative_window_zero");
    }
    {   // unusable 片段即使 interval 重叠也不计
        std::vector<SegmentRef> v = {mkref("A", 0, 2000, false), mkref("A", 500, 2500, true)};
        check(covered_ms_in_window(0, 3000, v) == 2000, "v08_unusable_excluded");
    }
    {   // 完整不可用 -> 0
        std::vector<SegmentRef> v = {mkref("A", 0, 1000, false), mkref("A", 1000, 2000, false)};
        check(covered_ms_in_window(0, 2000, v) == 0, "v09_all_unusable_zero");
    }
    {   // 乱序输入仍正确 (需要
        std::vector<SegmentRef> v = {mkref("A", 2000, 3000, true), mkref("A", 0, 1000, true)};
        check(covered_ms_in_window(0, 3000, v) == 2000, "v10_unsorted_input_ok");
    }
    {   // 空输入
        std::vector<SegmentRef> v = {};
        check(covered_ms_in_window(0, 1000, v) == 0, "v11_empty_input_zero");
    }

    // --- classify_segments 三态 ---
    {   // 未配置优先于一切（即使有可用片段）→ unavailable/UNCONFIGURED，segments 仍列出
        SegmentQueryResult r = classify_segments(mkreq("CAM-01", 0, 1000),
                                                 {mkref("CAM-01", 0, 1000, true)}, {}, false);
        check(st(r) == ProviderStatus::Unavailable && r.reason == provider_reason::kUnconfigured,
              "v12_unconfigured_priority");
        check(r.segments.empty(), "v13_unconfigured_no_segments_listed");
    }
    {   // 不可用覆盖 >= 窗口仍不可播 -> ok/coverage_complete
        SegmentQueryResult r = classify_segments(mkreq("CAM-01", 1000, 5000),
                                                 {mkref("CAM-01", 0, 6000, true)}, {}, true);
        check(st(r) == ProviderStatus::Ok && r.coverage_complete && r.covered_ms == 4000,
              "v14_coverage_over_window_ok");
    }
    {   // 恰好等于窗口（[t0,t1)==窗长）-> coverage_complete（>= 窗口即完整覆盖）
        SegmentQueryResult r = classify_segments(mkreq("CAM-01", 0, 1000),
                                                 {mkref("CAM-01", 0, 1000, true)}, {}, true);
        check(st(r) == ProviderStatus::Ok && r.coverage_complete, "v15_exact_window_complete");
    }
    {   // 窗长-1 -> partial（紧邻但差 1ms）
        SegmentQueryResult r = classify_segments(mkreq("CAM-01", 0, 1000),
                                                 {mkref("CAM-01", 0, 999, true)}, {}, true);
        check(st(r) == ProviderStatus::Partial && covered_ms_in_window(0, 1000, {mkref("A", 0, 999, true)}) == 999,
              "v16_one_short_partial");
    }
    {   // 混合：部分可用部分不可用可用片段负责覆盖
        std::vector<SegmentRef> segs = {mkref("CAM-01", 0, 500, false), mkref("CAM-01", 500, 1500, true)};
        SegmentQueryResult r = classify_segments(mkreq("CAM-01", 0, 1500), segs, {}, true);
        check(st(r) == ProviderStatus::Partial && r.covered_ms == 1000, "v17_mixed_usable_partial");
        // unusable 片段仍在 segments 列表（诊断）
        check(r.segments.size() == 2, "v18_unusable_listed_for_diagnosis");
    }
    {   // 有重叠但全部不可用 -> unavailable/NOT_FOUND
        std::vector<SegmentRef> segs = {mkref("CAM-01", 0, 1500, false)};
        SegmentQueryResult r = classify_segments(mkreq("CAM-01", 0, 1500), segs, {}, true);
        check(st(r) == ProviderStatus::Unavailable && r.reason == provider_reason::kNotFound,
              "v19_all_unavailable_not_found");
    }
    {   // 半开窗边界 [t0,t1)：t0 恰等端点片段算重叠
        SegmentQueryResult r = classify_segments(mkreq("CAM-01", 1000, 2000),
                                                 {mkref("CAM-01", 1000, 1500, true)}, {}, true);
        check(st(r) == ProviderStatus::Partial, "v20_half_open_left_overlap");
        r = classify_segments(mkreq("CAM-01", 1000, 2000),
                              {mkref("CAM-01", 2000, 2500, true)}, {}, true);
        check(st(r) == ProviderStatus::Ok && r.segments.empty(),
              "v21_half_open_right_empty_ok");
    }
    {   // 缺口过滤器：不同相机缺口不透出
        RecordingGapRow g1; g1.cam = "CAM-01"; g1.t0_ms = 500; g1.t1_ms = 1500; g1.reason = "R1";
        RecordingGapRow g2; g2.cam = "CAM-02"; g2.t0_ms = 500; g2.t1_ms = 1500; g2.reason = "R2";
        RecordingGapRow g3; g3.cam = "CAM-01"; g3.t0_ms = 2000; g3.t1_ms = 2500; g3.reason = "R3";
        SegmentQueryResult r = classify_segments(mkreq("CAM-01", 0, 2000),
                                                 {}, {g1, g2, g3}, true);
        check(st(r) == ProviderStatus::Ok && r.gaps.size() == 1 && r.gaps[0].reason == "R1",
              "v22_gap_camera_window_filter");
        check(r.reason.empty(), "v23_ok_no_reason_token");
    }
    {   // 未配置时查询层不吐缺口（unavailable 即不展开候选）
        RecordingGapRow g1; g1.cam = "CAM-01"; g1.t0_ms = 0; g1.t1_ms = 1000; g1.reason = "G";
        SegmentQueryResult r = classify_segments(mkreq("CAM-01", 0, 1000),
                                                 {}, {g1}, false);
        check(st(r) == ProviderStatus::Unavailable && r.reason == provider_reason::kUnconfigured &&
              r.gaps.empty(), "v42_unconfigured_no_gaps");
    }
    // --- deadline ---
    {   // 恰好等于 deadline: not expired（>只取 strict greater）
        ProviderRequest d = mkreq("CAM-01", 0, 1000);
        d.deadline_mono_us = 100;
        check(!provider_deadline_expired(d, 100), "v24_deadline_inclusive_boundary");
    }
    {   // deadline<=0 永不超时
        ProviderRequest d = mkreq("CAM-01", 0, 1000);
        d.deadline_mono_us = 0;
        check(!provider_deadline_expired(d, 999999999999LL), "v25_no_deadline");
        d.deadline_mono_us = -5;
        check(!provider_deadline_expired(d, 999999999999LL), "v26_negative_deadline_no_expires");
    }

    // --- reason token 稳定字符串口径 ---
    check(std::string(provider_reason::kUnconfigured) == "UNCONFIGURED", "v27_reason_unconfigured");
    check(std::string(provider_reason::kNotFound) == "NOT_FOUND", "v28_reason_not_found");
    check(std::string(provider_reason::kTimeout) == "TIMEOUT", "v29_reason_timeout");
    check(std::string(provider_reason::kStorageError) == "STORAGE_ERROR", "v30_reason_storage_error");
    check(std::string(provider_reason::kUnverified) == "UNVERIFIED", "v31_reason_unverified");
    check(std::string(provider_reason::kAuthFailed) == "AUTH_FAILED", "v32_reason_auth_failed");
    check(std::string(provider_reason::kUnsupported) == "UNSUPPORTED", "v33_reason_unsupported");
    check(std::string(provider_reason::kNone) == "", "v34_reason_none_empty");

    // --- parse/to_string roundtrip + reject ---
    ProviderStatus p;
    check(parse_provider_status("ok", &p) && p == ProviderStatus::Ok, "v35_parse_ok");
    check(parse_provider_status("partial", &p) && p == ProviderStatus::Partial, "v36_parse_partial");
    check(parse_provider_status("unavailable", &p) && p == ProviderStatus::Unavailable,
          "v37_parse_unavailable");
    check(parse_provider_status("error", &p) && p == ProviderStatus::Error, "v38_parse_error");
    check(!parse_provider_status("", &p) && !parse_provider_status("OK", &p) &&
              !parse_provider_status("okx", &p), "v39_parse_reject_variants");

    // --- PlaybackResult Default actual -1 ---
    {
        ProviderRequest pr = mkreq("CAM-01", 0, 1000);
        pr.request_id = "pb";
        core::PlaybackResult pb;  // default actual -1
        check(pb.actual_pos_ms == -1 && pb.reached == false && pb.decoded_frames == 0,
              "v40_playback_defaults");
        core::ExportResult xe;
        check(xe.complete == false && xe.size_bytes == 0 && xe.path.empty(),
              "v41_export_defaults");
    }

    std::printf("vext %s (%d failure(s))\n", fails == 0 ? "OK" : "FAILED", fails);
    return fails == 0 ? 0 : 1;
}
