#include "railhmi/core/health.h"

#include <algorithm>
#include <cstdio>

#include "railhmi/core/alarm_center.h"   // format_time_text
#include "railhmi/core/maintenance.h"    // log_day_key（复用定义，避免重复符号）

namespace railhmi {
namespace core {

const char* to_string(HealthLevel l) {
    switch (l) {
        case HealthLevel::Ok: return "ok";
        case HealthLevel::Warn: return "warn";
        case HealthLevel::Error: return "error";
    }
    return "ok";
}
const char* health_level_label(HealthLevel l) {
    switch (l) {
        case HealthLevel::Ok: return "正常";
        case HealthLevel::Warn: return "警告";
        case HealthLevel::Error: return "异常";
    }
    return "正常";
}

namespace {
std::string fmt1(double v) {
    char b[32];
    std::snprintf(b, sizeof(b), "%.1f", v);
    return b;
}
}  // namespace

HealthEval evaluate_health(const HealthSample& s, const HealthThresholds& th) {
    HealthEval e;
    auto add = [&](const std::string& a, HealthLevel lv) {
        e.alerts.push_back(a);
        if (static_cast<int>(lv) > static_cast<int>(e.level)) e.level = lv;
    };
    if (s.temp_c >= 0) {
        if (s.temp_c > th.temp_warn + 10) add("温度 " + fmt1(s.temp_c) + "°C 超限", HealthLevel::Error);
        else if (s.temp_c > th.temp_warn) add("温度 " + fmt1(s.temp_c) + "°C > " + fmt1(th.temp_warn) + "°C", HealthLevel::Warn);
    }
    if (s.npu_load >= 0 && s.npu_load > th.npu_warn)
        add("NPU " + fmt1(s.npu_load) + "% > " + fmt1(th.npu_warn) + "%", HealthLevel::Warn);
    if (s.cpu >= 0 && s.cpu > th.cpu_warn)
        add("CPU " + fmt1(s.cpu) + "% > " + fmt1(th.cpu_warn) + "%", HealthLevel::Warn);
    if (s.net_errors > th.net_err_warn)
        add("网络错误 " + std::to_string(s.net_errors) + " > " + std::to_string(th.net_err_warn), HealthLevel::Warn);
    if (s.disk_total > 0) {
        const double free_pct = static_cast<double>(s.disk_free) * 100.0 / static_cast<double>(s.disk_total);
        if (free_pct < th.disk_low_pct) add("磁盘剩余 " + fmt1(free_pct) + "% < " + fmt1(th.disk_low_pct) + "%", HealthLevel::Warn);
    }
    std::string t = "NPU ";
    t += (s.npu_load >= 0) ? (fmt1(s.npu_load) + "%") : "—";
    t += " / CPU ";
    t += (s.cpu >= 0) ? (fmt1(s.cpu) + "%") : "—";
    t += " / 温度 ";
    t += (s.temp_c >= 0) ? (fmt1(s.temp_c) + "°C") : "—";
    t += " / 网络错误 " + std::to_string(s.net_errors);
    t += " / " + std::string(health_level_label(e.level));
    e.text = t;
    return e;
}

const char* to_string(LogSeverity s) {
    switch (s) {
        case LogSeverity::Debug: return "debug";
        case LogSeverity::Info: return "info";
        case LogSeverity::Warn: return "warn";
        case LogSeverity::Error: return "error";
    }
    return "info";
}
const char* log_severity_label(LogSeverity s) {
    switch (s) {
        case LogSeverity::Debug: return "调试";
        case LogSeverity::Info: return "信息";
        case LogSeverity::Warn: return "警告";
        case LogSeverity::Error: return "错误";
    }
    return "信息";
}

namespace {
std::string json_escape(const std::string& s) {
    std::string out;
    for (char c : s) {
        if (c == '"' || c == '\\') { out += '\\'; out += c; }
        else if (c == '\n') out += "\\n";
        else out += c;
    }
    return out;
}
}  // namespace

std::string log_record_json(const LogRecord& r) {
    std::string j = "{\"ts_ms\":" + std::to_string(r.ts_wall_ms) + ",\"level\":\"" + to_string(r.severity) +
                    "\",\"source\":\"" + json_escape(r.source) + "\",\"message\":\"" + json_escape(r.message) + "\"";
    if (!r.fields.empty()) {
        j += ",\"fields\":{";
        bool first = true;
        for (const auto& kv : r.fields) {
            if (!first) j += ",";
            first = false;
            j += "\"" + json_escape(kv.first) + "\":\"" + json_escape(kv.second) + "\"";
        }
        j += "}";
    }
    j += "}";
    return j;
}

std::string log_day_key(long long ts_wall_ms);   // 定义见 maintenance.cpp（UTC YYYY-MM-DD，复用）

namespace {
bool matches(const LogRecord& r, const LogFilter& f) {
    if (f.has_min_severity && static_cast<int>(r.severity) < static_cast<int>(f.min_severity)) return false;
    if (!f.source.empty() && r.source != f.source) return false;
    if (f.ts_from_ms > 0 && r.ts_wall_ms < f.ts_from_ms) return false;
    if (f.ts_to_ms > 0 && r.ts_wall_ms >= f.ts_to_ms) return false;
    if (!f.keyword.empty()) {
        if (r.message.find(f.keyword) != std::string::npos) return true;
        for (const auto& kv : r.fields)
            if (kv.first.find(f.keyword) != std::string::npos || kv.second.find(f.keyword) != std::string::npos)
                return true;
        return false;
    }
    return true;
}
}  // namespace

std::vector<LogRecord> query_logs(const std::vector<LogRecord>& records, const LogFilter& f) {
    std::vector<LogRecord> out;
    for (const auto& r : records)
        if (matches(r, f)) out.push_back(r);
    return out;
}

std::vector<LogRecord> roll_health_logs_by_day(const std::vector<LogRecord>& records, int max_days) {
    std::vector<std::string> days;
    for (const auto& r : records) {
        const std::string d = log_day_key(r.ts_wall_ms);
        if (std::find(days.begin(), days.end(), d) == days.end()) days.push_back(d);
    }
    std::sort(days.begin(), days.end());
    std::vector<std::string> keep = days;
    if (max_days > 0 && static_cast<int>(keep.size()) > max_days)
        keep.erase(keep.begin(), keep.begin() + (static_cast<int>(keep.size()) - max_days));
    std::vector<LogRecord> out;
    for (const auto& r : records)
        if (std::find(keep.begin(), keep.end(), log_day_key(r.ts_wall_ms)) != keep.end()) out.push_back(r);
    return out;
}

}  // namespace core
}  // namespace railhmi
