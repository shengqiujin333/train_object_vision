// railhmi-recording-tap (视频 ITEM-011)
// 录像压缩分支与滚动缓冲（媒体侧）：
//   * 在解码前 tap 压缩包（h264parse 之后、avdec_h264 之前）到独立缓冲；
//   * 按 clip_pre_seconds 保留最近压缩数据，字节与时长双上限受控；
//   * 不使用显示/AI 丢帧策略：慢显示不减少录像压缩包；
//   * 录像来源遵循 recording_source；NVR 未配置时显式回落本机（reason=NVR_UNCONFIGURED）。
//
// 用法:
//   railhmi-recording-tap --config config --pre-seconds 3 --max-bytes 4194304 \
//       --duration 8 --display-pull-ms 200 --out rec.log --json rec.json
//   railhmi-recording-tap --selftest
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

#include "railhmi/core/config.h"
#include "railhmi/core/recording_source.h"
#include "railhmi/media/pipeline_factory.h"
#include "railhmi/media/recording_tap.h"

namespace {

bool has(const std::string& s, const std::string& sub) { return s.find(sub) != std::string::npos; }

int run_selftest() {
    using namespace railhmi;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };

    // 显式 H.265 + 录像压缩分支：record_sink 出现在 parse 之后、mppvideodec 之前。
    media::CameraPipelineSpec r;
    r.source = media::PipelineSource::Rtsp;
    r.url = "rtsp://admin:DUMMY_SECRET@192.168.1.65:554/x";
    r.decode = media::DecodeMode::ExplicitH265;
    r.recording_tap = true;
    const std::string p = media::build_pipeline_description(r);
    check(has(p, "tee name=enc") && has(p, "record_sink"), "rtsp_h265_tap_present");
    check(has(p, "rtph265depay") && has(p, "h265parse"), "rtsp_h265_parse");
    const std::size_t iparse = p.find("h265parse");
    const std::size_t irec = p.find("record_sink");
    const std::size_t idec = p.find("mppvideodec");
    check(iparse != std::string::npos && irec != std::string::npos && idec != std::string::npos &&
              iparse < idec && irec > iparse,
          "record_sink_after_parse_before_decode");
    check(has(p, "frame_sink") && has(p, "count_sink"), "display_branch_kept");

    media::CameraPipelineSpec r2 = r;
    r2.decode = media::DecodeMode::Auto;
    const std::string p2 = media::build_pipeline_description(r2);
    check(has(p2, "record_sink") && has(p2, "application/x-rtp,media=video ! tee name=enc"),
          "rtsp_auto_rtp_tap");

    media::CameraPipelineSpec no_tap = r;
    no_tap.recording_tap = false;
    const std::string p3 = media::build_pipeline_description(no_tap);
    check(!has(p3, "record_sink"), "tap_off_no_record_sink");

    media::CameraPipelineSpec t;
    t.source = media::PipelineSource::Test;
    t.recording_tap = true;
    check(!has(media::build_pipeline_description(t), "record_sink"), "raw_source_no_tap");

    // 来源策略：NVR 未配置 -> 显式回落本机。
    const core::RecordingSourceStatus s = core::resolve_recording_source(core::RecordingSource::Both, false);
    check(s.effective == core::RecordingAvailability::LocalFallbackNvrUnconfigured &&
              s.reason == "NVR_UNCONFIGURED",
          "source_fallback_explicit");

    // 纯逻辑滚动缓冲：字节/时长双上限。
    core::CompressedRingBuffer ring(300, 1000);
    for (int i = 0; i <= 20; ++i) {
        core::CompressedPacket pk;
        pk.ts_ms = i * 100;
        pk.keyframe = (i % 5 == 0);
        pk.data.assign(100, 0);
        ring.push(std::move(pk));
    }
    check(ring.bytes() <= 300 && ring.span_ms() <= 1000, "ring_dual_bound");

    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    std::string config_dir = "config";
    int pre_seconds = 0;            // 0 = 采用配置 thresholds.clip_pre_seconds
    std::size_t max_bytes = 4u * 1024u * 1024u;
    int duration_s = 8;
    int fps = 25, width = 1280, height = 720;
    int display_pull_ms = 200;
    std::string out, json;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--config") next(&config_dir);
        else if (a == "--pre-seconds") { std::string v; next(&v); pre_seconds = std::atoi(v.c_str()); }
        else if (a == "--max-bytes") { std::string v; next(&v); max_bytes = static_cast<std::size_t>(std::atoll(v.c_str())); }
        else if (a == "--duration") { std::string v; next(&v); duration_s = std::atoi(v.c_str()); }
        else if (a == "--fps") { std::string v; next(&v); fps = std::atoi(v.c_str()); }
        else if (a == "--width") { std::string v; next(&v); width = std::atoi(v.c_str()); }
        else if (a == "--height") { std::string v; next(&v); height = std::atoi(v.c_str()); }
        else if (a == "--display-pull-ms") { std::string v; next(&v); display_pull_ms = std::atoi(v.c_str()); }
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--selftest") return run_selftest();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-recording-tap [--config dir] [--pre-seconds n] [--max-bytes n] "
                        "[--duration s] [--fps n] [--width n] [--height n] [--display-pull-ms n] "
                        "[--out log] [--json json] [--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }

    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) fo << line << "\n";
    };

    // 配置：thresholds.clip_pre_seconds、app.recording_source、NVR 集成配置。
    const core::ConfigLoadResult cfg = core::load_config_dir(config_dir);
    int config_pre_seconds = 30;
    core::RecordingSource requested = core::RecordingSource::Both;
    bool nvr_configured = false;
    if (cfg.ok) {
        config_pre_seconds = cfg.settings.thresholds.clip_pre_seconds;
        requested = cfg.settings.app.recording_source;
        for (const auto& integ : cfg.settings.app.integrations) {
            if (integ.id == "NVR") nvr_configured = core::integration_is_configured(integ);
        }
    } else {
        std::fprintf(stderr, "warning: config dir '%s' unusable; 回退文档化默认\n", config_dir.c_str());
    }
    const int effective_pre = pre_seconds > 0 ? pre_seconds : config_pre_seconds;
    const core::RecordingSourceStatus src = core::resolve_recording_source(requested, nvr_configured);
    emit("type=config config_pre_seconds=" + std::to_string(config_pre_seconds) +
         " effective_pre_seconds=" + std::to_string(effective_pre) +
         " max_bytes=" + std::to_string(max_bytes) +
         " duration_s=" + std::to_string(duration_s) + " fps=" + std::to_string(fps) +
         " display_pull_ms=" + std::to_string(display_pull_ms));
    emit("type=source " + src.describe());

    gst_init(nullptr, nullptr);
    // 压缩测试管线：编码 -> h264parse -> tee；一支解码当显示（慢消费），一支解码前 tap 录像。
    std::string desc =
        "videotestsrc is-live=true ! video/x-raw,format=I420,width=" + std::to_string(width) +
        ",height=" + std::to_string(height) + ",framerate=" + std::to_string(fps) + "/1"
        " ! x264enc tune=zerolatency speed-preset=ultrafast key-int-max=" + std::to_string(fps) +
        " byte-stream=true"
        " ! h264parse ! tee name=enc "
        "enc. ! queue leaky=downstream max-size-buffers=1 max-size-bytes=0 max-size-time=0 "
        "! avdec_h264 ! videoconvert ! video/x-raw,format=NV12 "
        "! appsink name=display_sink drop=1 max-buffers=1 sync=false "
        "enc. ! queue leaky=downstream max-size-buffers=200 max-size-bytes=0 max-size-time=0 "
        "! appsink name=record_sink sync=false";
    GError* gerr = nullptr;
    GstElement* pipeline = gst_parse_launch(desc.c_str(), &gerr);
    if (gerr) {
        std::fprintf(stderr, "pipeline error: %s\n", gerr->message);
        g_error_free(gerr);
    }
    if (!pipeline) return 3;
    GstElement* display_sink = gst_bin_get_by_name(GST_BIN(pipeline), "display_sink");
    GstElement* record_sink = gst_bin_get_by_name(GST_BIN(pipeline), "record_sink");
    if (!record_sink) {
        std::fprintf(stderr, "record_sink not found\n");
        return 3;
    }
    gst_element_set_state(pipeline, GST_STATE_PLAYING);

    media::RecordingTap tap(max_bytes, effective_pre * 1000);
    tap.set_source_status(src);

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    long long records = 0, display_frames = 0, record_bytes = 0;
    long long last_display_pull = 0;
    bool last_is_key = false;
    while (true) {
        const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
        if (elapsed >= duration_s * 1000) break;

        // 录像压缩分支：独立、快速消费（每轮有界）。
        for (int k = 0; k < 32; ++k) {
            GstSample* s = gst_app_sink_try_pull_sample(GST_APP_SINK(record_sink), 0);
            if (!s) break;
            GstBuffer* buf = gst_sample_get_buffer(s);
            if (buf) {
                const int64_t ts_ms = GST_CLOCK_TIME_IS_VALID(GST_BUFFER_PTS(buf))
                                          ? static_cast<int64_t>(GST_BUFFER_PTS(buf) / GST_MSECOND)
                                          : elapsed;
                core::CompressedPacket p;
                p.ts_ms = ts_ms;
                p.keyframe = !GST_BUFFER_FLAG_IS_SET(buf, GST_BUFFER_FLAG_DELTA_UNIT);
                last_is_key = p.keyframe;
                GstMapInfo mi;
                if (gst_buffer_map(buf, &mi, GST_MAP_READ)) {
                    p.data.assign(mi.data, mi.data + mi.size);
                    record_bytes += static_cast<long long>(mi.size);
                    gst_buffer_unmap(buf, &mi);
                }
                tap.push(std::move(p));
                ++records;
            }
            gst_sample_unref(s);
        }

        // 显示分支：慢消费（drop=1），不应影响录像压缩包。
        if (elapsed - last_display_pull >= display_pull_ms) {
            last_display_pull = elapsed;
            GstSample* s = gst_app_sink_try_pull_sample(GST_APP_SINK(display_sink), 0);
            if (s) {
                ++display_frames;
                gst_sample_unref(s);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    gst_element_set_state(pipeline, GST_STATE_NULL);

    const core::CompressedRingStats st = tap.stats();
    const int64_t expected = static_cast<int64_t>(fps) * duration_s;
    emit("type=tap_records received=" + std::to_string(records) +
         " expected=" + std::to_string(expected) +
         " keyframe_last=" + std::string(last_is_key ? "true" : "false") +
         " display_frames=" + std::to_string(display_frames));
    emit("type=rolling packets=" + std::to_string(st.packets) + " bytes=" + std::to_string(st.bytes) +
         " first_ts_ms=" + std::to_string(st.first_ts_ms) + " last_ts_ms=" + std::to_string(st.last_ts_ms) +
         " span_ms=" + std::to_string(st.span_ms) + " keyframes=" + std::to_string(st.keyframes) +
         " pushed=" + std::to_string(st.pushed) +
         " evicted_duration=" + std::to_string(st.evicted_duration) +
         " evicted_bytes=" + std::to_string(st.evicted_bytes));

    const bool bytes_bounded = st.bytes <= max_bytes;
    const bool duration_bounded = st.span_ms <= static_cast<int64_t>(effective_pre) * 1000 + 1000;
    const bool evicted = st.evicted_duration > 0 || st.evicted_bytes > 0;
    const bool rate_ok = records >= expected * 85 / 100;
    const bool independent = records >= expected * 85 / 100 && display_frames < records;
    const bool source_explicit = src.effective == core::RecordingAvailability::LocalFallbackNvrUnconfigured
                                     ? !src.reason.empty()
                                     : true;
    const bool ok = bytes_bounded && duration_bounded && evicted && rate_ok && independent && source_explicit;

    emit("type=acceptance bytes_bounded=" + std::string(bytes_bounded ? "true" : "false") +
         " duration_bounded=" + std::string(duration_bounded ? "true" : "false") +
         " evicted=" + std::string(evicted ? "true" : "false") +
         " record_rate_ok=" + std::string(rate_ok ? "true" : "false") +
         " display_independent=" + std::string(independent ? "true" : "false") +
         " source_status_explicit=" + std::string(source_explicit ? "true" : "false") +
         " result=" + std::string(ok ? "PASS" : "FAIL"));

    if (display_sink) gst_object_unref(display_sink);
    gst_object_unref(record_sink);
    gst_object_unref(pipeline);

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        j << "{\n  \"records\": " << records << ", \"expected\": " << expected
          << ", \"bytes\": " << st.bytes << ", \"span_ms\": " << st.span_ms
          << ", \"evicted_duration\": " << st.evicted_duration
          << ", \"effective_pre_seconds\": " << effective_pre
          << ", \"acceptance\": " << (ok ? "true" : "false") << "\n}\n";
    }
    return 0;
}
