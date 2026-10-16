// railhmi-core 健康监控/结构化日志/自恢复测试（ITEM-024 / DESKTOP §15.3/§15.4）。
// 覆盖：健康等级与告警、日志 JSON/条件查询/按日滚动、恢复顺序/进度/降级隔离/关键步存活判定。

#include "test_util.h"

#include <string>
#include <vector>

#include "railhmi/core/alarm_center.h"   // parse_date_utc_ms
#include "railhmi/core/health.h"
#include "railhmi/core/recovery.h"

namespace {

using namespace railhmi::core;

void test_health_eval() {
    HealthThresholds th;
    HealthSample ok;
    ok.npu_load = 40; ok.cpu = 30; ok.temp_c = 55; ok.net_errors = 0;
    ok.disk_free = 15LL * 1024 * 1024 * 1024; ok.disk_total = 20LL * 1024 * 1024 * 1024;
    const HealthEval e1 = evaluate_health(ok, th);
    railhmi_test::check(e1.level == HealthLevel::Ok && e1.alerts.empty(), "healthy -> ok, no alerts");
    railhmi_test::check(e1.text.find("NPU 40.0%") != std::string::npos && e1.text.find("正常") != std::string::npos,
                        "health text");

    HealthSample warn = ok;
    warn.temp_c = 80;   // > 75
    warn.net_errors = 20;
    const HealthEval e2 = evaluate_health(warn, th);
    railhmi_test::check(e2.level == HealthLevel::Warn && e2.alerts.size() == 2, "temp+net warn");

    HealthSample err = ok;
    err.temp_c = 90;   // > 75+10
    railhmi_test::check(evaluate_health(err, th).level == HealthLevel::Error, "very high temp -> error");

    HealthSample disk = ok;
    disk.disk_free = 1LL * 1024 * 1024 * 1024;   // 5% free < 10%
    railhmi_test::check(evaluate_health(disk, th).level == HealthLevel::Warn, "low disk -> warn");
}

void test_structured_logs() {
    const long long day = parse_date_utc_ms("2026-10-16");
    std::vector<LogRecord> logs;
    logs.push_back(LogRecord{day, LogSeverity::Info, "app", "启动完成", {{"role", "driver"}}});
    logs.push_back(LogRecord{day + 1000, LogSeverity::Error, "stream", "CAM-04 断流", {{"cam", "CAM-04"}}});
    logs.push_back(LogRecord{day - 3 * 86400000LL, LogSeverity::Warn, "ptz", "旧日志", {}});
    const std::string j = log_record_json(logs[1]);
    railhmi_test::check(j.find("\"level\":\"error\"") != std::string::npos && j.find("\"cam\":\"CAM-04\"") != std::string::npos,
                        "log json with fields");

    LogFilter f;
    f.source = "stream";
    railhmi_test::check(query_logs(logs, f).size() == 1, "query by source");
    LogFilter fs;
    fs.has_min_severity = true;
    fs.min_severity = LogSeverity::Warn;
    railhmi_test::check(query_logs(logs, fs).size() == 2, "query by min severity");
    LogFilter fk;
    fk.keyword = "断流";
    railhmi_test::check(query_logs(logs, fk).size() == 1, "query by keyword");
    LogFilter ft;
    ft.ts_from_ms = day;
    ft.ts_to_ms = day + 86400000LL;
    railhmi_test::check(query_logs(logs, ft).size() == 2, "query by time window");

    railhmi_test::check(log_day_key(day) == "2026-10-16", "log day key");
    railhmi_test::check(roll_health_logs_by_day(logs, 1).size() == 2, "day-rolling keeps latest day");
}

void test_recovery_ok() {
    RecoveryTracker t;
    t.start(1000);
    const RecoveryStep seq[8] = {RecoveryStep::LoadConfig, RecoveryStep::OpenDatabase, RecoveryStep::StartRecording,
                                 RecoveryStep::StartStreaming, RecoveryStep::StartAi, RecoveryStep::PtzHome,
                                 RecoveryStep::FrontPreset, RecoveryStep::FrontAiReady};
    railhmi_test::check(t.state().steps.size() == 8, "recovery has 8 steps");
    for (RecoveryStep s : seq) t.complete_step(s, true);
    t.finish(2000);
    const RecoveryState st = t.state();
    railhmi_test::check(st.completed && st.progress_pct() == 100 && t.application_alive(), "all ok -> completed");
    railhmi_test::check(recovery_step_label(seq[0]) == std::string("加载配置"), "step label");
}

void test_recovery_isolation() {
    RecoveryTracker t;
    t.start(1000);
    t.complete_step(RecoveryStep::LoadConfig, true);
    t.complete_step(RecoveryStep::OpenDatabase, true);
    t.complete_step(RecoveryStep::StartRecording, true);
    t.camera_result(false, false, "CAM-04 启动失败");   // 单路非关键异常
    t.complete_step(RecoveryStep::StartAi, true);
    t.complete_step(RecoveryStep::PtzHome, true);
    t.complete_step(RecoveryStep::FrontPreset, true);
    t.complete_step(RecoveryStep::FrontAiReady, true);
    t.finish(3000);
    const RecoveryState st = t.state();
    railhmi_test::check(t.application_alive(), "single non-critical camera failure does not exit app");
    railhmi_test::check(st.degraded_cameras == 1, "degraded camera counted");
    bool stream_degraded = false;
    for (const auto& s : st.steps)
        if (s.step == RecoveryStep::StartStreaming && s.status == StepStatus::Degraded) stream_degraded = true;
    railhmi_test::check(stream_degraded, "streaming step degraded, rest continue");
    railhmi_test::check(st.completed, "recovery still completes");
}

void test_recovery_critical_failure() {
    RecoveryTracker t;
    t.start(1000);
    t.complete_step(RecoveryStep::LoadConfig, false, "配置非法");   // critical
    t.complete_step(RecoveryStep::OpenDatabase, true);
    railhmi_test::check(!t.application_alive(), "critical step failure -> app not alive");
    bool cfg_failed = false;
    for (const auto& s : t.state().steps)
        if (s.step == RecoveryStep::LoadConfig && s.status == StepStatus::Failed) cfg_failed = true;
    railhmi_test::check(cfg_failed, "config step failed recorded");
    railhmi_test::check(t.state().progress_pct() == 25, "progress reflects terminal steps");
}

}  // namespace

void run_health_tests() {
    test_health_eval();
    test_structured_logs();
    test_recovery_ok();
    test_recovery_isolation();
    test_recovery_critical_failure();
}
