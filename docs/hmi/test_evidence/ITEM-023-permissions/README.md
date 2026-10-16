# ITEM-023 证据 — 权限与二次确认

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-023`
- 依据: `docs/hmi/INTERACTION_DESIGN.md` v2 §8（司机/维护/研发/管理员 页面与导出权限矩阵；受限入口不渲染；配置修改与导出需二次确认并写维护日志 HLT-007）、§5.3（导出仅授权角色可见 PLY-007）；`docs/hmi/DESKTOP_APP_DESIGN.md` §11（权限）
- 交付文档: `docs/hmi/PERMISSION_MATRIX.md`（权限矩阵）
- 源码: `app/src/core/include/railhmi/core/permissions.h`、`app/src/core/src/permissions.cpp`、`app/src/core/tools/permissions_probe.cpp`、`app/tests/core/test_permissions.cpp`、`app/src/ui/src/permissions_model.{h,cpp}`、`app/src/ui/qml/pages/P3Replay.qml`、`app/src/ui/qml/pages/P6Maintenance.qml`、`app/src/ui/src/main.cpp`、`app/config/app.yaml`、`app/src/core/{include/railhmi/core/config.h,src/config.cpp}`、`app/tools/check_permissions_wiring.py`、`app/tools/board_build.py`

## 1. 交付内容

1. **`railhmi-core` 权限矩阵**（`permissions.{h,cpp}`）：13 项 `Permission`（实时/报警/回放/确认、全摄像头、AI 调试、设备维护、云台、重连、设备配置、系统配置、录像导出、快照导出）× 4 角色的运行时校验 `role_has_permission`；`permission_matrix` 输出矩阵；**导出为“受配置”**（`app.permissions.export_ops_allowed`，默认 false=仅管理员）。
2. **二次确认门控** `permission_gate` + `requires_secondary_confirm`：配置修改/录像导出/快照导出需二次确认；未确认不执行（`result=cancelled`），司机或越权 `result=denied`。敏感动作 `export_recording` / `modify_config`（设备/系统分级）执行并 `build_action_log` 写**维护日志**（action/target/user/result/detail，按日滚动复用 ITEM-022）。
3. **UI 桥接** `RailhmiPermissionsModel`：`matrix()`、`canExportRecording/canModifyDeviceConfig/canModifySystemConfig`、`exportRecording`/`modifyConfig`（二次确认）、`recentLogs`。
4. **QML**：
   - **P3 历史回放**：`导出录像` 控件 **`visible: canExportRecording()`**（司机不渲染——受限入口不存在）+ 二次确认模态 `exportConfirmModal`，确认后调用桥接导出并写日志。
   - **P6 设备维护**：新增**权限矩阵卡** `maintPermMatrixCard`（逐项 × 角色 Y/-，当前角色高亮）。
5. **配置**：`app.permissions.export_ops_allowed` 解析并记入 `applied_defaults`。
6. **静态取证工具** `app/tools/check_permissions_wiring.py`。

## 2. 实测（板端 RK3588 本机；TestRun）

`board_build.py --mode TestRun`（stamp `20261016-ITEM023-r3`）→ `TESTRUN_EXIT=0` `SCAN_EXIT=0` `REPLAY_WIRE_EXIT=0` `AIDBG_WIRE_EXIT=0` `MAINT_WIRE_EXIT=0` `PERM_WIRE_EXIT=0` `PERM_EXIT=0`。核心单测 **641 passed / 0 failed**（含 ITEM-023 新增权限/门控/动作/日志断言）。\n
`railhmi-permissions-probe`（板端原文节选）：
```
type=matrix export_ops_allowed=false
row perm=view_all_cameras label=全摄像头 driver=- maintainer=Y developer=Y admin=Y
row perm=export_recording label=录像导出 driver=- maintainer=- developer=- admin=Y
row perm=modify_system_config label=系统配置修改 driver=- maintainer=- developer=- admin=Y
type=export-gate driver=- maintainer_off=- maintainer_on=Y
type=gate driver reason="权限不足" allowed=false
type=gate admin_unconfirmed reason="需二次确认" requires_confirm=true
type=gate admin_confirmed allowed=true
type=action kind=export_recording target=CAM-01 result=ok executed=true
type=maint-log action=export_recording user=admin result=ok detail="录像导出 CAM-01 [1000,61000]"
type=config scope=device role=maintainer result=ok
type=config scope=system role=maintainer result=denied
type=config scope=system role=admin result=ok
type=acceptance driver_export_absent=true admin_export_confirm_required=true admin_export_logged=true export_config_gated=true config_system_admin_only=true result=PASS
[check_permissions_wiring] PASS (ITEM-023 permissions + secondary confirmation wired)
```

判据核对（用户可观察点）：
- **司机导出入口不存在或显示无权限**：`role_has_permission(Driver, ExportRecording)=false`；P3 导出控件 `visible: canExportRecording()`=false（不渲染）✅
- **管理员导出出现二次确认且完成后维护日志新增记录**：`admin_unconfirmed` → `需二次确认`（不执行）；确认后 `export_recording result=ok executed=true` 且写 `maint-log` ✅
- **配置修改需二次确认并分级**：设备配置维护/研发可（确认后 ok）；系统配置仅管理员（维护 `denied`）✅
- **受限入口不渲染**：导航按角色过滤 + P3 导出按钮权限控制 + 矩阵文档 ✅

## 3. 边界

- **真实导出落盘**（录像文件写出/下载）属平台/媒体；本项交付权限门控、二次确认、动作模型与维护日志（`build_action_log`）。日志持久化复用 ITEM-007 `maintenance_log` 表。
- **配置修改**：本项交付门控与日志；实际写回 `*.yaml` 归后续集成（当前仅校验/记录）。
- **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（E-01b）；证据为 core 板端实跑（641/0）+ 权限探针 + 源码/静态扫描。

## 4. 证据文件

- `build-TestRun-...-ITEM023-r3.core-evidence_permissions-probe-...-r3.log`（矩阵/门控/动作/日志原文 + PASS）
- `...-ITEM023-r3.core-evidence_core-tests-...-r3.log` / `.xml`（641 passed / 0 failed）
- `...-ITEM023-r3.transcript.json` / `.log`（构建 + 单测 + 探针 + 扫描/接线 + 取回）
- r1/r2 留痕（r1 探针编译错 `const char* + const char*`；r2 检查器矩阵文档路径板端未命中）——均已修复

## 5. 复现

```sh
# 板端
bash app/build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-permissions-probe
python3 tools/check_permissions_wiring.py --root src/ui --matrix PERMISSION_MATRIX.md
# 经登记 SSH 入口
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-023-permissions --stamp <tag>
```
