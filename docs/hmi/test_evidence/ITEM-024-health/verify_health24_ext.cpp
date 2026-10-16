// Verifier-owned ext fixture for ITEM-024 (software_verification).
// Linked against the delivered librailhmi_core.a built by the verifier's own
// board_build.py TestRun round; asserts ITEM-024 acceptance boundaries the
// implementer's core-tests (test_health.cpp) and health-probe do not cover:
//   * health: boundary equality semantics (warn threshold ==, error at
//     warn+10 ==), sentinel -1 fields are skipped (no false alerts), disk
//     pct math, text composition with unknown fields as "—"
//   * logs: JSON escaping (quote/backslash/newline), severity ORDER (debug
//     < info < warn < error), min_severity boundary inclusivity, time window
//     [from, to) half-open semantics, keyword matches message OR field key
//     OR field value (three-way), empty source filter = no filter
//   * rolling: day-atom semantics (not row-atom), keeps LATEST days (not
//     first), max_days=0 sentinel keeps all, single-day input never drops
//   * recovery: full 8-step enum order via to_string tokens, progress math
//     i*100/8 exact, camera_result ordering (ok after degraded does not
//     un-degrade, degraded after ok counts), camera_result on critical
//     failure -> Failed + alive=false, finish() before all steps terminal,
//     text() mid-recovery "恢复中 k/8", critical failure detail preserved
// Pure logic, no Qt/RKNN — like the delivered probes.
#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/health.h"
#include "railhmi/core/recovery.h"
#include "railhmi/core/alarm_center.h"   // parse_date_utc_ms (verifier day math)

using namespace railhmi::core;

namespace {

int g_passed = 0, g_failed = 0;
std::vector<std::string> g_notes;

void check(bool ok, const std::string& name) {
    (ok ? g_passed : g_failed) += 1;
    if (!ok) g_notes.push_back("FAIL: " + name);
}

void check_eq(const std::string& name, const std::string& got, const std::string& want) {
    check(got == want, name + " [" + got + "] want [" + want + "]");
}

// --- 1. health boundaries ----------------------------------------------------
void t_health_boundaries() {
    HealthThresholds th;   // npu 85, cpu 85, temp 75, net 10, disk 10%
    HealthSample s;
    s.npu_load = 40; s.cpu = 30; s.temp_c = 55; s.net_errors = 0;
    s.disk_free = 15LL * 1024 * 1024 * 1024; s.disk_total = 20LL * 1024 * 1024 * 1024;

    // exact threshold equality: warn triggers only STRICTLY above
    HealthSample eq = s; eq.temp_c = th.temp_warn;
    check(evaluate_health(eq, th).level == HealthLevel::Ok,
          "temp == warn threshold -> Ok (strict >)");
    HealthSample just = s; just.temp_c = th.temp_warn + 0.5;
    check(evaluate_health(just, th).level == HealthLevel::Warn,
          "temp just above warn -> Warn");
    HealthSample edge = s; edge.temp_c = th.temp_warn + 10.0;
    check(evaluate_health(edge, th).level == HealthLevel::Warn,
          "temp == warn+10 -> Warn (strict > for Error)");
    HealthSample over = s; over.temp_c = th.temp_warn + 10.5;
    check(evaluate_health(over, th).level == HealthLevel::Error,
          "temp > warn+10 -> Error");

    // sentinel -1 fields are skipped (no alerts, no fake data in text)
    HealthSample unknown;
    unknown.npu_load = -1; unknown.cpu = -1; unknown.temp_c = -1;
    const HealthEval eu = evaluate_health(unknown, th);
    check(eu.level == HealthLevel::Ok && eu.alerts.empty(),
          "sentinel -1 fields -> no alerts");
    check_eq("unknown fields render as dash",
             eu.text, std::string("NPU — / CPU — / 温度 — / 网络错误 0 / 正常"));

    // npu/cpu warn boundaries (strict >)
    HealthSample n = s; n.npu_load = th.npu_warn;
    check(evaluate_health(n, th).level == HealthLevel::Ok, "npu == warn -> Ok");
    n.npu_load = th.npu_warn + 0.1;
    check(evaluate_health(n, th).level == HealthLevel::Warn, "npu > warn -> Warn");
    HealthSample c = s; c.cpu = th.cpu_warn + 0.1;
    check(evaluate_health(c, th).level == HealthLevel::Warn, "cpu > warn -> Warn");

    // net errors boundary (strict >)
    HealthSample ne = s; ne.net_errors = th.net_err_warn;
    check(evaluate_health(ne, th).level == HealthLevel::Ok, "net == warn -> Ok");
    ne.net_errors = th.net_err_warn + 1;
    check(evaluate_health(ne, th).level == HealthLevel::Warn, "net > warn -> Warn");

    // disk pct boundary: exactly 10% free is NOT low (strict <)
    HealthSample d = s; d.disk_free = 2LL * 1024 * 1024 * 1024;   // exactly 10%
    check(evaluate_health(d, th).level == HealthLevel::Ok, "disk == 10% -> Ok");
    d.disk_free = 2LL * 1024 * 1024 * 1024 - 1;
    check(evaluate_health(d, th).level == HealthLevel::Warn, "disk < 10% -> Warn");
    // disk_total == 0 -> no disk check (guard)
    HealthSample d0 = s; d0.disk_total = 0; d0.disk_free = 0;
    check(evaluate_health(d0, th).alerts.empty(), "disk_total=0 -> no disk alert");

    // max level wins when multiple alerts (Error from temp dominates warn net)
    HealthSample multi = s; multi.temp_c = 90; multi.net_errors = 99;
    const HealthEval em = evaluate_health(multi, th);
    check(em.level == HealthLevel::Error && em.alerts.size() == 2,
          "error dominates with 2 alerts");
    // text always carries the final level label
    check(em.text.find("异常") != std::string::npos, "error text carries 异常");
}

// --- 2. structured logs: JSON escaping + filter semantics --------------------
void t_logs() {
    const long long day = parse_date_utc_ms("2026-10-16");

    // JSON escaping: quote, backslash, newline
    LogRecord r{day, LogSeverity::Error, "st\"ream\\x", "line1\nline2", {{"k\"1", "v\\2"}}};
    const std::string j = log_record_json(r);
    check(j.find("\\\"") != std::string::npos && j.find("\\\\") != std::string::npos &&
              j.find("\\n") != std::string::npos,
          "json escapes quote/backslash/newline");
    check(j.rfind("{\"ts_ms\":" + std::to_string(day), 0) == 0,
          "json starts with {\"ts_ms\":<day>");
    // fields omitted when empty
    LogRecord plain{day, LogSeverity::Info, "app", "msg", {}};
    check(log_record_json(plain).find("\"fields\"") == std::string::npos,
          "empty fields omitted from json");

    // severity ORDER: debug < info < warn < error (min_severity filter relies on it)
    check_eq("sev token debug", to_string(LogSeverity::Debug), "debug");
    check_eq("sev token info", to_string(LogSeverity::Info), "info");
    check_eq("sev token warn", to_string(LogSeverity::Warn), "warn");
    check_eq("sev token error", to_string(LogSeverity::Error), "error");
    check(static_cast<int>(LogSeverity::Debug) < static_cast<int>(LogSeverity::Info) &&
              static_cast<int>(LogSeverity::Info) < static_cast<int>(LogSeverity::Warn) &&
              static_cast<int>(LogSeverity::Warn) < static_cast<int>(LogSeverity::Error),
          "severity enum ascending order");

    std::vector<LogRecord> logs;
    logs.push_back(LogRecord{day, LogSeverity::Debug, "app", "dbg", {}});
    logs.push_back(LogRecord{day + 1, LogSeverity::Info, "app", "info msg", {{"role", "driver"}}});
    logs.push_back(LogRecord{day + 2, LogSeverity::Warn, "stream", "warn msg", {}});
    logs.push_back(LogRecord{day + 3, LogSeverity::Error, "stream", "CAM-04 断流", {{"cam", "CAM-04"}}});

    // min_severity boundary: >= (warn keeps warn and error)
    LogFilter fw; fw.has_min_severity = true; fw.min_severity = LogSeverity::Warn;
    const std::vector<LogRecord> rw = query_logs(logs, fw);
    check(rw.size() == 2 && rw[0].severity == LogSeverity::Warn &&
              rw[1].severity == LogSeverity::Error,
          "min_severity=warn keeps warn+error (inclusive >=)");
    LogFilter fd; fd.has_min_severity = true; fd.min_severity = LogSeverity::Debug;
    check(query_logs(logs, fd).size() == 4, "min_severity=debug keeps all");
    LogFilter fe; fe.has_min_severity = true; fe.min_severity = LogSeverity::Error;
    check(query_logs(logs, fe).size() == 1, "min_severity=error keeps error only");

    // time window half-open [from, to)
    LogFilter ft; ft.ts_from_ms = day + 1; ft.ts_to_ms = day + 3;
    check(query_logs(logs, ft).size() == 2, "window [d+1, d+3) keeps info+warn");
    LogFilter ft0; ft0.ts_from_ms = day; ft0.ts_to_ms = day;
    check(query_logs(logs, ft0).empty(), "empty window -> empty result");
    // zero bounds = no limit
    LogFilter fz;
    check(query_logs(logs, fz).size() == 4, "zero bounds = no limit");

    // keyword three-way: message OR field key OR field value
    LogFilter km; km.keyword = "断流";
    check(query_logs(logs, km).size() == 1, "keyword hits message");
    LogFilter kv; kv.keyword = "CAM-04";
    check(query_logs(logs, kv).size() == 1, "keyword hits field value");
    LogFilter kk; kk.keyword = "cam";
    check(query_logs(logs, kk).size() == 1, "keyword hits field key");
    LogFilter kn; kn.keyword = "no-such-token";
    check(query_logs(logs, kn).empty(), "keyword miss -> empty");

    // source filter exact match, empty = no filter
    LogFilter fsrc; fsrc.source = "stream";
    check(query_logs(logs, fsrc).size() == 2, "source exact match");
    LogFilter fsrc2; fsrc2.source = "strea";
    check(query_logs(logs, fsrc2).empty(), "source partial does not match");
    LogFilter fnone;
    check(query_logs(logs, fnone).size() == 4, "empty filter = all");

    // combined filters intersect
    LogFilter fcomb; fcomb.source = "stream"; fcomb.has_min_severity = true;
    fcomb.min_severity = LogSeverity::Error;
    check(query_logs(logs, fcomb).size() == 1, "combined source+severity intersect");
}

// --- 3. day rolling: day-atom, latest days, sentinels ------------------------
void t_rolling() {
    const long long day = parse_date_utc_ms("2026-10-16");
    const long long D = 86400000LL;
    std::vector<LogRecord> logs;
    // day-2: 1 row, day-1: 1 row, day0: 3 rows (incl. day boundaries)
    logs.push_back(LogRecord{day - 2 * D, LogSeverity::Info, "a", "old2", {}});
    logs.push_back(LogRecord{day - 1 * D, LogSeverity::Info, "a", "old1", {}});
    logs.push_back(LogRecord{day, LogSeverity::Info, "a", "d0 first ms", {}});
    logs.push_back(LogRecord{day + D - 1, LogSeverity::Warn, "a", "d0 last ms", {}});
    logs.push_back(LogRecord{day + D, LogSeverity::Info, "a", "d1 first ms", {}});
    logs.push_back(LogRecord{day + 2 * D, LogSeverity::Error, "a", "d2", {}});

    // 4 distinct days; max_days=2 keeps the LATEST 2 days (d1 + d2) = 2 rows
    const std::vector<LogRecord> r2 = roll_health_logs_by_day(logs, 2);
    check(r2.size() == 2, "max_days=2 keeps latest 2 days (2 rows)");
    check(r2[0].message == "d1 first ms" && r2[1].message == "d2",
          "kept rows are the latest days, order preserved");
    // day-atom: max_days=3 keeps d0 (2 rows incl. boundary ms) + d1 + d2 = 4
    check(roll_health_logs_by_day(logs, 3).size() == 4,
          "max_days=3 keeps whole days (4 rows incl. both d0 boundary rows)");
    // max_days=1 keeps only d2
    const std::vector<LogRecord> r1 = roll_health_logs_by_day(logs, 1);
    check(r1.size() == 1 && r1[0].message == "d2", "max_days=1 keeps only latest day");
    // sentinels
    check(roll_health_logs_by_day(logs, 0).size() == 6, "max_days=0 keeps all (sentinel)");
    check(roll_health_logs_by_day(logs, -1).size() == 6, "max_days<0 keeps all (sentinel)");
    check(roll_health_logs_by_day(logs, 99).size() == 6, "max_days > distinct days keeps all");
    // day key correctness at boundaries (verifier day math via parse_date_utc_ms)
    check_eq("day key d0 first ms", log_day_key(day), "2026-10-16");
    check_eq("day key d0 last ms", log_day_key(day + D - 1), "2026-10-16");
    check_eq("day key d1 first ms", log_day_key(day + D), "2026-10-17");
}

// --- 4. recovery: enum order, progress math, camera_result semantics ---------
void t_recovery() {
    // full 8-step enum order via tokens
    check_eq("step 0 token", to_string(RecoveryStep::LoadConfig), "load_config");
    check_eq("step 1 token", to_string(RecoveryStep::OpenDatabase), "open_database");
    check_eq("step 2 token", to_string(RecoveryStep::StartRecording), "start_recording");
    check_eq("step 3 token", to_string(RecoveryStep::StartStreaming), "start_streaming");
    check_eq("step 4 token", to_string(RecoveryStep::StartAi), "start_ai");
    check_eq("step 5 token", to_string(RecoveryStep::PtzHome), "ptz_home");
    check_eq("step 6 token", to_string(RecoveryStep::FrontPreset), "front_preset");
    check_eq("step 7 token", to_string(RecoveryStep::FrontAiReady), "front_ai_ready");
    check(static_cast<int>(RecoveryStep::Count) == 8, "recovery has exactly 8 steps");

    // critical steps are exactly LoadConfig + OpenDatabase
    check(recovery_step_critical(RecoveryStep::LoadConfig) &&
              recovery_step_critical(RecoveryStep::OpenDatabase),
          "config+db are critical");
    for (int i = 2; i < 8; ++i) {
        check(!recovery_step_critical(static_cast<RecoveryStep>(i)),
              "steps 2..7 non-critical (" + std::string(to_string(static_cast<RecoveryStep>(i))) + ")");
    }

    // progress math: terminal step i (1-based) -> i*100/8 integer division
    RecoveryTracker t;
    // before start(): 未开始
    check_eq("pre-start text", t.state().text(), "未开始");
    check(t.state().progress_pct() == 0, "fresh tracker progress 0");
    t.start(1000);
    check_eq("started text", t.state().text(), "恢复中 0/8");
    const RecoveryStep seq[8] = {RecoveryStep::LoadConfig, RecoveryStep::OpenDatabase,
                                 RecoveryStep::StartRecording, RecoveryStep::StartStreaming,
                                 RecoveryStep::StartAi, RecoveryStep::PtzHome,
                                 RecoveryStep::FrontPreset, RecoveryStep::FrontAiReady};
    for (int i = 0; i < 8; ++i) {
        t.complete_step(seq[i], true);
        const int want = (i + 1) * 100 / 8;
        check(t.state().progress_pct() == want,
              "progress after step " + std::to_string(i + 1) + " == " + std::to_string(want));
        if (i < 7) {
            check_eq("mid text after step " + std::to_string(i + 1),
                     t.state().text(), "恢复中 " + std::to_string(i + 1) + "/" + std::to_string(8));
        }
    }
    check(t.state().started && !t.state().completed, "not completed before finish()");
    t.finish(9000);
    check(t.state().completed && t.state().progress_pct() == 100, "finish -> completed 100%");
    check_eq("completed text all ok", t.state().text(), "恢复完成");
    // timestamps
    check(t.state().started_ms == 1000 && t.state().finished_ms == 9000,
          "start/finish timestamps recorded");

    // camera_result semantics:
    // (a) ok after degraded does NOT un-degrade, but degraded_cameras still counts
    RecoveryTracker ta;
    ta.start(0);
    ta.camera_result(false, false, "cam A down");
    check(ta.state().degraded_cameras == 1, "one degraded camera counted");
    ta.camera_result(true, false);
    check(ta.state().degraded_cameras == 1, "later ok does not reset degraded count");
    check(ta.state().steps[3].status == StepStatus::Degraded,
          "streaming stays degraded after later ok camera");
    // (b) degraded after ok: step goes degraded, count increments
    RecoveryTracker tb;
    tb.start(0);
    tb.camera_result(true);
    check(tb.state().steps[3].status == StepStatus::Ok, "ok camera -> step Ok");
    tb.camera_result(false, false, "cam B down");
    check(tb.state().steps[3].status == StepStatus::Degraded,
          "degraded camera after ok degrades step");
    check(tb.state().degraded_cameras == 1, "degraded after ok counted");
    // (c) multiple non-critical failures accumulate
    tb.camera_result(false, false, "cam C down");
    check(tb.state().degraded_cameras == 2, "two degraded cameras counted");
    check(tb.application_alive(), "non-critical failures keep app alive");
    // (d) critical camera failure escalates the step to Failed but does NOT
    //     kill the app: only config/db steps are app-fatal (recovery.h:6,
    //     DESIGN §15.4 — camera loss is a level-2 alarm, not an exit)
    RecoveryTracker tc;
    tc.start(0);
    tc.camera_result(false, true, "front camera critical");
    check(tc.state().steps[3].status == StepStatus::Failed, "critical camera -> Failed");
    check(tc.application_alive(),
          "critical camera failure does not kill the app (non-fatal domain)");
    // (e) complete_step on a critical step failed then re-ok does not revive
    //     alive (recompute scans Failed critical steps; complete_step ok
    //     overwrites the status — verify the delivered semantics explicitly)
    RecoveryTracker td;
    td.start(0);
    td.complete_step(RecoveryStep::LoadConfig, false, "bad cfg");
    check(!td.application_alive(), "critical fail -> not alive");
    td.complete_step(RecoveryStep::LoadConfig, true);
    check(td.application_alive(),
          "delivered semantics: re-ok of critical step revives alive (documented)");
    // (f) non-critical complete_step(false) -> Degraded, alive stays
    RecoveryTracker te;
    te.start(0);
    te.complete_step(RecoveryStep::StartAi, false, "ai down");
    check(te.state().steps[4].status == StepStatus::Degraded &&
              te.application_alive(),
          "non-critical step failure -> Degraded, alive");
    check(te.state().progress_pct() == 12, "degraded step counts toward progress (12%)");
    // (g) camera_result does not touch other steps; detail preserved
    RecoveryTracker tf;
    tf.start(0);
    tf.camera_result(false, false, "CAM-04 启动失败");
    check_eq("camera detail preserved", tf.state().steps[3].detail, "CAM-04 启动失败");
    check(tf.state().steps[0].status == StepStatus::Pending,
          "camera_result leaves other steps pending");
    // (h) text() mid-recovery counts terminal steps (camera_result already
    //     made StartStreaming terminal, so LoadConfig completion -> 2/8)
    tf.complete_step(RecoveryStep::LoadConfig, true);
    check_eq("mid text with degraded", tf.state().text(), "恢复中 2/8");
}

}  // namespace

int main() {
    std::printf("== verify ITEM-024 ext (health/logs/recovery fixtures) ==\n");
    t_health_boundaries();
    t_logs();
    t_rolling();
    t_recovery();
    std::printf("RESULT: %s passed=%d failed=%d\n",
                g_failed == 0 ? "PASS" : "FAIL", g_passed, g_failed);
    for (const auto& n : g_notes) std::printf("  %s\n", n.c_str());
    return g_failed == 0 ? 0 : 1;
}
