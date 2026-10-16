// railhmi 权限矩阵/二次确认探针（ITEM-023）
// 打印角色×权限矩阵，演示导出受配置、配置修改分级、二次确认门控与维护日志写入。
// 纯 core，无 Qt/SQLite。
//
// 用法: railhmi-permissions-probe [--out log]
// 退出码: 0 验收 PASS；1 FAIL。

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "railhmi/core/permissions.h"

using namespace railhmi::core;

int main(int argc, char** argv) {
    std::string out;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--out") next(&out);
        else if (a == "--help" || a == "-h") { std::printf("usage: railhmi-permissions-probe [--out log]\n"); return 0; }
        else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }
    std::ofstream fo(out, std::ios::trunc);
    auto emit = [&](const std::string& line) { std::printf("%s\n", line.c_str()); if (fo) fo << line << "\n"; };

    PermissionsConfig off;   // 导出默认仅管理员
    PermissionsConfig on; on.export_ops_allowed = true;

    emit("type=matrix export_ops_allowed=false");
    for (const auto& r : permission_matrix(off))
        emit("row perm=" + std::string(to_string(r.permission)) + " label=" + r.label +
             " driver=" + (r.driver ? "Y" : "-") + " maintainer=" + (r.maintainer ? "Y" : "-") +
             " developer=" + (r.developer ? "Y" : "-") + " admin=" + (r.admin ? "Y" : "-"));

    const bool driver_export = role_has_permission(Role::Driver, Permission::ExportRecording, off);
    const bool maint_export_off = role_has_permission(Role::Maintainer, Permission::ExportRecording, off);
    const bool maint_export_on = role_has_permission(Role::Maintainer, Permission::ExportRecording, on);
    emit("type=export-gate driver=" + std::string(driver_export ? "Y" : "-") +
         " maintainer_off=" + std::string(maint_export_off ? "Y" : "-") +
         " maintainer_on=" + std::string(maint_export_on ? "Y" : "-"));

    const ActionGate g1 = permission_gate(Role::Driver, Permission::ExportRecording, off, true);
    const ActionGate g2 = permission_gate(Role::Admin, Permission::ExportRecording, off, false);
    const ActionGate g3 = permission_gate(Role::Admin, Permission::ExportRecording, off, true);
    emit("type=gate driver reason=\"" + g1.reason + "\" allowed=" + (g1.allowed ? "true" : "false"));
    emit("type=gate admin_unconfirmed reason=\"" + g2.reason + "\" requires_confirm=" +
         (g2.requires_confirm ? "true" : "false"));
    emit(std::string("type=gate admin_confirmed allowed=") + (g3.allowed ? "true" : "false"));

    const SensitiveAction exp = export_recording(Role::Admin, off, "CAM-01", 1000, 61000, true);
    const MaintenanceLogRow elog = build_action_log(exp, "admin", 1760000000000LL, 5);
    emit("type=action kind=" + exp.kind + " target=" + exp.target + " result=" + exp.result +
         " executed=" + (exp.executed ? "true" : "false"));
    emit("type=maint-log action=" + elog.action + " user=" + elog.user + " result=" + elog.result +
         " detail=\"" + elog.detail + "\"");

    const SensitiveAction dev = modify_config(Role::Maintainer, off, "thresholds.confirm_frames", "3", true, false);
    const SensitiveAction sys = modify_config(Role::Maintainer, off, "retention_days", "7", true, true);
    const SensitiveAction adm = modify_config(Role::Admin, off, "retention_days", "7", true, true);
    emit("type=config scope=device role=maintainer result=" + dev.result);
    emit("type=config scope=system role=maintainer result=" + sys.result);
    emit("type=config scope=system role=admin result=" + adm.result);

    const bool ok = !driver_export && !maint_export_off && maint_export_on && !g1.allowed &&
                    g2.allowed && g2.requires_confirm && !g2.reason.empty() && g3.allowed && exp.executed &&
                    (elog.result == "ok") && dev.executed && !sys.allowed && adm.executed;
    emit(std::string("type=acceptance driver_export_absent=") + (!driver_export ? "true" : "false") +
         " admin_export_confirm_required=" + (g2.requires_confirm && !g2.reason.empty() ? "true" : "false") +
         " admin_export_logged=" + (elog.result == "ok" ? "true" : "false") +
         " export_config_gated=" + (maint_export_off != maint_export_on ? "true" : "false") +
         " config_system_admin_only=" + (!sys.allowed && adm.executed ? "true" : "false") +
         " result=" + (ok ? "PASS" : "FAIL"));
    return ok ? 0 : 1;
}
