# ITEM-023 验证轮 — 权限与二次确认（VERIFY-20261016-v1）

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-023`
- 被验证交付: 实现者轮 commit `80463d8`（`embedded_linux_engineer.desktop_app_implementation` → IMPLEMENTED，权威轮 stamp `20261016-ITEM023-r3`）
- 验证身份: `software_tester.software_verification`（独立复跑，非实现者自检）
- 用户可观察点: 司机角色导出入口不存在或显示无权限；管理员导出出现二次确认且完成后维护日志新增记录；配置修改需二次确认并写维护日志；受限入口不渲染

## 1. 结论: **TEST_PASS**

本轮全部为独立板端复跑（RK3588 192.168.0.103，主机指纹核验一致），判据由 `docs/hmi/INTERACTION_DESIGN.md` §8 + `docs/hmi/TEST_DESIGN.md` G3（TC-ROLE-01/03/04）+ IMPLEMENTATION 权限矩阵（`docs/hmi/PERMISSION_MATRIX.md`）推得，**未采信交付自身 acceptance 行作为判据来源**（探针逐行用 §8 派生 oracle 断言）。

| 验证步 | 结果 | 证据 |
|---|---|---|
| 交付入口 TestRun 复跑（verifier stamp，--keep-remote） | `TESTRUN_EXIT=0` | `build-TestRun-...-VERIFY-20261016-v1.transcript.json` |
| 核心单测逐行比对（fresh vs 实现 r3，21 项 CASE 全等） | 641/641 行逐行一致 identical=True verify_fails=0 | `log-compare`（transcript）+ 两份 core-tests log |
| JUnit 独立解析 | tests=641 failures=0；ITEM-023 精确 21/21 判例名全命中 | `core-tests-...-VERIFY-20261016-v1.xml` |
| 权限探针 oracle 断言（§8 派生 13×4 矩阵 + gate/action/config/acceptance 精确行） | fails=[] | `permissions-probe-...-VERIFY-20261016-v1.log` |
| 权限探针跨轮一致性 | 与实现者 r3 归档逐行 identical=True（确定性复现） | `perm-probe-compare` |
| 板端二次复跑探针 diff | IDENTICAL（两次运行逐字节一致） | `perm-probe-determinism` |
| 验证者 ext 夹具（链接本轮 build-core 静态库） | **233 passed / 0 failed** `EXT_EXIT=0` | `VERIFY-FIXTURES-...-v1.transcript.json` |
| 验证者接线扫描（对板端 fresh 副本源码，非实现者 checker） | 14/14 键全 True，mismatch=[] | `wiring-scan-findings` |
| 单测四接线 checker 回归（replay/ai/maintenance/permissions） | 全 PASS、exit 全 0 | fresh transcript `ui-scan` |
| 回归探针（sim-inject/status×2/ai/alarm/clip/replay/maintenance） | 全 EXIT=0；sim-inject JSON 与实现者轮逐条相等（含 summary） | fresh evidence 各文件 |
| 清理（验证工作目录按名删除确认） | cleanup_ok=True | `cleanup-verify` |

## 2. 判据分项（G3 用户可观察点）

1. **司机导出入口不存在**：core 权限校验 `role_has_permission(Driver, ExportRecording)=false`（ext 全矩阵 off/on 双态断言 4 角色 × 13 权限，含 config-on 时司机仍 false 的隔离）；P3 `exportRecording` 控件 `visible: page.canExport()`（`canExportRecording()` 委托 core）→ 无权限时不渲染（接线扫描 `T1_export_control_gated` + `N2_driver_export_visibility_gated`：可见性来自权限查询而非硬编码角色判断）。实现者探针原文 `row perm=export_recording ... driver=-` 与 `type=export-gate driver=-`。
2. **管理员导出二次确认 + 维护日志新增**：gate `admin_unconfirmed reason="需二次确认" requires_confirm=true`（未确认不执行）；确认后 `type=action kind=export_recording result=ok executed=true` 且 `type=maint-log action=export_recording user=admin result=ok detail="录像导出 CAM-01 [1000,61000]"`（`build_action_log` 走 `build_maintenance_log`，按日滚动链路为 ITEM-022 已交付能力并在 ext 复查 action 日志形状下的滚动语义）。ext 断言通道：`build_action_log(action/result/detail/user/timestamps)` 全列 = `SensitiveAction` 透传；`requires_secondary_confirm` 全 13 权限扫描仅 4 敏感项；动作状态机 denied/cancelled/ok 全边界（含 maintenance/developer 分级系统配置 denied）。
3. **配置修改需二次确认并分级**：`type=config scope=device role=maintainer result=ok`、`scope=system role=maintainer result=denied`、`scope=system role=admin result=ok`（ext 另覆盖 developer 系统配置 denied、设备级未确认 cancelled、admin 未确认 cancelled）。
4. **受限入口不渲染**：导航按 `role_can_access` 过滤（main.cpp，ITEM-011 回归 PASS 行仍在 641 中）；P3 导出控件权限可见性门控（同 1）；P6 仅呈现权限矩阵卡（只读），无越权控件。

## 3. ext 夹具覆盖清单（233 断言，验证者自有，交付 641 之外）

- **全 13×4 矩阵** off 与 on 两态（test_permissions.cpp 只采样 6 格；矩阵文档仅文本比对，未行为化）—— 逐格 + config-on 仅翻转 export 两行的精确性（admin/driver 不受 config 影响）。
- `permission_matrix` 输出 = `role_has_permission` 逐格镜像 + 枚举序 + label 契约（`permission_label` 非空/非"未知"）。
- gate 语义全矩阵：denied reason 精确"权限不足"、sensitive-unconfirmed 精确"需二次确认"、confirmed reason 空；非敏感权限 requires_confirm=false 且 reason 恒空（患者未覆盖）；denied 优先于 confirm reason；`Permission::Count` 越界拒绝。
- `requires_secondary_confirm` 13 权限全扫描（枚举下标用 to_string 锚定，防隐式依赖）。
- 动作状态机：denied×(confirmed 任意)/cancelled/ok；maintainer 受配置三态；developer/admin 系统级；detail/target/kind 透传；窗口边界 [-1,0] 不被门控层改写。
- `build_action_log` 列契约（action/target/result/detail/user/ts 全透传）+ cancelled/denied 动作日志透传（过滤在桥接层按 executed 决定）+ config 日志键=目标。
- 导出动作日志的按日滚动（ITEM-022 D-02 耦合回归）。

## 4. 过程与自查（round 内零产品缺陷误报）

- 上板前 3 件离线自检全部先于板端运行：`offline_selfcheck0.py`（探针 oracle 对比例外捕获：验证侧解析列序 bug；修正后 §8 派生判据与归档探针逐格一致）、`offline_selfcheck1.py`（扫描器对真实源码本地重映射 dry-run，14/14 True）、`offline_selfcheck2.py`（oracle 负例 8 变异全捕获）、`offline_selfcheck3.py`（log-compare/junit 解析正负例）。
- ext 夹具编译一次通过（`GXX_EXIT=0`，无 0 除/未用变量告警），板端运行 233/233。
- 夹具 4 处编译隐患（`check()` 变参不匹配、数组拷贝初始化、恒真断言、多占位符）在离线审查修正后才上板。

## 5. 边界与未核验

- **QML 运行期未执行**（板端缺 Qt5 Quick/Qml 开发包，历史 E-01b）：UI 级可观察证据为源码/接线级（fresh 副本扫描）+ core 行为级；无 GUI 实测截图（与 ITEM-008..022 各轮一致）。
- **真实录像文件落盘**、配置写回 `*.yaml` 的实际 IO：设计边界归媒体/后续集成（实现 README §3 已声明），不在本项验收内。
- **角色切换运行时入口**（登录/切换 UI）不在本项交付；`default_role: driver` 配置链为唯一注入点（main.cpp 桥接扫描 T6 证据）。
- `export_ops_allowed` 的"受配置"行为在 core 层经 ext 全断言（off/on 两态 × 4 角色）；配置热更新路径未接入（load_config_dir 启动加载为准）。

## 6. 复现

```sh
# 离线自检（无板）
python docs/hmi/test_evidence/ITEM-023-permissions/offline_selfcheck{0,1,2,3}.py
# 板端独立复跑（Windows，启动同 PowerShell 已注入凭据）
bash docs/hmi/test_evidence/ITEM-023-permissions/run_verify_round.sh
# 或直接
python docs/hmi/test_evidence/ITEM-023-permissions/verify_perm23_fixtures.py --host 192.168.0.103 \
  --user admin --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --expected-fingerprint "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" \
  --cpp docs/hmi/test_evidence/ITEM-023-permissions/verify_perm23_ext.cpp \
  --impl-testrun-log docs/hmi/test_evidence/ITEM-023-permissions/build-TestRun-192_168_0_103-20261016-ITEM023-r3.core-evidence_core-tests-20261016-ITEM023-r3.log \
  --impl-permprobe-log docs/hmi/test_evidence/ITEM-023-permissions/build-TestRun-192_168_0_103-20261016-ITEM023-r3.core-evidence_permissions-probe-20261016-ITEM023-r3.log \
  --outdir docs/hmi/test_evidence/ITEM-023-permissions --stamp VERIFY-20261016-v1
```

## 7. 证据文件

- `build-TestRun-192_168_0_103-VERIFY-20261016-v1.transcript.json` / `.log` — 本轮 TestRun 全过程（构建/单测/探针/接线/取回）
- `build-TestRun-...-VERIFY-20261016-v1.core-evidence_core-tests-...log/.xml` — 641/0 权威复跑
- `build-TestRun-...-VERIFY-20261016-v1.core-evidence_permissions-probe-...log` — 权限探针（oracle 断言对象）
- `VERIFY-FIXTURES-VERIFY-20261016-v1.transcript.json` / `.log` — 验证者夹具驱动全过程（ext compile/run + 扫描 + 确定性 + 回归 + 清理）
- 验证者自有夹具源：`verify_perm23_ext.cpp`、`verify_perm23_fixtures.py`、`run_verify_round.sh`、`offline_selfcheck0..3.py`
