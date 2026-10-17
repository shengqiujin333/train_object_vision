#include "railhmi/core/recording_source.h"

namespace railhmi {
namespace core {

bool integration_is_configured(const IntegrationConfig& cfg) {
    return cfg.has_protocol && cfg.has_channel_mapping;
}

const char* to_string(RecordingAvailability v) {
    switch (v) {
        case RecordingAvailability::LocalOnly: return "local";
        case RecordingAvailability::NvrOnly: return "nvr";
        case RecordingAvailability::Both: return "both";
        case RecordingAvailability::LocalFallbackNvrUnconfigured: return "local_fallback";
        case RecordingAvailability::Unavailable: return "unavailable";
    }
    return "?";
}

RecordingSourceStatus resolve_recording_source(RecordingSource requested, bool nvr_configured) {
    RecordingSourceStatus s;
    s.requested = requested;
    s.nvr_configured = nvr_configured;
    switch (requested) {
        case RecordingSource::Local:
            s.effective = RecordingAvailability::LocalOnly;
            s.local_enabled = true;
            s.nvr_enabled = false;
            return s;
        case RecordingSource::Nvr:
            if (nvr_configured) {
                s.effective = RecordingAvailability::NvrOnly;
                s.local_enabled = false;
                s.nvr_enabled = true;
            } else {
                // 请求 NVR 但未配置：回落本机保护片段（显式，不静默）。
                s.effective = RecordingAvailability::LocalFallbackNvrUnconfigured;
                s.local_enabled = true;
                s.nvr_enabled = false;
                s.reason = "NVR_UNCONFIGURED";
            }
            return s;
        case RecordingSource::Both:
        default:
            if (nvr_configured) {
                s.effective = RecordingAvailability::Both;
                s.local_enabled = true;
                s.nvr_enabled = true;
            } else {
                s.effective = RecordingAvailability::LocalFallbackNvrUnconfigured;
                s.local_enabled = true;
                s.nvr_enabled = false;
                s.reason = "NVR_UNCONFIGURED";
            }
            return s;
    }
}

std::string RecordingSourceStatus::describe() const {
    std::string out = "requested=";
    out += to_string(requested);
    out += " nvr_configured=";
    out += (nvr_configured ? "true" : "false");
    out += " effective=";
    out += to_string(effective);
    out += " local=";
    out += (local_enabled ? "true" : "false");
    out += " nvr=";
    out += (nvr_enabled ? "true" : "false");
    if (!reason.empty()) {
        out += " reason=";
        out += reason;
    }
    return out;
}

}  // namespace core
}  // namespace railhmi
