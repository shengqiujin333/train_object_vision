// Verifier-owned ext fixture for ITEM-023 (software_verification).
// Linked against the delivered librailhmi_core.a built by the verifier's own
// board_build.py TestRun round; asserts ITEM-023 acceptance boundaries the
// implementer's core-tests (test_permissions.cpp) and permissions-probe do
// not cover:
//   * FULL 13x4 matrix at export_ops_allowed=false AND =true (the delivered
//     tests only sample 6 permission cells; the matrix doc is checked textually
//     by the implementer checker, not behaviorally)
//   * matrix label contract (label == permission_label), unknown enum -> token
//   * gate reason EXACTLY "权限不足" when denied; confirm gate leaves
//     non-sensitive permissions unmodified (requires_confirm==false, reason
//     empty at confirmed OR not) — the delivered tests only assert two reasons
//   * action state machine: wrong-role unconfirmed -> denied (NOT cancelled);
//     right-role unconfirmed -> cancelled and NOT logged by contract
//     build_action_log passthrough; snapshot export same shape as recording;
//     detail text carries the value; order of permission resolution
//   * export_ops boundary: cfg with export_ops_allowed=true flips maintainer
//     gate but NOT snapshot-only drivers, and admin flips neither way
//   * requires_secondary_confirm only on the four sensitive permissions (full
//     13-permission scan, delivered tests sample only 4 non-sensitive)
//   * build_action_log column contract (passthrough of the action fields, no
//     invention); log of a denied/cancelled action is NOT executed in the
//     bridge seam (core returns executed=false; no log written) — this fixture
//     asserts the core-level action/log seam directly
//   * roll_logs_by_day semantics regression for exported logs (ITEM-022 D-02
//     coupling) on the ITEM-023 action-log scenario shape
// Pure logic, no Qt/RKNN — like the delivered probes.
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "railhmi/core/permissions.h"
#include "railhmi/core/maintenance.h"
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

const char* yn(bool b) { return b ? "Y" : "-"; }

// --- 1. full 13x4 matrix (both config states) -------------------------------
void t_matrix_full() {
    PermissionsConfig off;   // export_ops_allowed = false (default)
    PermissionsConfig on;
    on.export_ops_allowed = true;

    // derived from INTERACTION_DESIGN.md §8 (authority), expressed as the
    // delivered permission token order
    struct Expect { Permission p; bool d, m, dev, a; };
    const Expect rows_off[13] = {
        {Permission::ViewRealtime,        true, true, true, true},
        {Permission::ViewAlarmCenter,     true, true, true, true},
        {Permission::ViewReplay,          true, true, true, true},
        {Permission::ViewAllCameras,      false, true, true, true},
        {Permission::ViewAiDebug,         false, true, true, true},
        {Permission::ViewMaintenance,     false, true, true, true},
        {Permission::Acknowledge,         true, true, true, true},
        {Permission::ExportRecording,     false, false, false, true},   // off: admin only
        {Permission::ExportSnapshot,      false, false, false, true},
        {Permission::ModifyDeviceConfig,  false, true, true, true},
        {Permission::ModifySystemConfig,  false, false, false, true},
        {Permission::PtzControl,          false, true, true, true},
        {Permission::Reconnect,           false, true, true, true},
    };
    const int n = static_cast<int>(Permission::Count);
    check(n == 13, "permission count is 13 (enum contract)");

    // matrix-off: the §8-derived baseline
    for (const Expect& e : rows_off) {
        const Role rs[4] = {Role::Driver, Role::Maintainer, Role::Developer, Role::Admin};
        const bool want[4] = {e.d, e.m, e.dev, e.a};
        for (int i = 0; i < 4; ++i) {
            const bool got = role_has_permission(rs[i], e.p, off);
            check(got == want[i], std::string("matrix-off ") + to_string(e.p) +
                  " role=" + to_string(rs[i]) + " got=" + yn(got) + " want=" + yn(want[i]));
        }
        // export/config permissions carry the "受配置" column only via the
        // config; verify label non-empty (UI renders modelData.label)
        check(permission_label(e.p) != std::string("") &&
                  std::string(permission_label(e.p)) != "未知",
              "label non-empty for " + std::string(to_string(e.p)));
    }

    // config-on flips exactly the export rows for maintainer/developer
    for (const Expect& e : rows_off) {
        const Role rs[4] = {Role::Driver, Role::Maintainer, Role::Developer, Role::Admin};
        const bool base[4] = {e.d, e.m, e.dev, e.a};
        bool want[4];
        for (int i = 0; i < 4; ++i) want[i] = base[i];
        if (e.p == Permission::ExportRecording || e.p == Permission::ExportSnapshot) {
            want[1] = true;   // maintainer allowed when config on
            want[2] = true;   // developer allowed when config on
            // driver stays false, admin stays true
        }
        for (int i = 0; i < 4; ++i) {
            const bool got = role_has_permission(rs[i], e.p, on);
            check(got == want[i], std::string("matrix-on ") + to_string(e.p) +
                  " role=" + to_string(rs[i]) + " got=" + yn(got) + " want=" + yn(want[i]));
        }
    }

    // permission_matrix rows mirror role_has_permission exactly (13 rows,
    // in enum order) for both configs
    const std::vector<PermissionRow> moff = permission_matrix(off);
    const std::vector<PermissionRow> mon = permission_matrix(on);
    check(moff.size() == 13 && mon.size() == 13, "matrix output has 13 rows both configs");
    for (int i = 0; i < 13; ++i) {
        const Permission p = static_cast<Permission>(i);
        check(moff[i].permission == p,
              std::string("matrix row enumeration order (off, ") + to_string(p) + ")");
        check_eq("matrix label mirrors permission_label (off)", moff[i].label,
                 std::string(permission_label(p)));
        check(moff[i].driver == role_has_permission(Role::Driver, p, off) &&
                  moff[i].maintainer == role_has_permission(Role::Maintainer, p, off) &&
                  moff[i].developer == role_has_permission(Role::Developer, p, off) &&
                  moff[i].admin == role_has_permission(Role::Admin, p, off),
              "matrix row mirrors role_has_permission (off, " + std::string(to_string(p)) + ")");
        // on-config: export rows flip for maintainer/developer; verify at the
        // matrix seams
        const bool export_row =
            (p == Permission::ExportRecording || p == Permission::ExportSnapshot);
        check(mon[i].driver == moff[i].driver &&
                  mon[i].admin == moff[i].admin &&
                  mon[i].maintainer == (export_row ? true : moff[i].maintainer) &&
                  mon[i].developer == (export_row ? true : moff[i].developer),
              "matrix-on flip limited to export ops (" + std::string(to_string(p)) + ")");
    }
}

// --- 2. gate semantics (full reason matrix) ---------------------------------
void t_gate() {
    PermissionsConfig off;
    PermissionsConfig on;
    on.export_ops_allowed = true;

    // denied -> reason exactly 权限不足, confirm flag mirrors requires_secondary_confirm
    const ActionGate gd = permission_gate(Role::Driver, Permission::ExportRecording, off, true);
    check(!gd.allowed && gd.reason == "\xE6\x9D\x83\xE9\x99\x90\xE4\xB8\x8D\xE8\xB6\xB3",
          "driver export gate reason exact 权限不足");
    check(gd.requires_confirm, "denied gate still reports requires_confirm=true (sensitive perm)");

    // sensitive allowed + unconfirmed -> 需二次确认, confirmed=false
    const ActionGate gp = permission_gate(Role::Admin, Permission::ExportRecording, off, false);
    check(gp.allowed && gp.requires_confirm && !gp.confirmed &&
              gp.reason == "\xE9\x9C\x80\xE4\xBA\x8C\xE6\xAC\xA1\xE7\xA1\xAE\xE8\xAE\xA4",
          "admin export unconfirmed gate reason exact 需二次确认");

    // sensitive allowed + confirmed -> reason empty
    const ActionGate ga = permission_gate(Role::Admin, Permission::ExportRecording, off, true);
    check(ga.allowed && ga.confirmed && ga.reason.empty(), "admin export confirmed gate reason empty");

    // non-sensitive permission: requires_confirm=false, reason empty whether
    // confirmed or not (not asserted by delivered tests)
    const ActionGate gv1 = permission_gate(Role::Driver, Permission::ViewReplay, off, false);
    const ActionGate gv2 = permission_gate(Role::Driver, Permission::ViewReplay, off, true);
    check(gv1.allowed && !gv1.requires_confirm && gv1.reason.empty() && !gv1.confirmed,
          "view gate: no confirm, empty reason, unconfirmed");
    check(gv2.allowed && !gv2.requires_confirm && gv2.reason.empty() && gv2.confirmed,
          "view gate confirmed still empty reason");

    // config-gated admin unaffected by config; maintainer flips with config
    check(!permission_gate(Role::Maintainer, Permission::ExportRecording, off, true).allowed,
          "maintainer export gated off");
    check(permission_gate(Role::Maintainer, Permission::ExportRecording, on, true).allowed &&
              permission_gate(Role::Maintainer, Permission::ExportRecording, on, true).reason.empty(),
          "maintainer export gated on (confirmed)");
    // maintainer unconfirmed + config on still needs confirm
    const ActionGate gm = permission_gate(Role::Maintainer, Permission::ExportRecording, on, false);
    check(gm.allowed && gm.requires_confirm && gm.reason == "\xE9\x9C\x80\xE4\xBA\x8C\xE6\xAC\xA1\xE7\xA1\xAE\xE8\xAE\xA4",
          "maintainer config-on unconfirmed still 需二次确认");

    // denied reason wins over confirm reason (unknown role/perm combos)
    const ActionGate gdn = permission_gate(Role::Driver, Permission::ModifySystemConfig, off, false);
    check(!gdn.allowed && gdn.requires_confirm && gdn.reason == "\xE6\x9D\x83\xE9\x99\x90\xE4\xB8\x8D\xE8\xB6\xB3",
          "denied sensitive: reason is 权限不足 (not 需二次确认)");
    // unknown permission (Count) is always denied
    const ActionGate gu = permission_gate(Role::Admin, Permission::Count, off, true);
    check(!gu.allowed && !gu.reason.empty(), "Count permission -> denied with reason");
}

// --- 3. secondary-confirm full scan -----------------------------------------
void t_confirm_scan() {
    const bool sensitive[13] = {
        false, false, false, false, false, false, false,   // view*+ack
        true, true,                                        // export_* (enum idx 7,8?)
        true, true,                                        // modify_device/system
        false, false,                                      // ptz, reconnect
    };
    // NOTE: enum order is ViewRealtime(0) ViewAlarmCenter(1) ViewReplay(2)
    // ViewAllCameras(3) ViewAiDebug(4) ViewMaintenance(5) Acknowledge(6)
    // ExportRecording(7) ExportSnapshot(8) ModifyDeviceConfig(9)
    // ModifySystemConfig(10) PtzControl(11) Reconnect(12) — verified below by
    // to_string mapping rather than assuming index arithmetic.
    check_eq("enum idx7 token", to_string(static_cast<Permission>(7)), "export_recording");
    check_eq("enum idx8 token", to_string(static_cast<Permission>(8)), "export_snapshot");
    check_eq("enum idx9 token", to_string(static_cast<Permission>(9)), "modify_device_config");
    check_eq("enum idx10 token", to_string(static_cast<Permission>(10)), "modify_system_config");
    for (int i = 0; i < 13; ++i) {
        const Permission p = static_cast<Permission>(i);
        check(requires_secondary_confirm(p) == sensitive[i],
              "requires_secondary_confirm scan " + std::string(to_string(p)));
    }
}

// --- 4. action state machine (no trusted flags) ------------------------------
void t_action_machine() {
    PermissionsConfig off;
    PermissionsConfig on;
    on.export_ops_allowed = true;

    // driver + denied + ANY confirm -> denied (never executed)
    const SensitiveAction d1 = export_recording(Role::Driver, off, "CAM-01", 1, 2, false);
    const SensitiveAction d2 = export_recording(Role::Driver, off, "CAM-01", 1, 2, true);
    const SensitiveAction d3 = export_recording(Role::Driver, on, "CAM-01", 1, 2, true);
    check(!d1.allowed && d1.result == "denied" && !d1.executed, "driver off unconfirmed denied");
    check(!d2.allowed && d2.result == "denied" && !d2.executed, "driver off confirmed still denied");
    check(!d3.allowed && d3.result == "denied" && !d3.executed,
          "driver config-on confirmed still denied (instance boundary)");

    // admin/gate seam: unconfirmed -> cancelled; confirmed -> ok
    const SensitiveAction c = export_recording(Role::Admin, off, "CAM-07", 5, 9, false);
    check(c.allowed && !c.executed && c.result == "cancelled" && c.kind == "export_recording",
          "admin unconfirmed -> cancelled");
    const SensitiveAction ok = export_recording(Role::Admin, off, "CAM-07", 5, 9, true);
    check(ok.allowed && ok.executed && ok.result == "ok", "admin confirmed -> ok");
    check_eq("detail camera passthrough", ok.target, "CAM-07");
    check(ok.detail.find("录像导出 CAM-07 [5,9]") != std::string::npos,
          "detail carries camera and window");
    // negative/edge window passthrough (no clamping invention at the gate layer)
    const SensitiveAction neg = export_recording(Role::Admin, off, "CAM-07", -1, 0, true);
    check(neg.executed && neg.detail.find("[-1,0]") != std::string::npos,
          "negative window passthrough (no clamp at the permission layer)");

    // maintainer with config on: unconfirmed cancelled, confirmed ok
    const SensitiveAction mc = export_recording(Role::Maintainer, on, "CAM-02", 1, 2, false);
    const SensitiveAction mek = export_recording(Role::Maintainer, on, "CAM-02", 1, 2, true);
    check(mc.result == "cancelled", "maintainer config-on unconfirmed cancelled");
    check(mek.executed && mek.result == "ok", "maintainer config-on confirmed ok");
    // maintainer config off -> denied (not cancelled)
    const SensitiveAction md = export_recording(Role::Maintainer, off, "CAM-02", 1, 2, true);
    check(!md.allowed && md.result == "denied", "maintainer config-off denied");

    // modify/config action mirrors the export state machine (kind passthrough,
    // same two-phase confirm): admin unconfirmed -> cancelled
    const SensitiveAction sc = modify_config(Role::Admin, off, "k", "v", false, false);
    check(sc.result == "cancelled" && sc.kind == "modify_config",
          "config action unconfirmed cancelled");

    // config actions: role/scope matrix
    const SensitiveAction dev1 = modify_config(Role::Maintainer, off, "thresholds.confirm_frames", "3", true, false);
    check(dev1.executed && dev1.result == "ok", "maintainer device-scope ok");
    const SensitiveAction dev0 = modify_config(Role::Maintainer, off, "key", "v", false, false);
    check(!dev0.executed && dev0.result == "cancelled", "maintainer device-scope unconfirmed cancelled");
    const SensitiveAction sys1 = modify_config(Role::Maintainer, off, "retention_days", "7", true, true);
    check(!sys1.allowed && sys1.result == "denied", "maintainer system-scope denied");
    const SensitiveAction sys2 = modify_config(Role::Developer, off, "retention_days", "7", true, true);
    check(!sys2.allowed && sys2.result == "denied", "developer system-scope denied");
    const SensitiveAction sys3 = modify_config(Role::Admin, off, "retention_days", "7", true, true);
    check(sys3.executed && sys3.result == "ok", "admin system-scope ok");
    const SensitiveAction sys4 = modify_config(Role::Admin, off, "retention_days", "7", false, true);
    check(!sys4.executed && sys4.result == "cancelled", "admin system-scope unconfirmed cancelled");
    // driver device-scope also denied
    const SensitiveAction drv = modify_config(Role::Driver, off, "key", "v", true, false);
    check(!drv.allowed && drv.result == "denied", "driver device-scope denied");
    // detail carries key=value
    check(sys3.detail.find("retention_days=7") != std::string::npos,
          "config detail carries key=value");
}

// --- 5. build_action_log contract (job of extracting from the bridge seam) ---
void t_action_log() {
    PermissionsConfig off;
    const SensitiveAction ok = export_recording(Role::Admin, off, "CAM-01", 1000, 61000, true);
    const MaintenanceLogRow l = build_action_log(ok, "admin", 1760000000000LL, 42);
    check_eq("log action == kind", l.action, "export_recording");
    check_eq("log target == camera", l.target, "CAM-01");
    check_eq("log result == action result", l.result, ok.result);
    check_eq("log detail == action detail", l.detail, ok.detail);
    check_eq("log user passthrough", l.user, "admin");
    check(l.ts_wall_ms == 1760000000000LL && l.ts_mono_us == 42, "log timestamps passthrough");

    // cancelled/denied actions CAN be logged (passthrough, no filtering at the
    // core seam: the bridge only filters by executed)
    const SensitiveAction cc = export_recording(Role::Admin, off, "CAM-02", 1, 2, false);
    const MaintenanceLogRow lc = build_action_log(cc, "admin", 1, 0);
    check_eq("cancelled action log result", lc.result, "cancelled");
    const SensitiveAction dd = export_recording(Role::Driver, off, "CAM-02", 1, 2, true);
    const MaintenanceLogRow ld = build_action_log(dd, "driver", 1, 0);
    check_eq("denied action log result", ld.result, "denied");

    // config log shape
    const SensitiveAction cs = modify_config(Role::Admin, off, "retention_days", "7", true, true);
    const MaintenanceLogRow lcfg = build_action_log(cs, "admin", 1760000000000LL, 9);
    check_eq("config log action", lcfg.action, "modify_config");
    check_eq("config log target == key", lcfg.target, "retention_days");
    check(lcfg.detail.find("retention_days=7") != std::string::npos,
          "config log detail carries key=value");

    // exported action logs roll by day (ITEM-022 D-02 coupling):
    // two on 2026-10-16 + one 5 days earlier; max_days=1 keeps the two
    const long long now = parse_date_utc_ms("2026-10-16");
    std::vector<MaintenanceLogRow> logs;
    logs.push_back(build_action_log(export_recording(Role::Admin, off, "A", 0, 1, true), "admin", now, 1));
    logs.push_back(build_action_log(export_recording(Role::Admin, off, "B", 0, 1, true), "admin", now + 1, 2));
    logs.push_back(build_action_log(export_recording(Role::Admin, off, "C", 0, 1, true), "admin",
                                    now - 5LL * 86400000LL, 3));
    const std::vector<MaintenanceLogRow> rolled = roll_logs_by_day(logs, 1);
    check(rolled.size() == 2, "action log roll keeps only the current day (2 rows)");
    for (const auto& r : rolled) {
        check(r.action == "export_recording", "rolled action kind preserved");
    }
}

}  // namespace

int main() {
    std::printf("== verify ITEM-023 ext (permissions + secondary confirmation fixtures) ==\n");
    t_matrix_full();
    t_gate();
    t_confirm_scan();
    t_action_machine();
    t_action_log();
    std::printf("RESULT: %s passed=%d failed=%d\n",
                g_failed == 0 ? "PASS" : "FAIL", g_passed, g_failed);
    for (const auto& n : g_notes) std::printf("  %s\n", n.c_str());
    return g_failed == 0 ? 0 : 1;
}
