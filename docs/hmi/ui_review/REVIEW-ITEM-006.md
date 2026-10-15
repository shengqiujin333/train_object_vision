# UI 实现评审证据 — ITEM-006（railhmi-core 检测职责与报警规则/事件层）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（v2 修复轮 commit `e79e2ae`，2026-10-15；历史轮 `cb76fef`）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/rules.h`、`app/src/core/src/rules.cpp`、`app/tests/core/test_rules.cpp`
  - 构建证据: `docs/hmi/test_evidence/ITEM-006-rules/README.md`
- 被评工作项: `ITEM-006`（按 `ai_profile` 分派判定、多帧确认、`track_id`/抑制窗口、级别严格 PRD §8.2、`ack` 只改状态不删事件；用户可观察：CoreTests 注入每类事件断言级别一致、同 `track_id` 窗口内不新增事件）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §6.1/§6.4/§7.2/§7.3（`ef47841`）、`docs/hmi/VISUAL_DESIGN.md` v2 §3.4/§7.3/§7.4 + `docs/hmi/design_tokens.yaml` v2 `alarm`（`40ccf4e`）、PRD §8.2
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（v2 修复轮；`docs/hmi/test_evidence/ITEM-006-rules/VERIFY-ITEM-006.md`）；历史轮 `f6aa4c1` 曾 TEST_FAIL（结果码/叙述不一致，非实现缺陷，见 §1/§0.2）
- 评审日期/环境: 2026-10-15；Windows 主机（MINGW64 x86_64，无 C/C++ 工具链）；**本轮未连接 RK3588 目标板**（板端事实采用上游软件验证的独立板端复跑）
- 判定: **TEST_PASS（范围严格限定为 ITEM-006 规则/事件核心层；不构成任何报警界面视觉/交互或 UI 可运行结论）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-006**（检测职责与报警规则/事件核心层）。判定对象是任务内嵌用户可观察点：`ai_profile` 分派、ROI/三态/轮轨/车门判定、多帧确认、`track_id`+抑制窗口、级别严格 PRD §8.2、`ack` 只改状态不删事件、同 track 窗口内不新增事件。
2. **本次未对任何界面做视觉/交互还原评审**：本项交付的是**无 GUI 的 core 规则/事件层**；报警横幅/卡片/声音/快照呈现尚未实现（`VISUAL_DESIGN` §7.3/§7.4）。因此本文不对布局、颜色、字形、焦点、反馈、空/恢复状态做任何 PASS 结论（§5 D-06）。
3. **本轮未重新连接目标板**：板端 176/176 与验证自有 82 条扩展夹具已由上游软件验证独立复跑（v2 修复轮）。本 capability 授权方法为 `read/ui_evidence/design_assets`，主机无 C/C++ 工具链；本轮以**源码审阅 + 契约对照（级别/枚举/事件字段/ack/抑制 vs 交互视觉设计）+ 归档证据核对**为独立证据，并标注引用关系。
4. **历史轮 TEST_FAIL 的性质**：`f6aa4c1` 的 TEST_FAIL 由**验证侧结果码与自身叙述不一致**造成（当时证据叙述为 TEST_PASS、124/124、65/65，未指出实现缺陷），非本项实现缺陷。实现者以显式化设备类别映射 + 吸收边界夹具为第一方回归后重提（`e79e2ae`），v2 验证返回 TEST_PASS。本评审确认 **v2 状态与证据一致**。
5. 依赖缺口（板端无 `libsqlite3-dev`/Qt5 Quick/Qml）按职责交平台/环境；本评审不判定其“不可修复”，不返回 BLOCKED。
6. 本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现（v2） | `e79e2ae` | 新增 `rules.{h,cpp}`/`test_rules.cpp`；显式 `device_category_from_position`/`to_string`；第一方回归扩到规则段 94 条；板端 176/176 | 
| 被评实现（v1，历史） | `cb76fef` | 124/124；验证 `f6aa4c1` 结果码 TEST_FAIL（叙述 TEST_PASS，非实现缺陷） |
| 交互设计 | `ef47841` | v2 §6.1 级别三重编码、§6.4 建议映射、§7.2 事件字段、§7.3 报警卡片 |
| 视觉设计 / 令牌 | `40ccf4e` | v2 §3.4 `alarm` L1/L2/L3、§7.3 AlarmCard、§7.4 AlarmBanner |
| 软件验证 | `df54b10` | TEST_PASS；176/176 + 82/82 + config 好/坏一致 |

实现自述与仓库证据一致：`ITEM-006-rules/README.md`（含 §6 v2 修复记录）、板端 r2 日志与验证件相互印证；**级别映射未由 UI 改写，未把推理/持久化职责冒充已实现**。

---

## 2. 评审方法

1. 源码审阅：`rules.{h,cpp}`（枚举/`to_string`/`map_level`/`map_device_level`/`device_category_from_position`/`on_detection`/`on_device_event`/`ack`/抑制与确认键）、`test_rules.cpp`。
2. 契约对照：`AlarmLevel`/`AlarmType`/`AckState` 与 PRD §8.2、交互 §6.1/§6.4、`design_tokens.yaml` `alarm` L1/L2/L3 逐项对照；`AlarmEvent` 字段与交互 §7.2 报警事件字段对照。
3. 归档证据核对：板端 r2 日志（176/176、级别与抑制断言）、`VERIFY-ITEM-006.md`（v2）。
4. 逻辑独立推演：抑制窗口 `<` 边界、多帧确认窗口重置、`ack` 幂等、设备/检测通道隔离。

**方法限制**：主机无 C/C++ 工具链 → 不能本地编译/运行 CoreTests；板端结论采用上游软件验证复跑证据。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 级别映射逐项对照 PRD §8.2 | `PersonOnRail/FrontObstacle/WheelDerail→L1`；`PantographAbnormal/DoorAjar/FrontCameraDevice/Device→L2`；`SideCameraDevice→L3` —— **与 PRD §8.2 与交互 §6.4 一致** |
| R2 | `AlarmLevel` token 对照 `design_tokens.yaml` `alarm` | `to_string` 产出 `L1/L2/L3`，与令牌键名**一致** |
| R3 | `AlarmEvent` 字段对照交互 §7.2 | `id/level/type/source_cam/ts_ms/track_id/confidence/distance_m/description/ack_state/record_available/snapshot_ref` —— **字段齐备**（`ts` 语义见 D-01） |
| R4 | `ack` 语义对照 ALM-004/交互 §6.2 | `ack(id)` 仅置 `Acked`，事件保留；`find`/`unacked_count` 可查 —— 满足“确认只改处理状态，不删除事件” |
| R5 | 抑制/确认键与窗口推演 | 键 `camera|type|track_id`；窗口内 `d.t_ms - last_emit_ms < suppress_window_ms` **严格 `<`** 时更新既有事件不新增；确认窗口 `d.t_ms - cs.last_ms > confirm_window_ms` 重置 —— 满足 ALM-005 |
| R6 | 归档证据核对 | 板端 `176 passed, 0 failed`；`person_on_rail=L1 … device side=L3`；`same track within suppress window -> no new event`；`ack does NOT delete event` |
| R7 | ai_profile 分派门控 | front 需 `in_roi && conf≥min`；pantograph 仅 `Abnormal`；wheel 需 `wheel_abnormal`；cab_door 仅 `Ajar`；`None` 直返不产事件 |

---

## 4. 判据逐条核对（ITEM-006 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 按 `ai_profile` 分派判定 | 五 profile 分支齐备 + `None` 直返（R7） | **满足** |
| 前方危险区 ROI / 受电弓三态 / 轮轨基准 / 车门 ROI | `in_roi`、`PantographState::Abnormal`、`wheel_abnormal`、`DoorState::Ajar` 门控 | **满足** |
| 多帧确认 | `confirm_frames`/`confirm_window_ms` 累计与重置（R5） | **满足** |
| `track_id` + 抑制窗口 | 键含 track_id；窗口内更新不新增（R5） | **满足** |
| 级别严格 PRD §8.2 | R1 逐项一致；9 断言 + 验证夹具 | **满足** |
| `ack` 只改状态不删事件 | R4 | **满足** |
| 同 track 窗口内不新增事件 | 实现断言 + 82 夹具（严格 `<` 边界） | **满足** |

---

## 5. 具体偏差与修复要求

**D-01（UI 契约缺口）— 事件只带单调毫秒 `ts_ms`，UI 无法显示“发生时间”**
- 观察：`AlarmEvent.ts_ms`/`last_update_ms` 注释为“core 用单调毫秒；wall/持久化由上层”；结构体**无** wall-clock（epoch）字段。而交互 §6.1/ALM-001 要求报警内容至少含“发生时间”，§5.2 报警中心列“发生时间”，`VISUAL_DESIGN` §7.3/§7.4 以 mono 显示 HH:MM:SS。
- 影响（UI 可见）：单调毫秒不能直接渲染为用户可读时间；若由多个上层各自换算，`DATA-001 统一时间基准`难保证。
- 复现路径：`read app/src/core/include/railhmi/core/rules.h`（`ts_ms`/`last_update_ms` 注释与字段）；对照 `INTERACTION_DESIGN` §7.2 `ts`。
- 期望：明确**单一时间基准的归属**——或在 `AlarmEvent` 增加 wall/epoch 时间戳（由事件层在产生时用统一时钟写入），或定义“上层注入 wall 时间”的契约并在 §7.2/§DATA-001 落字；避免 UI 各自换算。

**D-02（UI 文案交接）— `description` 为机器拼接串，UI 不得原样显示**
- 观察：`make_event` 生成 `description = "<type> @ <camera>"`（如 `"person_on_rail @ CAM-01"`）；设备事件则以 `detail`（如离线/冻结文本）填入 `description`。
- 影响：`VISUAL_DESIGN` §7.3 AlarmCard/§7.4 Banner 需要中文“类型 + 来源摄像头 + 简要描述”；直接渲染 `description` 会显示机器 token。
- 期望：在 UI 文案层建立**单一映射**（`AlarmType`→中文类型、`source_cam`→摄像头名、设备 `detail`→离线/冻结文案），与 §6.1/§7.3 一致；`description` 仅作调试/回退，不进入司机主界面。

**D-03（UI 语义歧义）— `record_available` 默认 false，易被 UI 误读为“录像不可用”**
- 观察：`AlarmEvent.record_available` 默认 `false`，`make_event`/`on_device_event` 均不设置（留给录制层）；而 PLY-008/交互 §5.2/§5.3 要求“录像缺失时显式标记录像不完整/不可用”。
- 影响：若 UI 以 `record_available==false` 触发 PLY-008 黄条，每条新报警都会短暂显示“录像不可用”，直到录制层翻转——与设计“仅当录像确实失败才标注”不符。
- 期望：为录像可用性定义**三态或显式状态**（如 `unknown/available/unavailable` 或 `record_state` 字段），并约定 UI 仅在“不可用”态显示 PLY-008；或明确“false 表示未解析、UI 不得据此告警”。

**D-04（产品确认/交接）— 车顶/车底/司机室等“其它设备”设备级别为 L2，PRD §8.2 未列举**
- 观察：`device_category_from_position` 把 roof/underfloor/cab → `Other`，`map_device_level(Other)=L2`（显式分支，非静默默认）；PRD §8.2 仅列举前方主摄=2、侧视=3。
- 影响（UI 可见）：车顶/车底/司机室相机离线会显示 L2（橙），与“侧视离线=3（黄）”不同；是否符合用户预期需产品确认。
- 期望：请产品/设计确认其余四路设备故障级别（L2 或另有要求）；确认后在 PRD/交互 §6.4 补明文并保持 core 映射同步。当前为显式、可追溯，不阻塞本层。

**D-05（测试覆盖/交接）— 级别映射与抑制键为**不可由 UI 改写**的单点真相，但缺少“配置/契约”文档指向**
- 观察：`map_level`/`map_device_level` 硬编码于 core，注释“UI 不得改写”；级别值与视觉 `alarm` 令牌一一对应。
- 期望：在 `VISUAL_DESIGN`/`INTERACTION_DESIGN` 与 core 之间建立一次**可追溯对照表**（AlarmType→L1/L2/L3→令牌键），供 UI 实现与设计还原审阅引用，避免两端漂移（本证据 R1/R2 已给出对照）。

**D-06（范围缺口，非本项缺陷）— 无可评审的 UI**
- 观察：报警横幅/卡片/声音/自动弹窗/快照呈现尚未实现，无可运行界面、无截图/录屏。
- 期望：待页面/组件（AlarmBanner、AlarmCard、报警中心）落地后，UI 实现评审按 `VISUAL_DESIGN` §3.4/§7.3/§7.4 与交互 §6.1–§6.4 逐状态核对（级别三重编码、确认后不删事件、同目标不刷屏、录像不可用标注、两级弹窗优先级），并要求运行录屏/交互记录。

### 已确认无问题/正向项

- 级别映射与 PRD §8.2/交互 §6.4 **逐项一致**（R1），`AlarmLevel` token 与 `alarm` 令牌键一致（R2）。
- `AlarmEvent` 字段与交互 §7.2 报警事件字段**基本齐备**（缺少显示用 wall time 与录像三态，见 D-01/D-03）。
- `ack` 语义严格（只改 `ack_state`、幂等、不删除），与 ALM-004 一致（R4）。
- 抑制窗口严格 `<`（恰阈值不抑制、差 1ms 抑制）与“更新同一事件距离/时间”符合 ALM-005（R5）。
- 确认/抑制键 `camera|type|track_id` 支持跨 track 新事件、跨类型不串扰；设备通道与检测通道隔离。
- 设备级别映射显式函数化（v2 修复），不依赖调用方隐式选择；`to_string` 枚举全值覆盖。
- core 纯标准库；级联回归（规则段 94 条）与验证 82 条夹具双重覆盖。

---

## 6. 未覆盖 / 未执行（不得外推）

1. 目标板任何命令本轮**未执行**；板端结论采用上游软件验证复跑与夹具证据。
2. 任何界面视觉/交互还原（无 UI 产物）。
3. 推理/后处理层提供的检测框中心、置信度、距离、受电弓/车门/轮轨状态（ITEM-017）。
4. 事件持久化（SQLite `events`/`event_targets`）——ITEM-007/008。
5. 快照/保护片段/声音/横幅呈现与离线/冻结文案（后续 UI 与录制层）。
6. 主机编译/运行 CoreTests（主机无工具链）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-006 规则/事件核心层）。
- 依据：任务内嵌用户可观察点——`ai_profile` 分派、ROI/三态/轮轨/车门判定、多帧确认、`track_id`+抑制窗口、级别严格 PRD §8.2、`ack` 只改状态、同 track 窗口内不新增事件——经源码审阅、契约对照、独立推演与归档证据核对逐条成立；板端 176/176 与验证 82 夹具一致；v2 结果与证据叙述一致（历史轮 TEST_FAIL 为验证侧不一致，非实现缺陷）。
- 明确边界：本 PASS **不表示**任何报警界面可运行或视觉/交互通过，**不表示**推理/持久化/录像已实现。
- 后续修复/交接要求见 §5：D-01（显示用 wall 时间）、D-02（文案映射）、D-03（录像可用性三态）为 UI 契约项；D-04 需产品确认其它设备级别；D-05 建议建立级别对照表；D-06 待可运行 UI 后再评审。
