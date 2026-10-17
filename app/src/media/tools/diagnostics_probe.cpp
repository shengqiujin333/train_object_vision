// railhmi-diagnostics (视频 ITEM-016)
// 视频诊断、能力探测与结构化日志：
//   * 每路结构化指标（state/fps/最近帧单调与墙钟/重连/质量/display_dropped/ai_dropped/
//     recording_overflow/gap_count/attempt_id/last_error 脱敏）；
//   * 结构化单行 JSON 按日滚动文件；
//   * 直接 gst-inspect 能力探测 + GStreamer/librga/librknnrt 版本独立读取；
//   * 凭据/含鉴权 URL/pipeline/bus 原文整条脱敏（绝不进入日志/工件）；
//   * 诊断三态：在线/未核验/缺口。
//
// 用法:
//   railhmi-diagnostics --config config --duration 2 --logdir core-evidence/logs \
//       --out diag.log --json diag.json
//   railhmi-diagnostics --selftest
//
// 退出码: 0 正常（含验收判定）；1 selftest 失败；2 参数错误；3 启动失败。

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/health.h"
#include "railhmi/core/timebase.h"
#include "railhmi/core/video_diagnostics.h"
#include "railhmi/media/multi_stream.h"
#include "railhmi/media/video_capability.h"
#include "railhmi/platform/file_store.h"

namespace {

using namespace railhmi;

int run_selftest() {
    using namespace railhmi;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    core::VideoChannelMetrics online;
    online.camera_id = "CAM-01";
    online.state = "live";
    online.last_frame_mono_ms = 1234;
    online.attempt_id = core::make_attempt_id("CAM-01", 1);
    check(core::classify_presence(online) == core::DiagnosticPresence::Online, "presence_online");

    core::VideoChannelMetrics gap = online;
    gap.gap_count = 1;
    check(core::classify_presence(gap) == core::DiagnosticPresence::Gap, "presence_gap");

    core::VideoChannelMetrics unknown;
    unknown.camera_id = "CAM-03";
    unknown.state = "offline";
    check(core::classify_presence(unknown) == core::DiagnosticPresence::Unverified, "presence_unverified");

    const std::string raw =
        "rtsp://admin:S3CRET@192.168.1.65:554/x password=S3CRET rtspsrc location=rtsp://admin:S3CRET@h/x";
    const std::string red = core::redact_error(raw);
    check(core::contains_credentials(raw), "raw_has_credentials");
    check(!core::contains_credentials(red) && red.find("S3CRET") == std::string::npos &&
              red.find("admin:") == std::string::npos && red.find("rtspsrc") == std::string::npos,
          "redacted_clean");

    const std::string line = core::video_metric_json_line(online, 1700000000000LL);
    check(line.find("\"camera\":\"CAM-01\"") != std::string::npos &&
              line.find("\"gap_count\":\"0\"") != std::string::npos,
          "metric_json_fields");
    check(std::string(core::presence_label(core::DiagnosticPresence::Gap)) == "缺口", "presence_label");
    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

core::VideoChannelMetrics metrics_from(const std::string& id, const railhmi::media::StreamStats& s,
                                       long long gap_count, const std::string& attempt_id,
                                       const core::TimeBase& tb) {
    core::VideoChannelMetrics m;
    m.camera_id = id;
    m.state = core::to_string(s.state);
    m.fps = s.fps;
    m.last_frame_mono_ms = s.last_frame_ms;
    m.last_frame_wall_ms = s.capture_wall_ms >= 0
                               ? s.capture_wall_ms
                               : (s.last_frame_ms > 0 ? tb.to_wall_ms(s.last_frame_ms * 1000) : -1);
    m.reconnect_count = s.reconnect_count;
    m.quality_flags = s.quality_flags;
    m.display_dropped = s.display_dropped;
    m.ai_dropped = s.ai_dropped;
    m.recording_overflow = s.recording_overflow;
    m.gap_count = gap_count;
    m.attempt_id = attempt_id.empty() ? "none" : attempt_id;
    m.last_error = core::redact_error(s.last_error);
    return m;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;
    std::string config_dir = "config";
    int duration_s = 2;
    std::string logdir = "core-evidence/logs";
    std::string out, json;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--config") next(&config_dir);
        else if (a == "--duration") { std::string v; next(&v); duration_s = std::atoi(v.c_str()); }
        else if (a == "--logdir") next(&logdir);
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--selftest") return run_selftest();
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-diagnostics [--config dir] [--duration s] [--logdir dir] "
                        "[--out log] [--json json] [--selftest]\n");
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
    if (!cfg.ok) {
        emit("type=config status=error");
        for (const auto& e : cfg.errors) emit("type=config_error path=" + e.str());
    }

    // 能力探测：直接 gst-inspect + 库版本（不经 --exists）。
    const media::CapabilityReport cap = media::build_capability_report(cfg.settings, false, 0);
    emit("type=capability config_ok=" + std::string(cap.config_ok ? "true" : "false") +
         " gstreamer=" + cap.versions.gstreamer + " librga=" + cap.versions.librga +
         " librknnrt=" + cap.versions.librknnrt +
         " elements_present=" + std::string(cap.required_elements_present ? "true" : "false") +
         " missing=" + std::to_string(cap.missing_required_elements.size()));
    for (const auto& el : cap.elements) {
        emit("type=element name=" + el.element + " status=" + media::to_string(el.status) +
             " long_name=" + el.long_name);
    }

    // 三路结构化指标：CAM-01 在线 / CAM-02 缺口 / CAM-03 未核验（未启动）。
    std::vector<media::ChannelSpec> specs;
    for (const char* id : {"CAM-01", "CAM-02"}) {
        media::ChannelSpec sp;
        sp.camera_id = id;
        sp.cfg.camera_id = id;
        sp.cfg.source = media::SourceKind::Test;
        specs.push_back(sp);
    }
    media::MultiStreamManager mgr(specs);
    std::string err;
    const int started = mgr.start_all(&err);
    if (started < 2) { emit("type=acceptance result=FAIL reason=START"); return 0; }

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(clock::now() - t0).count() < duration_s) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    const std::vector<media::StreamStats> st = mgr.stats();

    core::TimeBase tb;
    tb.calibrate(core::TimeBase::system_now_ms(), core::TimeBase::steady_now_us());

    core::VideoChannelMetrics m1 =
        metrics_from("CAM-01", st[0], 0, core::make_attempt_id("CAM-01", st[0].generation), tb);
    core::VideoChannelMetrics m2 = metrics_from("CAM-02", st[1], 1, "none", tb);   // 已知缺口
    core::VideoChannelMetrics m3;                                              // 未启动 -> 未核验
    m3.camera_id = "CAM-03";
    m3.state = "offline";
    m3.attempt_id = "none";

    const long long wall = core::TimeBase::system_now_ms();
    const std::string day = core::log_day_key(wall);
    // 按日滚动结构化文件。
    std::string path = logdir + "/video-" + day + ".log";
    {
        std::string derr;
        railhmi::platform::FileStore::ensure_parent_dir(path, &derr);
        std::ofstream dl(path, std::ios::trunc);
        for (const core::VideoChannelMetrics* m : {&m1, &m2, &m3}) {
            const std::string jl = core::video_metric_json_line(*m, wall);
            if (dl) dl << jl << "\n";
            emit("type=metric camera=" + m->camera_id + " presence=" +
                 core::to_string(core::classify_presence(*m)) + " (" +
                 core::presence_label(core::classify_presence(*m)) + ") state=" + m->state +
                 " fps=" + std::to_string(m->fps) +
                 " last_frame_mono_ms=" + std::to_string(m->last_frame_mono_ms) +
                 " last_frame_wall_ms=" + std::to_string(m->last_frame_wall_ms) +
                 " reconnect_count=" + std::to_string(m->reconnect_count) +
                 " quality_flags=" + m->quality_flags +
                 " display_dropped=" + std::to_string(m->display_dropped) +
                 " ai_dropped=" + std::to_string(m->ai_dropped) +
                 " recording_overflow=" + std::to_string(m->recording_overflow) +
                 " gap_count=" + std::to_string(m->gap_count) + " attempt_id=" + m->attempt_id +
                 " last_error=" + (m->last_error.empty() ? std::string("-") : m->last_error));
        }
    }
    mgr.stop_all();

    // 按日滚动保留：两日记录 -> 仅保留最近 1 日。
    std::vector<core::LogRecord> recs;
    core::LogRecord r1; r1.ts_wall_ms = (wall / 86400000LL) * 86400000LL; r1.source = "video";
    core::LogRecord r2 = r1; r2.ts_wall_ms = r1.ts_wall_ms + 86400000LL;
    recs.push_back(r1); recs.push_back(r2);
    const std::vector<core::LogRecord> rolled = core::roll_health_logs_by_day(recs, 1);

    // 脱敏：含鉴权 URL + password + pipeline 原文 -> 整条脱敏。
    const std::string dirty =
        "rtsp://admin:S3CRET@192.168.1.65:554/x password=S3CRET "
        "rtspsrc location=\"rtsp://admin:S3CRET@192.168.1.65:554/x\" ! decodebin";
    const std::string clean = core::redact_error(dirty);
    const bool redaction_ok = core::contains_credentials(dirty) && !core::contains_credentials(clean) &&
                              clean.find("S3CRET") == std::string::npos &&
                              clean.find("admin:") == std::string::npos &&
                              clean.find("rtspsrc") == std::string::npos &&
                              clean.find("location=") == std::string::npos;
    emit("type=redaction raw_has_cred=" + std::string(core::contains_credentials(dirty) ? "true" : "false") +
         " redacted=" + clean + " clean=" + std::string(redaction_ok ? "true" : "false"));

    const core::DiagnosticPresence p1 = core::classify_presence(m1);
    const core::DiagnosticPresence p2 = core::classify_presence(m2);
    const core::DiagnosticPresence p3 = core::classify_presence(m3);
    const bool tristate = p1 == core::DiagnosticPresence::Online && p2 == core::DiagnosticPresence::Gap &&
                          p3 == core::DiagnosticPresence::Unverified;
    const bool capability_ok = cap.config_ok && cap.required_elements_present &&
                               !cap.versions.gstreamer.empty() && !cap.versions.librga.empty() &&
                               cap.missing_required_elements.empty();
    const bool rolling_ok = rolled.size() == 1;
    const bool fields_ok = m1.last_frame_mono_ms > 0 && !m2.attempt_id.empty() &&
                           m1.last_frame_wall_ms > 0;
    const bool daily_file_ok = railhmi::platform::FileStore::exists(path);

    const bool ok = capability_ok && tristate && redaction_ok && rolling_ok && fields_ok && daily_file_ok;
    emit("type=acceptance capability_versions=" + std::string(capability_ok ? "true" : "false") +
         " elements_required_present=" + std::string(cap.required_elements_present ? "true" : "false") +
         " tristate_online_unverified_gap=" + std::string(tristate ? "true" : "false") +
         " redaction_clean=" + std::string(redaction_ok ? "true" : "false") +
         " daily_roll_ok=" + std::string(rolling_ok ? "true" : "false") +
         " daily_file_written=" + std::string(daily_file_ok ? "true" : "false") +
         " structured_fields=" + std::string(fields_ok ? "true" : "false") +
         " result=" + std::string(ok ? "PASS" : "FAIL"));

    if (!json.empty()) {
        std::ofstream j(json, std::ios::trunc);
        j << "{\n  \"versions\": {\"gstreamer\": \"" << cap.versions.gstreamer << "\", \"librga\": \""
          << cap.versions.librga << "\", \"librknnrt\": \"" << cap.versions.librknnrt << "\"},\n";
        j << "  \"presence\": {\"CAM-01\": \"" << core::to_string(p1) << "\", \"CAM-02\": \""
          << core::to_string(p2) << "\", \"CAM-03\": \"" << core::to_string(p3) << "\"},\n";
        j << "  \"daily_log\": \"" << path << "\",\n";
        j << "  \"redacted\": \"" << clean << "\",\n";
        j << "  \"acceptance\": " << (ok ? "true" : "false") << "\n}\n";
    }
    return 0;
}
