
// vext_clip.cpp — ITEM-012 验证侧自有扩展夹具（software_verification）
// 直接链接交付 media 层 clip_writer.cpp + core clip_request.cpp + platform file_store.cpp，
// 独立复验边界；判据全部验证侧独立构造，不采信 core-tests/selftest。
// 本夹具只做纯逻辑/路径/缺口结构复验（不落真实盘写），写盘+封装+原子顺序语义由
// 探针 EX-A/独立解析比对与真实 .ts 结构核对覆盖。
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/clip_request.h"
#include "railhmi/media/clip_writer.h"
#include "railhmi/platform/file_store.h"

using railhmi::core::ClipAvailability;
using railhmi::core::ClipRequest;
using railhmi::core::ClipRequestOutcome;
using railhmi::core::ClipRequestRegistry;
using railhmi::core::ClipResult;
using railhmi::core::make_clip_request;
using railhmi::media::ClipContainer;
using railhmi::media::ClipPublisher;
using railhmi::media::ClipWriter;
using railhmi::media::ClipWriterConfig;

static int failures = 0;
static void check(bool ok, const std::string& name, const std::string& detail = "") {
    std::printf("vext %s %s%s\n", ok ? "PASS" : "FAIL", name.c_str(),
                detail.empty() ? "" : (" [" + detail + "]").c_str());
    if (!ok) ++failures;
}

static railhmi::core::CompressedPacket pkt(int64_t ts, std::size_t bytes, bool key = false) {
    railhmi::core::CompressedPacket p;
    p.ts_ms = ts;
    p.keyframe = key;
    p.data.assign(bytes, static_cast<std::uint8_t>(ts & 0xff));
    return p;
}

int main() {
    // --- path 形状: 容器扩展名 + request_id sanitize ---
    {
        ClipWriterConfig c;
        c.output_dir = "/tmp/vext_clips";
        ClipWriter w(c);
        const ClipRequest r = make_clip_request("evt-a/1", 1, "CAM-01", 500, 3, 2);
        const std::string p = w.clip_path(r);
        check(p == "/tmp/vext_clips/CAM-01_evt-a_1.ts", "w01_clip_path_shape", p);
        const ClipRequest rr = make_clip_request("evt-a/2", 2, "CAM-01", 500, 1, 1);
        check(w.clip_path(rr) != p, "w02_path_request_id_distinct");
        // 尾斜杠归一
        ClipWriterConfig c2;
        c2.output_dir = "/tmp/vext_clips/";
        ClipWriter w2(c2);
        check(w2.clip_path(r) == p, "w03_trailing_slash_normalized",
              w2.clip_path(r));
    }
    // --- 窗口算术 / 同参 / 幂等注册表 ---
    {
        const ClipRequest r = make_clip_request("req-a", 7, "CAM-01", 100000, 30, 60);
        check(r.t0_wall_ms == 70000 && r.t1_wall_ms == 160000 && r.duration_ms() == 90000,
              "w04_window_arithmetic",
              "t0=" + std::to_string(r.t0_wall_ms) + " t1=" + std::to_string(r.t1_wall_ms));
        ClipRequestRegistry reg;
        check(reg.submit(r) == ClipRequestOutcome::Added, "w05_idem_first_added");
        check(reg.submit(r) == ClipRequestOutcome::Duplicate, "w06_idem_repeat_duplicate");
        check(reg.size() == 1, "w07_duplicate_does_not_reinsert");
        const ClipRequest c = make_clip_request("req-a", 7, "CAM-01", 100000, 30, 61);
        check(reg.submit(c) == ClipRequestOutcome::Conflict, "w08_idem_conflict");
        check(reg.size() == 1, "w09_conflict_does_not_overwrite");
        const ClipRequest* f = reg.find("req-a");
        check(f && f->t1_wall_ms == 160000, "w10_conflict_keeps_original",
              f ? "t1=" + std::to_string(f->t1_wall_ms) : "null");
        check(reg.find("nope") == nullptr, "w11_find_unknown_nullptr");
        // 同参判定: event_id/camera/trigger 任一不同 -> 不同参
        const ClipRequest cam_differ = make_clip_request("req-a", 7, "CAM-02", 100000, 30, 60);
        check(reg.submit(cam_differ) == ClipRequestOutcome::Conflict, "w12_camera_diff_conflict");
        const ClipRequest trig_differ = make_clip_request("req-a", 7, "CAM-01", 100001, 30, 60);
        check(reg.submit(trig_differ) == ClipRequestOutcome::Conflict, "w13_trigger_diff_conflict");
    }
    // --- record_unavailable: 缺口结构 + Unavailable 缺省 ---
    {
        ClipWriterConfig c;
        c.output_dir = "/tmp/vext_clips";
        ClipWriter w(c);
        const ClipRequest r = make_clip_request("evt-u-1", 9, "CAM-02", 1000, 30, 60);
        const ClipResult u = w.record_unavailable(r, "NO_MEDIA");
        check(std::string(railhmi::core::to_string(u.availability)) == "unavailable",
              "w14_unavailable_tristate");
        check(!u.published && u.has_gap, "w15_unavailable_gap_and_unpublished");
        const auto& g = u.gap;
        check(g.cam == "CAM-02" && g.t0_ms == 1000 - 30000 && g.t1_ms == 1000 + 60000 &&
              g.reason == "NO_MEDIA", "w16_unavailable_gap_fields",
              "cam=" + g.cam + " t0=" + std::to_string(g.t0_ms) + " t1=" +
              std::to_string(g.t1_ms) + " reason=" + g.reason);
        check(u.reason == "NO_MEDIA", "w17_unavailable_reason_token");
        check(u.actual_t0_ms == 0 && u.actual_t1_ms == 0, "w18_unavailable_no_actual_window");
    }
    // --- 三态解析往返 + 拒绝未知 ---
    {
        ClipAvailability a;
        check(railhmi::core::parse_clip_availability("complete", &a) &&
              a == ClipAvailability::Complete, "w19_parse_complete");
        check(railhmi::core::parse_clip_availability("incomplete", &a) &&
              a == ClipAvailability::Incomplete, "w20_parse_incomplete");
        check(railhmi::core::parse_clip_availability("unavailable", &a) &&
              a == ClipAvailability::Unavailable, "w21_parse_unavailable");
        check(!railhmi::core::parse_clip_availability("bogus", &a), "w22_reject_bogus");
        for (auto v : {ClipAvailability::Complete, ClipAvailability::Incomplete,
                       ClipAvailability::Unavailable}) {
            ClipAvailability back;
            const std::string n = railhmi::core::to_string(v);
            const bool ok = railhmi::core::parse_clip_availability(n, &back) && back == v;
            if (!ok) check(false, "w23_roundtrip_" + n);
        }
        check(true, "w23_roundtrip_all");
    }
    // --- 容器缺省与命名 ---
    {
        ClipWriterConfig c;
        check(c.container == ClipContainer::MpegTs, "w24_container_default");
        check(std::string(railhmi::media::clip_container_name(ClipContainer::MpegTs)) == "mpegts",
              "w25_container_name_ts");
        check(std::string(railhmi::media::clip_container_extension(ClipContainer::MpegTs)) == "ts",
              "w26_container_ext_ts");
        check(std::string(railhmi::media::clip_container_name(ClipContainer::Matroska)) == "matroska",
              "w27_container_name_mkv");
        check(std::string(railhmi::media::clip_container_extension(ClipContainer::Matroska)) == "mkv",
              "w28_container_ext_mkv");
        check(c.coverage_tolerance_ms == 1000, "w29_tolerance_default_1000");
        check(c.mux_timeout_ms == 20000, "w30_mux_timeout_default");
        check(c.validate, "w31_validate_default_true");
    }
    // --- empty packets -> NO_MEDIA（不写盘不发布）---
    {
        ClipWriterConfig c;
        c.output_dir = "/tmp/vext_clips2";
        ClipWriter w(c);
        const ClipRequest r = make_clip_request("evt-no-media", 11, "CAM-03", 1000, 3, 2);
        ClipPublisher nop = [](const ClipResult&) { return true; };
        const ClipResult u = w.solidify(r, {}, "h264", nop);
        check(u.availability == ClipAvailability::Unavailable, "w32_solidify_empty_unavailable");
        check(u.reason == "NO_MEDIA" && u.has_gap && !u.published,
              "w33_solidify_empty_no_media_gap");
        // .json 清单/空文件冒充: 情景守恒 — 无媒体不产生 path
        check(u.path.empty() && u.size_bytes == 0, "w34_no_path_no_size_without_media");
        // 目录还未创建: NO_MEDIA 早退不得残留目录副作用
        railhmi::platform::FileStore fs;
        check(!railhmi::platform::FileStore::exists("/tmp/vext_clips2"),
              "w35_empty_request_no_dir_side_effect");
    }

    std::printf("vext %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
