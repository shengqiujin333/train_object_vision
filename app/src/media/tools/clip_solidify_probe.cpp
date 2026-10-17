// railhmi-clip-solidify (视频 ITEM-012)
// 保护片段固化与缺口记录（媒体侧）：
//   * 幂等处理 ClipRequest（同 request_id 同参重复返回同一作业，不同参 CONFLICT）；
//   * 关键帧对齐起点，回报 actual_t0/actual_t1 与请求窗口；
//   * 直通复制压缩包到真实可解析媒体文件（MPEG-TS），临时文件→校验→fsync→rename→fsync 目录→发布；
//   * 无媒体/写失败写 recording_gaps，可用性三态 complete/incomplete/unavailable；
//   * 不以 .json 清单或空文件冒充录像。
//
// 用法:
//   railhmi-clip-solidify --config config --pre-seconds 3 --post-seconds 2 --warmup 4 --extra 2 \
//       --outdir /tmp/railhmi_clips --out clip.log --json clip.json
//   railhmi-clip-solidify --selftest
//
// 退出码: 0 正常（含验收判定）；1 selftest 失败；2 参数错误；3 启动失败。

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

#include "railhmi/core/clip_request.h"
#include "railhmi/core/config.h"
#include "railhmi/media/clip_writer.h"
#include "railhmi/media/recording_tap.h"
#include "railhmi/platform/file_store.h"

namespace {

int run_selftest() {
    using namespace railhmi;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    const core::ClipRequest r = core::make_clip_request("req-1", 7, "CAM-01", 100000, 30, 60);
    check(r.t0_wall_ms == 70000 && r.t1_wall_ms == 160000 && r.duration_ms() == 90000, "window_pre_post");
    core::ClipRequestRegistry reg;
    check(reg.submit(r) == core::ClipRequestOutcome::Added, "registry_added");
    check(reg.submit(r) == core::ClipRequestOutcome::Duplicate, "registry_duplicate");
    const core::ClipRequest r2 = core::make_clip_request("req-1", 7, "CAM-01", 100000, 30, 61);
    check(reg.submit(r2) == core::ClipRequestOutcome::Conflict, "registry_conflict");
    core::ClipAvailability a;
    check(core::parse_clip_availability("incomplete", &a) && a == core::ClipAvailability::Incomplete,
          "availability_parse");
    check(std::string(core::to_string(core::ClipAvailability::Unavailable)) == "unavailable",
          "availability_name");
    check(std::string(media::clip_container_extension(media::ClipContainer::MpegTs)) == "ts", "ext_ts");
    check(std::string(media::clip_container_extension(media::ClipContainer::Matroska)) == "mkv", "ext_mkv");
    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    std::string config_dir = "config";
    int pre_seconds = 3, post_seconds = 2, warmup_s = 4, extra_s = 2;
    int fps = 25, width = 1280, height = 720;
    std::string outdir = "/tmp/railhmi_clips";
    std::string out, json;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--config") next(&config_dir);
        else if (a == "--pre-seconds") { std::string v; next(&v); pre_seconds = std::atoi(v.c_str()); }
        else if (a == "--post-seconds") { std::string v; next(&v); post_seconds = std::atoi(v.c_str()); }
        else if (a == "--warmup") { std::string v; next(&v); warmup_s = std::atoi(v.c_str()); }
        else if (a == "--extra") { std::string v; next(&v); extra_s = std::atoi(v.c_str()); }
        else if (a == "--fps") { std::string v; next(&v); fps = std::atoi(v.c_str()); }
        else if (a == "--width") { std::string v; next(&v); width = std::atoi(v.c_str()); }
        else if (a == "--height") { std::string v; next(&v); height = std::atoi(v.c_str()); }
        else if (a == "--outdir") next(&outdir);
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--selftest") return run_selftest();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-clip-solidify [--config dir] [--pre-seconds n] [--post-seconds n] "
                        "[--warmup s] [--extra s] [--outdir dir] [--out log] [--json json] [--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    std::ofstream fo(out, std::ios::trunc);
    std::setvbuf(stdout, nullptr, _IONBF, 0);  // 无缓冲：超时中断也保留已打印观测
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) { fo << line << "\n"; fo.flush(); }
    };

    const core::ConfigLoadResult cfg = core::load_config_dir(config_dir);
    int cfg_pre = 30, cfg_post = 60;
    if (cfg.ok) {
        cfg_pre = cfg.settings.thresholds.clip_pre_seconds;
        cfg_post = cfg.settings.thresholds.clip_post_seconds;
    } else {
        std::fprintf(stderr, "warning: config dir '%s' unusable; 回退文档化默认\n", config_dir.c_str());
    }
    const int buffer_s = pre_seconds + post_seconds + 2;
    emit("type=config config_pre_seconds=" + std::to_string(cfg_pre) +
         " config_post_seconds=" + std::to_string(cfg_post) +
         " effective_pre_seconds=" + std::to_string(pre_seconds) +
         " effective_post_seconds=" + std::to_string(post_seconds) +
         " buffer_seconds=" + std::to_string(buffer_s) + " outdir=" + outdir);

    gst_init(nullptr, nullptr);
    std::string desc =
        "videotestsrc is-live=true ! video/x-raw,format=I420,width=" + std::to_string(width) +
        ",height=" + std::to_string(height) + ",framerate=" + std::to_string(fps) + "/1"
        " ! x264enc tune=zerolatency speed-preset=ultrafast key-int-max=" + std::to_string(fps) +
        " byte-stream=true"
        " ! h264parse ! video/x-h264,stream-format=byte-stream,alignment=au ! tee name=enc "
        "enc. ! queue leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0 "
        "! avdec_h264 ! videoconvert ! video/x-raw,format=NV12 "
        "! appsink name=display_sink drop=1 max-buffers=1 sync=false "
        "enc. ! queue leaky=downstream max-size-buffers=200 max-size-bytes=0 max-size-time=0 "
        "! appsink name=record_sink sync=false";
    GError* gerr = nullptr;
    GstElement* pipeline = gst_parse_launch(desc.c_str(), &gerr);
    if (gerr) { std::fprintf(stderr, "pipeline error: %s\n", gerr->message); g_error_free(gerr); }
    if (!pipeline) return 3;
    GstElement* display_sink = gst_bin_get_by_name(GST_BIN(pipeline), "display_sink");
    GstElement* record_sink = gst_bin_get_by_name(GST_BIN(pipeline), "record_sink");
    if (!record_sink) { std::fprintf(stderr, "record_sink not found\n"); return 3; }
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    media::RecordingTap tap(64u * 1024u * 1024u, buffer_s * 1000);
    auto drain = [&](long long* last_ts) {
        for (int k = 0; k < 32; ++k) {
            GstSample* s = gst_app_sink_try_pull_sample(GST_APP_SINK(record_sink), 0);
            if (!s) break;
            GstBuffer* buf = gst_sample_get_buffer(s);
            if (buf) {
                const int64_t ts_ms = GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(buf))
                                          ? static_cast<int64_t>(GST_BUFFER_PTS(buf) / GST_MSECOND)
                                          : *last_ts;
                core::CompressedPacket p;
                p.ts_ms = ts_ms;
                p.keyframe = !GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT);
                GstMapInfo mi;
                if (gst_buffer_map(buf, &mi, GST_MAP_READ)) {
                    p.data.assign(mi.data, mi.data + mi.size);
                    gst_buffer_unmap(buf, &mi);
                }
                tap.push(std::move(p));
                if (ts_ms > *last_ts) *last_ts = ts_ms;
            }
            gst_sample_unref(s);
        }
    };
    auto pull_display_once = [&]() {
        GstSample* s = gst_app_sink_try_pull_sample(GST_APP_SINK(display_sink), 0);
        if (s) gst_sample_unref(s);
    };

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    auto elapsed_ms = [&]() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
    };
    long long last_ts = 0, display_frames = 0;
    long long trigger_ts = -1;
    long long display_next = 0;
    const long long total_ms = static_cast<long long>(warmup_s + post_seconds + extra_s) * 1000;
    while (elapsed_ms() < total_ms) {
        drain(&last_ts);
        const long long e = elapsed_ms();
        if (e >= display_next) { pull_display_once(); ++display_frames; display_next = e + 200; }
        if (trigger_ts < 0 && e >= warmup_s * 1000 && last_ts > 0) trigger_ts = last_ts;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    drain(&last_ts);
    if (trigger_ts < 0) trigger_ts = last_ts;
    gst_element_set_state(pipeline, GST_STATE_NULL);

    const core::CompressedRingStats st = tap.stats();
    emit("type=buffer packets=" + std::to_string(st.packets) + " span_ms=" + std::to_string(st.span_ms) +
         " last_ts_ms=" + std::to_string(st.last_ts_ms) + " display_frames=" + std::to_string(display_frames) +
         " trigger_ts_ms=" + std::to_string(trigger_ts));

    // 幂等注册表检查。
    core::ClipRequestRegistry registry;
    const core::ClipRequest req1 = core::make_clip_request("evt-100-CAM-01", 100, "CAM-01", trigger_ts,
                                                           pre_seconds, post_seconds);
    const core::ClipRequestOutcome o1 = registry.submit(req1);
    const core::ClipRequestOutcome o2 = registry.submit(req1);
    const core::ClipRequest req_conflict = core::make_clip_request("evt-100-CAM-01", 100, "CAM-01",
                                                                   trigger_ts, pre_seconds, post_seconds + 5);
    const core::ClipRequestOutcome o3 = registry.submit(req_conflict);
    emit("type=idempotency first=" + std::string(core::to_string(o1)) +
         " repeat=" + std::string(core::to_string(o2)) +
         " different_params=" + std::string(core::to_string(o3)));

    // 关键帧对齐 + 窗口收集。
    auto collect_aligned = [&](const core::ClipRequest& r, bool* aligned) -> std::vector<core::CompressedPacket> {
        int64_t start = r.t0_wall_ms;
        const int64_t kf = tap.first_keyframe_at_or_after(r.t0_wall_ms);
        if (kf >= 0 && kf <= r.t1_wall_ms) { start = kf; *aligned = true; } else { *aligned = false; }
        return tap.collect(start, r.t1_wall_ms);
    };

    media::ClipWriterConfig wcfg;
    wcfg.output_dir = outdir;
    wcfg.container = media::ClipContainer::MpegTs;
    media::ClipWriter writer(wcfg);

    bool publish_saw_file = false;
    media::ClipPublisher publish = [&](const core::ClipResult& r) -> bool {
        publish_saw_file = railhmi::platform::FileStore::exists(r.path);
        return true;
    };

    // S1 完整窗口（后录已覆盖）。
    bool aligned1 = false;
    const std::vector<core::CompressedPacket> pk1 = collect_aligned(req1, &aligned1);
    const core::ClipResult res1 = writer.solidify(req1, pk1, "h264", publish);
    emit("type=clip case=complete requested=[" + std::to_string(res1.requested_t0_ms) + "," +
         std::to_string(res1.requested_t1_ms) + ") actual=[" + std::to_string(res1.actual_t0_ms) + "," +
         std::to_string(res1.actual_t1_ms) + "] packets=" + std::to_string(pk1.size()) +
         " keyframe_aligned=" + std::string(aligned1 ? "true" : "false") +
         " availability=" + core::to_string(res1.availability) +
         " size_bytes=" + std::to_string(res1.size_bytes) + " published=" +
         std::string(res1.published ? "true" : "false") + " path=" + res1.path +
         " reason=" + res1.reason);

    // S2 后录不足（请求 t1 远超实际）。
    const core::ClipRequest req2 = core::make_clip_request("evt-101-CAM-01", 101, "CAM-01", trigger_ts,
                                                           pre_seconds, post_seconds + 30);
    bool aligned2 = false;
    const std::vector<core::CompressedPacket> pk2 = collect_aligned(req2, &aligned2);
    const core::ClipResult res2 = writer.solidify(req2, pk2, "h264", publish);
    emit("type=clip case=incomplete requested=[" + std::to_string(res2.requested_t0_ms) + "," +
         std::to_string(res2.requested_t1_ms) + ") actual=[" + std::to_string(res2.actual_t0_ms) + "," +
         std::to_string(res2.actual_t1_ms) + "] availability=" + core::to_string(res2.availability) +
         " has_gap=" + std::string(res2.has_gap ? "true" : "false") + " gap_reason=" + res2.gap.reason +
         " gap=[" + std::to_string(res2.gap.t0_ms) + "," + std::to_string(res2.gap.t1_ms) + ")");

    // S3 无覆盖（窗口在缓冲之前）。
    const core::ClipRequest req3 = core::make_clip_request("evt-102-CAM-01", 102, "CAM-01",
                                                           trigger_ts - 100000, pre_seconds, 0);
    bool aligned3 = false;
    const std::vector<core::CompressedPacket> pk3 = collect_aligned(req3, &aligned3);
    const core::ClipResult res3 = writer.solidify(req3, pk3, "h264", publish);
    emit("type=clip case=unavailable packets=" + std::to_string(pk3.size()) +
         " availability=" + core::to_string(res3.availability) +
         " has_gap=" + std::string(res3.has_gap ? "true" : "false") + " reason=" + res3.reason);

    // 写失败路径（不可创建的输出目录）→ 缺口 + Unavailable。
    media::ClipWriterConfig badcfg;
    badcfg.output_dir = "/proc/railhmi_nonexistent/clips";
    media::ClipWriter bad_writer(badcfg);
    const core::ClipResult res4 = bad_writer.solidify(req1, pk1, "h264", nullptr);
    emit(std::string("type=clip case=mux_failed availability=") + core::to_string(res4.availability) +
         " has_gap=" + std::string(res4.has_gap ? "true" : "false") + " reason=" + res4.reason);

    const bool no_stray_temp = !railhmi::platform::FileStore::exists(res1.path + ".part");
    const bool real_media = res1.availability == core::ClipAvailability::Complete && res1.size_bytes > 0 &&
                            railhmi::platform::FileStore::exists(res1.path);
    const bool coverage_matches = res1.actual_t0_ms <= res1.requested_t0_ms + 1000 &&
                                  res1.actual_t1_ms >= res1.requested_t1_ms - 1000 &&
                                  res1.reason.empty();
    const bool gap_recorded = res2.has_gap && res3.has_gap && res4.has_gap;
    const bool tristate = res1.availability == core::ClipAvailability::Complete &&
                          res2.availability == core::ClipAvailability::Incomplete &&
                          res3.availability == core::ClipAvailability::Unavailable;
    const bool idempotent = o1 == core::ClipRequestOutcome::Added && o2 == core::ClipRequestOutcome::Duplicate &&
                            o3 == core::ClipRequestOutcome::Conflict;
    const bool publish_after_rename = publish_saw_file;

    const bool ok = real_media && coverage_matches && aligned1 && gap_recorded && tristate && idempotent &&
                    publish_after_rename && no_stray_temp;
    emit("type=acceptance real_media=" + std::string(real_media ? "true" : "false") +
         " coverage_matches=" + std::string(coverage_matches ? "true" : "false") +
         " keyframe_aligned=" + std::string(aligned1 ? "true" : "false") +
         " gap_recorded=" + std::string(gap_recorded ? "true" : "false") +
         " availability_tristate=" + std::string(tristate ? "true" : "false") +
         " idempotent=" + std::string(idempotent ? "true" : "false") +
         " publish_after_rename=" + std::string(publish_after_rename ? "true" : "false") +
         " no_stray_temp=" + std::string(no_stray_temp ? "true" : "false") +
         " result=" + std::string(ok ? "PASS" : "FAIL"));

    if (display_sink) gst_object_unref(display_sink);
    gst_object_unref(record_sink);
    gst_object_unref(pipeline);

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        j << "{\n  \"clips\": [\n";
        j << "    {\"id\": \"" << res1.request_id << "\", \"availability\": \"" << core::to_string(res1.availability)
          << "\", \"size_bytes\": " << res1.size_bytes << ", \"path\": \"" << res1.path
          << "\", \"actual_t0_ms\": " << res1.actual_t0_ms << ", \"actual_t1_ms\": " << res1.actual_t1_ms
          << ", \"published\": " << (res1.published ? "true" : "false") << "},\n";
        j << "    {\"id\": \"" << res2.request_id << "\", \"availability\": \"" << core::to_string(res2.availability)
          << "\", \"has_gap\": " << (res2.has_gap ? "true" : "false") << "},\n";
        j << "    {\"id\": \"" << res3.request_id << "\", \"availability\": \"" << core::to_string(res3.availability)
          << "\", \"has_gap\": " << (res3.has_gap ? "true" : "false") << "}\n";
        j << "  ],\n  \"acceptance\": " << (ok ? "true" : "false") << "\n}\n";
    }
    return 0;
}
