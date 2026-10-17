#include "railhmi/core/video_diagnostics.h"

#include <cctype>
#include <vector>

namespace railhmi {
namespace core {

namespace {

bool contains(const std::string& hay, const char* needle) { return hay.find(needle) != std::string::npos; }

// 把 key 之后的值（到 & / 空白 / 引号 / 行尾）替换为 ***。
void mask_value_after(std::string& s, const std::string& key) {
    std::size_t pos = 0;
    while ((pos = s.find(key, pos)) != std::string::npos) {
        std::size_t v = pos + key.size();
        if (v >= s.size()) break;
        if (s[v] == '"') {
            std::size_t end = s.find('"', v + 1);
            if (end == std::string::npos) { s.erase(v + 1); break; }
            s.replace(v + 1, end - v - 1, "***");
            pos = v + 4;
        } else {
            std::size_t end = v;
            while (end < s.size() && s[end] != '&' && s[end] != ' ' && s[end] != '\t' && s[end] != ')' &&
                   s[end] != ',')
                ++end;
            s.replace(v, end - v, "***");
            pos = v + 3;
        }
    }
}

}  // namespace

const char* to_string(DiagnosticPresence p) {
    switch (p) {
        case DiagnosticPresence::Online: return "online";
        case DiagnosticPresence::Unverified: return "unverified";
        case DiagnosticPresence::Gap: return "gap";
    }
    return "unverified";
}

const char* presence_label(DiagnosticPresence p) {
    switch (p) {
        case DiagnosticPresence::Online: return "在线";
        case DiagnosticPresence::Unverified: return "未核验";
        case DiagnosticPresence::Gap: return "缺口";
    }
    return "未核验";
}

DiagnosticPresence classify_presence(const VideoChannelMetrics& m) {
    if (m.gap_count > 0 || m.state == "frozen") return DiagnosticPresence::Gap;
    if ((m.state == "live" || m.state == "degraded") && m.last_frame_mono_ms > 0)
        return DiagnosticPresence::Online;
    return DiagnosticPresence::Unverified;
}

std::string make_attempt_id(const std::string& camera_id, unsigned long long generation) {
    if (generation == 0) return "none";
    return camera_id + "#g" + std::to_string(generation);
}

LogRecord video_metric_log_record(const VideoChannelMetrics& m, long long ts_wall_ms) {
    LogRecord r;
    r.ts_wall_ms = ts_wall_ms;
    r.severity = LogSeverity::Info;
    r.source = "video";
    r.message = "channel_metric";
    r.fields["camera"] = m.camera_id;
    r.fields["presence"] = to_string(classify_presence(m));
    r.fields["state"] = m.state;
    r.fields["fps"] = std::to_string(m.fps);
    r.fields["last_frame_mono_ms"] = std::to_string(m.last_frame_mono_ms);
    r.fields["last_frame_wall_ms"] = std::to_string(m.last_frame_wall_ms);
    r.fields["reconnect_count"] = std::to_string(m.reconnect_count);
    r.fields["quality_flags"] = m.quality_flags;
    r.fields["display_dropped"] = std::to_string(m.display_dropped);
    r.fields["ai_dropped"] = std::to_string(m.ai_dropped);
    r.fields["recording_overflow"] = std::to_string(m.recording_overflow);
    r.fields["gap_count"] = std::to_string(m.gap_count);
    r.fields["attempt_id"] = m.attempt_id.empty() ? "none" : m.attempt_id;
    r.fields["last_error"] = redact_error(m.last_error);
    return r;
}

std::string video_metric_json_line(const VideoChannelMetrics& m, long long ts_wall_ms) {
    return log_record_json(video_metric_log_record(m, ts_wall_ms));
}

std::string redact_error(const std::string& text) {
    std::string s = text;
    // 1) URL userinfo: scheme://user:pass@host -> scheme://***@host
    std::size_t pos = 0;
    while ((pos = s.find("://", pos)) != std::string::npos) {
        const std::size_t start = pos + 3;
        const std::size_t at = s.find('@', start);
        const std::size_t stop = s.find_first_of("/ \t\n", start);
        if (at != std::string::npos && (stop == std::string::npos || at < stop)) {
            s.replace(start, at - start, "***");
            pos = start + 3;
        } else {
            pos = start;
        }
    }
    // 2) key=value secrets.
    for (const char* key : {"password=", "passwd=", "pwd=", "user-pw=", "token=", "secret=",
                            "user-id=", "user=", "location=", "url="}) {
        mask_value_after(s, key);
    }
    // 3) pipeline/bus raw text must never be emitted -> whole redaction.
    if (contains(s, "rtspsrc") || contains(s, "GstPipeline") || contains(s, "GST_MESSAGE") ||
        contains(s, "pipeline0") || contains(s, "gst_pipeline")) {
        return "<redacted>";
    }
    return s;
}

bool contains_credentials(const std::string& text) {
    // URL userinfo
    std::size_t pos = 0;
    while ((pos = text.find("://", pos)) != std::string::npos) {
        const std::size_t start = pos + 3;
        const std::size_t at = text.find('@', start);
        const std::size_t stop = text.find_first_of("/ \t\n", start);
        if (at != std::string::npos && (stop == std::string::npos || at < stop)) return true;
        pos = start;
    }
    for (const char* key : {"password=", "passwd=", "pwd=", "user-pw=", "token=", "secret=",
                            "user-id=", "user="}) {
        if (contains(text, key)) return true;
    }
    if (contains(text, "rtspsrc") || contains(text, "GstPipeline") || contains(text, "GST_MESSAGE") ||
        contains(text, "pipeline0"))
        return true;
    return false;
}

}  // namespace core
}  // namespace railhmi
