// railhmi-core 预算测量装置测试（视频 ITEM-017 纯逻辑）。

#include "test_util.h"

#include "railhmi/core/metrics.h"

namespace {

using namespace railhmi::core;

void test_histogram() {
    LatencyHistogram h;
    for (int i = 1; i <= 100; ++i) h.add(i);
    railhmi_test::check(h.count() == 100, "count");
    railhmi_test::check(h.min_us() == 1 && h.max_us() == 100, "min/max");
    railhmi_test::check(h.percentile(0) == 1 && h.percentile(100) == 100, "bounds");
    railhmi_test::check(h.percentile(50) == 50 && h.percentile(90) == 90 && h.percentile(99) == 99, "p50/p90/p99");
    railhmi_test::check(h.mean_us() > 50.0 && h.mean_us() < 51.0, "mean");
    h.clear();
    railhmi_test::check(h.count() == 0 && h.percentile(50) == -1 && h.min_us() == -1, "empty");
}

void test_histogram_bounded() {
    LatencyHistogram h(10);
    for (int i = 1; i <= 20; ++i) h.add(i);
    railhmi_test::check(h.count() == 10, "bounded window");
    railhmi_test::check(h.min_us() == 11 && h.max_us() == 20, "bounded keeps newest");
}

void test_source_and_status() {
    railhmi_test::check(std::string(to_string(SourceClass::Rtsp)) == "rtsp", "source rtsp");
    railhmi_test::check(std::string(to_string(SourceClass::File)) == "file", "source file");
    railhmi_test::check(std::string(to_string(SourceClass::Simulation)) == "simulation", "source sim");
    railhmi_test::check(std::string(to_string(MeasurementStatus::Recorded)) == "recorded", "status recorded");
    railhmi_test::check(std::string(to_string(MeasurementStatus::ToBeExecuted)) == "to_be_executed",
                        "status to_be_executed");
}

void test_measurement_json() {
    MeasurementRecord r;
    r.source_class = SourceClass::Simulation;
    r.status = MeasurementStatus::Recorded;
    r.label = "test";
    r.window_s = 6.0;
    r.throughput.source_fps = 25.0;
    r.throughput.display_fps = 24.5;
    r.throughput.cpu_pct = 12.5;
    r.throughput.rss_kb = 20000;
    r.throughput.npu_pct = -1.0;
    r.throughput.npu_note = "unavailable:needs_privileged_rknpu_load";
    r.throughput.temp_c = 45.0;
    for (int i = 1; i <= 50; ++i) r.end_to_end.add(i * 100);
    r.sync.available = true;
    r.sync.in_sync = true;
    r.sync.max_deviation_ms = 10;
    r.storage.clip_bytes = 1300000;
    r.storage.measured_bitrate_bps = 1700000;
    r.storage.free_bytes = 30000000000LL;
    r.storage.total_bytes = 60000000000LL;
    r.recovery.attempts = 1;
    r.recovery.last_recovery_ms = 320;

    const std::string j = measurement_json(r);
    railhmi_test::check(j.find("\"source_class\":\"simulation\"") != std::string::npos, "json class");
    railhmi_test::check(j.find("\"status\":\"recorded\"") != std::string::npos, "json status");
    railhmi_test::check(j.find("\"end_to_end\":{\"n\":50") != std::string::npos &&
                            j.find("\"p50\":") != std::string::npos && j.find("\"p99\":") != std::string::npos,
                        "json latency percentiles");
    railhmi_test::check(j.find("\"clip_bytes\":1300000") != std::string::npos &&
                            j.find("\"free_bytes\":30000000000") != std::string::npos,
                        "json storage");
    railhmi_test::check(j.find("\"last_recovery_ms\":320") != std::string::npos, "json recovery");
    railhmi_test::check(j.find("rknpu") != std::string::npos, "json npu note");

    MeasurementRecord tbe;
    tbe.source_class = SourceClass::Rtsp;
    tbe.status = MeasurementStatus::ToBeExecuted;
    tbe.reason = "CREDENTIAL_MISSING";
    const std::vector<MeasurementRecord> arch{tbe};
    const std::string a = measurements_archive_json(arch);
    railhmi_test::check(a.find("\"schema\":\"railhmi.measure.v1\"") != std::string::npos &&
                            a.find("\"status\":\"to_be_executed\"") != std::string::npos,
                        "archive json");
}

}  // namespace

void run_metrics_tests() {
    test_histogram();
    test_histogram_bounded();
    test_source_and_status();
    test_measurement_json();
}
