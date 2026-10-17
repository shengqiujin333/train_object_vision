// railhmi-core 录像来源策略测试（视频 ITEM-011 纯逻辑）。
// recording_source 解析 + NVR 未配置时显式回落本机（reason 可观察）。

#include "test_util.h"

#include "railhmi/core/recording_source.h"

namespace {

using namespace railhmi::core;

IntegrationConfig nvr(bool proto, bool mapping) {
    IntegrationConfig c;
    c.id = "NVR";
    c.name = "网络录像机";
    c.registered = true;
    c.has_protocol = proto;
    c.has_channel_mapping = mapping;
    return c;
}

void test_integration_is_configured() {
    railhmi_test::check(!integration_is_configured(nvr(false, false)), "nvr unconfigured");
    railhmi_test::check(!integration_is_configured(nvr(true, false)), "nvr proto only");
    railhmi_test::check(integration_is_configured(nvr(true, true)), "nvr configured");
}

void test_local_only() {
    const RecordingSourceStatus s = resolve_recording_source(RecordingSource::Local, false);
    railhmi_test::check(s.effective == RecordingAvailability::LocalOnly, "local effective");
    railhmi_test::check(s.local_enabled && !s.nvr_enabled && s.reason.empty(), "local flags");
}

void test_nvr_unconfigured_fallback() {
    const RecordingSourceStatus s = resolve_recording_source(RecordingSource::Both, false);
    railhmi_test::check(s.effective == RecordingAvailability::LocalFallbackNvrUnconfigured,
                        "both unconfigured -> fallback");
    railhmi_test::check(s.local_enabled && !s.nvr_enabled, "fallback local enabled");
    railhmi_test::check(s.reason == "NVR_UNCONFIGURED", "fallback reason explicit");

    const RecordingSourceStatus sn = resolve_recording_source(RecordingSource::Nvr, false);
    railhmi_test::check(sn.effective == RecordingAvailability::LocalFallbackNvrUnconfigured,
                        "nvr unconfigured -> fallback");
    railhmi_test::check(!sn.reason.empty(), "nvr fallback reason");
}

void test_nvr_configured() {
    const RecordingSourceStatus both = resolve_recording_source(RecordingSource::Both, true);
    railhmi_test::check(both.effective == RecordingAvailability::Both && both.nvr_enabled &&
                            both.local_enabled && both.reason.empty(),
                        "both configured");
    const RecordingSourceStatus nvr_only = resolve_recording_source(RecordingSource::Nvr, true);
    railhmi_test::check(nvr_only.effective == RecordingAvailability::NvrOnly && nvr_only.nvr_enabled &&
                            !nvr_only.local_enabled,
                        "nvr configured -> nvr only");
}

void test_describe_is_observable() {
    const RecordingSourceStatus s = resolve_recording_source(RecordingSource::Both, false);
    const std::string d = s.describe();
    railhmi_test::check(d.find("NVR_UNCONFIGURED") != std::string::npos, "describe reason");
    railhmi_test::check(d.find("local_fallback") != std::string::npos, "describe effective");
}

}  // namespace

void run_recording_source_tests() {
    test_integration_is_configured();
    test_local_only();
    test_nvr_unconfigured_fallback();
    test_nvr_configured();
    test_describe_is_observable();
}
