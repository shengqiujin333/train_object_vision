// railhmi-core 设备维护模型测试（ITEM-022 / INTERACTION §5.6、VISUAL §8.6）。
// 覆盖：PTZ 实际值 vs 目标值容差校验、归位/预置位二次确认动作与维护日志、超差 -> 前方 AI 未就绪、
// 摄像头维护行（在线/状态/帧率/最近帧/质量/重连）、录像/存储、网络与温度文本。

#include "test_util.h"

#include <string>
#include <vector>

#include "railhmi/core/alarm_center.h"   // parse_date_utc_ms
#include "railhmi/core/maintenance.h"

namespace {

using namespace railhmi::core;

std::vector<CameraSlot> two_slots() {
    std::vector<CameraSlot> v;
    CameraSlot a; a.id = "CAM-01"; a.name = "前方"; a.position = CameraPosition::Primary; a.order = 0;
    CameraSlot b; b.id = "CAM-04"; b.name = "车顶"; b.position = CameraPosition::Roof; b.order = 1;
    v.push_back(a); v.push_back(b);
    return v;
}

void test_ptz_check() {
    PtzTolerance tol;   // pan/tilt 1.0°, zoom 0.1
    PtzState ok;
    ok.pan_actual = 0.2; ok.pan_target = 0.0;
    ok.tilt_actual = -0.3; ok.tilt_target = 0.0;
    ok.zoom_actual = 1.05; ok.zoom_target = 1.0;
    const PtzCheck c = ptz_check(ok, tol);
    railhmi_test::check(c.result == PtzResult::Ok && c.ok, "within tolerance -> ok");
    railhmi_test::check(c.pan_delta > 0.19 && c.pan_delta < 0.21, "pan delta computed");
    railhmi_test::check(c.text.find("容差内") != std::string::npos, "ok text");

    PtzState out = ok;
    out.tilt_actual = 3.5;   // Δ3.5 > 1.0
    const PtzCheck c2 = ptz_check(out, tol);
    railhmi_test::check(c2.result == PtzResult::OutOfTolerance && !c2.ok, "over tolerance -> out");
    railhmi_test::check(c2.text.find("超差") != std::string::npos, "out text");

    PtzState un = ok;
    un.verified = false;
    railhmi_test::check(ptz_check(un, tol).result == PtzResult::Unverified, "unreadable -> unverified");
}

void test_ptz_actions_and_log() {
    PtzTolerance tol;
    PtzState before; before.pan_target = 10; before.pan_actual = 12;   // 之前超差
    PtzState after; after.pan_target = 0; after.pan_actual = 0.1;
    after.tilt_target = 0; after.tilt_actual = -0.2; after.zoom_target = 1; after.zoom_actual = 1.02;
    const PtzAction home = ptz_home(before, after, tol);
    railhmi_test::check(home.requires_confirm && home.applied, "home requires confirm then applied");
    railhmi_test::check(home.check.ok, "home check ok");
    const PtzAction pre = ptz_recall_front_preset(before, after, tol);
    railhmi_test::check(pre.kind == "preset_front" && pre.check.ok, "preset check ok");

    const MaintenanceLogRow log = build_ptz_log(home, "maintainer", 1000, 55);
    railhmi_test::check(log.action == std::string("home") && log.user == std::string("maintainer"),
                        "ptz log action/user");
    railhmi_test::check(log.result == std::string("ok"), "ptz log result ok");
    railhmi_test::check(log.detail.find("一键归位") != std::string::npos &&
                            log.detail.find("容差内") != std::string::npos,
                        "ptz log detail");

    PtzState bad = after; bad.tilt_actual = 4.0;
    const PtzAction fail = ptz_recall_front_preset(before, bad, tol);
    railhmi_test::check(!fail.check.ok, "over-tolerance action not ok");
    const MaintenanceLogRow flog = build_ptz_log(fail, "maintainer", 1000, 55);
    railhmi_test::check(flog.result == std::string("out_of_tolerance"), "failed action logged as out_of_tolerance");
}

void test_front_ai_readiness() {
    railhmi_test::check(front_ai_ready(true, true), "ptz ok + ai ok -> ready");
    railhmi_test::check(!front_ai_ready(false, true), "ptz over-tolerance -> front AI NOT ready");
    railhmi_test::check(!front_ai_ready(true, false), "ai down -> front AI NOT ready");
}

void test_camera_rows() {
    std::vector<CamRuntime> rt(2);
    rt[0].has_stream = true;
    rt[0].state = StreamState::Live;
    rt[0].fps = 25.0;
    rt[0].last_frame_ms = 99500;   // now 100000 -> <1 秒前
    rt[0].reconnect_count = 2;
    rt[1].has_stream = true;
    rt[1].state = StreamState::Frozen;
    rt[1].fps = 0.0;
    rt[1].last_frame_ms = 90000;   // 10s 前
    rt[1].quality.too_dark = true;
    rt[1].quality.occluded = true;
    const std::vector<CamMaintenanceRow> rows = build_maintenance_cameras(two_slots(), rt, 100000);
    railhmi_test::check(rows.size() == 2, "two maintenance rows");
    railhmi_test::check(rows[0].online && rows[0].state_label == "实时" && rows[0].fps_text == "25.0 fps",
                        "live camera row");
    railhmi_test::check(rows[0].last_frame_text == "<1 秒前" && rows[0].reconnect_text == "重连 2 次",
                        "live last frame/reconnect");
    railhmi_test::check(!rows[1].online && rows[1].quality_label == "过暗/遮挡", "frozen + quality flags");
    railhmi_test::check(quality_label(QualityFlags{}) == "正常", "quality normal");
}

void test_recording_health() {
    const long long gb = 1024LL * 1024LL * 1024LL;
    const RecordingStatus s = recording_status(true, 5 * gb, 20 * gb);
    railhmi_test::check(s.running && s.used_pct == 75, "recording used pct");
    railhmi_test::check(s.free_text == "5.0 GB" && s.state_text == "录像中", "recording text");
    const RecordingStatus stopped = recording_status(false, 0, 20 * gb);
    railhmi_test::check(!stopped.running && stopped.state_text == "已停止", "stopped recording");

    HealthSnapshot h;
    h.npu_load = 42; h.cpu = 33; h.temp_c = 58; h.net_errors = 3;
    const std::string t = health_text(h);
    railhmi_test::check(t.find("NPU 42%") != std::string::npos && t.find("温度 58°C") != std::string::npos &&
                            t.find("网络错误 3") != std::string::npos,
                        "health text");
}

void test_day_rolling_logs() {
    // D-02：日键 + 按日滚动（保留最近 max_days 天）
    const long long day = parse_date_utc_ms("2026-10-16");
    std::vector<MaintenanceLogRow> logs;
    logs.push_back(build_maintenance_log("reconnect", "CAM-01", "ok", "手动重连", "maint", day - 3 * 86400000LL, 1));
    logs.push_back(build_maintenance_log("home", "ptz", "ok", "一键归位", "maint", day - 2 * 86400000LL, 2));
    logs.push_back(build_maintenance_log("reconnect", "CAM-02", "ok", "手动重连", "maint", day - 86400000LL, 3));
    logs.push_back(build_maintenance_log("preset_front", "ptz", "ok", "重调前方预置位", "maint", day, 4));
    railhmi_test::check(log_day_key(day) == "2026-10-16", "log day key (D-02)");
    const std::vector<MaintenanceLogRow> rolled = roll_logs_by_day(logs, 2);
    railhmi_test::check(rolled.size() == 2, "roll keeps latest 2 days (D-02)");
    railhmi_test::check(rolled[0].action == "reconnect" && rolled[0].target == "CAM-02" &&
                            rolled[1].action == "preset_front",
                        "rolled entries are the most recent days");
    railhmi_test::check(roll_logs_by_day(logs, 0).size() == logs.size(), "max_days=0 keeps all");
    const MaintenanceLogRow rc = build_maintenance_log("reconnect", "CAM-03", "ok", "手动重连", "maint", day, 5);
    railhmi_test::check(rc.action == "reconnect" && rc.target == "CAM-03" && rc.result == "ok",
                        "generic maintenance log (reconnect)");
}

}  // namespace

void run_maintenance_tests() {
    test_ptz_check();
    test_ptz_actions_and_log();
    test_front_ai_readiness();
    test_camera_rows();
    test_recording_health();
    test_day_rolling_logs();
}
