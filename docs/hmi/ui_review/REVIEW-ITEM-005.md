# UI 实现评审证据 — ITEM-005（railhmi-core 流状态机与质量判定）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `ad27e8b`，2026-10-15 22:46:12 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/stream_state.h`、`app/src/core/src/stream_state.cpp`、`app/tests/core/test_stream.cpp`、`app/config/thresholds.yaml`（+`first_frame_timeout_ms`/`reconnect_backoff_ms`）、`app/src/core/src/config.cpp`（阈值校验）
  - 构建证据: `docs/hmi/test_evidence/ITEM-005-stream/README.md`
- 被评工作项: `ITEM-005`（六态 `CONNECTING/LIVE/FROZEN/OFFLINE/RECONNECTING/DEGRADED`、首帧/冻结/重连阈值退避、质量标志（过暗/过曝/遮挡/模糊）、达重连阈值升级设备事件、非 LIVE 禁止旧帧实时的判定接口；模拟时钟单测覆盖六态与升级序列）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §7.1（`ef47841`）、`docs/hmi/VISUAL_DESIGN.md` v2 §3.5/§7.5/§7.9 + `docs/hmi/design_tokens.yaml` v2 `stream_state`（`40ccf4e`）、`docs/hmi/DESKTOP_APP_DESIGN.md` v2 §6.2/§7.1（`e691118`）
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-005-stream/VERIFY-ITEM-005.md`）
- 评审日期/环境: 2026-10-15；Windows 主机（MINGW64 x86_64，无 C/C++ 工具链）；**本轮未连接 RK3588 目标板**（板端事实采用上游软件验证的独立板端复跑，见 §0.3）
- 判定: **TEST_PASS（范围严格限定为 ITEM-005 状态机/质量判定核心层；不构成任何 UI 视觉/交互或 UI 可运行结论）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-005**（流状态机与质量判定核心层）。判定对象是任务内嵌用户可观察点：六态齐备、模拟时钟驱动全部转换、阈值退避与升级、质量标志、非 LIVE 禁止旧帧实时、可复现“断流→重连计数→达阈值升级”序列。
2. **本次未对任何界面做视觉/交互还原评审**：本项交付的是**无 GUI 的 core 状态机**，没有可运行界面；六个状态的**视觉呈现**（`VISUAL_DESIGN` §3.5 状态色/字形、§7.5 CameraTile 占位层）尚未实现。因此本文不对布局、颜色、字形、焦点、空/错/恢复状态做任何 PASS 结论（§5 D-06）。
3. **本轮未重新连接目标板**：板端 79/79 与验证自有 40 条扩展夹具已由上游软件验证独立复跑（`VERIFY-ITEM-005.md`）。本 capability 授权方法为 `read/ui_evidence/design_assets`，主机无 C/C++ 工具链；本轮以**源码审阅 + 契约对照（状态 token/质量标志/硬约束 vs 交互视觉设计）+ 归档证据核对**为独立证据，并标注引用关系。
4. 依赖缺口（板端无 `libsqlite3-dev`/Qt5 Quick/Qml）按职责交平台/环境；本评审不判定其“不可修复”，不返回 BLOCKED。
5. 本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `ad27e8b`（2026-10-15 22:46） | 新增 `stream_state.{h,cpp}`、`test_stream.cpp`；阈值 schema 扩展 `first_frame_timeout_ms`/`reconnect_backoff_ms` 并校验；核实证据归档；无范围外改动 |
| 交互设计 | `ef47841` | v2 §7.1 六态状态机与非 LIVE 硬约束为本项直接判据 |
| 视觉设计 / 令牌 | `40ccf4e` | v2 §3.5 `stream_state` 六状态色/字形、§7.5 质量角标与占位层 |
| 桌面应用设计 | `e691118` | §6.2 流状态机、§7.1 状态契约 |
| 软件验证 | `a1d5729` | TEST_PASS；79/79 + 40 独立夹具 + 2 阈值负例一致 |

实现自述与仓库证据一致：`ITEM-005-stream/README.md`、板端 r1 日志（79/79、good/bad）与验证件相互印证；**未把运行时/媒体层职责冒充已实现**（取流、冻结检测、事件到报警映射均如实标注归后续）。

---

## 2. 评审方法

1. 源码审阅：`stream_state.{h,cpp}`（枚举/`to_string`/转换逻辑/`may_present_as_live`/事件）、`test_stream.cpp`、`config.h`/`config.cpp`（阈值字段与校验）、`app/config/thresholds.yaml`。
2. 契约对照：六态 token 与 `design_tokens.yaml` `stream_state` 键、`VISUAL_DESIGN` §3.5 状态表逐字对照；质量标志与 §7.5 质量角标、§7.2 运行时 `quality_flags` 对照；非 LIVE 硬约束与交互 §7.1 对照。
3. 归档证据核对：板端 r1 日志逐条断言、`VERIFY-ITEM-005.md`（79/79 + 40 夹具 + 阈值负例）。
4. 逻辑独立推演：用模拟时钟手动推演 `offline→connecting→live→frozen→reconnecting(1)→connecting→reconnecting(2)→connecting→offline(3)` 序列，核对升级恰在阈值处。

**方法限制**：主机无 C/C++ 工具链 → 不能本地编译/运行 CoreTests；板端结论采用上游软件验证复跑证据。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 六态 token 对照 `design_tokens.yaml` `stream_state` / `VISUAL_DESIGN` §3.5 | `to_string` 产出 `connecting/live/frozen/offline/reconnecting/degraded`，与令牌键名**逐字一致** |
| R2 | `may_present_as_live` 语义对照交互 §7.1 | 仅 `LIVE`/`DEGRADED` 且帧龄 < `freeze_timeout_ms` 为 true；`OFFLINE` 初始态、`CONNECTING`、`FROZEN` 均 false —— 满足“非在线禁止旧帧实时”；`DEGRADED` 按设计“降级可用”允许展示新鲜帧 |
| R3 | 质量标志对照 §7.5 | `too_dark/too_bright/occluded/blurred` 与 CameraTile 质量角标（暗/过曝/遮挡/模糊）一一对应 |
| R4 | 独立推演升级序列（模拟时钟） | `count` 1→2→3，第 3 次（=阈值）转 `OFFLINE` 并 emit `Escalated`；恢复出帧 `count` 清零并 emit `Recovered` —— 与 r1 断言一致 |
| R5 | 归档证据核对 | 板端 `79 passed, 0 failed`、`[PASS] stream timing thresholds parsed`、`config-check` good exit 0 / bad exit 1 |
| R6 | 阈值 schema 核对 | `thresholds.yaml` 含 `first_frame_timeout_ms: 5000`/`reconnect_backoff_ms: 1000`；`config.cpp:356-367` 校验 ≥1 / ≥0 并报具体字段 |

---

## 4. 判据逐条核对（ITEM-005 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 六态齐备 | 枚举与 `to_string` 六值；token 与视觉设计一致（R1） | **满足** |
| 模拟时钟驱动全部六态与阈值升级 | `tick(now_ms)` + `on_frame/on_source_error/on_reconnect_attempt`；板端 38 断言 + 验证 40 夹具覆盖 | **满足** |
| 首帧/冻结/重连阈值退避与升级 | `first_frame_timeout_ms`/`freeze_timeout_ms`/`reconnect_threshold`/`reconnect_backoff_ms`；配置校验（R6） | **满足** |
| 质量标志（过暗/过曝/遮挡/模糊） | `classify_quality` 四标志 + 无效统计不产标志；DEGRADED 注入 | **满足** |
| 达重连阈值升级设备事件 | `Escalated` 在 `count>=threshold` 时 emit（R4） | **满足** |
| 非 LIVE 禁止旧帧实时判定接口 | `may_present_as_live`（R2） | **满足** |
| 复现“断流→重连计数→达阈值升级”序列 | r1 原文 + R4 推演一致 | **满足** |

---

## 5. 具体偏差与修复要求

**D-01（UI 契约缺口）— 状态机不暴露“当前质量标志”，UI 只能从自由文本事件 detail 反推**
- 观察：`on_frame(now_ms, quality)` 只用 `quality.any()` 决定 `DEGRADED`，并以 `quality.describe()`（如 `"too_dark,blurred"`）写入 `QualityDegraded` 事件的 `detail`；类内**没有** `quality_flags()` 访问器，也不保存当前标志。
- 影响（UI 可见）：`VISUAL_DESIGN` §7.5 要求 CameraTile 渲染“画面降级”质量角标（暗/过曝/遮挡/模糊），交互 §7.2 将 `quality_flags` 列为实现须提供的运行时字段。当前 UI 若要显示当前角标，只能解析 `detail` 自由文本（脆弱、非类型化）或自行在媒体层另存一份标志——同一状态两处真相。
- 复现路径：`read app/src/core/include/railhmi/core/stream_state.h`（无 `quality_flags()`；`QualityFlags::describe()` 返回逗号串）；`read app/src/core/src/stream_state.cpp:on_frame`。
- 期望：在状态机上增加**类型化的当前质量标志**访问器（`const QualityFlags& quality_flags() const`，在 `on_frame` 更新、进入非流状态时清空），或在契约中明确“质量标志由媒体层单独发布、状态机不持有”，避免 UI 解析 `detail` 字符串。

**D-02（命名/口径澄清）— `may_present_as_live` 对 `DEGRADED` 返回 true，与“非 LIVE 禁止旧帧实时”的字面表述存在歧义**
- 观察：接口名含 `live`，但 `DEGRADED`（非 `Live`）也会返回 true（符合 §7.1“降级可用”）。
- 影响：实现者可能误用 `state()==Live` 作为“可否实时呈现”的判据，从而错误地把 DEGRADED 画面当成断流占位（或反之）。
- 期望：在头文件/设计中明确“可实时呈现 = LIVE 或 DEGRADED 且有新鲜帧”，或改名/补充 `is_presentable()` 语义；UI 侧渲染规则以该接口为准，不以 `state==Live` 判断。

**D-03（报警去重与粘滞交接）— `OFFLINE` 后 `Escalated` 非边沿触发，且状态机不再自动重连**
- 观察：`begin_reconnect` 每次调用都 `emit(ReconnectAttempt)`，且 `count>=threshold` 时每次都 `emit(Escalated)`；`tick` 的 `case Offline: break;` 使 `OFFLINE` 粘滞，不再尝试重连（恢复仅靠外部送入首帧或 `start()`）。
- 影响（UI/报警）：媒体层若在 `OFFLINE` 后持续调用 `on_source_error/on_reconnect_attempt`，会**反复产生 `Escalated`**，前端报警横幅可能反复抬升；同时“是否继续重连/何时手动重连”需由媒体层与交互 §5.6/§10 定义。
- 期望：二选一并写明——①将 `Escalated` 改为**每轮升级只触发一次**（边沿触发），或②明确要求事件/报警层（ITEM-015）按 ALM-005 抑制窗口去重；并在 `ITEM-013` 明确 `OFFLINE` 后是否继续退避重连及手动“重连”入口。

**D-04（恢复反馈事件缺口）— `DEGRADED → LIVE`（质量恢复）不产生任何事件**
- 观察：`on_frame` 中仅在 `prev ∈ {Connecting, Reconnecting, Frozen, Offline}` 时 emit `Recovered`；`prev == Degraded` 且新帧无标志时静默转 `LIVE`，无事件。
- 影响（UI 可见）：`VISUAL_DESIGN` §7.9 的“恢复”反馈（绿色 toast“已恢复：<对象>”+ 状态徽标转 ok）在**质量降级恢复**时无法由事件驱动，UI 只能轮询 `state()`。
- 期望：为 `DEGRADED→LIVE` 增加恢复事件（如 `QualityRecovered`），或明确文档化“质量恢复靠状态轮询、不产事件”，并让 UI 采用与之一致的驱动方式。

**D-05（可维护性/配置交接）— 质量判定阈值（过暗/过曝/遮挡/模糊）未纳入配置**
- 观察：`QualityThresholds` 默认值（`dark_mean=40`、`bright_mean=220`、`min_stddev=3`、`blur_lapvar=30`）为代码常量；`thresholds.yaml` 未含质量阈值，`config.cpp` 亦未解析。而 PRD 非功能需求“可维护性”要求阈值通过配置管理。
- 说明：`classify_quality(stats, t)` 已把阈值作为参数，接口可配置；缺的是配置来源。
- 期望：在配置层（ITEM-004 后续或本项配套）为 `thresholds.yaml` 增加 `quality: {dark_mean, bright_mean, min_stddev, blur_lapvar}`（含字段级校验），由媒体/质量层装载后传入；否则现场调光/遮挡灵敏度只能改代码。

**D-06（范围缺口，非本项缺陷）— 无可评审的 UI**
- 观察：六态的**视觉呈现**（§3.5 色/字形、§7.5 占位层与质量角标）尚未实现，无可运行界面、无截图/录屏。
- 期望：待 ITEM-004+ 的页面/组件（CameraTile、StatusStrip）落地后，UI 实现评审按 `VISUAL_DESIGN` §3.5/§7.5/§7.9 逐状态核对（连接中/在线/冻结/离线/重连中/降级 + 质量角标 + 恢复反馈），并要求运行录屏/交互记录。

### 已确认无问题/正向项

- 六态 token 与 `design_tokens.yaml`/`VISUAL_DESIGN` §3.5 **逐字一致**（R1），UI 无需二次翻译。
- 非 LIVE 禁止旧帧实时有**单一判定接口** `may_present_as_live`（含帧新鲜度严格 `<`），与交互 §7.1 硬约束一致（R2）。
- 质量标志与 §7.5 质量角标语义一一对应（R3）。
- 升级序列可复现且恰在阈值处触发；恢复清零计数并 emit `Recovered`（R4）。
- 依赖注入良好：`classify_quality` 只消费 `FrameStats`（不触图像），状态机用显式单调时钟，便于单测与 UI 复用。
- core 仍纯标准库；阈值字段缺失/越界报**具体字段路径**并非零退出（R6）。

---

## 6. 未覆盖 / 未执行（不得外推）

1. 目标板任何命令本轮**未执行**；板端结论采用上游软件验证复跑与夹具证据。
2. 任何界面视觉/交互还原（无 UI 产物）。
3. 实际取流/解码/冻结检测（`FrameStats` 计算）——归媒体层 ITEM-013。
4. 事件到报警级别映射（前方主摄离线/冻结→二级、侧视→三级）——归 ITEM-015。
5. 运行时状态发布（`fps/bitrate/resolution` 等 §7.2 字段）——归后续状态模型项；本层提供 `state/reconnect_count/last_frame_ms`。
6. 主机编译/运行 CoreTests（主机无工具链）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-005 流状态机与质量判定核心层）。
- 依据：任务内嵌用户可观察点——六态齐备（token 与视觉设计一致）、模拟时钟驱动全部转换、首帧/冻结/重连退避与阈值升级、质量标志（过暗/过曝/遮挡/模糊）、达阈值 `Escalated`、非 LIVE 禁止旧帧实时接口、可复现“断流→重连计数→达阈值升级”序列——经源码审阅、契约对照、独立推演与归档证据核对逐条成立；板端 79/79 与验证 40 夹具一致。
- 明确边界：本 PASS **不表示**任何 UI 可运行或六态视觉呈现通过，**不表示**取流/冻结检测/报警映射已实现。
- 后续修复/交接要求见 §5：D-01（当前质量标志类型化暴露）为 UI 契约关键项；D-02 命名澄清、D-03 报警去重与粘滞交接、D-04 质量恢复事件、D-05 质量阈值配置化为 UI/运维侧改进；D-06 待可运行 UI 后再评审。
