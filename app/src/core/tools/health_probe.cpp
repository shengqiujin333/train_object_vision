// railhmi 健康/结构化日志/自恢复探针（ITEM-024）
// 演示：健康采样等级与告警、日志 JSON/条件查询/按日滚动、启动自恢复顺序与进度、
// 单路非关键相机异常降级不退出、关键步失败应用不可用。纯 core，无 Qt/SQLite。
//
// 用法: railhmi-health-probe [--out log]
// 退出码: 0 验收 PASS；1 FAIL。

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/alarm_center.h"   // parse_date_utc_ms
#include "railhmi/core/health.h"
#include "railhmi/core/recovery.h"

using namespace railhmi::core;

int main(int argc, char** argv) {
    std::string out;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--out") next(&out);
        else if (a == "--help" || a == "-h") { std::printf("usage: railhmi-health-probe [--out log]\n"); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }
    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) { std::printf("%s\n", line.c_str()); if (fo) fo << line << "\n"; };

    HealthThresholds th;
    HealthSample s;
    s.ts_wall_ms = 1760000000000LL;
    s.npu_load = 42; s.cpu = 33; s.temp_c = 58; s.net_errors = 3;
    s.disk_free = 15LL * 1024 * 1024 * 1024; s.disk_total = 20LL * 1024 * 1024 * 1024;
    const HealthEval e = evaluate_health(s, th);
    emit("type=health level=" + std::string(to_string(e.level)) + " text=\"" + e.text + "\" alerts=" +
         std::to_string(e.alerts.size()));
    HealthSample hot = s; hot.temp_c = 80;
    const HealthEval eh = evaluate_health(hot, th);
    emit("type=health level=" + std::string(to_string(eh.level)) + " alerts=" + std::to_string(eh.alerts.size()) +
         (eh.alerts.empty() ? "" : (" first=\"" + eh.alerts[0] + "\"")));

    const long long day = parse_date_utc_ms("2026-10-16");
    std::vector<LogRecord> logs;
    logs.push_back(LogRecord{day, LogSeverity::Info, "app", "启动完成", {{"role", "driver"}}});
    logs.push_back(LogRecord{day + 1000, LogSeverity::Error, "stream", "CAM-04 断流", {{"cam", "CAM-04"}}});
    logs.push_back(LogRecord{day - 5 * 86400000LL, LogSeverity::Warn, "ptz", "旧日志", {}});
    emit("type=log-json " + log_record_json(logs[1]));
    LogFilter f; f.source = "stream";
    LogFilter fk; fk.keyword = "断流";
    LogFilter fs; fs.has_min_severity = true; fs.min_severity = LogSeverity::Warn;
    emit("type=log-query by_source=" + std::to_string(query_logs(logs, f).size()) +
         " by_keyword=" + std::to_string(query_logs(logs, fk).size()) +
         " by_severity=" + std::to_string(query_logs(logs, fs).size()) +
         " day_rolled=" + std::to_string(roll_health_logs_by_day(logs, 1).size()));

    // 自恢复：全部成功 + 单路非关键降级
    RecoveryTracker t;
    t.start(1000);
    const RecoveryStep seq[8] = {RecoveryStep::LoadConfig, RecoveryStep::OpenDatabase, RecoveryStep::StartRecording,
                                 RecoveryStep::StartStreaming, RecoveryStep::StartAi, RecoveryStep::PtzHome,
                                 RecoveryStep::FrontPreset, RecoveryStep::FrontAiReady};
    for (int i = 0; i < 8; ++i) {
        if (seq[i] == RecoveryStep::StartStreaming) t.camera_result(false, false, "CAM-04 启动失败");
        else t.complete_step(seq[i], true);
        emit("type=recovery step=" + std::string(to_string(seq[i])) + " label=" + recovery_step_label(seq[i]) +
             " status=" + step_status_label(t.state().steps[i].status) +
             " progress=" + std::to_string(t.state().progress_pct()));
    }
    t.finish(9000);
    emit("type=recovery-summary completed=" + std::string(t.state().completed ? "true" : "false") +
         " alive=" + std::string(t.application_alive() ? "true" : "false") +
         " degraded_cameras=" + std::to_string(t.state().degraded_cameras) +
         " progress=" + std::to_string(t.state().progress_pct()) + " text=\"" + t.state().text() + "\"");

    RecoveryTracker tc;
    tc.start(1000);
    tc.complete_step(RecoveryStep::LoadConfig, false, "配置非法");
    tc.complete_step(RecoveryStep::OpenDatabase, true);
    emit("type=recovery-critical config_failed_alive=" +
         std::string(tc.application_alive() ? "true" : "false") +
         " progress=" + std::to_string(tc.state().progress_pct()));

    const bool ok = (e.level == HealthLevel::Ok) && (eh.level == HealthLevel::Warn) &&
                    (log_record_json(logs[1]).find("\"level\":\"error\"") != std::string::npos) &&
                    (query_logs(logs, f).size() == 1) && (query_logs(logs, fk).size() == 1) &&
                    (query_logs(logs, fs).size() == 2) && (roll_health_logs_by_day(logs, 1).size() == 2) &&
                    t.state().completed && t.application_alive() && (t.state().degraded_cameras == 1) &&
                    (t.state().progress_pct() == 100) && !tc.application_alive();
    emit(std::string("type=acceptance health_ok_warn=true log_query_and_rolling=true recovery_completed=") +
         (t.state().completed ? "true" : "false") + " noncritical_isolated=" +
         (t.application_alive() && t.state().degraded_cameras == 1 ? "true" : "false") +
         " critical_stops_alive=" + (!tc.application_alive() ? "true" : "false") +
         " result=" + (ok ? "PASS" : "FAIL"));
    return ok ? 0 : 1;
}
