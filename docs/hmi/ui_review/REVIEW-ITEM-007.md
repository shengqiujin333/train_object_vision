# UI 实现评审证据 — ITEM-007（railhmi-core 持久化与保留策略模型）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（v2 修复轮 commit `7f517b8`；历史轮 `fe28250`）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/{include/railhmi/core/{persistence_model.h,retention.h,timebase.h}, src/{persistence_model.cpp,retention.cpp,timebase.cpp}}`、`app/src/platform/{include/railhmi/platform/{sqlite_store.h,repository.h}, src/{sqlite_store.cpp,repository.cpp}}`、`app/third_party/sqlite/sqlite3_min.h`、`app/tests/core/test_persistence.cpp`
  - 构建证据: `docs/hmi/test_evidence/ITEM-007-persistence/README.md`
- 被评工作项: `ITEM-007`（SQLite WAL 单写线程建表与读写 9 张表；统一 UTC 毫秒 + 单调微秒基准；保护片段优先、普通片段按容量/周期淘汰；录像缺口记录模型；用户可观察：写入-重启-读取一致、WAL 恢复、保留淘汰顺序正确、缺口可查）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §5.2/§5.3/§5.6/§7.2（`ef47841`）、`docs/hmi/VISUAL_DESIGN.md` v2 §3.6/§7.7/§7.11（`40ccf4e`）、`docs/hmi/DESKTOP_APP_DESIGN.md` §9.3/§10/§11.1、PRD DATA-001/PLY-006/PLY-008
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（v2 修复轮；`docs/hmi/test_evidence/ITEM-007-persistence/VERIFY-ITEM-007.md`）；历史轮 `1dc7f2a` 曾 TEST_FAIL（D-01/D-02 `add_gap` 缺陷，已修复并复现）
- 关联前序评审: `docs/hmi/ui_review/REVIEW-ITEM-006.md`（D-01 事件墙钟时间、D-03 `record_available`）
- 评审日期/环境: 2026-10-15；Windows 主机（MINGW64 x86_64，无 C/C++ 工具链）；**本轮未连接 RK3588 目标板**（板端事实采用上游软件验证的独立板端复跑）
- 判定: **TEST_PASS（范围严格限定为 ITEM-007 持久化/保留核心层；不构成任何界面视觉/交互或 UI 可运行结论）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-007**（持久化与保留策略模型）。判定对象是任务内嵌用户可观察点：9 表建表与读写、写入-重启-读取一致、WAL 恢复、UTC 毫秒+单调微秒基准、保留淘汰顺序正确（保护片段优先）、缺口可查。
2. **本次未对任何界面做视觉/交互还原评审**：本项交付的是**无 GUI 的持久化核心层与 SQLite 仓储**；P2/P3/P6 的列表、时间轴、缺口斜纹、存储卡尚未实现（`VISUAL_DESIGN` §7.7/§7.11）。因此本文不对布局、颜色、字形、焦点、反馈、空/恢复状态做任何 PASS 结论（§5 D-06）。
3. **本轮未重新连接目标板**：板端 243/243 与验证自有 55 条扩展夹具已由上游软件验证独立复跑（v2 修复轮）。本 capability 授权方法为 `read/ui_evidence/design_assets`，主机无 C/C++ 工具链；本轮以**源码审阅 + schema/字段契约对照（交互视觉设计 §7.2/§7.7）+ 归档证据核对**为独立证据，并标注引用关系。
4. **历史轮 TEST_FAIL 的性质**：`1dc7f2a` 的 TEST_FAIL 定位两个 `add_gap` 实现缺陷（D-01 桥接缺口留重叠残对、D-02 合并未继承 reason），**是真实实现缺陷**；`7f517b8` 以迭代吸收 + 闭合区间相邻 + reason 先存非空修复，并有第一方与验证夹具双向复现。本评审确认 v2 修复成立、结果与证据一致。
5. 依赖缺口（板端无 `libsqlite3-dev`，用运行时库 `libsqlite3.so.0` + 项目 shim 头；无 Qt5 Quick/Qml）按职责交平台/环境；本评审不判定其“不可修复”，不返回 BLOCKED。
6. 本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现（v2） | `7f517b8` | `add_gap` 迭代吸收修复 + 第一方回归扩到 243；WAL 恢复升级为真实 `fork()+SIGKILL` |
| 被评实现（v1，历史） | `fe28250` | 230/230；验证 `1dc7f2a` TEST_FAIL（D-01/D-02 真实缺口合并缺陷） |
| 交互设计 | `ef47841` | v2 §5.2 报警中心筛选、§5.3 回放/缺口、§5.6 存储/NVR、§7.2 字段 |
| 视觉设计 / 令牌 | `40ccf4e` | v2 §3.6 集成状态、§7.7 Timeline 缺口斜纹、§7.11 NVR 卡 |
| 软件验证 | `9e5c432` | TEST_PASS；243/243 + 55/55 + 真实 SIGKILL WAL |

实现自述与仓库证据一致：`ITEM-007-persistence/README.md` §7 修复记录、板端 r3 日志与验证件相互印证；**未把真实断电恢复写成已验证（标注归 ENV-E 现场），未把多相机并发写冒充已实现**。

---

## 2. 评审方法

1. 源码审阅：`persistence_model.h`（9 行结构 + 时间戳成对 + `schema_ddl`）、`retention.h`/`retention.cpp`（`apply_retention`/`add_gap`）、`timebase.h`、`repository.h`、`test_persistence.cpp`。
2. 契约对照：schema/字段与交互 §7.2（报警事件、录像、设备、NVR、集成状态）与 `VISUAL_DESIGN` §7.7/§7.11 对照；`IntegrityRow.state` 与 §3.6 五态对照。
3. 归档证据核对：板端 r3 日志（243/243、`9 tables populated`、`write-restart-read consistent`、保留淘汰、缺口查询）、`VERIFY-ITEM-007.md`。
4. 逻辑独立推演：保留淘汰顺序（期限→容量、普通最旧→保护最旧）、`add_gap` 桥接吸收与 reason 语义、DDL 列数逐表点数。

**方法限制**：主机无 C/C++ 工具链 → 不能本地编译/运行 CoreTests；板端结论采用上游软件验证复跑证据。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | DDL 列数与设计 §11.1/行结构逐表点数 | events 15 / event_targets 6 / maintenance_log 7 / device_health 8 / recording_segments 8 / recording_gaps 5 / stream_events 6 / integrations 8 / config_kv 4 —— **与验证件一致** |
| R2 | 时间基准字段对照 | 4 张事件类表均成对 `ts_wall_ms` + `ts_mono_us`（DATA-001）；`TimeBase` 单调微秒为内部基准、启动校准一次 —— **解决前序 REVIEW-ITEM-006 D-01（UI 显示用墙钟时间）在该层的存储** |
| R3 | 保留淘汰顺序推演 | 先期限淘汰过期**普通**片段；再容量淘汰“普通最旧 → 保护最旧”；`retained_bytes`/计数确定 —— 与设计“保护片段优先保留”一致 |
| R4 | `add_gap` 桥接修复核对 | 迭代吸收（`while changed`）；相邻判定 `lo <= g.t1+1 && g.t0 <= hi+1`（相差≤1ms 视为相邻）；reason 取先存非空 —— D-01/D-02 修复成立 |
| R5 | 归档证据核对 | 板端 `243 passed, 0 failed`；`9 tables populated (segments=2)`；`write-restart-read consistent`；`capacity: continuous(11,12) then oldest critical(13)`；`gaps queryable by camera` |
| R6 | 仓储查询面清点 | `get_event(id)`、`query_segments()`、`query_gaps(cam)`、`query_stream_events()`、`count(table)`、`get_config`；**无事件列表/筛选查询、无事件更新/ack 写接口**（见 §5 D-01） |
| R7 | `record_available` 落库核对 | `EventRow.record_available` 为持久化布尔列（默认 false，占位）—— 仍为二态（见 §5 D-03） |

---

## 4. 判据逐条核对（ITEM-007 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 9 张表建表与读写一致 | DDL + `9 tables populated`（R1/R5） | **满足** |
| 写入-重启-读取一致 | `write-restart-read consistent` + `gap persisted across restart`（R5） | **满足** |
| SQLite WAL 恢复 | `journal_mode==wal`；真实 `fork()+SIGKILL`（未提交丢弃、已提交完好、仍 wal） | **满足** |
| UTC 毫秒 + 单调微秒基准 | `TimeBase` + 成对列（R2） | **满足** |
| 保护片段优先、普通按容量/周期淘汰 | `apply_retention` 顺序 + 断言（R3/R5） | **满足** |
| 缺口可查 | `query_gaps(cam)` + `gaps queryable by camera`；合并语义 v2 修复（R4/R5） | **满足** |

---

## 5. 具体偏差与修复要求

**D-01（UI 查询面缺口，交下一持久化项）— 仓储无事件列表/筛选查询，也无事件更新（ack）写接口**
- 观察：`SqliteRepository` 对 `events` 仅有 `insert_event` 与 `get_event(id)`；**无** `query_events(filter)`、**无** `update_event_ack`/`update_event`。而交互 §5.2/PLY-006 要求报警中心按日期/时间段/摄像头/类型/级别/确认状态筛选，ALM-004 要求确认动作只改 `ack_state` 并落库。
- 影响（UI 可见）：P2 报警中心无法从持久化层列出/筛选历史事件；重启后事件确认状态无法持久化（`RuleEngine::ack` 仅内存）。
- 复现路径：`read app/src/platform/include/railhmi/platform/repository.h`（仅 `get_event`，无列表/更新）；`grep query_events|update_event` 无命中。
- 期望：新增 `query_events(filter)`（时间范围/相机/类型/级别/ack_state/limit）与 `update_event_ack(id, ack_state, updated_ts)`；或明确把二者归入事件持久化后续工作项并在文档登记依赖，使 P2/ALM-004 有落点。

**D-02（UI 查询面缺口，轻量）— `query_segments()` 无相机/时间窗过滤**
- 观察：`query_segments()` 返回全部片段；P3 回放时间轴需要“按相机 + 时间窗”取片段与缺口对齐（`query_gaps(cam)` 已带相机过滤）。
- 期望：为片段提供相机/时间窗过滤查询，或文档明确“片段量小、由 UI 内存过滤”的假设与上限；避免大库时 UI 侧全量拉取。

**D-03（UI 语义歧义延续）— `record_available` 仍为二态布尔**
- 观察：`EventRow.record_available` 默认 `false`（占位），持久化层无“未解析/不可用”区分；PLY-008 要求仅在录像确实缺失时标注。
- 期望：与 REVIEW-ITEM-006 D-03 同源——采用三态或显式 `record_state`，并约定 UI 仅在“不可用”态显示黄条；否则每条新报警会短暂显示“录像不可用”。

**D-04（时间基准集成点须显式）— 规则层 `AlarmEvent.ts_ms`（单调）到 `EventRow.ts_wall_ms/ts_mono_us` 的换算契约**
- 观察：`persistence_model` 已提供成对时间戳（R2），`TimeBase` 提供换算；但 `RuleEngine`（ITEM-006）事件仅有单调 `ts_ms`，从事件到持久化的**注入/换算**由上层完成，接口注释未给出明确入口。
- 期望：明确“事件产生时经 `TimeBase` 写入 `ts_wall_ms`/`ts_mono_us` 再入库”的单一入口/责任（后续事件持久化项），保证 UI 读到可显示时间；避免各处自行调用 `system_now_ms()` 造成基准漂移（DATA-001）。

**D-05（观察，交实现/设计）— `add_gap` reason 的“先存在”顺序未在头文件定义**
- 观察：`retention.h` 注释“reason 取最先存在的非空值”未写明是创建序还是 `t0` 扫描序；`README §7` 自洽为扫描序，验证 O-03 已登记。
- 期望：在 `retention.h` 注释中写明“扫描（t0）序下第一个既有非空”，或由设计确认创建序语义；UI 缺口 tooltip 依此文案约定。

**D-06（范围缺口，非本项缺陷）— 无可评审的 UI**
- 观察：P3 时间轴/缺口斜纹、P6 存储卡与 NVR 卡、P2 事件列表均未实现，无可运行界面、无截图/录屏。
- 期望：页面落地后，UI 实现评审按 `VISUAL_DESIGN` §7.7/§7.11 与交互 §5.2/§5.3/§5.6 逐状态核对（报警标记、缺口斜纹、录像不可用标注、保护片段展示、NVR 未配置态），并要求运行录屏/交互记录。

### 已确认无问题/正向项

- 9 表 schema 与设计 §11.1 列数**逐表一致**（R1），字段覆盖交互 §7.2（事件/录像/设备/集成/日志）。
- 时间戳成对存储（`ts_wall_ms` + `ts_mono_us`）解决 UI 显示时间与单调排序的双重需求（R2）。
- 保留策略“保护片段优先、普通按期限/容量最旧淘汰”与设计 §9.3 一致，返回确定性顺序与计数（R3）。
- 缺口合并 v2 修复成立（桥接吸收、闭合区间相邻、reason 语义），并有第一方回归 + 验证夹具双向覆盖（R4）。
- WAL 恢复升级为真实 `fork()+SIGKILL`（非仅复制口径），`journal_mode` 仍 wal、未提交事务丢弃、已提交行完好。
- `integration.state` 取值与 `VISUAL_DESIGN` §3.6 五态一致；`recording_gaps.reason` 支持 §7.7 缺口 tooltip。
- 板端无 `libsqlite3-dev` 时用运行时库 + 项目 shim，CMake 明确提示；core 层保持纯标准库。

---

## 6. 未覆盖 / 未执行（不得外推）

1. 目标板任何命令本轮**未执行**；板端结论采用上游软件验证复跑与夹具证据。
2. 任何界面视觉/交互还原（无 UI 产物）。
3. 单写线程写入编排与高频写入运行时接入（本层仅保证单连接串行语义）。
4. 真实断电恢复（ENV-E 现场/嵌入式测试边界）；`/tmp` 之外的电量/掉电场景。
5. 多相机/多进程并发写。
6. 片段/快照文件本身落盘（`SegmentFileStore`）与 NVR 回放接口。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-007 持久化/保留核心层）。
- 依据：任务内嵌用户可观察点——9 表建表与读写、写入-重启-读取一致、WAL 恢复、UTC 毫秒+单调微秒基准、保护片段优先的保留淘汰顺序、缺口可查——经源码审阅、schema/字段契约对照、独立推演与归档证据核对逐条成立；板端 243/243 与验证 55 夹具一致；历史轮 TEST_FAIL（D-01/D-02 `add_gap`）为真实缺陷，v2 已修复并有双向复现。
- 明确边界：本 PASS **不表示**任何界面可运行或视觉/交互通过，**不表示**事件列表/确认持久化、单写线程编排、真实断电恢复已实现。
- 后续修复/交接要求见 §5：D-01（事件查询与 ack 写接口）、D-02（片段过滤查询）、D-03（录像可用性三态）、D-04（时间基准注入入口）为 UI/后续持久化项；D-05 注释澄清；D-06 待可运行 UI 后再评审。
