// railhmi-core 权限与二次确认测试（ITEM-023 / INTERACTION §8、HLT-007）。
// 覆盖：角色×权限矩阵运行时校验、导出“受配置”、配置修改分级、二次确认门控、
// 敏感动作执行与维护日志、受限入口（导出）不渲染依据。

#include "test_util.h"

#include <string>
#include <vector>

#include "railhmi/core/permissions.h"

namespace {

using namespace railhmi::core;

void test_matrix() {
    PermissionsConfig cfg;   // export_ops_allowed=false
    // 全部角色可见
    for (Role r : {Role::Driver, Role::Maintainer, Role::Developer, Role::Admin}) {
        railhmi_test::check(role_has_permission(r, Permission::ViewRealtime, cfg) &&
                                role_has_permission(r, Permission::ViewAlarmCenter, cfg) &&
                                role_has_permission(r, Permission::ViewReplay, cfg) &&
                                role_has_permission(r, Permission::Acknowledge, cfg),
                            "all roles view realtime/alarm/replay + ack");
    }
    // 司机受限
    railhmi_test::check(!role_has_permission(Role::Driver, Permission::ViewAllCameras, cfg) &&
                            !role_has_permission(Role::Driver, Permission::ViewAiDebug, cfg) &&
                            !role_has_permission(Role::Driver, Permission::ViewMaintenance, cfg) &&
                            !role_has_permission(Role::Driver, Permission::ExportRecording, cfg),
                        "driver denied all-cameras/ai-debug/maintenance/export");
    // 操作角色可见
    railhmi_test::check(role_has_permission(Role::Maintainer, Permission::ViewAllCameras, cfg) &&
                            role_has_permission(Role::Developer, Permission::ViewAiDebug, cfg) &&
                            role_has_permission(Role::Admin, Permission::ViewMaintenance, cfg),
                        "ops roles see all-cameras/ai-debug/maintenance");
    const std::vector<PermissionRow> rows = permission_matrix(cfg);
    railhmi_test::check(rows.size() == static_cast<std::size_t>(Permission::Count), "matrix has all permissions");
}

void test_export_config_gate() {
    PermissionsConfig off;    // 默认导出仅管理员
    railhmi_test::check(role_has_permission(Role::Admin, Permission::ExportRecording, off),
                        "admin export allowed by default");
    railhmi_test::check(!role_has_permission(Role::Maintainer, Permission::ExportRecording, off) &&
                            !role_has_permission(Role::Developer, Permission::ExportRecording, off),
                        "ops export denied when config off");
    PermissionsConfig on;
    on.export_ops_allowed = true;
    railhmi_test::check(role_has_permission(Role::Maintainer, Permission::ExportRecording, on) &&
                            role_has_permission(Role::Developer, Permission::ExportRecording, on),
                        "ops export allowed when config on (受配置)");
    railhmi_test::check(!role_has_permission(Role::Driver, Permission::ExportRecording, on),
                        "driver export still denied when config on");
}

void test_secondary_confirm() {
    railhmi_test::check(requires_secondary_confirm(Permission::ExportRecording) &&
                            requires_secondary_confirm(Permission::ExportSnapshot) &&
                            requires_secondary_confirm(Permission::ModifyDeviceConfig) &&
                            requires_secondary_confirm(Permission::ModifySystemConfig),
                        "export/config require secondary confirm");
    railhmi_test::check(!requires_secondary_confirm(Permission::ViewReplay) &&
                            !requires_secondary_confirm(Permission::Acknowledge),
                        "view/ack do not require confirm");

    PermissionsConfig cfg;
    const ActionGate denied = permission_gate(Role::Driver, Permission::ExportRecording, cfg, true);
    railhmi_test::check(!denied.allowed && denied.reason == "权限不足", "driver export gate denied");
    const ActionGate pending = permission_gate(Role::Admin, Permission::ExportRecording, cfg, false);
    railhmi_test::check(pending.allowed && pending.requires_confirm && pending.reason == "需二次确认",
                        "admin export needs confirm");
    const ActionGate ready = permission_gate(Role::Admin, Permission::ExportRecording, cfg, true);
    railhmi_test::check(ready.allowed && ready.confirmed && ready.reason.empty(), "admin export confirmed");
}

void test_export_action_and_log() {
    PermissionsConfig cfg;
    const SensitiveAction d = export_recording(Role::Driver, cfg, "CAM-01", 1000, 61000, true);
    railhmi_test::check(!d.allowed && !d.executed && d.result == "denied", "driver export denied/not executed");
    const SensitiveAction c = export_recording(Role::Admin, cfg, "CAM-01", 1000, 61000, false);
    railhmi_test::check(c.allowed && c.requires_confirm && !c.executed && c.result == "cancelled",
                        "admin export unconfirmed -> cancelled");
    const SensitiveAction ok = export_recording(Role::Admin, cfg, "CAM-01", 1000, 61000, true);
    railhmi_test::check(ok.executed && ok.result == "ok", "admin export confirmed -> ok");
    const MaintenanceLogRow log = build_action_log(ok, "admin", 1760000000000LL, 5);
    railhmi_test::check(log.action == "export_recording" && log.target == "CAM-01" && log.result == "ok",
                        "export writes maintenance log");
}

void test_config_action() {
    PermissionsConfig cfg;
    const SensitiveAction dev = modify_config(Role::Maintainer, cfg, "thresholds.confirm_frames", "3", true, false);
    railhmi_test::check(dev.allowed && dev.executed && dev.result == "ok", "maintainer device config ok");
    const SensitiveAction sys = modify_config(Role::Maintainer, cfg, "retention_days", "7", true, true);
    railhmi_test::check(!sys.allowed && sys.result == "denied", "maintainer system config denied");
    const SensitiveAction adminsys = modify_config(Role::Admin, cfg, "retention_days", "7", true, true);
    railhmi_test::check(adminsys.allowed && adminsys.executed, "admin system config ok");
    const SensitiveAction noc = modify_config(Role::Admin, cfg, "retention_days", "7", false, true);
    railhmi_test::check(noc.allowed && !noc.executed && noc.result == "cancelled", "config unconfirmed -> cancelled");
}

}  // namespace

void run_permissions_tests() {
    test_matrix();
    test_export_config_gate();
    test_secondary_confirm();
    test_export_action_and_log();
    test_config_action();
}
