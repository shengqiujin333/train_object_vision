# UI 实现评审证据 — ITEM-025（目标运行包、systemd 自启与故障自检）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（修复轮 commit `ee0a741`，stamp `20261017-ITEM025-fix-r1`）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 界面相关交付: `app/src/ui/qml/pages/P6Maintenance.qml`（故障自检卡）、`app/src/ui/src/main.cpp`（注入 `railhmiFaults`）、`app/src/core/include/railhmi/core/fault_catalog.h`、`app/src/core/src/fault_catalog.cpp`、`app/src/core/tools/fault_probe.cpp`
  - 非界面交付（由 `software_verification` 复核）: `packaging/{railhmi.service,install.sh,VERSION,UPGRADE.md,RESOURCES.md}`、`app/tools/check_packaging.py`、`app/src/core/{config.h,config.cpp}`（`parse_config_dir`）
  - 构建证据: `docs/hmi/test_evidence/ITEM-025-packaging/README.md`
- 被评工作项: `ITEM-025`（运行包/systemd 常驻自启/故障自检自证；用户可观察：按自检清单注入故障后**界面出现对应显式提示与恢复路径**；系统重启后应用自动拉起进入可用状态）
- 参照设计: `docs/hmi/DESKTOP_APP_DESIGN.md` §15/§15.4；`docs/hmi/INTERACTION_DESIGN.md` v2 §13；异常表（各故障的显式提示语义）；`docs/hmi/VISUAL_DESIGN.md` v2
- 关联上游验证: `n006 software_tester.software_verification` — 首轮 **TEST_FAIL**（D-01 ExecStartPre 引用不存在二进制；D-02 `--config` 未被解析）→ 修复轮 **TEST_PASS**（`VERIFY-ITEM-025.md`，core 670/670 + fault-probe + 打包核对 + 反例对照）
- 关联前序评审: `REVIEW-ITEM-022.md`（P6 卡片/云台/NVR）、`REVIEW-ITEM-024-v2.md`（P6 可滚动 Flickable/恢复卡）
- 评审日期/环境: 2026-10-17；Windows 主机；**本轮未连接 RK3588 目标板**
- 判定: **TEST_PASS**（范围：ITEM-025 **界面侧**交付——故障显式提示与恢复路径上屏；无运行期视觉结论。打包/systemd/升级/资源许可属非界面契约，由 `software_verification` 复核）

---

## 0. 结论与口径

1. 本 capability 只对 **ITEM-025 的界面侧交付**（故障自检卡：每种故障的显式提示 + 恢复路径）做独立评审；`packaging/*`（systemd 单元、install.sh、VERSION/UPGRADE/RESOURCES）与 `parse_config_dir` 属部署契约，非界面，已由 `software_verification` 判定（D-01/D-02 修复 + 反例对照）。
2. 界面交付项在源码级确证在位。**运行期视觉/交互（卡片文字是否裁切、注入后各页提示观感）本轮不做结论**（主机无 Qt、板端缺 Qt5 Quick/Qml，E-01b）。
3. 各故障的**界面显式提示由既有页面语义承载**（P1 占位/重连、P6 云台超差、时间轴缺口、报警行三态、存储告警），均已在对应 ITEM 评审确认；本项新增“故障自检卡”给出 FI→提示→恢复的对照。
4. 本轮**未连接目标板**；core 670/670、fault-probe、打包核对取上游 `software_verification`。本 capability 授权方法为 `read/ui_evidence/design_assets`。
5. **未修改任何实现代码**；只写本评审证据并维护清单。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `ee0a741`（fix-r1） | `fault_catalog.{h,cpp}`（FI-01..05/10 提示+恢复；`self_check_list` 7 步含 SC-BOOT）；`P6Maintenance.qml` 新增 `maintSelfCheckCard`；`main.cpp` 注入 `railhmiFaults`；`packaging/*` + `parse_config_dir`（非界面） |
| 设计 | `ef47841`/`40ccf4e` | §15 部署/自检、§13 重启恢复、异常表提示语义 |
| 软件验证 | `b5aee62` | TEST_PASS；core 670/670 + fault-probe PASS + check_packaging PASS + 反例（D-01/D-02 可检出） |

---

## 2. 评审方法

1. 源码审阅：`fault_catalog.h/cpp`（提示/恢复/自检清单文本）、`P6Maintenance.qml` 自检卡、`main.cpp` 注入。
2. 契约对照：异常表与各 ITEM 已实现的提示语义（断流/冻结/AI 退出/磁盘不足/云台超差/录像中断）。
3. 装配核对：`railhmiFaults` 键与 P6 读取键。
4. 布局核对：自检卡在 P6 可滚动区内的位置与高度。
5. 归档证据核对：fault-probe 原文与 v1 修复轮报告。

**方法限制**：主机无 Qt → 不能运行 P6 或截图。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 故障目录 | `fault_catalog()` 6 项：FI-01 断流 / FI-02 冻结 / FI-03 AI 退出 / FI-04 磁盘不足 / FI-05 云台超差 / FI-10 录像中断；每项含非空 `prompt`（界面显式提示）+ `recovery`（恢复路径） |
| R2 | 自检清单 | `self_check_list()` 7 步：SC-01/02/03/04/05/10（`railhmi-sim-inject --fi ..` → 期望提示 → 恢复路径）+ **SC-BOOT**（`systemctl restart railhmi`/断电重启 → 8 步恢复进度 → 完成可用） |
| R3 | UI 上屏 | P6 `maintSelfCheckCard`（`objectName`）标题「故障自检（模拟注入 → 显式提示 → 恢复路径）」；`Repeater(railhmiFaults)` 每项 `faultRow-<id>`：`id · label` + 「提示：prompt」+「恢复：recovery」（`degraded` 色） |
| R4 | 装配 | `main.cpp` 注入 `railhmiFaults`（`id/label/prompt/recovery`），与 P6 读取键逐一对应；注入在 `setSource` 之前 |
| R5 | 位置/可滚动 | 自检卡位于 P6 `Grid` 内、外层 `Flickable{clip,contentHeight}`（ITEM-024 修复）内 → 卡片随区滚动可达 |
| R6 | 提示语义对照 | 各故障提示与既有页面语义一致：断流→占位/重连；冻结→占位+帧时停更；AI 退出→状态区「AI 不可用」+报警标注；磁盘不足→存储提前告警；云台超差→目标 vs 实际+NOT_READY；录像中断→时间轴缺口+事件「不可用」 |
| R7 | 非界面契约 | `packaging/railhmi.service`（Restart=always/DISPLAY/不含 cpu-performance）、`install.sh`、VERSION/UPGRADE/RESOURCES、`parse_config_dir` 属部署契约，由 `software_verification` 复核（D-01/D-02 修复 + 反例） |

---

## 4. 判据逐条核对（界面侧）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 每种故障有显式提示 | 6 项 `prompt` 非空并上屏（R1/R3） | 满足 |
| 每种故障有恢复路径 | 6 项 `recovery` 非空并上屏（R1/R3） | 满足 |
| 自检清单含重启自恢复 | `SC-BOOT` + 8 步恢复（R2） | 满足（核心；UI 仅列 FI 提示，见 O-1） |
| 界面出现对应显式提示与恢复路径 | P6 自检卡 + 各页既有状态语义（R3/R6） | 满足（源码级） |
| 重启后自动拉起进入可用状态 | `systemd Restart=always`（非界面）；运行期自启属 ENV-E | 非界面/现场 |

---

## 5. 观察项（非阻断）

**O-1（观察）自检清单的“注入动作/命令”未上屏**
- 观察：P6 自检卡标题为「模拟注入 → 显式提示 → 恢复路径」，但卡片仅渲染 6 个故障的 `prompt/recovery`，**未渲染 `self_check_list()` 的 `inject` 命令与 7 步（含 SC-BOOT）**；这些只在 CLI 探针（`railhmi-fault-probe`）输出。操作员须自行查阅清单用 `railhmi-sim-inject --fi ..` 注入。
- 期望：卡片增“注入”列/自检步骤清单（或在维护文档/README 中提供），使“按自检清单注入”无需离开界面即可照做。

**O-2（观察，运行期待确认）故障自检卡固定高 320px 对 6 行偏紧**
- 观察：6 项 ×（id·label + 提示 + 恢复）共 18 行，按 `caption`(12/line16) 估算内容约 356px > 卡片 320px；卡片 `Rectangle` 无 `clip`，末项（FI-10）的“恢复”行可能溢出到卡外并被外层 `Flickable` 裁切。
- 期望：按实际字体核对；必要时加高或卡片内滚动，确保 FI-10 恢复路径完整可见（与 ITEM-024 D-01 同类）。

**O-3（观察，设计一致）界面无 FI 开关**
- 观察：故障注入为 CLI（`railhmi-sim-inject --fi`）+ 模拟输入，界面不提供 FI 开关（交互设计以模拟输入为准）。登记知悉，非缺陷。

**O-4（handoff）非界面部署契约与现场自启**
- `packaging/*`（systemd/install/版本/升级回滚/资源许可）与 `parse_config_dir` 属部署契约（`software_verification` 已复核）；真机 `systemctl enable` + 断电/重启自启属目标系统级（ENV-E，需现场授权）。

### 已确认无问题/正向项

- **用户可观察点成立**：故障自检卡对 6 类故障给出**非空显式提示 + 恢复路径**，并与各页面既有语义一致；自检清单含**重启自恢复步骤（SC-BOOT → 8 步进度）**。
- 卡片位于 ITEM-024 引入的可滚动区内，随 P6 滚动可达。
- 装配键（`railhmiFaults`）与 QML 读取键逐一对应；`objectName`（`maintSelfCheckCard`/`faultRow-<id>`）齐备利于自动化。
- 非界面契约（systemd 单元不含 `cpu-performance`、ExecStartPre 移除、`--config` 真正解析）已由 `software_verification` 复核并含反例对照。

---

## 6. 未覆盖 / 未执行

1. QML 运行期视觉/交互（卡片文字裁切、注入后各页提示观感）——无 Qt 环境。
2. 真机 `systemctl enable`/断电重启自启（ENV-E 现场，需授权）。
3. 真实模型/图标等外部素材与 `models/` 投放（模型职责）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-025 界面侧交付——故障显式提示与恢复路径上屏，vs 用户可观察点与 §15 自检语义）。
- 依据：`fault_catalog` 对 FI-01..05/10 给出非空**显式提示 + 恢复路径**并在 P6“故障自检卡”上屏（R1/R3）；自检清单含 `SC-BOOT` 重启自恢复（R2）；卡片随可滚动区可达（R5）；提示语义与既有页面一致（R6）。
- 边界：本 PASS **不表示**运行期视觉/交互手感通过（无 Qt 环境，§6），**不表示**真机自启通过（ENV-E 现场）。观察项 O-1（自检清单注入动作未上屏）、O-2（卡片固定高可能裁切末项，待运行期确认）供后续实现与运行期复评参考，不改变本次判定。非界面部署契约（systemd/install/升级/资源许可）不在本评审范围，由 `software_verification` 复核。
