// railhmi 单路取流与状态机探针（ITEM-013 / 管线工厂 ITEM-002）
// 用 pipeline_factory 统一构建 rtsp(强制 UDP)/file/test 三类源管线，接入六态与质量标志，
// 输出 fps / 最近帧时间 / 重连次数 / presentable；`--selftest` 仅做纯字符串映射核对（不 spawn）。
//
// 用法:
//   railhmi-stream-probe --source test --duration 6 --cut-after 3 --out s.log --json s.json
//   railhmi-stream-probe --source file --file clip.mp4 --duration 6
//   railhmi-stream-probe --source rtsp --url 'rtsp://<cam>/...' --user admin --password-env VAR
//   railhmi-stream-probe --source rtsp --url 'rtsp://user:pass@<cam>/...' --decode h265
//   railhmi-stream-probe --selftest
//
// 退出码: 0 正常；1 selftest 失败；2 参数错误；3 启动失败。

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "railhmi/media/camera_stream.h"

namespace {

struct Sample {
    long long t_ms = 0;
    std::string state;
    double fps = 0.0;
    long long last_frame_ms = 0;
    int reconnect_count = 0;
    bool presentable = false;
};

std::string fmt_fps(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f", v);
    return b;
}

// 纯字符串映射自检（不 spawn GStreamer）：三类源 + auto/显式解码路径 + 脱敏。
int run_selftest() {
    using namespace railhmi::media;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    auto has = [](const std::string& s, const char* sub) {
        return s.find(sub) != std::string::npos;
    };

    CameraPipelineSpec t;
    t.source = PipelineSource::Test;
    const std::string tp = build_pipeline_description(t);
    check(has(tp, "videotestsrc"), "test_videotestsrc");
    check(has(tp, "format=NV12"), "test_nv12");

    CameraPipelineSpec f;
    f.source = PipelineSource::File;
    f.file = "/tmp/railhmi_probe.mp4";
    const std::string fp = build_pipeline_description(f);
    check(has(fp, "filesrc"), "file_filesrc");
    check(has(fp, "decodebin"), "file_decodebin");
    check(has(fp, "format=NV12"), "file_nv12");

    CameraPipelineSpec r;
    r.source = PipelineSource::Rtsp;
    r.url = "rtsp://admin:DUMMY_SECRET@192.168.1.65:554/cam/realmonitor?channel=1&subtype=0";
    r.decode = DecodeMode::Auto;
    const std::string ra = build_pipeline_description(r);
    check(has(ra, "protocols=udp"), "rtsp_udp");
    check(!has(ra, "protocols=tcp"), "rtsp_no_tcp");
    check(has(ra, "application/x-rtp,media=video"), "rtsp_video_only");
    check(has(ra, "decodebin"), "rtsp_auto_decodebin");
    check(has(ra, "user-id=\"admin\""), "rtsp_user_prop");
    check(has(ra, "user-pw=\"DUMMY_SECRET\""), "rtsp_pw_prop");
    check(!has(ra, "DUMMY_SECRET@"), "rtsp_no_creds_in_url");

    CameraPipelineSpec r5 = r;
    r5.decode = DecodeMode::ExplicitH265;
    const std::string rp5 = build_pipeline_description(r5);
    check(has(rp5, "rtph265depay") && has(rp5, "h265parse") && has(rp5, "mppvideodec"),
          "rtsp_explicit_h265");
    check(!has(rp5, "decodebin"), "rtsp_explicit_no_decodebin");

    CameraPipelineSpec r4 = r;
    r4.decode = DecodeMode::ExplicitH264;
    const std::string rp4 = build_pipeline_description(r4);
    check(has(rp4, "rtph264depay") && has(rp4, "h264parse") && has(rp4, "mppvideodec"),
          "rtsp_explicit_h264");

    check(!has(redact_secret(rp5, "DUMMY_SECRET"), "DUMMY_SECRET"), "redact_secret");

    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    media::CameraStreamConfig cfg;
    std::string src = "test";
    long long duration_s = 6;
    long long cut_after_s = -1;
    long long step_ms = 500;
    std::string out, json;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--source") next(&src);
        else if (a == "--file") next(&cfg.file);
        else if (a == "--url") next(&cfg.url);
        else if (a == "--camera") next(&cfg.camera_id);
        else if (a == "--duration") { std::string v; next(&v); duration_s = std::atoll(v.c_str()); }
        else if (a == "--cut-after") { std::string v; next(&v); cut_after_s = std::atoll(v.c_str()); }
        else if (a == "--step") { std::string v; next(&v); step_ms = std::atoll(v.c_str()); }
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--user") next(&cfg.username);
        else if (a == "--password-env") {
            std::string v; next(&v);
            const char* p = std::getenv(v.c_str());
            if (p) cfg.password = p;
        }
        else if (a == "--timeout-us") { std::string v; next(&v); cfg.timeout_us = std::atoi(v.c_str()); }
        else if (a == "--decode") {
            std::string v; next(&v);
            if (!media::parse_decode_mode(v, &cfg.decode)) {
                std::fprintf(stderr, "bad --decode '%s' (auto|h264|h265)\n", v.c_str());
                return 2;
            }
        }
        else if (a == "--selftest") return run_selftest();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-stream-probe --source test|file|rtsp [--file f] [--url u] "
                        "[--user u] [--password-env VAR] [--decode auto|h264|h265] [--timeout-us n] "
                        "[--duration s] [--cut-after s] [--step ms] [--out log] [--json json] [--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }
    if (src == "file") cfg.source = media::SourceKind::File;
    else if (src == "rtsp") cfg.source = media::SourceKind::Rtsp;
    else cfg.source = media::SourceKind::Test;

    media::CameraStream stream(cfg);
    std::string err;
    if (!stream.start(&err)) {
        std::fprintf(stderr, "stream start failed: %s\n", err.c_str());
        return 3;
    }

    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) {
        std::printf("%s\n", line.c_str());
        if (fo) fo << line << "\n";
    };

    emit("type=injection source=" + src + " camera=" + cfg.camera_id +
         " duration_s=" + std::to_string(duration_s) +
         " cut_after_s=" + std::to_string(cut_after_s) +
         " transport=" + std::string(cfg.source == media::SourceKind::Rtsp ? "udp" : "n/a"));

    std::vector<Sample> samples;
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    long long prev_slot = -1;
    bool cut_done = false;

    while (true) {
        const long long elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
        if (elapsed >= duration_s * 1000) break;
        if (cut_after_s >= 0 && !cut_done && elapsed >= cut_after_s * 1000) {
            stream.simulate_cut();
            cut_done = true;
            emit("t=" + std::to_string(elapsed) + " type=injection cut=FI-01 (simulated stream cut)");
        }
        stream.pump(200);
        const long long slot = elapsed / step_ms;
        if (slot != prev_slot) {
            prev_slot = slot;
            media::StreamStats s = stream.stats();
            Sample smp;
            smp.t_ms = elapsed;
            smp.state = railhmi::core::to_string(s.state);
            smp.fps = s.fps;
            smp.last_frame_ms = s.last_frame_ms;
            smp.reconnect_count = s.reconnect_count;
            smp.presentable = s.presentable;
            samples.push_back(smp);
            emit("t=" + std::to_string(elapsed) + " type=stream_state state=" + smp.state +
                 " fps=" + fmt_fps(s.fps) + " last_frame_ms=" + std::to_string(s.last_frame_ms) +
                 " reconnect_count=" + std::to_string(s.reconnect_count) +
                 " presentable=" + std::string(s.presentable ? "true" : "false") +
                 (s.last_error.empty() ? "" : " error=" + s.last_error));
        }
    }

    media::StreamStats s = stream.stats();
    emit("type=summary final_state=" + std::string(railhmi::core::to_string(s.state)) +
         " reconnect_count=" + std::to_string(s.reconnect_count) +
         " last_frame_ms=" + std::to_string(s.last_frame_ms) +
         " presentable=" + std::string(s.presentable ? "true" : "false"));
    stream.stop();

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        j << "[\n";
        for (std::size_t i = 0; i < samples.size(); ++i) {
            const auto& x = samples[i];
            j << "  {\"t_ms\": " << x.t_ms << ", \"state\": \"" << x.state << "\", \"fps\": " << x.fps
              << ", \"last_frame_ms\": " << x.last_frame_ms
              << ", \"reconnect_count\": " << x.reconnect_count
              << ", \"presentable\": " << (x.presentable ? "true" : "false") << "}";
            j << (i + 1 < samples.size() ? ",\n" : "\n");
        }
        j << "]\n";
    }
    return 0;
}
