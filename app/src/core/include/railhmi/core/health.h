#pragma once

// railhmi-core: 健康采样、结构化日志与查询（ITEM-024 / DESKTOP §15.3、HLT-004/005、DATA-003）。
// 周期采样 NPU/CPU/温度/网络错误/磁盘 -> 评估等级与告警文本；结构化日志记录/JSON/按日滚动/条件查询。
// 纯逻辑，无 Qt/SQLite。

#include <map>
#include <string>
#include <vector>

namespace railhmi {
namespace core {

// ---------------- 健康采样 ----------------
struct HealthSample {
    long long ts_wall_ms = 0;
    long long ts_mono_us = 0;
    double npu_load = -1;      // %
    double cpu = -1;           // %
    double temp_c = -1;        // °C
    long long net_errors = 0;  // UDP InErrors
    long long disk_free = 0;   // bytes
    long long disk_total = 0;  // bytes
};

struct HealthThresholds {
    double npu_warn = 85.0;
    double cpu_warn = 85.0;
    double temp_warn = 75.0;
    long long net_err_warn = 10;
    double disk_low_pct = 10.0;
};

enum class HealthLevel { Ok, Warn, Error };
const char* to_string(HealthLevel l);
const char* health_level_label(HealthLevel l);   // 正常 / 警告 / 异常

struct HealthEval {
    HealthLevel level = HealthLevel::Ok;
    std::vector<std::string> alerts;   // 触发项（如 “温度 80°C > 75°C”）
    std::string text;                  // 概要文本
};
HealthEval evaluate_health(const HealthSample& s, const HealthThresholds& th);

// ---------------- 结构化日志 ----------------
enum class LogSeverity { Debug, Info, Warn, Error };
const char* to_string(LogSeverity s);
const char* log_severity_label(LogSeverity s);

struct LogRecord {
    long long ts_wall_ms = 0;
    LogSeverity severity = LogSeverity::Info;
    std::string source;                              // app / stream / ai / ptz / storage ...
    std::string message;
    std::map<std::string, std::string> fields;       // 结构化键值
};
std::string log_record_json(const LogRecord& r);     // 单行 JSON
std::string log_day_key(long long ts_wall_ms);       // UTC YYYY-MM-DD

struct LogFilter {
    bool has_min_severity = false;
    LogSeverity min_severity = LogSeverity::Info;
    std::string source;        // 空 = 不限
    std::string keyword;       // message/fields 子串
    long long ts_from_ms = 0;  // 0 = 不限
    long long ts_to_ms = 0;    // 0 = 不限
};
std::vector<LogRecord> query_logs(const std::vector<LogRecord>& records, const LogFilter& f);
// 按日滚动：仅保留最近 max_days 天。
std::vector<LogRecord> roll_health_logs_by_day(const std::vector<LogRecord>& records, int max_days);

}  // namespace core
}  // namespace railhmi
