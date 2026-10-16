// railhmi 设备维护模型探针（ITEM-022）
// 演示：PTZ 实际值 vs 目标值容差校验、一键归位/重调前方预置位（二次确认）、超差 -> 前方 AI 未就绪、
// 维护日志写入、摄像头维护行、录像/存储、网络与温度。纯 core，无 Qt/SQLite。
//
// 用法: railhmi-maintenance-probe [--out log]
// 退出码: 0 验收 PASS；1 FAIL。

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/maintenance.h"

using namespace railhmi::core;

int main(int argc, char** argv) {
    std::string out;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--out") next(&out);
        else if (a == "--help" || a == "-h") { std::printf("usage: railhmi-maintenance-probe [--out log]\n"); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }
    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) { std::printf("%s\n", line.c_str()); if (fo) fo << line << "\n"; };

    PtzTolerance tol;   // pan/tilt 1.0°, zoom 0.1

    PtzState before;
    before.pan_target = 0; before.pan_actual = 15;     // 归位前明显偏
    before.tilt_target = 0; before.tilt_actual = 8; before.zoom_target = 1; before.zoom_actual = 2;

    PtzState good;   // 归位后容差内
    good.pan_target = 0; good.pan_actual = 0.2;
    good.tilt_target = 0; good.tilt_actual = -0.3;
    good.zoom_target = 1; good.zoom_actual = 1.05;

    PtzState bad = good; bad.tilt_actual = 4.0;   // 超差

    const PtzCheck cgood = ptz_check(good, tol);
    const PtzCheck cbad = ptz_check(bad, tol);
    emit("type=ptz-check result_ok=" + std::string(to_string(cgood.result)) + " text=\"" + cgood.text + "\"");
    emit("type=ptz-check result_bad=" + std::string(to_string(cbad.result)) + " text=\"" + cbad.text + "\"");

    const PtzAction home = ptz_home(before, good, tol);
    const PtzAction preset = ptz_recall_front_preset(before, good, tol);
    emit("type=action kind=" + home.kind + " confirm=" + (home.requires_confirm ? "true" : "false") +
         " applied=" + (home.applied ? "true" : "false") + " result=" + to_string(home.check.result));
    emit("type=action kind=" + preset.kind + " confirm=" + (preset.requires_confirm ? "true" : "false") +
         " applied=" + (preset.applied ? "true" : "false") + " result=" + to_string(preset.check.result));

    const MaintenanceLogRow log = build_ptz_log(home, "maintainer", 1760000000000LL, 12345);
    emit("type=maint-log action=" + log.action + " user=" + log.user + " result=" + log.result +
         " detail=\"" + log.detail + "\"");

    const PtzAction fail = ptz_recall_front_preset(before, bad, tol);
    const MaintenanceLogRow flog = build_ptz_log(fail, "maintainer", 1760000000000LL, 12346);
    emit("type=action kind=" + fail.kind + " result=" + to_string(fail.check.result));
    emit("type=maint-log action=" + flog.action + " result=" + flog.result + " detail=\"" + flog.detail + "\"");

    // D-01 手动重连 -> 维护日志；D-02 按日滚动
    const MaintenanceLogRow rc = build_maintenance_log("reconnect", "CAM-02", "ok", "手动重连", "maintainer", 1760000000000LL, 7);
    emit("type=maint-log action=" + rc.action + " target=" + rc.target + " result=" + rc.result + " detail=\"" + rc.detail + "\"");
    std::vector<MaintenanceLogRow> alllogs = {log, flog, rc};
    alllogs.push_back(build_maintenance_log("home", "ptz", "ok", "旧日志（应滚动淘汰）", "maintainer",
                                            1760000000000LL - 5LL * 86400000LL, 8));
    const std::vector<MaintenanceLogRow> rolled = roll_logs_by_day(alllogs, 1);
    emit("type=log-days total=" + std::to_string(alllogs.size()) + " rolled=" + std::to_string(rolled.size()) +
         " max_days=1 day_key=" + log_day_key(1760000000000LL));

    const bool ready_ok = front_ai_ready(cgood.ok, true);
    const bool ready_fail = front_ai_ready(cbad.ok, true);
    emit("type=front-ai ptz_ok=" + std::string(ready_ok ? "READY" : "NOT_READY") +
         " ptz_over=" + std::string(ready_fail ? "READY" : "NOT_READY"));

    // 摄像头维护行
    std::vector<CameraSlot> reg;
    CameraSlot s1; s1.id = "CAM-01"; s1.name = "前方"; s1.position = CameraPosition::Primary;
    CameraSlot s2; s2.id = "CAM-02"; s2.name = "左侧"; s2.position = CameraPosition::Left;
    reg = {s1, s2};
    std::vector<CamRuntime> rt(2);
    rt[0].has_stream = true; rt[0].state = StreamState::Live; rt[0].fps = 25; rt[0].last_frame_ms = 99500;
    rt[1].has_stream = true; rt[1].state = StreamState::Frozen; rt[1].last_frame_ms = 90000; rt[1].quality.too_dark = true;
    for (const auto& r : build_maintenance_cameras(reg, rt, 100000))
        emit("type=cam id=" + r.id + " online=" + (r.online ? "true" : "false") + " state=" + r.state_label +
             " fps=" + r.fps_text + " last=" + r.last_frame_text + " quality=" + r.quality_label);

    const long long gb = 1024LL * 1024LL * 1024LL;
    const RecordingStatus rs = recording_status(true, 5 * gb, 20 * gb);
    emit("type=recording running=" + std::string(rs.running ? "true" : "false") +
         " used_pct=" + std::to_string(rs.used_pct) + " free=" + rs.free_text + " state=" + rs.state_text);
    HealthSnapshot h; h.npu_load = 42; h.cpu = 33; h.temp_c = 58; h.net_errors = 3;
    emit("type=health text=\"" + health_text(h) + "\"");

    const bool ok = (cgood.ok) && (!cbad.ok) && (cbad.result == PtzResult::OutOfTolerance) &&
                    home.requires_confirm && home.applied && home.check.ok && preset.check.ok &&
                    (log.result == "ok") && (flog.result == "out_of_tolerance") && ready_ok && !ready_fail &&
                    !build_maintenance_cameras(reg, rt, 100000)[1].online &&
                    (rs.used_pct == 75) && (rc.action == "reconnect") && (rolled.size() == 3);
    emit(std::string("type=acceptance ptz_within_ok=") + (cgood.ok ? "true" : "false") +
         " ptz_over_detected=" + (!cbad.ok ? "true" : "false") +
         " home_logged=" + (log.result == "ok" ? "true" : "false") +
         " reconnect_logged=" + (rc.action == "reconnect" ? "true" : "false") +
         " log_day_rolling=" + (rolled.size() == 3 ? "true" : "false") +
         " over_tolerance_blocks_front_ai=" + (!ready_fail ? "true" : "false") +
         " result=" + (ok ? "PASS" : "FAIL"));
    return ok ? 0 : 1;
}
