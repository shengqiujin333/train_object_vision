# UI 实现评审证据 — ITEM-018（报警中心页面）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `c293c96`，2026-10-16 05:08:55 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/alarm_center.h`、`app/src/core/src/alarm_center.cpp`、`app/src/core/tools/alarm_probe.cpp`、`app/src/ui/src/alarm_model.{h,cpp}`、`app/src/ui/qml/pages/P2AlarmCenter.qml`、`app/src/ui/qml/components/FilterChip.qml`、`app/src/ui/qml/main.qml`、`app/src/ui/src/main.cpp`、`app/tests/core/test_alarm_center.cpp`
  - 构建证据: `docs/hmi/test_evidence/ITEM-018-alarm-center/README.md`
- 被评工作项: `ITEM-018`（列表字段级别/类型/时间/来源摄像头/描述/处理状态/录像可用性；默认级别降序→时间降序；确认/批量确认/查看录像/查看快照；日期/时间段/摄像头/类型/级别/确认状态筛选；录像不可用行内显著标注；用户可观察：筛选组合后列表与空态正确，批量确认后各行状态即时更新并写维护日志）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §5.2/§7.2（`ef47841`）、`docs/hmi/VISUAL_DESIGN.md` v2 §8.2/§3.4/§7.8/§10（`40ccf4e`）、`docs/hmi/DESKTOP_APP_DESIGN.md` §9、PRD §8.2/ALM-004/PLY-006/PLY-008/HLT-007
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-018-alarm-center/VERIFY-ITEM-018.md`）
- 关联前序评审: `REVIEW-ITEM-006.md` D-01/D-02/D-03（事件墙钟时间、description 文案、record_available 三态）、`REVIEW-ITEM-007.md` D-01/D-03（无事件列表/ack 查询接口、录像三态）
- 评审日期/环境: 2026-10-16；Windows 主机（MINGW64 x86_64，无 C/C++/Qt）；**本轮未连接 RK3588 目标板**
- 判定: **TEST_PASS（范围严格限定为 ITEM-018 报警中心模型与 QML 静态接线；不构成 QML 运行期视觉/交互结论；快照与维护日志持久化见 §5）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-018**。判定对象是任务内嵌用户可观察点：列表字段、默认排序、筛选+空态、批量确认即时更新并写维护日志、录像不可用行内标注。
2. **本次未做 QML 运行期视觉评审**：板端缺 Qt5 Quick/Qml（E-01b），无运行素材；本文以 **core 模型 + UI 桥接（QObject）+ QML 源码/装配 + 归档证据**为证据（§5 D-06）。
3. **本轮闭合多项前序 UI 发现**：录像可用性**三态**（可回放/不完整/不可用）与行内标注（闭合 REVIEW-ITEM-006 D-03、ITEM-007 D-03 的呈现面）；事件列表/筛选/确认在 core+桥接提供（部分闭合 ITEM-007 D-01）。
4. **快照与维护日志未落盘**：`snapshotRequested` 无外壳处理器（“查看快照”为死按钮）；`ack` 生成 `MaintenanceLogRow` 但只存 `last_log_` 字符串、未写 `SqliteRepository`（§5 D-01/D-02）。
5. 依赖缺口按职责交平台/集成；本评审不判定其“不可修复”，不返回 BLOCKED。本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `c293c96`（2026-10-16 05:08） | 新增 `alarm_center.{h,cpp}`/`alarm_probe.cpp`、`RailhmiAlarmModel`（桥接）、`P2AlarmCenter.qml`、`FilterChip.qml`；`main.qml` `alarm_center→P2AlarmCenter` 且连接 `replayRequested`；`main.cpp` 注入 `railhmiAlarms`（模拟开启时灌样例）；`test_alarm_center.cpp`；证据归档 |
| 交互设计 | `ef47841` | v2 §5.2 报警中心字段/排序/筛选/空态/录像缺失、§7.2 事件字段 |
| 视觉设计 | `40ccf4e` | §8.2 列表/空态/确认行描边、§3.4 级别三重编码、§7.8 行、§10 可访问性 |
| 软件验证 | `215b8f7` | TEST_PASS；512/512 + alarm-probe PASS（排序/筛选/空态/批量确认/维护日志） |

实现自述与仓库证据一致：`ITEM-018-alarm-center/README.md`、板端 alarm-probe 原文相符；**明确声明持久化事件接入未接线（默认空态，仅 simulation 灌样例）、QML 运行期未执行**，未外推。

---

## 2. 评审方法

1. 源码审阅：`alarm_center.{h,cpp}`（行派生/标签/三态/时间工具/排序/筛选/确认/日志）、`test_alarm_center.cpp`、`alarm_model.{h,cpp}`（桥接/`query`/`options`/`ack`）、`P2AlarmCenter.qml`、`FilterChip.qml`、`main.qml`（路由与信号连接）、`main.cpp`（注入/模拟样例）。
2. 契约对照：字段/排序/筛选/空态/录像标注与交互 §5.2、视觉 §8.2/§3.4/§7.8/§10；ack 语义与 ALM-004、维护日志 HLT-007、录像 PLY-008。
3. 装配链路核对：`RailhmiAlarmModel` map 键 vs P2 `modelData.*`；QML 交互（确认/批量/录像/快照）→ 桥接/外壳处理路径。
4. 归档证据核对：512/512、alarm-probe（order/filter/ack/maint-log）。

**方法限制**：主机无 Qt → 不能运行界面；不据探针数值宣称运行期视觉结果。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 字段派生对照交互 §5.2 | `AlarmRow` 含 级别/类型/时间/来源摄像头/描述/处理状态/录像可用性 + `snapshot_available/ref`；中文标签齐备（`alarm_type_label`/`alarm_level_label`/`ack_state_label`/`record_availability_label`） |
| R2 | 默认排序 | `sort_alarms_default`：级别降序→时间降序（探针 `order=level_desc,time_desc`；测试 L1 内 5000 先于 1000） |
| R3 | 录像三态与行内标注 | `RecordAvailability{Available,Incomplete,Unavailable}` → “可回放/录像不完整/录像不可用”；P2 `visible: !recordOk` 显示 degraded chip（PLY-008，不静默） |
| R4 | 筛选与空态 | 六维筛选（时间窗=日期/时间段、摄像头、类型、级别、确认状态）；`query_alarms`+`alarm_list_empty`；探针 `combo_empty count=0 empty=true` |
| R5 | 确认/批量确认 | `acknowledge` 只改 `ack_state`、返回变化 id；P2 `ackOne`/`ackAllUnacked` 调 `model.ack` 后 `refresh()` → 行 `ackLabel` 即时更新；事件不删除（测试 `rows.size()==4`） |
| R6 | 维护日志 | `build_ack_log({1,2,3},"maintainer",·,·)` → `ack_alarm target=1,2,3 count=3 user=maintainer`；但桥接 `ack()` 仅存 `last_log_`（见 D-02） |
| R7 | 装配链路 | `alarm_model.cpp` map 键（`id/level/levelLabel/type/typeLabel/time/tsWallMs/cameraId/cameraName/description/ack/ackLabel/record/recordLabel/recordOk/snapshot/snapshotRef`）与 P2 `modelData.*` **全部匹配** |
| R8 | 交互链路 | `P2.replayRequested` → `main.qml onReplayRequest` → `currentPageId="replay"`（driver 允许）；**`P2.snapshotRequested` 在 `main.qml` 未连接**（D-01） |
| R9 | 可访问性/令牌 | `FilterChip` 高 = `Theme.size.button.sm`(32px)、无焦点环/键盘；日期 `TextInput` 高 32px、无校验反馈（D-03）；颜色经 `Theme.*`，无硬编码十六进制 |

---

## 4. 判据逐条核对（ITEM-018 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 列表字段（级别/类型/时间/摄像头/描述/状态/录像可用性） | R1 | **满足（模型/静态）** |
| 默认级别降序→时间降序 | R2 | **满足** |
| 筛选组合命中/空态正确 | R4 | **满足（模型）** |
| 批量确认后各行状态即时更新 | R5 | **满足（模型/桥接；运行期未验证）** |
| 批量确认写维护日志 | 生成 `MaintenanceLogRow` 但**未持久化**（R6/D-02） | **部分满足** |
| 录像不可用行内显著标注 | R3 | **满足（静态）** |
| 查看录像 | R8 连接至 replay | **满足（跳转；回放页为本体后续项）** |
| 查看快照 | R8 **未连接处理器** | **未满足（死按钮，D-01）** |

---

## 5. 具体偏差与修复要求

**D-01（功能缺口）— “查看快照”按钮无处理器，点击无效**
- 观察：`P2AlarmCenter` 发出 `snapshotRequested(cameraId, snapshotRef)`；`main.qml` 仅连接了 `statusNavigate` 与 `replayRequested`，**未连接 `snapshotRequested`**；亦无快照查看页/对话框。交互 §5.2 明确列出“查看快照”操作。
- 复现路径：`read app/src/ui/qml/pages/P2AlarmCenter.qml`（`snapshotRequested`）；`read app/src/ui/qml/main.qml`（`onLoaded` 仅连两个信号）。
- 期望：连接 `snapshotRequested` 并提供快照查看（弹窗/详情或跳转），或在快照查看实现前隐藏该按钮并登记待办；不得保留无响应按钮。

**D-02（用户可观察点未闭环）— 批量确认的维护日志只存内存字符串，未写持久化、无真实时间/用户**
- 观察：`RailhmiAlarmModel::ack` 调用 `build_ack_log(res.acked_ids, "operator", 0, 0)` 只把结果存到 `last_log_` 字符串；**未调用 `SqliteRepository::insert_maintenance`**，`ts_wall_ms=0`、`user="operator"` 硬编码。工作项要求“批量确认后…写维护日志（HLT-007）”。
- 复现路径：`read app/src/ui/src/alarm_model.cpp`（`ack`）；对照 `app/src/platform/include/railhmi/platform/repository.h`（`insert_maintenance`）。
- 期望：在持久化接入时把 `MaintenanceLogRow` 写入 `maintenance_log`（真实 `ts_wall_ms`/`ts_mono_us`、当前用户），并让 UI 可查；在此之前不得宣称维护日志已写入。

**D-03（可访问性/输入校验）— 筛选 chip 与日期输入为 32px、无焦点环/键盘，日期非法静默忽略**
- 观察：`FilterChip` 高 = `Theme.size.button.sm`(32px) < 交互 §9/视觉 §10 的 44px 触控目标；chip 为裸 `Rectangle`+`MouseArea`，无 `focus`/键盘激活/可见焦点环；日期 `TextInput` 为自由文本，非法值经 `parse_date_utc_ms` 返回 -1 被静默忽略（无提示）。
- 期望：筛选控件触控目标 ≥44px（或明确豁免并记录）；支持键盘焦点与激活；日期输入加格式提示/非法反馈（如红边 + 文案），不静默丢弃。

**D-04（运行期接线）— 持久化事件未接入，默认空态；仅 simulation 灌样例**
- 观察：`main.cpp` 默认不打开 DB，`RailhmiAlarmModel` 仅在 `app.simulation=true` 时 `seedEvents` 样例；真实 `events` 表→报警中心接线归后续集成。故“筛选/批量确认”在应用内默认无数据可操作。
- 期望：集成项按周期从 `SqliteRepository`/事件层读取并 `seedEvents`，批量确认写库并刷新；本项边界如实。

**D-05（交互口径，轻量）— 批量确认范围=当前筛选结果，需明示**
- 观察：`ackAllUnacked()` 只确认 `rows`（当前已按筛选刷新）中的未确认项。若用户先筛选再批量确认，实际只影响筛选子集。
- 期望：按钮文案/提示明确“确认当前筛选结果”或提供“确认全部未确认”；避免用户误解。

**D-06（范围缺口，非本项缺陷）— QML 运行期未验证**
- 观察：板端缺 Qt5 Quick/Qml（E-01b）；无截图/录屏，行描边、空态、级别徽标去色可辨仅有静态证据。
- 期望：平台补齐 Qt 后运行并录制：筛选组合/空态、批量确认即时更新、录像不可用标注、级别徽标三重编码；UI 实现评审再做还原审阅（含 D-01/D-03）。

### 已确认无问题/正向项

- **录像可用性三态 + 行内显著标注**（§8.2/PLY-008）**闭合前序三态缺口**（REVIEW-ITEM-006 D-03 / ITEM-007 D-03 呈现面）。
- 默认排序严格“级别降序→时间降序”（UI-003/ALM-003），探针与单测一致（R2）。
- 六维筛选 + 空态“暂无符合条件报警”+[清除筛选]（R4），符合交互 §5.2/视觉 §8.2。
- 确认/批量确认**只改 ack_state、不删事件**（ALM-004；测试 `rows.size()==4`），已确认行描边转 `border`（视觉 §8.2）。
- 过滤/排序/确认**单一真相在 core**，QML 只展示与交互（R3/R7）；桥接键与 QML 逐键匹配。
- 级别徽标为“颜色+形状字形+文字”三重编码（▲/●/◆ + 一级/二级/三级），符合 §3.4/§9。
- 时间采用 UTC（`civil-from-days`，不依赖本地时区），符合同一时间基准方向。

---

## 6. 未覆盖 / 未执行（不得外推）

1. QML 运行期（列表/空态/批量确认即时反馈/记录标注）——缺口 E-01b。
2. 持久化事件接入（真实 `events` 表→报警中心）与维护日志落库（D-02/D-04）。
3. 快照查看（D-01）与历史回放页本体（跳转已通）。
4. 真机事件来源（ENV-D）与端到端报警闭环。
5. 目标板命令本轮未执行（由上游验证复跑覆盖）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-018 报警中心模型与 QML 静态接线）。
- 依据：任务内嵌用户可观察点中可核验部分——列表字段齐备、默认级别→时间降序、筛选组合/空态、批量确认只改 ack_state 且不删事件、录像不可用行内标注——经源码审阅、装配链路逐键核对、契约对照与归档证据（板端 512/512 + alarm-probe）逐条成立；并闭合前序录像三态缺口。
- 明确边界：本 PASS **不表示** QML 已运行或还原通过；**“查看快照”为死按钮（D-01）**、**维护日志未落库/无真实时间用户（D-02）**，故“查看快照”与“写维护日志”两项用户可观察点尚未闭环。
- 后续修复/交接要求见 §5：D-01（连接/隐藏快照）与 D-02（维护日志持久化）为**闭环项**；D-03（可访问性/输入校验）、D-05（批量范围文案）为交互质量项；D-04 持久化接入、D-06 运行期审阅为后续交接。
