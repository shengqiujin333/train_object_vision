// railhmi-core 视频诊断与脱敏测试（视频 ITEM-016 纯逻辑）。

#include "test_util.h"

#include "railhmi/core/video_diagnostics.h"

namespace {

using namespace railhmi::core;

VideoChannelMetrics live_metrics() {
    VideoChannelMetrics m;
    m.camera_id = "CAM-01";
    m.state = "live";
    m.fps = 25.0;
    m.last_frame_mono_ms = 1000;
    m.last_frame_wall_ms = 1700000000000LL;
    m.attempt_id = make_attempt_id("CAM-01", 1);
    return m;
}

void test_presence_tristate() {
    const VideoChannelMetrics base = live_metrics();
    railhmi_test::check(classify_presence(base) == DiagnosticPresence::Online, "presence online");

    VideoChannelMetrics gap = base;
    gap.gap_count = 1;
    railhmi_test::check(classify_presence(gap) == DiagnosticPresence::Gap, "presence gap_count");

    VideoChannelMetrics frozen = base;
    frozen.state = "frozen";
    railhmi_test::check(classify_presence(frozen) == DiagnosticPresence::Gap, "presence frozen");

    VideoChannelMetrics unknown;
    unknown.camera_id = "CAM-03";
    unknown.state = "offline";
    railhmi_test::check(classify_presence(unknown) == DiagnosticPresence::Unverified, "presence unverified");

    railhmi_test::check(std::string(presence_label(DiagnosticPresence::Online)) == "在线" &&
                            std::string(presence_label(DiagnosticPresence::Unverified)) == "未核验" &&
                            std::string(presence_label(DiagnosticPresence::Gap)) == "缺口",
                        "presence labels");
    railhmi_test::check(std::string(to_string(DiagnosticPresence::Gap)) == "gap", "presence token");
}

void test_attempt_id() {
    railhmi_test::check(make_attempt_id("CAM-01", 2) == "CAM-01#g2", "attempt id");
    railhmi_test::check(make_attempt_id("CAM-01", 0) == "none", "attempt id none");
}

void test_redaction() {
    const std::string userinfo = "open rtsp://admin:S3CRET@192.168.1.65:554/x failed";
    railhmi_test::check(contains_credentials(userinfo), "userinfo detected");
    std::string r = redact_error(userinfo);
    railhmi_test::check(r.find("S3CRET") == std::string::npos && r.find("admin:") == std::string::npos &&
                            r.find("***@") != std::string::npos,
                        "userinfo redacted");

    const std::string kv = "connect error password=S3CRET token=abc user=admin";
    r = redact_error(kv);
    railhmi_test::check(r.find("S3CRET") == std::string::npos && r.find("abc") == std::string::npos &&
                            r.find("password=***") != std::string::npos,
                        "kv redacted");

    const std::string loc = "pipeline location=\"rtsp://admin:S3CRET@h/x\" ! decodebin";
    r = redact_error(loc);
    railhmi_test::check(r.find("S3CRET") == std::string::npos && r.find("location=\"***\"") != std::string::npos,
                        "location redacted");

    const std::string pipe = "rtspsrc location=x ! decodebin error";
    railhmi_test::check(contains_credentials(pipe), "pipeline detected");
    r = redact_error(pipe);
    railhmi_test::check(r == "<redacted>" && !contains_credentials(r), "pipeline whole redacted");

    const std::string benign = "timeout waiting for first frame";
    railhmi_test::check(!contains_credentials(benign) && redact_error(benign) == benign, "benign untouched");
}

void test_metric_json() {
    const std::string line = video_metric_json_line(live_metrics(), 1700000000000LL);
    railhmi_test::check(line.find("\"camera\":\"CAM-01\"") != std::string::npos, "json camera");
    railhmi_test::check(line.find("\"presence\":\"online\"") != std::string::npos, "json presence");
    railhmi_test::check(line.find("\"display_dropped\":\"0\"") != std::string::npos &&
                            line.find("\"ai_dropped\":\"0\"") != std::string::npos &&
                            line.find("\"recording_overflow\":\"0\"") != std::string::npos &&
                            line.find("\"gap_count\":\"0\"") != std::string::npos &&
                            line.find("\"attempt_id\":\"CAM-01#g1\"") != std::string::npos,
                        "json fields");
    // last_error 在记录中已脱敏。
    VideoChannelMetrics m = live_metrics();
    m.last_error = "rtsp://admin:S3CRET@h/x password=S3CRET";
    const std::string l2 = video_metric_json_line(m, 1700000000000LL);
    railhmi_test::check(l2.find("S3CRET") == std::string::npos, "json last_error redacted");
}

}  // namespace

void run_video_diagnostics_tests() {
    test_presence_tristate();
    test_attempt_id();
    test_redaction();
    test_metric_json();
}
