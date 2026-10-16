# railhmi 权限矩阵（ITEM-023）

来源: `docs/hmi/INTERACTION_DESIGN.md` v2 §8（权限与角色交互）。运行时校验实现于
`railhmi-core::permissions`（`role_has_permission` / `permission_gate`），UI 仅据权限查询渲染
（受限入口不渲染）。导出为“受配置”项（`app.permissions.export_ops_allowed`，默认 `false`=仅管理员）。

| 权限（Permission） | 说明 | 司机 | 维护 | 研发/调试 | 管理员 |
|---|---|:--:|:--:|:--:|:--:|
| `view_realtime` | P1 实时监控 | ✓ | ✓ | ✓ | ✓ |
| `view_alarm_center` | P2 报警中心 | ✓ | ✓ | ✓ | ✓ |
| `view_replay` | P3 历史回放 | ✓ | ✓ | ✓ | ✓ |
| `acknowledge` | 报警确认 | ✓ | ✓ | ✓ | ✓ |
| `view_all_cameras` | P4 全摄像头 | ✗ | ✓ | ✓ | ✓ |
| `view_ai_debug` | P5 AI 调试 | ✗ | ✓ | ✓ | ✓ |
| `view_maintenance` | P6 设备维护 | ✗ | ✓ | ✓ | ✓ |
| `ptz_control` | 云台控制 | ✗ | ✓ | ✓ | ✓ |
| `reconnect` | 手动重连 | ✗ | ✓ | ✓ | ✓ |
| `modify_device_config` | 设备配置修改 | ✗ | ✓ | ✓ | ✓ |
| `modify_system_config` | 系统配置修改 | ✗ | ✗ | ✗ | ✓ |
| `export_recording` | 录像导出（受配置） | ✗ | 受配置 | 受配置 | ✓ |
| `export_snapshot` | 快照导出（受配置） | ✗ | 受配置 | 受配置 | ✓ |

说明：
- **受限入口不渲染**：无权限的导航项不出现（外壳按角色过滤）；直达受限页显示“无权限 + 返回”。
- **二次确认**：`modify_device_config` / `modify_system_config` / `export_recording` / `export_snapshot`
  需二次确认（`requires_secondary_confirm`）；确认前不执行（`result=cancelled`），确认后执行并写
  **维护日志**（`build_action_log`，action=`export_recording` / `modify_config`，含 user/result/detail，按日滚动）。
- **“受配置”**：`export_ops_allowed=false`（默认）时维护/研发不可导出；置 `true` 后允许，司机始终不可。

用户可观察点对应：
- 司机：导出入口**不存在**（P3 `exportRecording` 控件 `visible: canExportRecording()` = false）；
- 管理员：导出出现**二次确认**，确认完成后维护日志新增 `export_recording` 记录。
