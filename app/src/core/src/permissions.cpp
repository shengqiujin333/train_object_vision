#include "railhmi/core/permissions.h"

#include "railhmi/core/maintenance.h"   // build_maintenance_log

namespace railhmi {
namespace core {

const char* to_string(Permission p) {
    switch (p) {
        case Permission::ViewRealtime: return "view_realtime";
        case Permission::ViewAlarmCenter: return "view_alarm_center";
        case Permission::ViewReplay: return "view_replay";
        case Permission::ViewAllCameras: return "view_all_cameras";
        case Permission::ViewAiDebug: return "view_ai_debug";
        case Permission::ViewMaintenance: return "view_maintenance";
        case Permission::Acknowledge: return "acknowledge";
        case Permission::ExportRecording: return "export_recording";
        case Permission::ExportSnapshot: return "export_snapshot";
        case Permission::ModifyDeviceConfig: return "modify_device_config";
        case Permission::ModifySystemConfig: return "modify_system_config";
        case Permission::PtzControl: return "ptz_control";
        case Permission::Reconnect: return "reconnect";
        case Permission::Count: break;
    }
    return "unknown";
}

const char* permission_label(Permission p) {
    switch (p) {
        case Permission::ViewRealtime: return "实时监控";
        case Permission::ViewAlarmCenter: return "报警中心";
        case Permission::ViewReplay: return "历史回放";
        case Permission::ViewAllCameras: return "全摄像头";
        case Permission::ViewAiDebug: return "AI 调试";
        case Permission::ViewMaintenance: return "设备维护";
        case Permission::Acknowledge: return "报警确认";
        case Permission::ExportRecording: return "录像导出";
        case Permission::ExportSnapshot: return "快照导出";
        case Permission::ModifyDeviceConfig: return "设备配置修改";
        case Permission::ModifySystemConfig: return "系统配置修改";
        case Permission::PtzControl: return "云台控制";
        case Permission::Reconnect: return "手动重连";
        case Permission::Count: break;
    }
    return "未知";
}

bool role_has_permission(Role role, Permission p, const PermissionsConfig& cfg) {
    const bool ops = (role == Role::Maintainer || role == Role::Developer || role == Role::Admin);
    switch (p) {
        case Permission::ViewRealtime:
        case Permission::ViewAlarmCenter:
        case Permission::ViewReplay:
        case Permission::Acknowledge: return true;               // 全部角色（含司机）
        case Permission::ViewAllCameras:
        case Permission::ViewAiDebug:
        case Permission::ViewMaintenance: return true;
        case Permission::PtzControl:
        case Permission::Reconnect: return ops;
        case Permission::ModifyDeviceConfig: return ops;         // 维护/研发（部分）/管理员
        case Permission::ModifySystemConfig: return role == Role::Admin;   // 仅管理员
        case Permission::ExportRecording:
        case Permission::ExportSnapshot:
            return role == Role::Admin || (ops && cfg.export_ops_allowed);  // 受配置
        case Permission::Count: break;
    }
    return false;
}

std::vector<PermissionRow> permission_matrix(const PermissionsConfig& cfg) {
    std::vector<PermissionRow> rows;
    for (int i = 0; i < static_cast<int>(Permission::Count); ++i) {
        const Permission p = static_cast<Permission>(i);
        PermissionRow r;
        r.permission = p;
        r.label = permission_label(p);
        r.driver = role_has_permission(Role::Driver, p, cfg);
        r.maintainer = role_has_permission(Role::Maintainer, p, cfg);
        r.developer = role_has_permission(Role::Developer, p, cfg);
        r.admin = role_has_permission(Role::Admin, p, cfg);
        rows.push_back(r);
    }
    return rows;
}

bool requires_secondary_confirm(Permission p) {
    return p == Permission::ModifyDeviceConfig || p == Permission::ModifySystemConfig ||
           p == Permission::ExportRecording || p == Permission::ExportSnapshot;
}

ActionGate permission_gate(Role role, Permission p, const PermissionsConfig& cfg, bool confirmed) {
    ActionGate g;
    g.allowed = role_has_permission(role, p, cfg);
    g.requires_confirm = requires_secondary_confirm(p);
    g.confirmed = confirmed;
    if (!g.allowed) g.reason = "权限不足";
    else if (g.requires_confirm && !confirmed) g.reason = "需二次确认";
    return g;
}

namespace {
SensitiveAction make_action(const std::string& kind, const std::string& target, const std::string& detail,
                            const ActionGate& g) {
    SensitiveAction a;
    a.kind = kind;
    a.target = target;
    a.detail = detail;
    a.allowed = g.allowed;
    a.requires_confirm = g.requires_confirm;
    a.confirmed = g.confirmed;
    a.executed = g.allowed && (!g.requires_confirm || g.confirmed);
    a.result = !g.allowed ? "denied" : (!a.executed ? "cancelled" : "ok");
    return a;
}
}  // namespace

SensitiveAction export_recording(Role role, const PermissionsConfig& cfg, const std::string& camera,
                                 long long from_ms, long long to_ms, bool confirmed) {
    const ActionGate g = permission_gate(role, Permission::ExportRecording, cfg, confirmed);
    const std::string detail = "录像导出 " + camera + " [" + std::to_string(from_ms) + "," +
                               std::to_string(to_ms) + "]";
    return make_action("export_recording", camera, detail, g);
}

SensitiveAction modify_config(Role role, const PermissionsConfig& cfg, const std::string& key,
                              const std::string& value, bool confirmed, bool system_scope) {
    const Permission p = system_scope ? Permission::ModifySystemConfig : Permission::ModifyDeviceConfig;
    const ActionGate g = permission_gate(role, p, cfg, confirmed);
    return make_action("modify_config", key, "配置修改 " + key + "=" + value, g);
}

MaintenanceLogRow build_action_log(const SensitiveAction& a, const std::string& user,
                                   long long ts_wall_ms, long long ts_mono_us) {
    return build_maintenance_log(a.kind, a.target, a.result, a.detail, user, ts_wall_ms, ts_mono_us);
}

}  // namespace core
}  // namespace railhmi
