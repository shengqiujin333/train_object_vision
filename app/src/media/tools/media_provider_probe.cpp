// railhmi-media-provider (视频 ITEM-013)
// 录像 provider 与回放媒体链路（媒体侧）：
//   * querySegments/queryGaps：真实可读/可解码判定 + 覆盖三态（ok 空列表≠可播放；partial；unavailable/UNCONFIGURED）；
//   * playSegment：真实解码并回报**实际解码 position**（请求接受≠播放成功）；
//   * exportSegment：真实复制返回路径/大小；
//   * 缩略图：媒体生成的可读引用；
//   * 同步：以各路**实际解码播放头**驱动 core::sync_status（max−min 偏差阈值提示）。
//
// 用法:
//   railhmi-media-provider --config config --pre-seconds 3 --post-seconds 2 --warmup 4 --extra 2 \
//       --outdir /tmp/railhmi_prov --out prov.log --json prov.json
//   railhmi-media-provider --selftest
//
// 退出码: 0 正常（含验收判定）；1 selftest 失败；2 参数错误；3 启动失败。

#include <chrono>
#include <algorithm>
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
#include "railhmi/core/replay.h"
#include "railhmi/media/clip_writer.h"
#include "railhmi/media/media_provider.h"
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
    core::ProviderRequest req;
    req.request_id = "q1";
    req.camera_id = "CAM-01";
    req.t0_ms = 1000;
    req.t1_ms = 5000;

    // 无覆盖 -> ok 空列表。
    const auto empty = core::classify_segments(req, {}, {}, true);
    check(empty.status == core::ProviderStatus::Ok && empty.segments.empty() && !empty.coverage_complete,
          "empty_ok_not_playable");

    // 未配置 -> unavailable/UNCONFIGURED。
    const auto unc = core::classify_segments(req, {}, {}, false);
    check(unc.status == core::ProviderStatus::Unavailable &&
              unc.reason == core::provider_reason::kUnconfigured,
          "unconfigured");

    // 完整覆盖。
    std::vector<core::SegmentRef> full{{"CAM-01", 1000, 5000, "/x.ts", core::SegmentKind::Critical, 100, true, true}};
    const auto c1 = core::classify_segments(req, full, {}, true);
    check(c1.status == core::ProviderStatus::Ok && c1.coverage_complete, "complete");

    // 部分覆盖 -> partial。
    std::vector<core::SegmentRef> part{{"CAM-01", 1000, 2500, "/x.ts", core::SegmentKind::Critical, 100, true, true}};
    const auto c2 = core::classify_segments(req, part, {}, true);
    check(c2.status == core::ProviderStatus::Partial && c2.covered_ms == 1500, "partial");

    // 有片段但均不可用 -> unavailable/NOT_FOUND。
    std::vector<core::SegmentRef> bad{{"CAM-01", 1000, 5000, "/x.ts", core::SegmentKind::Critical, 0, false, false}};
    const auto c3 = core::classify_segments(req, bad, {}, true);
    check(c3.status == core::ProviderStatus::Unavailable && c3.reason == core::provider_reason::kNotFound,
          "unusable_not_found");

    // 截止时间。
    core::ProviderRequest dl = req;
    dl.deadline_mono_us = 100;
    check(core::provider_deadline_expired(dl, 200), "deadline_expired");

    check(std::string(core::to_string(core::ProviderStatus::Partial)) == "partial", "status_name");
    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    std::string config_dir = "config";
    int pre_seconds = 3, post_seconds = 2, warmup_s = 4, extra_s = 2;
    int fps = 25, width = 1280, height = 720;
    std::string outdir = "/tmp/railhmi_prov";
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
        else if (a == "--outdir") next(&outdir);
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--selftest") return run_selftest();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-media-provider [--config dir] [--pre-seconds n] [--post-seconds n] "
                        "[--warmup s] [--extra s] [--outdir dir] [--out log] [--json json] [--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    std::ofstream fo(out, std::ios::trunc);
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) { fo << line << "\n"; fo.flush(); }
    };

    const core::ConfigLoadResult cfg = core::load_config_dir(config_dir);
    if (!cfg.ok) std::fprintf(stderr, "warning: config dir '%s' unusable\n", config_dir.c_str());
    const int buffer_s = pre_seconds + post_seconds + 2;

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
    long long last_ts = 0, display_frames = 0, trigger_ts = -1, display_next = 0;
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

    // 生成真实保护片段（复用 ITEM-012 ClipWriter）。
    media::ClipWriterConfig wcfg;
    wcfg.output_dir = outdir;
    wcfg.container = media::ClipContainer::MpegTs;
    media::ClipWriter writer(wcfg);
    const core::ClipRequest clip_req =
        core::make_clip_request("evt-200-CAM-01", 200, "CAM-01", trigger_ts, pre_seconds, post_seconds);
    int64_t start = clip_req.t0_wall_ms;
    const int64_t kf = tap.first_keyframe_at_or_after(start);
    if (kf >= 0 && kf <= clip_req.t1_wall_ms) start = kf;
    const std::vector<core::CompressedPacket> pk = tap.collect(start, clip_req.t1_wall_ms);
    const core::ClipResult clip = writer.solidify(clip_req, pk, "h264", nullptr);
    emit("type=clip availability=" + std::string(core::to_string(clip.availability)) +
         " size_bytes=" + std::to_string(clip.size_bytes) + " actual=[" + std::to_string(clip.actual_t0_ms) +
         "," + std::to_string(clip.actual_t1_ms) + "] path=" + clip.path);
    if (clip.size_bytes <= 0) {
        emit("type=acceptance result=FAIL reason=NO_CLIP");
        return 0;
    }

    // 构建 provider：真实片段 + 一个缺口。
    std::vector<core::RecordingSegmentRow> segs;
    core::RecordingSegmentRow seg;
    seg.cam = "CAM-01";
    seg.t0_ms = clip.actual_t0_ms;
    seg.t1_ms = clip.actual_t1_ms;
    seg.path = clip.path;
    seg.kind = core::SegmentKind::Critical;
    seg.complete = (clip.availability == core::ClipAvailability::Complete);
    seg.size_bytes = clip.size_bytes;
    segs.push_back(seg);
    std::vector<core::RecordingGapRow> gaps;
    core::RecordingGapRow gap;
    gap.cam = "CAM-01";
    gap.t0_ms = clip.actual_t1_ms;
    gap.t1_ms = clip.actual_t1_ms + 5000;
    gap.reason = "SYNTH_GAP";
    gaps.push_back(gap);

    media::MediaProviderConfig pcfg;
    pcfg.export_dir = outdir + "/exports";
    pcfg.thumbnail_dir = outdir + "/thumbs";
    pcfg.verdict_timeout_ms = 5000;
    pcfg.playback_media_ms = 500;
    pcfg.seek_tolerance_ms = 1500;
    media::LocalMediaProvider provider(segs, gaps, pcfg);

    auto us = []() {
        return std::chrono::duration_cast<std::chrono::microseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
            .count();
    };
    core::ProviderRequest base;
    base.camera_id = "CAM-01";
    base.t0_ms = clip.actual_t0_ms;
    base.t1_ms = clip.actual_t1_ms;

    // Q1 完整覆盖。
    core::ProviderRequest q1 = base;
    q1.request_id = "seg-complete";
    const auto r1 = provider.query_segments(q1, us());
    emit("type=query case=complete status=" + std::string(core::to_string(r1.status)) +
         " reason=" + r1.reason + " segments=" + std::to_string(r1.segments.size()) +
         " coverage_complete=" + std::string(r1.coverage_complete ? "true" : "false") +
         " covered_ms=" + std::to_string(r1.covered_ms));

    // Q2 无覆盖（查询成功空列表，不称可播放）。
    core::ProviderRequest q2;
    q2.request_id = "seg-empty";
    q2.camera_id = "CAM-01";
    q2.t0_ms = clip.actual_t1_ms + 10000;
    q2.t1_ms = clip.actual_t1_ms + 20000;
    const auto r2 = provider.query_segments(q2, us());
    emit("type=query case=empty status=" + std::string(core::to_string(r2.status)) + " reason=" + r2.reason +
         " segments=" + std::to_string(r2.segments.size()) +
         " coverage_complete=" + std::string(r2.coverage_complete ? "true" : "false"));

    // Q3 部分覆盖。
    core::ProviderRequest q3;
    q3.request_id = "seg-partial";
    q3.camera_id = "CAM-01";
    q3.t0_ms = clip.actual_t1_ms - 1000;
    q3.t1_ms = clip.actual_t1_ms + 10000;
    const auto r3 = provider.query_segments(q3, us());
    emit("type=query case=partial status=" + std::string(core::to_string(r3.status)) + " reason=" + r3.reason +
         " segments=" + std::to_string(r3.segments.size()) + " covered_ms=" + std::to_string(r3.covered_ms) +
         " coverage_complete=" + std::string(r3.coverage_complete ? "true" : "false"));

    // QG 缺口。
    core::ProviderRequest gq;
    gq.request_id = "gap";
    gq.camera_id = "CAM-01";
    gq.t0_ms = clip.actual_t1_ms;
    gq.t1_ms = clip.actual_t1_ms + 6000;
    const auto rg = provider.query_gaps(gq, us());
    emit("type=query case=gaps status=" + std::string(core::to_string(rg.status)) + " gaps=" +
         std::to_string(rg.gaps.size()) + (rg.gaps.empty() ? "" : (" reason=" + rg.gaps[0].reason)));

    // 截止时间已过 -> error/TIMEOUT。
    core::ProviderRequest expired = q1;
    expired.request_id = "seg-expired";
    expired.deadline_mono_us = us() - 1;
    const auto rexp = provider.query_segments(expired, us());
    emit("type=query case=expired status=" + std::string(core::to_string(rexp.status)) + " reason=" + rexp.reason);

    // playSegment：实际解码 position（多次不同请求位置）。
    auto play = [&](const std::string& id, long long from_ms) {
        core::ProviderRequest pr = base;
        pr.request_id = id;
        core::PlaybackResult pb = provider.play_segment(pr, clip.path, from_ms, us());
        emit("type=play id=" + id + " status=" + std::string(core::to_string(pb.status)) + " reason=" + pb.reason +
             " requested=" + std::to_string(pb.requested_pos_ms) + " actual=" + std::to_string(pb.actual_pos_ms) +
             " reached=" + std::string(pb.reached ? "true" : "false") +
             " frames=" + std::to_string(pb.decoded_frames));
        return pb;
    };
    const core::PlaybackResult p1 = play("play-400", clip.actual_t0_ms + 400);
    const core::PlaybackResult p2 = play("play-500", clip.actual_t0_ms + 500);
    const core::PlaybackResult p3 = play("play-600", clip.actual_t0_ms + 600);
    const core::PlaybackResult p4 = play("play-2500", clip.actual_t0_ms + 2500);
    const core::PlaybackResult p5 = play("play-4500", clip.actual_t0_ms + 4500);

    // exportSegment：真实复制返回路径/大小。
    core::ProviderRequest er = base;
    er.request_id = "export-1";
    const auto ex = provider.export_segment(er, clip.path, us());
    const bool export_ok = ex.status == core::ProviderStatus::Ok &&
                           railhmi::platform::FileStore::exists(ex.path) && ex.size_bytes > 0;
    emit("type=export status=" + std::string(core::to_string(ex.status)) + " reason=" + ex.reason +
         " path=" + ex.path + " size_bytes=" + std::to_string(ex.size_bytes) +
         " complete=" + std::string(ex.complete ? "true" : "false"));
    // 导出失败路径（源不存在）。
    const auto ex_bad = provider.export_segment(er, clip.path + ".missing", us());
    emit("type=export case=missing status=" + std::string(core::to_string(ex_bad.status)) +
         " reason=" + ex_bad.reason);

    // 缩略图：媒体生成的可读引用。
    const std::string thumb = provider.thumbnail(clip.path, clip.actual_t0_ms + 400, "cam01");
    const bool thumb_ok = !thumb.empty() && railhmi::platform::FileStore::exists(thumb) &&
                          (thumb.find(".jpg") != std::string::npos);
    emit("type=thumbnail path=" + thumb + " exists=" + std::string(thumb_ok ? "true" : "false"));

    // NVR 未配置：unavailable/UNCONFIGURED，不生成模拟片段。
    media::NvrProvider nvr;
    core::ProviderRequest nr = base;
    nr.request_id = "nvr";
    nr.t0_ms = 0;
    nr.t1_ms = 1000000000;
    const auto nq = nvr.query_segments(nr);
    const auto np = nvr.play_segment(nr);
    const auto ne = nvr.export_segment(nr);
    const bool nvr_ok = nq.status == core::ProviderStatus::Unavailable &&
                        nq.reason == core::provider_reason::kUnconfigured && nq.segments.empty() &&
                        np.status == core::ProviderStatus::Unavailable &&
                        ne.status == core::ProviderStatus::Unavailable;
    emit("type=nvr query=" + std::string(core::to_string(nq.status)) + "/" + nq.reason +
         " play=" + std::string(core::to_string(np.status)) + "/" + np.reason +
         " export=" + std::string(core::to_string(ne.status)) + "/" + ne.reason +
         " segments=" + std::to_string(nq.segments.size()));

    // 同步：以真实解码播放头驱动 core::sync_status（max−min）。
    std::vector<long long> close_heads = {p1.actual_pos_ms, p2.actual_pos_ms, p3.actual_pos_ms};
    std::vector<long long> spread_heads = {p1.actual_pos_ms, p4.actual_pos_ms, p5.actual_pos_ms};
    const core::SyncResult sc = core::sync_status(close_heads, 300);
    const core::SyncResult ss = core::sync_status(spread_heads, 300);
    auto maxmin = [](const std::vector<long long>& v) {
        long long mn = v[0], mx = v[0];
        for (long long x : v) { mn = std::min(mn, x); mx = std::max(mx, x); }
        return mx - mn;
    };
    const long long spread_dev = maxmin(spread_heads);
    emit("type=sync close_max_deviation=" + std::to_string(sc.max_deviation_ms) +
         " close_in_sync=" + std::string(sc.in_sync ? "true" : "false") +
         " spread_max_deviation=" + std::to_string(ss.max_deviation_ms) +
         " spread_in_sync=" + std::string(ss.in_sync ? "true" : "false") +
         " spread_prompt=" + ss.prompt);

    const bool actual_not_requested = (p1.actual_pos_ms != p1.requested_pos_ms);
    const bool play_within_tol = p1.reached && std::llabs(p1.actual_pos_ms - p1.requested_pos_ms) <= pcfg.seek_tolerance_ms;
    const bool sync_consistency = (sc.in_sync == (sc.max_deviation_ms <= 300)) &&
                                  (ss.in_sync == (ss.max_deviation_ms <= 300)) &&
                                  (ss.max_deviation_ms == spread_dev);
    const bool sync_prompt = !ss.in_sync && !ss.prompt.empty() && spread_dev > 300;

    const bool ok = r1.status == core::ProviderStatus::Ok && r1.coverage_complete &&
                    r2.status == core::ProviderStatus::Ok && r2.segments.empty() &&
                    r3.status == core::ProviderStatus::Partial && rg.status == core::ProviderStatus::Ok &&
                    !rg.gaps.empty() && rexp.status == core::ProviderStatus::Error &&
                    rexp.reason == core::provider_reason::kTimeout && actual_not_requested && play_within_tol &&
                    export_ok && ex_bad.status == core::ProviderStatus::Unavailable && thumb_ok && nvr_ok &&
                    sync_consistency && sync_prompt;
    emit("type=acceptance query_complete=" + std::string(r1.coverage_complete ? "true" : "false") +
         " query_empty_ok=" + std::string(r2.status == core::ProviderStatus::Ok && r2.segments.empty() ? "true" : "false") +
         " query_partial=" + std::string(r3.status == core::ProviderStatus::Partial ? "true" : "false") +
         " gaps_returned=" + std::string(!rg.gaps.empty() ? "true" : "false") +
         " deadline_timeout=" + std::string(rexp.reason == core::provider_reason::kTimeout ? "true" : "false") +
         " play_actual_position=" + std::string(actual_not_requested ? "true" : "false") +
         " play_within_tolerance=" + std::string(play_within_tol ? "true" : "false") +
         " export_real=" + std::string(export_ok ? "true" : "false") +
         " export_missing_unavailable=" + std::string(ex_bad.status == core::ProviderStatus::Unavailable ? "true" : "false") +
         " thumbnail_real=" + std::string(thumb_ok ? "true" : "false") +
         " nvr_unconfigured=" + std::string(nvr_ok ? "true" : "false") +
         " sync_prompt=" + std::string(sync_prompt ? "true" : "false") +
         " sync_consistency=" + std::string(sync_consistency ? "true" : "false") +
         " result=" + std::string(ok ? "PASS" : "FAIL"));

    if (display_sink) gst_object_unref(display_sink);
    gst_object_unref(record_sink);
    gst_object_unref(pipeline);

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        j << "{\n  \"clip\": {\"path\": \"" << clip.path << "\", \"size_bytes\": " << clip.size_bytes
          << ", \"actual_t0_ms\": " << clip.actual_t0_ms << ", \"actual_t1_ms\": " << clip.actual_t1_ms << "},\n";
        j << "  \"query\": {\"complete\": \"" << core::to_string(r1.status) << "\", \"empty\": \""
          << core::to_string(r2.status) << "\", \"partial\": \"" << core::to_string(r3.status) << "\"},\n";
        j << "  \"play\": {\"requested\": " << p1.requested_pos_ms << ", \"actual\": " << p1.actual_pos_ms
          << ", \"reached\": " << (p1.reached ? "true" : "false") << "},\n";
        j << "  \"export\": {\"path\": \"" << ex.path << "\", \"size_bytes\": " << ex.size_bytes << "},\n";
        j << "  \"thumbnail\": \"" << thumb << "\",\n";
        j << "  \"nvr\": \"" << core::to_string(nq.status) << "/" << nq.reason << "\",\n";
        j << "  \"acceptance\": " << (ok ? "true" : "false") << "\n}\n";
    }
    return 0;
}
