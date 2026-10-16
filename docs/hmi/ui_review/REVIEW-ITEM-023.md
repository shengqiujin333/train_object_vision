# UI 实现评审证据 — ITEM-023（权限与二次确认）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `80463d8`，stamp `20261016-ITEM023-r3`）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/permissions.h`、`app/src/core/src/permissions.cpp`、`app/src/ui/src/permissions_model.{h,cpp}`、`app/src/ui/qml/pages/P3Replay.qml`、`app/src/ui/qml/pages/P6Maintenance.qml`、`app/src/ui/src/main.cpp`、`app/config/app.yaml`、`app/src/core/{include/railhmi/core/config.h,src/config.cpp}`、`app/tools/check_permissions_wiring.py`
  - 交付文档: `docs/hmi/PERMISSION_MATRIX.md`
  - 构建证据: `docs/hmi/test_evidence/ITEM-023-permissions/README.md`
- 被评工作项: `ITEM-023`（权限与二次确认：司机/维护/研发/管理员 页面与导出权限矩阵并落到运行时校验；配置修改与录像导出需二次确认并写维护日志；受限入口不渲染。用户可观察：司机导出入口不存在或显示无权限；管理员导出出现二次确认且完成后维护日志新增记录）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §8（权限矩阵）、§2.3、§5.3（PLY-007 导出仅授权角色可见）、§3.3（限制）、§7（HLT-007 操作日志）；`docs/hmi/DESKTOP_APP_DESIGN.md` §11；`docs/hmi/VISUAL_DESIGN.md` v2
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`VERIFY-ITEM-023.md`，core 641/641 + 权限探针 + 接线扫描）
- 关联前序评审: `REVIEW-ITEM-011.md`（外壳/角色导航）、`REVIEW-ITEM-018.md`（维护日志/查看快照）、`REVIEW-ITEM-022.md`（维护日志与 P6 卡片）
- 评审日期/环境: 2026-10-16；Windows 主机；**本轮未连接 RK3588 目标板**
- 判定: **TEST_PASS**（范围：ITEM-023 交付项与 `INTERACTION §8`/§5.3 逐项满足；无运行期视觉结论）

---

## 0. 结论与口径

1. 判定针对 ITEM-023 交付项与用户可观察点（司机导出入口不存在/无权限；管理员导出二次确认且完成后维护日志新增；受限入口不渲染；配置修改/导出二次确认）。
2. 所有交付项均可在**源码级**确证在位。**运行期视觉/交互（模态手感、反馈时序）本轮不做结论**（主机无 Qt、板端缺 Qt5 Quick/Qml，E-01b）。
3. 真实导出落盘/下载、配置写回 YAML、维护日志入库属平台/集成（handoff，§5）。
4. 本轮**未连接目标板**；core 641/641、权限探针、扫描取上游 `software_verification`。本 capability 授权方法为 `read/ui_evidence/design_assets`。
5. **未修改任何实现代码**；只写本评审证据并维护清单。残存均为观察项，不构成失败判据（§5）。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `80463d8` | core `permissions.{h,cpp}`（13 权限 × 4 角色 `role_has_permission`、`permission_matrix`、`permission_gate`、`requires_secondary_confirm`、`export_recording`/`modify_config`、`build_action_log`）；桥接 `RailhmiPermissionsModel`；P3 导出控件 + 二次确认模态；P6 权限矩阵卡；`app.yaml permissions.export_ops_allowed=false`；`check_permissions_wiring.py` |
| 权限文档 | `docs/hmi/PERMISSION_MATRIX.md` | 13 行 × 4 角色矩阵，与运行时校验逻辑一致 |
| 交互设计 | `ef47841` | v2 §8 权限矩阵、§5.3 PLY-007、§7 HLT-007 |
| 软件验证 | `11138e6` | TEST_PASS；641/0 + 探针 + 扫描（r1 探针编译错/r2 检查器路径为过程轮，已修） |

---

## 2. 评审方法

1. 源码审阅：`permissions.h/cpp`（矩阵/门控/动作/日志）、`permissions_model.{h,cpp}`、`P3Replay.qml`（导出控件/模态）、`P6Maintenance.qml`（矩阵卡）、`main.cpp`（注入）、`app.yaml`（导出配置）。
2. 契约对照：§8 权限矩阵/受限不渲染/二次确认写日志；§5.3 导出仅授权角色可见。
3. 运行时校验一致性：`role_has_permission` 与 `PERMISSION_MATRIX.md` 逐行比对。
4. 用户可观察链路追踪：导航过滤 → P3 导出 `visible: canExport()` → 二次确认模态 → `exportRecording(...,confirmed=true)` → `build_action_log` → 日志。
5. 令牌核对：`Theme.color.ok/text_disabled/overlay_scrim`、`Theme.radius.modal` 存在。

**方法限制**：主机无 Qt → 不能运行 P3/P6 或截图。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 权限矩阵 | `permission_matrix(cfg)` 遍历 13 项 × 4 角色；`role_has_permission`：司机仅 Realtime/AlarmCenter/Replay/Acknowledge；维护/研发/管理 `ops`；`ModifySystemConfig` 仅 Admin；导出 `Admin \|\| (ops && export_ops_allowed)` |
| R2 | 二次确认门控 | `requires_secondary_confirm` = 设备/系统配置修改 + 录像/快照导出；`permission_gate`：越权 `reason=权限不足` allowed=false；需确认未确认 `reason=需二次确认`；确认后 allowed=true |
| R3 | 动作/日志 | `make_action`：`executed=allowed && (!requires_confirm || confirmed)`，`result=denied/cancelled/ok`；`build_action_log`→`build_maintenance_log`（action/target/user/result/detail） |
| R4 | 司机导出（关键） | `role_has_permission(Driver, ExportRecording)=false`（`export_ops_allowed=false` 时且司机 `ops=false` 恒 false）；P3 `FilterChip{objectName:"exportRecording"; visible: page.canExport()}` → 司机**不渲染**（Qt Row 忽略 `visible:false` 项，不留空位） |
| R5 | 管理员导出（关键） | Admin → `canExportRecording()`=true → 控件渲染；`askExport()` 置 `exportPending` → `exportConfirmModal`（遮罩 + 确认/取消）；`confirmExport()` 调 `exportRecording(activeCam, cursor, cursor+60000, true)`；`executed` → 桥接 `logs_.push_back(build_action_log(...))` → P6 日志可见 |
| R6 | 矩阵卡 | P6 `maintPermMatrixCard`：标题含当前角色 `roleName()`；`Repeater(permissions.matrix())` 逐行 `permRow-<permission>` 显示 司机/维护/研发/管理员 `Y/-`（`ok`/`text_disabled`） |
| R7 | 配置 | `app.yaml permissions.export_ops_allowed=false`（默认仅管理员）；`config.cpp` 解析并计入 `applied_defaults`（上游探针 `type=matrix export_ops_allowed=false`） |
| R8 | 文档一致 | `PERMISSION_MATRIX.md` 13 行与 `role_has_permission` 逐行一致（含导出“受配置”语义） |
| R9 | 受限不渲染 | 导航按 `role_can_access` 过滤（`REVIEW-ITEM-011.md`）；P3 导出按 `canExportRecording()`；受限直达由 `DeniedView` 显示无权限 |
| R10 | 令牌 | `ok/text_disabled/overlay_scrim/surface_elev/border_strong`、`radius.modal` 均在 `Theme.qml` 存在 |

---

## 4. 判据逐条核对（ITEM-023 交付项）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 司机/维护/研发/管理员页面权限矩阵落到运行时校验 | `permission_matrix` + `role_has_permission`（R1） | 满足 |
| 导出权限矩阵落到运行时校验 | 导出 `Admin \|\| (ops && export_ops_allowed)`（R1/R7） | 满足 |
| 受限入口不渲染 | 导航过滤 + P3 导出 `visible` + 矩阵卡（R4/R9） | 满足 |
| 配置修改需二次确认 | `modify_config` + `requires_secondary_confirm`（R2/R3） | 满足 |
| 录像导出需二次确认 | `export_recording` + 模态（R2/R5） | 满足 |
| 二次确认后写维护日志 | `build_action_log`（R3/R5） | 满足 |
| 司机导出入口不存在/无权限 | 控件不渲染；`role_has_permission=false`（R4） | 满足 |
| 管理员导出出现二次确认且完成后日志新增 | 模态 + `maint-log action=export_recording`（R5） | 满足 |

---

## 5. 偏差/观察项（非阻断）

**D-01（观察）配置修改在界面上无操作入口**
- 观察：`modify_config`（设备/系统分级）门控与日志在 core/桥接已实现，但 **QML 无调用 `modifyConfig` 的控件**；「配置修改需二次确认」目前仅在运行时校验/文档层成立，界面未暴露配置修改判定入口。
- 期望：如本期需可操作，补受权限控制的配置修改控件 + 二次确认（否则在文档中明确本项仅门控、配置入口属后续参数页）。

**D-02（观察）导出完成无界面反馈**
- 观察：`confirmExport()` 把结果存入 `exportResult`，但 **P3 未展示**任何成功/失败提示；用户需到 P6 日志区查看“日志新增”。§8 用户可观察以此为准，但缺少即时反馈。
- 期望：补 3s toast/结果行提示（`VISUAL §7.x` toast），并区分 ok/denied/cancelled。

**D-03（观察）P6 权限矩阵卡固定 320px 无滚动**
- 观察：13 行矩阵 + 表头固定 320px，未用 `Flickable`；窄屏/长标签可能裁切（沿用 `REVIEW-ITEM-022.md` D-02 固定卡高观察）。
- 期望：加滚动或自适应高。

**D-04（观察）导出区间硬编码**
- 观察：P3 导出区间 `[cursor, cursor+60000]` 固定 60s，用户不可选范围（本项未要求区间选择，PLY-007 只要求“导出受权限/二次确认”）。
- 期望：如后续需要，提供区间/时长选择。

**D-05（观察，本地残留）P2「查看快照」仍为未接信号**
- 观察：`P2AlarmCenter.qml` `FilterChip 查看快照` emit `snapshotRequested`，`main.qml` 仍未连接（沿用 `REVIEW-ITEM-018.md` D-01）；与 `ExportSnapshot` 权限/二次确认无 UI 关联。
- 期望：接查看器或将快照导出纳入权限门控。

**D-06（handoff）真实能力与持久化**
- 真实导出落盘/下载（媒体/平台）、配置写回 `*.yaml`、维护日志写入 `maintenance_log` 表归后续集成（当前桥接内存 cap 1000，按日滚动）。

### 已确认无问题/正向项

- **用户可观察两点成立**：司机导出控件 `visible:false`（不渲染）；管理员导出经二次确认后写 `export_recording` 维护日志。
- 权限矩阵单一真相（core `role_has_permission`）驱动导航、P3 导出、P6 矩阵卡与文档；`export_ops_allowed` 配置化（默认仅管理员）。
- 门控语义完整：未确认不执行（`cancelled`）、越权 `denied`、确认后 `executed/ok`；日志只在 `executed` 时写入。
- 二次确认模态为遮罩 + 确认/取消（44px 可键盘 `FilterChip`，沿用 ITEM-022 模式）。

---

## 6. 未覆盖 / 未执行

1. QML 运行期视觉/交互（模态手感、反馈时序、矩阵卡滚动）——无 Qt 环境。
2. 真实导出落盘/配置写回/日志入库（平台/集成）。
3. 真机角色登录/切换（按 `default_role` 配置驱动）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-023 交付项与静态接线 vs `INTERACTION §8` / §5.3 `PLY-007`）。
- 依据：权限矩阵（页面 + 导出 + 配置）落到运行时校验（`role_has_permission`/`permission_gate`）；受限入口不渲染（导航过滤 + P3 导出 `visible` 驱动）；配置修改/录像导出需二次确认（`requires_secondary_confirm`）并写维护日志（`build_action_log`）；**司机导出入口不渲染**、**管理员导出二次确认且完成后日志新增**，均在源码级确证（R1..R10），文档 `PERMISSION_MATRIX.md` 与运行时一致。
- 边界：本 PASS **不表示**运行期视觉/交互手感通过（无 Qt 环境，§6），**不表示**真实导出/配置写回/日志入库可用（平台/集成 handoff，D-06）。观察项 D-01（配置修改无 UI 入口）、D-02（导出无即时反馈）、D-03（矩阵卡固定高）、D-04（导出区间固定）、D-05（P2 查看快照未接）供后续实现与运行期复评参考，不改变本次判定。
