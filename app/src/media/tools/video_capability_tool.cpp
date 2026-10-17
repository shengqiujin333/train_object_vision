// railhmi-video-capability (视频 ITEM-001)
// 视频源能力探测与配置绑定的运行入口。
//
// 用法:
//   railhmi-video-capability --config ./config [--json cap.json] [--out cap.log]
//       [--probe-caps] [--caps-timeout-ms 20000] [--camera CAM-01 ...]
//
// 行为:
//   * 从配置目录加载 cameras/app/thresholds/roi（缺字段报具体字段路径）。
//   * 直接 gst-inspect-1.0 <element> 判定元素可用性（不使用 --exists）。
//   * 记录 GStreamer / librga / librknnrt 版本。
//   * 逐路输出能力记录；缺凭据 -> CREDENTIAL_MISSING；缺元素 -> 显式列出元素名。
//   * 报告不含任何凭据值。
//
// 退出码: 0 报告产出；2 参数/配置错误；3 必需 GStreamer 元素缺失。

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/media/video_capability.h"

namespace {

std::string arg_value(int argc, char** argv, int* i) {
    if (*i + 1 < argc) return argv[++(*i)];
    return "";
}

int run_selftest() {
    using namespace railhmi;
    int failures = 0;
    auto check = [&](bool ok, const char* name) {
        std::printf("selftest %s %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };

    media::ElementProbeResult r;
    const std::string good =
        "Factory Details:\n  Rank  primary + 1 (257)\n  Long-name  Rockchip's MPP video decoder\n";
    const bool present = media::parse_gst_inspect("mppvideodec", good, 0, &r);
    check(present && r.status == media::ElementStatus::Present, "parse_present");
    check(r.long_name == "Rockchip's MPP video decoder", "parse_long_name");

    const std::string missing = "No such element or plugin 'mppvideodec'\n";
    const bool absent = media::parse_gst_inspect("mppvideodec", missing, 255, &r);
    check(!absent && r.status == media::ElementStatus::Absent, "parse_absent");
    check(r.detail.find("No such element") != std::string::npos, "parse_absent_detail");

    check(media::evaluate_readiness(media::CredentialStatus::Missing, {}) ==
              "blocked_credential_missing",
          "readiness_credential_missing");
    check(media::evaluate_readiness(media::CredentialStatus::Present, {"mppvideodec"}) ==
              "blocked_element_missing:mppvideodec",
          "readiness_element_missing");
    check(media::evaluate_readiness(media::CredentialStatus::Present, {}) == "ready",
          "readiness_ready");

    const auto req = media::required_gstreamer_elements();
    const bool has_mpp = std::find(req.begin(), req.end(), "mppvideodec") != req.end();
    const bool has_rtsp = std::find(req.begin(), req.end(), "rtspsrc") != req.end();
    check(!req.empty() && has_mpp && has_rtsp, "required_elements");

    std::printf("selftest %s (%d failure(s))\n", failures == 0 ? "OK" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    using namespace railhmi;

    std::string config_dir = "./config";
    std::string json_path;
    std::string log_path;
    bool probe_caps = false;
    int caps_timeout_ms = 20000;
    std::vector<std::string> only_cameras;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--selftest") return run_selftest();
        if (a == "--config") config_dir = arg_value(argc, argv, &i);
        else if (a.rfind("--config=", 0) == 0) config_dir = a.substr(9);
        else if (a == "--json") json_path = arg_value(argc, argv, &i);
        else if (a == "--out") log_path = arg_value(argc, argv, &i);
        else if (a == "--probe-caps") probe_caps = true;
        else if (a == "--caps-timeout-ms") caps_timeout_ms = std::atoi(arg_value(argc, argv, &i).c_str());
        else if (a == "--camera") only_cameras.push_back(arg_value(argc, argv, &i));
        else if (a == "--help" || a == "-h") {
            std::printf(
                "usage: railhmi-video-capability --config <dir> [--json f] [--out f] "
                "[--probe-caps] [--caps-timeout-ms n] [--camera ID]... [--selftest]\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }
    if (config_dir.empty()) {
        std::fprintf(stderr, "--config requires a directory\n");
        return 2;
    }
    if (caps_timeout_ms < 1000) caps_timeout_ms = 1000;

    core::ConfigLoadResult loaded = core::load_config_dir(config_dir);
    if (!loaded.ok) {
        std::fprintf(stderr, "config load failed (%zu error(s)):\n", loaded.errors.size());
        for (const auto& e : loaded.errors) std::fprintf(stderr, "  %s\n", e.str().c_str());
        return 2;
    }

    core::AppSettings settings = loaded.settings;
    if (!only_cameras.empty()) {
        std::vector<core::CameraConfig> filtered;
        for (const auto& c : settings.cameras) {
            if (std::find(only_cameras.begin(), only_cameras.end(), c.id) != only_cameras.end())
                filtered.push_back(c);
        }
        if (filtered.size() != only_cameras.size()) {
            std::fprintf(stderr, "requested camera id not found in registry\n");
            return 2;
        }
        settings.cameras = filtered;
    }

    media::CapabilityReport report = media::build_capability_report(settings, probe_caps, caps_timeout_ms);

    const std::string text = media::capability_report_to_text(report);
    const std::string json = media::capability_report_to_json(report);
    std::fputs(text.c_str(), stdout);

    if (!log_path.empty()) {
        std::ofstream f(log_path, std::ios::trunc);
        if (!f) {
            std::fprintf(stderr, "cannot write --out %s\n", log_path.c_str());
            return 2;
        }
        f << text;
    }
    if (!json_path.empty()) {
        std::ofstream f(json_path, std::ios::trunc);
        if (!f) {
            std::fprintf(stderr, "cannot write --json %s\n", json_path.c_str());
            return 2;
        }
        f << json;
    }

    if (!report.required_elements_present) {
        std::fprintf(stderr, "required GStreamer elements missing:");
        for (const auto& m : report.missing_required_elements) std::fprintf(stderr, " %s", m.c_str());
        std::fprintf(stderr, "\n");
        return 3;
    }
    return 0;
}
