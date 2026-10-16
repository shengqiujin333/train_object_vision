// Verifier-owned ext fixture for ITEM-022 (software_verification).
// Linked against the delivered librailhmi_core.a built by the verifier's own
// board_build.py TestRun round; asserts ITEM-022 acceptance boundaries the
// implementer's core-tests/maintenance-probe do not cover:
//   * ptz_check tolerance boundary: delta == tolerance -> Ok (<=, not <);
//     delta just above -> OutOfTolerance; zoom boundary; unverified -> Unverified
//     even when all deltas are zero
//   * manual recompute of deltas from actual/target (verifier-side, not
//     trusting PtzCheck fields) feeds build_ptz_log assertion matrix
//   * build_ptz_log column contract: action kind passthrough, target=="ptz",
//     result mapping (ok -> "ok"; fail -> "out_of_tolerance"; unverified ->
//     "unverified"), detail carries the action label + check text
//   * front_ai_ready 2x2 truth table (P6 shows front AI NOT_READY on over)
//   * build_maintenance_cameras: stale-frame demotion boundary (age >
//     stale_ms -> offline despite state==Live; age == stale_ms stays online),
//     missing runtime entry (fewer runtimes than registry) -> unstarted/—,
//     Degraded counts online, offline-state label, reconnect text absent at 0,
//     quality combination strings, per-index pairing (not by id)
//   * recording_status: exact used_pct integer math, GB/MB formatting
//     boundary (exactly 1 GiB -> "1.0 GB"; 0 bytes -> "0 MB"), stopped text
//   * health_text: -1 placeholders for NPU/CPU/temp, net_errors passthrough
//   * channel_view helper regression (channel_state_label six-state mapping,
//     format_fps epsilon, format_last_frame boundaries)
// Fix-round additions (v3 TEST_FAIL D-01/D-02 re-verify):
//   * build_maintenance_log generic row contract (action/target/result/detail
//     passthrough, no invention), reconnect row shape
//   * log_day_key: UTC YYYY-MM-DD via independent date math (not trusting
//     format_time_text); 1760000000000 ms -> 2025-10-09; t<=0 dash remains
//     dash and substr(0,10) of the dash placeholder -> "—" day key
//   * roll_logs_by_day: keeps latest max_days distinct days, max_days=0 keeps
//     all, drops whole oldest day (not oldest rows), preserves time order,
//     day boundary inclusivity (ms inside day), single-day input
// Pure logic, no Qt/RKNN — like the delivered probes.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/alarm_center.h"   // parse_date_utc_ms (verifier day math)
#include "railhmi/core/channel_view.h"
#include "railhmi/core/maintenance.h"
#include "railhmi/core/stream_state.h"

using namespace railhmi::core;

namespace {

int g_passed = 0, g_failed = 0;
std::vector<std::string> g_notes;

void check(bool ok, const std::string& name) {
    (ok ? g_passed : g_failed) += 1;
    if (!ok) g_notes.push_back("FAIL: " + name);
}

void check_contains(const std::string& got, const std::string& needle,
                    const std::string& name) {
    const bool ok = got.find(needle) != std::string::npos;
    (ok ? g_passed : g_failed) += 1;
    if (!ok) g_notes.push_back("FAIL: " + name + " got=[" + got + "] needle=[" + needle + "]");
}

void check_eq(const std::string& name, const std::string& got, const std::string& want) {
    check(got == want, name + " [" + got + "] want [" + want + "]");
}

// verifier-side manual delta recompute (does not trust PtzCheck fields)
PtzCheck verify_check(const PtzState& s, const PtzTolerance& tol) {
    PtzCheck c;
    if (!s.verified) {
        c.result = PtzResult::Unverified;
        c.ok = false;
        c.text = ptz_result_label(PtzResult::Unverified);
        return c;
    }
    const double pd = std::fabs(s.pan_actual - s.pan_target);
    const double td = std::fabs(s.tilt_actual - s.tilt_target);
    const double zd = std::fabs(s.zoom_actual - s.zoom_target);
    c.pan_delta = pd;
    c.tilt_delta = td;
    c.zoom_delta = zd;
    c.ok = pd <= tol.pan_deg && td <= tol.tilt_deg && zd <= tol.zoom;
    c.result = c.ok ? PtzResult::Ok : PtzResult::OutOfTolerance;
    c.text = ptz_result_label(c.result);
    return c;
}

// --- 1. tolerance boundary --------------------------------------------------
void t_tolerance() {
    const PtzTolerance tol;  // pan/tilt 1.0, zoom 0.1
    // deltas must be exactly representable; use half-fractions of the tol so
    // (actual-target) is binary-exact (0.5, 0.25 are powers of two halves)
    PtzState s;
    s.pan_target = 10; s.pan_actual = 10.5;    // delta 0.5 < 1.0
    s.tilt_target = 5; s.tilt_actual = 4.5;    // delta 0.5 < 1.0
    s.zoom_target = 1; s.zoom_actual = 1.25;   // delta 0.25 > 0.1
    const PtzCheck c = ptz_check(s, tol);
    const PtzCheck cv = verify_check(s, tol);
    check(cv.ok == c.ok && cv.result == c.result &&
              std::fabs(cv.pan_delta - c.pan_delta) < 1e-9 &&
              std::fabs(cv.tilt_delta - c.tilt_delta) < 1e-9 &&
              std::fabs(cv.zoom_delta - c.zoom_delta) < 1e-9,
          "delivered check matches verifier recomputation");
    check(!c.ok && c.result == PtzResult::OutOfTolerance,
          "zoom-only 0.25 > 0.1 -> Out (verify_check recomputation agrees)");

    // inclusive boundary with all three axes exactly at tol: construct via
    // target==actual∓1 pan/tilt (deltas 1.0, binary-exact) and zoom delta 0
    // (so FP noise on 0.1 cannot enter the verdict)
    PtzState exact = s;
    exact.pan_actual = 11;   // delta 1.0 == tol
    exact.tilt_actual = 6;   // delta 1.0 == tol
    exact.zoom_actual = exact.zoom_target;   // 0 <= 0.1 trivially
    const PtzCheck cex = ptz_check(exact, tol);
    check(cex.ok && cex.result == PtzResult::Ok,
          "pan/tilt delta == tolerance (inclusive <=, zoom 0) -> Ok");
    check_eq("ok label Chinese", ptz_result_label(cex.result), "\xE5\xAE\xB9\xE5\xB7\xAE\xE5\x86\x85");
    check_eq("ok to_string token", to_string(cex.result), "ok");
    check(std::fabs(cex.pan_delta - 1.0) < 1e-9 && std::fabs(cex.tilt_delta - 1.0) < 1e-9,
          "boundary deltas are exactly 1.0");

    // just above (binary-exact: 11.5-10 = 1.5 > 1.0)
    PtzState over = exact;
    over.pan_actual = 11.5;
    const PtzCheck c2 = ptz_check(over, tol);
    check(!c2.ok && c2.result == PtzResult::OutOfTolerance, "delta (1.5) above tolerance -> Out");
    check_eq("out label Chinese", ptz_result_label(c2.result),
             "\xE8\xB6\x85\xE5\xB7\xAE");
    check_eq("out to_string token", to_string(c2.result), "out_of_tolerance");

    // zoom solely out: pan/tilt at target, zoom delta 0.25
    PtzState zout = exact;
    zout.pan_actual = 10; zout.tilt_actual = 5;
    zout.zoom_actual = 1.25;
    const PtzCheck c3 = ptz_check(zout, tol);
    check(!c3.ok && std::fabs(c3.zoom_delta - 0.25) < 1e-9 && c3.pan_delta == 0 &&
              c3.tilt_delta == 0,
          "zoom-only violation -> Out, zoom delta 0.25, pan/tilt 0");

    // unverified: unreadable wins even with zero deltas
    PtzState un = s;
    un.pan_actual = 10; un.tilt_actual = 5; un.zoom_actual = 1;
    un.verified = false;
    const PtzCheck c4 = ptz_check(un, tol);
    check(c4.result == PtzResult::Unverified && !c4.ok, "verified=false + zero deltas -> Unverified");
    check_eq("unverified label", ptz_result_label(c4.result), "\xE6\x9C\xAA\xE6\xA0\xB8\xE9\xAA\x8C");
    check_eq("unverified token", to_string(c4.result), "unverified");
    // text of the boundary case contains the delta summary
    check(c.text.find("Pan") != std::string::npos && c.text.find("Tilt") != std::string::npos &&
              c.text.find("Zoom") != std::string::npos,
          "check text carries all three axes");
}

// --- 2. actions + log matrix -------------------------------------------------
void t_actions_logs() {
    const PtzTolerance tol;
    PtzState before; before.pan_target = 0; before.pan_actual = 15;   // initially off
    PtzState after; after.pan_target = 0; after.pan_actual = 0.1;
    after.tilt_target = 0; after.tilt_actual = -0.2;
    after.zoom_target = 1; after.zoom_actual = 1.02;

    PtzState bad = after; bad.tilt_actual = 4.0;       // over tolerance
    PtzState un = after; un.verified = false;          // unreadable readback

    const PtzAction home = ptz_home(before, after, tol);
    const PtzAction pre = ptz_recall_front_preset(before, after, tol);
    check(home.kind == "home" && home.label == "\xE4\xB8\x80\xE9\x94\xAE\xE5\xBD\x92\xE4\xBD\x8D",
          "home kind/label");
    check(pre.kind == "preset_front" &&
              pre.label == "\xE9\x87\x8D\xE8\xB0\x83\xE5\x89\x8D\xE6\x96\xB9\xE9\xA2\x84\xE7\xBD\xAE\xE4\xBD\x8D",
          "preset kind/label");
    check(home.requires_confirm && pre.requires_confirm, "both actions require confirm");
    check(home.applied && pre.applied, "both applied after confirm");
    check(home.before.pan_actual == 15 && home.after.pan_actual == 0.1,
          "action carries before/after snapshot");

    // log result mapping across the three outcomes
    const MaintenanceLogRow lok = build_ptz_log(home, "maintainer", 1760000000000LL, 11);
    check_eq("log result ok", lok.result, "ok");
    check_eq("log action passthrough", lok.action, "home");
    check_eq("log target fixed", lok.target, "ptz");
    check(lok.ts_wall_ms == 1760000000000LL && lok.ts_mono_us == 11, "log timestamps passthrough");
    check_eq("log user passthrough", lok.user, "maintainer");
    check(lok.detail.find("\xE4\xB8\x80\xE9\x94\xAE\xE5\xBD\x92\xE4\xBD\x8D") == 0,
          "log detail starts with action label");
    check(lok.detail.find("\xE5\xAE\xB9\xE5\xB7\xAE\xE5\x86\x85") != std::string::npos,
          "log detail carries tolerance verdict");

    PtzAction fail = ptz_recall_front_preset(before, bad, tol);
    const MaintenanceLogRow lfail = build_ptz_log(fail, "operator", 1, 2);
    check_eq("log result fail -> out_of_tolerance", lfail.result, "out_of_tolerance");

    PtzAction unread = ptz_home(before, un, tol);
    const MaintenanceLogRow lun = build_ptz_log(unread, "operator", 1, 2);
    check_eq("log result unverified passthrough", lun.result, "unverified");

    // empty user tolerated (passthrough, no invention)
    const MaintenanceLogRow lnobody = build_ptz_log(home, "", 1, 2);
    check_eq("log empty user passthrough", lnobody.user, "");
}

// --- 3. front AI readiness truth table --------------------------------------
void t_front_ai() {
    check(front_ai_ready(true, true), "ptz ok + ai available -> ready");
    check(!front_ai_ready(false, true), "ptz over -> front AI not ready (spec)");
    check(!front_ai_ready(true, false), "ai down -> front AI not ready");
    check(!front_ai_ready(false, false), "both bad -> not ready");
}

// --- 4. maintenance camera rows ----------------------------------------------
void t_cam_rows() {
    std::vector<CameraSlot> reg;
    CameraSlot a; a.id = "CAM-01"; a.name = "\xE5\x89\x8D\xE6\x96\xB9"; a.position = CameraPosition::Primary; a.order = 0;
    CameraSlot b; b.id = "CAM-02"; b.name = "\xE5\xB7\xA6"; b.position = CameraPosition::Left; b.order = 1;
    CameraSlot c; c.id = "CAM-03"; c.name = "\xE5\x8F\xB3"; c.position = CameraPosition::Right; c.order = 2;
    reg.push_back(a); reg.push_back(b); reg.push_back(c);

    // runtime shorter than registry: missing entries -> unstarted/—
    // (now=100000; frames at 100000-age so both fresh per stale_ms=3000)
    std::vector<CamRuntime> rt(2);
    rt[0].has_stream = true; rt[0].state = StreamState::Live; rt[0].fps = 25;
    rt[0].last_frame_ms = 100000 - 400;             // fresh, <1 秒 ago
    rt[0].reconnect_count = 0;
    rt[1].has_stream = true; rt[1].state = StreamState::Degraded; rt[1].fps = 24.96;
    rt[1].last_frame_ms = 100000 - 2000; rt[1].reconnect_count = 3;
    rt[1].quality.too_bright = true; rt[1].quality.blurred = true;

    const auto rows = build_maintenance_cameras(reg, rt, 100000, 3000);

    check(rows.size() == 3, "row per registered camera even without runtime");
    check_eq("row0 id", rows[0].id, "CAM-01");
    check(rows[0].online, "Live age0 online");
    check_eq("row0 state label", rows[0].state_label, "\xE5\xAE\x9E\xE6\x97\xB6");
    check_eq("row0 fps text", rows[0].fps_text, "25.0 fps");
    check_eq("row0 last text", rows[0].last_frame_text, "<1 \xE7\xA7\x92\xE5\x89\x8D");
    check_eq("row0 quality", rows[0].quality_label, "\xE6\xAD\xA3\xE5\xB8\xB8");
    // reconnect == 0 -> empty text (not "重连 0 次")
    check_eq("row0 reconnect absent at 0", rows[0].reconnect_text, "");

    // Degraded counts online; combined quality string "过曝/模糊" in flag order
    check(rows[1].online, "Degraded counts online");
    check_eq("row1 state label", rows[1].state_label, "\xE9\x99\x8D\xE7\xBA\xA7");
    check_eq("row1 quality combo (flag order: bright before blur)",
             rows[1].quality_label, "\xE8\xBF\x87\xE6\x9B\x9D/\xE6\xA8\xA1\xE7\xB3\x8A");
    check_eq("row1 reconnect text", rows[1].reconnect_text,
             "\xE9\x87\x8D\xE8\xBF\x9E 3 \xE6\xAC\xA1");

    // missing runtime -> unstarted, all placeholders
    check(!rows[2].online, "missing runtime offline");
    check_eq("row2 state", rows[2].state_label, "\xE6\x9C\xAA\xE5\x90\xAF\xE5\x8A\xA8");
    check_eq("row2 fps", rows[2].fps_text, "\xE2\x80\x94");
    check_eq("row2 last", rows[2].last_frame_text, "\xE2\x80\x94");
    check_eq("row2 quality", rows[2].quality_label, "\xE2\x80\x94");

    // stale boundary: age > stale_ms demotes to offline; equality stays
    std::vector<CamRuntime> rt2(1);
    rt2[0].has_stream = true; rt2[0].state = StreamState::Live; rt2[0].fps = 25;
    rt2[0].last_frame_ms = 100000;  // now 103001 -> age 3001 > 3000
    auto r2 = build_maintenance_cameras(reg, rt2, 103001, 3000);
    check(!r2[0].online, "age 3001 > stale 3000 -> online=false despite Live");
    rt2[0].last_frame_ms = 100000;   // now 103000 -> age == stale stays
    auto r3 = build_maintenance_cameras(reg, rt2, 103000, 3000);
    check(r3[0].online, "age == stale_ms stays online");

    // has_stream but last_frame never set -> fps shows but last is dash, offline
    std::vector<CamRuntime> rt3(1);
    rt3[0].has_stream = true; rt3[0].state = StreamState::Connecting; rt3[0].fps = 0;
    rt3[0].last_frame_ms = -1;
    auto r4 = build_maintenance_cameras(reg, rt3, 1000, 3000);
    check(!r4[0].online, "connecting is not online");
    check_eq("connecting label", r4[0].state_label, "\xE8\xBF\x9E\xE6\x8E\xA5\xE4\xB8\xAD");
    check_eq("fps 0 -> dash", r4[0].fps_text, "\xE2\x80\x94");
    check_eq("no frame -> dash", r4[0].last_frame_text, "\xE2\x80\x94");
}

// --- 5. recording / storage ---------------------------------------------------
void t_recording() {
    const long long gb = 1024LL * 1024LL * 1024LL;
    const RecordingStatus s = recording_status(true, 5 * gb, 20 * gb);
    check(s.running && s.used_pct == 75, "used_pct integer math 15/20 -> 75");
    check_eq("free text GB", s.free_text, "5.0 GB");
    check_eq("state text", s.state_text, "\xE5\xBD\x95\xE5\x83\x8F\xE4\xB8\xAD");

    const RecordingStatus mb = recording_status(true, 512 * 1024 * 1024, 0);
    check_eq("MB formatting", mb.free_text, "512 MB");
    check(mb.used_pct == 0, "total<=0 -> used_pct 0");

    const RecordingStatus one_gib = recording_status(false, gb, 10 * gb);
    check_eq("exactly 1 GiB -> 1.0 GB", one_gib.free_text, "1.0 GB");
    check_eq("stopped state", one_gib.state_text, "\xE5\xB7\xB2\xE5\x81\x9C\xE6\xAD\xA2");
    check(!one_gib.running, "running flag false");

    const RecordingStatus zero = recording_status(false, 0, 0);
    check_eq("zero free MB", zero.free_text, "0 MB");
}

// --- 6. health text -----------------------------------------------------------
void t_health() {
    HealthSnapshot h;
    h.npu_load = 42.4; h.cpu = 33.5; h.temp_c = 58.6; h.net_errors = 3;
    // round-half: 42.4->42, 33.5->34(not 33), 58.6->59(int temp 59)
    const std::string t = health_text(h);
    check_contains(t, "NPU 42%", "health npu round");
    check_contains(t, "CPU 34%", "health cpu round-half 33.5 -> 34");
    check_contains(t, "\xE6\xB8\xA9\xE5\xBA\xA6 59\xC2\xB0" "C", "health temp round 58.6 -> 59");
    check_contains(t, "\xE7\xBD\x91\xE7\xBB\x9C\xE9\x94\x99\xE8\xAF\xAF 3", "health net errors");

    HealthSnapshot nb;
    nb.npu_load = -1; nb.cpu = -1; nb.temp_c = -1; nb.net_errors = 0;
    const std::string t2 = health_text(nb);
    // all placeholders present
    check_contains(t2, "NPU \xE2\x80\x94 / CPU \xE2\x80\x94", "unavailable fields -> dash");
    check_contains(t2, "\xE7\xBD\x91\xE7\xBB\x9C\xE9\x94\x99\xE8\xAF\xAF 0", "net errors zero shown");
}

// --- 7. format helpers regression (channel view consumed by rows) ------------
void t_format_regression() {    check_eq("fps epsilon: 0 -> dash", format_fps(0.0), "\xE2\x80\x94");
    check_eq("fps 24.96 below epsilon behaves", format_fps(24.96), "25.0 fps");
    check_eq("last frame 999ms", format_last_frame(999), "<1 \xE7\xA7\x92\xE5\x89\x8D");
    check_eq("last frame 60s -> 1 分 0 秒", format_last_frame(60000), "1 \xE5\x88\x86 0 \xE7\xA7\x92\xE5\x89\x8D");
    check_eq("last frame 119s", format_last_frame(119000), "1 \xE5\x88\x86 59 \xE7\xA7\x92\xE5\x89\x8D");
    check_eq("last frame 1h", format_last_frame(3600000), "60 \xE5\x88\x86 0 \xE7\xA7\x92\xE5\x89\x8D");
    check_eq("negative age -> dash", format_last_frame(-5), "\xE2\x80\x94");
    check_eq("state label live", channel_state_label("live"), "\xE5\xAE\x9E\xE6\x97\xB6");
    check_eq("state label frozen", channel_state_label("frozen"), "\xE7\x94\xBB\xE9\x9D\xA2\xE5\x86\xBB\xE7\xBB\x93");
    check_eq("state label offline", channel_state_label("offline"), "\xE7\xA6\xBB\xE7\xBA\xBF");
    check_eq("state label reconnecting", channel_state_label("reconnecting"),
             "\xE9\x87\x8D\xE8\xBF\x9E\xE4\xB8\xAD");
    // unknown token passthrough (ITEM-015 contract)
    check_eq("unknown state token passthrough", channel_state_label("weird-state"), "weird-state");
}

}  // namespace

// --- 8. D-01 generic maintenance log (fix round) -----------------------------
void t_generic_maintenance_log() {
    // reconnect row: full column passthrough, no invented fields
    const MaintenanceLogRow rc = build_maintenance_log(
        "reconnect", "CAM-02", "ok", "\xE6\x89\x8B\xE5\x8A\xA8\xE9\x87\x8D\xE8\xBF\x9E",
        "operator", 1760000000000LL, 7);
    check_eq("D-01 generic log action", rc.action, "reconnect");
    check_eq("D-01 generic log target passthrough", rc.target, "CAM-02");
    check_eq("D-01 generic log result passthrough", rc.result, "ok");
    check_eq("D-01 generic log detail passthrough",
             rc.detail, "\xE6\x89\x8B\xE5\x8A\xA8\xE9\x87\x8D\xE8\xBF\x9E");
    check_eq("D-01 generic log user passthrough", rc.user, "operator");
    check(rc.ts_wall_ms == 1760000000000LL && rc.ts_mono_us == 7,
          "D-01 generic log timestamps passthrough");

    // non-reconnect generic rows must not be rewritten by the build layer
    const MaintenanceLogRow param = build_maintenance_log(
        "param_change", "CAM-05", "unverified", "\xE8\xAE\xBE\xE7\xBD\xAE\xE6\x9C\xAA\xE6\xA0\xB8\xE9\xAA\x8C",
        "", 5, 0);
    check_eq("generic log keys not rewritten (param_change)", param.action, "param_change");
    check_eq("generic result not rewritten", param.result, "unverified");
    check_eq("generic empty user stays empty", param.user, "");

    // build_maintenance_log and build_ptz_log agree on the shared columns
    const PtzTolerance tol;
    PtzState before; before.pan_target = 0; before.pan_actual = 15;
    PtzState after = before; after.pan_actual = 0.2; after.tilt_actual = -0.3;
    after.zoom_target = 1; after.zoom_actual = 1.05;
    const PtzAction home = ptz_home(before, after, tol);
    const MaintenanceLogRow via_ptz = build_ptz_log(home, "maintainer", 99, 3);
    const MaintenanceLogRow via_generic = build_maintenance_log(
        home.kind, "ptz", home.check.ok ? "ok" : to_string(home.check.result),
        home.label + " " + home.check.text, "maintainer", 99, 3);
    check(via_ptz.action == via_generic.action && via_ptz.target == via_generic.target &&
              via_ptz.result == via_generic.result && via_ptz.detail == via_generic.detail &&
              via_ptz.user == via_generic.user,
          "build_ptz_log == generic implementation of the same row (equivalence)");
}

// --- 9. D-02 log_day_key (verifier-side UTC math, not format_time_text) ------
void t_log_day_key() {
    // verifier's own civil-from-days (Howard Hinnant's algorithm) so the
    // expected day strings do not come from the delivered formatter
    auto days_from_civil = [](int y, unsigned m, unsigned d) -> long long {
        y -= m <= 2;
        const int era = (y >= 0 ? y : y - 399) / 400;
        const unsigned yoe = static_cast<unsigned>(y - era * 400);
        const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
        const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097LL + doe - 719468;
    };
    auto ms = [&](int y, unsigned m, unsigned d) -> long long {
        return (days_from_civil(y, m, d)) * 86400000LL;
    };

    check_eq("day key 1760000000000 -> 2025-10-09",
             log_day_key(1760000000000LL), "2025-10-09");
    check_eq("day key at 2026-10-16 00:00 UTC",
             log_day_key(ms(2026, 10, 16)), "2026-10-16");
    check_eq("day key last ms of 2026-10-15 (23:59:59.999)",
             log_day_key(ms(2026, 10, 16) - 1), "2026-10-15");
    check_eq("day key first ms of 2026-10-16",
             log_day_key(ms(2026, 10, 16)), "2026-10-16");
    check_eq("day key leap day 2024-02-29",
             log_day_key(ms(2024, 2, 29)), "2024-02-29");
    check_eq("day key year boundary 2025-12-31/2026-01-01",
             log_day_key(ms(2026, 1, 1)), "2026-01-01");
    // ts<=0 falls into the dash placeholder: substr(0,10) of "—" yields the
    // dash itself (documented placeholder, same convention as time_text)
    check_eq("day key ts<=0 placeholder", log_day_key(0), "\xE2\x80\x94");

    // day_key/ms round-trip consistency with the delivered parser
    const long long d0 = parse_date_utc_ms("2026-10-16");
    const long long inside = d0 + 3600000;               // 01:00:00 UTC same day
    check_eq("round trip day key (date parser -> day key)",
             log_day_key(inside), "2026-10-16");
}

// --- 10. D-02 roll_logs_by_day (keep-latest-days semantics) ------------------
void t_roll_logs_by_day() {
    // verifier-side day list: 4 distinct days around 2026-10-16
    const long long d13 = parse_date_utc_ms("2026-10-13");
    const long long d14 = parse_date_utc_ms("2026-10-14");
    const long long d15 = parse_date_utc_ms("2026-10-15");
    const long long d16 = parse_date_utc_ms("2026-10-16");
    const MaintenanceLogRow old1 = build_maintenance_log("home", "ptz", "ok", "d13a", "u", d13, 1);
    const MaintenanceLogRow old2 = build_maintenance_log("home", "ptz", "ok", "d13b", "u", d13 + 1, 2);
    const MaintenanceLogRow mid = build_maintenance_log("home", "ptz", "ok", "d14", "u", d14, 3);
    const MaintenanceLogRow recent = build_maintenance_log("home", "ptz", "ok", "d15", "u", d15, 4);
    const MaintenanceLogRow today1 = build_maintenance_log("reconnect", "CAM-01", "ok", "d16a", "u", d16, 5);
    const MaintenanceLogRow today2 = build_maintenance_log("home", "ptz", "ok", "d16b", "u", d16 + 5, 6);
    const std::vector<MaintenanceLogRow> logs = {old1, old2, mid, recent, today1, today2};

    // max_days=2 keeps whole days 15 and 16: day15 has 1 row, day16 has 2
    // rows -> 3 rows; drops the whole 13/14 days (no partial-day drop)
    const std::vector<MaintenanceLogRow> r2 = roll_logs_by_day(logs, 2);
    check(r2.size() == 3, "roll 6 logs/4 days max_days=2 -> 3 rows (1+2)");
    check(r2[0].detail == "d15" && r2[1].detail == "d16a" && r2[2].detail == "d16b",
          "roll keeps days 15/16 in time order");
    // verifier-side filter: all kept rows must carry one of the two latest days
    for (const auto& r : r2) {
        const std::string d = log_day_key(r.ts_wall_ms);
        check(d == "2026-10-15" || d == "2026-10-16",
              "kept row in a dropped day: " + d + " " + r.detail);
    }
    check(std::find_if(r2.begin(), r2.end(), [](const MaintenanceLogRow& r) {
              return log_day_key(r.ts_wall_ms) == "2026-10-13";
          }) == r2.end(), "day 13 rows fully dropped");
    check(std::find_if(r2.begin(), r2.end(), [](const MaintenanceLogRow& r) {
              return log_day_key(r.ts_wall_ms) == "2026-10-14";
          }) == r2.end(), "day 14 rows fully dropped");
    check(log_day_key(r2.front().ts_wall_ms) == "2026-10-15" &&
              log_day_key(r2.back().ts_wall_ms) == "2026-10-16",
          "kept order oldest-first within kept days");

    // max_days=1 keeps only today's rows
    const std::vector<MaintenanceLogRow> r1 = roll_logs_by_day(logs, 1);
    check(r1.size() == 2 && r1[0].detail == "d16a" && r1[1].detail == "d16b",
          "roll max_days=1 -> only the latest day (2 rows)");

    // max_days=0 keeps all (documented sentinel)
    check(roll_logs_by_day(logs, 0).size() == logs.size(),
          "max_days=0 sentinel keeps all rows");
    check(roll_logs_by_day(logs, -1).size() == logs.size(),
          "non-positive max_days treated as keep-all (no accidental wipe)");

    // day-boundary inclusivity: a row exactly at 00:00:00.000 belongs to that day
    const std::vector<MaintenanceLogRow> edge = {
        build_maintenance_log("home", "ptz", "ok", "e_prev", "", d16 - 1, 1),
        build_maintenance_log("home", "ptz", "ok", "e_exact", "", d16, 2),
        build_maintenance_log("home", "ptz", "ok", "e_last", "", d16 + 86399999, 3),
    };
    const std::vector<MaintenanceLogRow> re1 = roll_logs_by_day(edge, 1);
    check(re1.size() == 2 && re1[0].detail == "e_exact" && re1[1].detail == "e_last",
          "00:00.000 belongs to the new day; last ms of the day kept with it");

    // five distinct days with 1 kept day drops the single 2026-10-09 row of
    // the probe scenario shape (mirrors the delivered probe inputs)
    std::vector<MaintenanceLogRow> probe_shape = {
        build_maintenance_log("home", "ptz", "ok", "now1", "", 1760000000000LL, 1),
        build_maintenance_log("preset_front", "ptz", "out_of_tolerance", "now2", "",
                              1760000000000LL, 2),
        build_maintenance_log("reconnect", "CAM-02", "ok", "now3", "", 1760000000000LL, 3),
        build_maintenance_log("home", "ptz", "ok", "old", "",
                              1760000000000LL - 5LL * 86400000LL, 4),
    };
    const std::vector<MaintenanceLogRow> probe_rolled = roll_logs_by_day(probe_shape, 1);
    check(probe_rolled.size() == 3 &&
              probe_rolled[probe_rolled.size() - 1].detail == "now3",
          "probe scenario: 4 logs (3 on 2025-10-09 + 1 on 2025-10-04) max_days=1 -> 3");
    check(log_day_key(probe_shape[0].ts_wall_ms) == "2025-10-09",
          "probe scenario day key 2025-10-09 (verifier math)");

    // single-day input is untouched
    const std::vector<MaintenanceLogRow> one_day = {today1, today2};
    check(roll_logs_by_day(one_day, 3).size() == 2,
          "single-day input survives any max_days");
}

int main() {
    std::printf("== verify ITEM-022 ext (device maintenance page fixtures) ==\n");
    t_tolerance();
    t_actions_logs();
    t_front_ai();
    t_cam_rows();
    t_recording();
    t_health();
    t_format_regression();
    t_generic_maintenance_log();
    t_log_day_key();
    t_roll_logs_by_day();
    std::printf("RESULT: %s passed=%d failed=%d\n",
                g_failed == 0 ? "PASS" : "FAIL", g_passed, g_failed);
    for (const auto& n : g_notes) std::printf("  %s\n", n.c_str());
    return g_failed == 0 ? 0 : 1;
}
