#pragma once

// railhmi-core: 权限矩阵与二次确认（ITEM-023 / INTERACTION §8、HLT-007）。
// 司机/维护/研发/管理员的页面与操作权限矩阵落到运行时校验；配置修改与录像导出需二次确认并写维护日志；
// 受限入口不渲染（由权限查询驱动）。纯逻辑，无 Qt/SQLite。

#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/persistence_model.h"

namespace railhmi {
namespace core {

enum class Permission {
    ViewRealtime,
    ViewAlarmCenter,
    ViewReplay,
    ViewAllCameras,
    ViewAiDebug,
    ViewMaintenance,
    Acknowledge,
    ExportRecording,
    ExportSnapshot,
    ModifyDeviceConfig,
    ModifySystemConfig,
    PtzControl,
    Reconnect,
    Count,
};
const char* to_string(Permission p);
const char* permission_label(Permission p);

// 运行时校验（export 由 PermissionsConfig 门控）。
bool role_has_permission(Role role, Permission p, const PermissionsConfig& cfg);

struct PermissionRow {
    Permission permission = Permission::ViewRealtime;
    std::string label;
    bool driver = false;
    bool maintainer = false;
    bool developer = false;
    bool admin = false;
};
std::vector<PermissionRow> permission_matrix(const PermissionsConfig& cfg);

// 敏感操作需二次确认（配置修改 / 录像导出 / 快照导出）。
bool requires_secondary_confirm(Permission p);

struct ActionGate {
    bool allowed = false;
    bool requires_confirm = false;
    bool confirmed = false;
    std::string reason;   // 例如 “权限不足” / “需二次确认”
};
ActionGate permission_gate(Role role, Permission p, const PermissionsConfig& cfg, bool confirmed);

// 敏感动作（导出/配置修改）执行模型 + 维护日志。
struct SensitiveAction {
    std::string kind;      // export_recording | export_snapshot | modify_config
    std::string target;    // 摄像头/键名
    std::string detail;
    bool allowed = false;
    bool requires_confirm = true;
    bool confirmed = false;
    bool executed = false;
    std::string result;    // ok | denied | cancelled
};
SensitiveAction export_recording(Role role, const PermissionsConfig& cfg, const std::string& camera,
                                 long long from_ms, long long to_ms, bool confirmed);
SensitiveAction modify_config(Role role, const PermissionsConfig& cfg, const std::string& key,
                              const std::string& value, bool confirmed, bool system_scope = false);
MaintenanceLogRow build_action_log(const SensitiveAction& a, const std::string& user,
                                   long long ts_wall_ms, long long ts_mono_us);

}  // namespace core
}  // namespace railhmi
