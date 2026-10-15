# UI 实现评审证据 — ITEM-017（推理服务接入与双模型复用）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `47638a8`，2026-10-16 04:24:01 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/inference.h`、`app/src/core/src/inference.cpp`、`app/src/core/tools/ai_probe.cpp`、`app/src/media/include/railhmi/media/ai_worker.h`、`app/src/media/src/ai_worker.cpp`、`app/src/ui/qml/pages/P5AiDebug.qml`、`app/src/ui/qml/main.qml`、`app/src/ui/src/main.cpp`、`app/config/app.yaml`、`app/src/core/src/config.cpp`、`app/tests/core/test_inference.cpp`
  - 构建证据: `docs/hmi/test_evidence/ITEM-017-inference/README.md`
- 被评工作项: `ITEM-017`（为 ai_enabled 相机按 ai_profile 运行检测；有界丢旧帧队列 + 配置帧间隔；上报模型版本/推理 fps/最近结果时间/耗时；优先级 + 温度守卫调度；两侧广角不入队；用户可观察：AI 调试页显示检测框/ROI/置信度/耗时，停推理线程后状态区 AI 项与相关报警明确标注不可用而非静默）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §5.5/§7.2/§7.4（`ef47841`）、`docs/hmi/VISUAL_DESIGN.md` v2 §8.5/§7.6（`40ccf4e`）、`docs/hmi/DESKTOP_APP_DESIGN.md` §8.1/§8.2
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-017-inference/VERIFY-ITEM-017.md`）
- 关联前序评审: `REVIEW-ITEM-015.md` D-01（媒体未接入运行时）、`REVIEW-ITEM-016.md` D-01/D-05（返回目标、实时来源未接线）、`REVIEW-ITEM-014.md` D-01（渲染面为桩）
- 评审日期/环境: 2026-10-16；Windows 主机（MINGW64 x86_64，无 C/C++/Qt/GStreamer/RKNN）；**本轮未连接 RK3588 目标板**
- 判定: **TEST_PASS（范围严格限定为 ITEM-017 推理调度/遥测模型、工作线程与 QML 静态接线；不构成 AI 精度或 QML 运行期视觉结论）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-017**。判定对象是任务内嵌用户可观察点：仅 ai_enabled 相机推理、有界丢旧帧 + 帧间隔、优先级/预算/温度守卫、模型版本/fps/最近结果/耗时上报、AI 调试页字段、停线程后“AI 不可用”明确标注。
2. **本次未做 QML 运行期视觉评审**：板端缺 Qt5 Quick/Qml（E-01b）；本文以 **core 推理模型 + media 工作线程 + QML 源码/装配 + 归档证据**为证据（§5 D-06）。
3. **模型职责边界如实**：本轮检测器为 `SimDetector`（替代输入、**非 RKNN**）；真实模型加载/NV12 前处理/后处理精度归视觉/模型职责，应用只消费“模型版本/框/ROI/置信度/耗时”。本评审不据此宣称 AI 精度或真机结果。
4. **“AI 不可用”呈现仅到模型/调试页**：`ai_status_category`/`ai_alarm_annotation` 未被 `main.cpp` 状态区注入或报警层消费（§5 D-01/D-02）；状态区 AI 项在应用内为默认“未接入”、相关报警未标注。
5. **本轮未重新连接目标板**：板端 480/480 与 ai-probe/ai-runner 由上游软件验证复跑。本 capability 授权方法为 `read/ui_evidence/design_assets`，主机无工具链；结论以源码/契约与归档证据为准。
6. 依赖缺口按职责交平台/模型/环境；本评审不判定其“不可修复”，不返回 BLOCKED。本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `47638a8`（2026-10-16 04:24） | 新增 `inference.{h,cpp}`/`ai_probe.cpp`、`AiWorker`/`ai_runner.cpp`；`P5AiDebug.qml`；`app.yaml` 增 `ai:` 配置；`main.qml` `ai_debug→P5AiDebug`；`main.cpp` 注入 `railhmiAiDebug`；`test_inference.cpp`；证据归档（r1/r2 失败留痕：缺枚举、YAML 流式映射） |
| 交互设计 | `ef47841` | v2 §5.5 AI 调试页、§7.2 AI 运行时字段、§7.4 报警关闭/标注 |
| 视觉设计 | `40ccf4e` | §8.5 P5 AI 调试、§7.6 关键状态条、§10 可访问性 |
| 软件验证 | `5ed099f` | TEST_PASS；480/480 + ai-probe + ai-runner 线程 PASS + 取流回归 |

实现自述与仓库证据一致：`ITEM-017-inference/README.md`、板端探针原文相符；**明确声明未绑定 RKNN、真机 ENV-D 未执行、QML 运行期未执行、应用未接入 AiWorker**，未外推。

---

## 2. 评审方法

1. 源码审阅：`inference.{h,cpp}`（有界队列/优先级/帧间隔/调度/温度守卫/`ai_status_*`/`ai_alarm_annotation`/`build_ai_debug`）、`ai_worker.h`、`P5AiDebug.qml`、`main.qml`、`main.cpp`（`railhmiAiDebug` 注入）、`test_inference.cpp`。
2. 契约对照：AI 调试页字段与交互 §5.5/视觉 §8.5；AI 状态级别与 §7.6/§3.5；“AI 不可用”标注与交互 §7.4。
3. 装配链路逐键：`main.cpp` map 键 vs `P5AiDebug` `modelData.*` 引用。
4. 运行期接线核对：原生 Grep `ai_status_category`/`ai_alarm_annotation`/`plan_inference` 调用点。
5. 归档证据核对：480/480、ai-probe 与 ai-runner 原文。

**方法限制**：主机无 Qt/RKNN → 不能运行界面或真实推理；不据探针数值宣称视觉/精度结果。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 推理范围核对 | `ai_priority` front(1)>wheel_rail(2)>pantograph(3)>cab_door(4)；`ai_eligible = profile!=none` → 左右广角不入队；`build_ai_debug` 仅 `ai_enabled` 相机 |
| R2 | 队列/降频核对 | `BoundedFrameQueue` 满丢最旧并计数；`frame_due(seq,interval)=seq%interval==0` |
| R3 | 调度/温度守卫核对 | `plan_inference` worker 停→不调度；超温 `budget/2` 且 `temperature_guard=true`（仍运行，不静默停） |
| R4 | AI 状态语义核对 | worker 停→`Unavailable`/“AI 不可用”；有路陈旧→`Warning`/“推理陈旧”；否则 `Ok`/“运行”；`ai_alarm_annotation(false)="AI 不可用"`（status.h 仍 5 级，**未新增 Degraded 枚举**，与 README r1 叙述一致地改用 Warning） |
| R5 | 装配链路逐键 | `main.cpp` 键（`cameraId/name/profile/profileLabel/modelVersion/available/unavailableReason/fpsText/latencyMs/lastResultText/confidenceText/boxes{x,y,w,h,confidence,label}/roi{x,y,w,h,name}`）与 `P5AiDebug` `modelData.*` **全部匹配** |
| R6 | P5 呈现核对 | 叠加层**默认关闭**（`overlayVisible:false`），框/ROI 仅 `overlayVisible && available` 时渲染；指标面板含 模型/FPS/延迟/最近结果/置信度；不可用时每路显示 `unavailableReason` |
| R7 | 运行期接线核对（Grep） | `ai_status_category`/`ai_alarm_annotation` **仅出现在 tests/probes**；`main.cpp` 用 `build_key_status(KeyStatusSnapshot{}, role)`（默认快照）注入 → 状态区 AI 项恒为“未接入”；`main.cpp` 的 `aiRt.worker_alive=false` → 调试页全“AI 不可用”（D-01/D-02） |
| R8 | 硬编码/可访问性核对 | P5 含字面量 `color:"transparent"`、`border.width:1/2`（非令牌）；叠加层开关为裸 `Rectangle`+`MouseArea`、尺寸 `button.sm`（32px）、无焦点环/键盘（D-03/D-04） |

---

## 4. 判据逐条核对（ITEM-017 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 仅 ai_enabled 相机按 ai_profile 推理、两侧广角不入队 | R1 | **满足（模型）** |
| 有界丢旧帧 + 配置帧间隔 | R2 | **满足** |
| 上报模型版本/推理 fps/最近结果/耗时 | `AiChannelRuntime` 字段 + 探针（`model/fps/latency/last`） | **满足（模型/线程）** |
| 优先级 + 温度守卫调度 | R3 | **满足** |
| AI 调试页显示检测框/ROI/置信度/耗时 | `build_ai_debug` + P5 视图（R5/R6） | **满足（视图模型/静态；无真实像素/模型）** |
| 停推理线程后状态区 AI 项明确标注不可用 | 模型语义正确（R4），但**状态区未接线**（R7/D-01） | **模型满足；应用未接线** |
| 相关报警明确标注不可用而非静默 | `ai_alarm_annotation` 存在但**未被报警层消费**（R7/D-02） | **未接线** |

---

## 5. 具体偏差与修复要求

**D-01（运行期接线缺口）— 状态区 AI 项未消费 `ai_status_category`，应用内恒为“未接入”**
- 观察：`main.cpp` 以 `build_key_status(railhmi::core::KeyStatusSnapshot{}, role)` 注入，AI 项取自默认快照（`Unavailable/未接入`）；`ai_status_category`/`ai_status_text` `仅出现在 tests/probes`（R7）。因此“停推理线程→状态区 AI 项标注不可用”在应用内不可观察。
- 复现路径：`read app/src/ui/src/main.cpp`（`build_key_status(KeyStatusSnapshot{}, role)`）；Grep `ai_status_category` 无 `main.cpp`/`main.qml` 命中。
- 期望：把 `AiWorker::snapshot()`（或运行时 `AiRuntime`）经 `ai_status_category` 映射为状态区 AI 项（`railhmiKeyStatus`）并周期刷新；停线程时转为“AI 不可用”（`Alarm/Unavailable` 级，按 §7.6 突出）。

**D-02（报警标注未接线）— `ai_alarm_annotation` 未被报警/事件层消费**
- 观察：`ai_alarm_annotation(false)="AI 不可用"` 只在 `test_inference.cpp`/`ai_probe.cpp` 使用；ITEM-006 报警层（`AlarmEvent.description` 等）未调用它。故“相关报警明确标注不可用”未端到端实现。
- 期望：报警/事件层在 AI 不可用时把标注写入事件描述/字段（并保持与 ALM/PLY-008 一致），或明确归后续报警接入项并登记依赖。

**D-03（可访问性）— P5 调试叠加层开关不满足焦点/触控目标要求**
- 观察：开关为裸 `Rectangle` + `MouseArea`，`width/height = Theme.size.button.sm`（32px < 交互 §9/视觉 §10 的 44px 触控目标），无 `focus`/键盘激活、无可见焦点环、无控件语义（仅相邻 Text 标明“调试叠加层”）。
- 期望：改用可聚焦的开关/复选框控件（或自绘并补 `focus` 处理与 `focus_ring`），触控目标 ≥44px；键盘可切换。

**D-04（令牌/扫描一致性）— P5 含未令牌化字面量与命名色**
- 观察：`color: "transparent"`、`border.width: 1/2` 为字面量；`check_no_hardcoded_ui.py` 只覆盖十六进制与字号，**不检命名色/尺寸字面量**（印证 REVIEW-ITEM-010 D-01）。当前产品 UI 其余无硬编码十六进制（扫描 PASS），但该模式仍可绕过。
- 期望：把可令牌化的值（边框宽度/叠加色/透明处理）纳入令牌或明确允许；扩展扫描到 `Qt.rgba`/命名色/尺寸类（同 REVIEW-ITEM-010 D-01）。

**D-05（渲染面为桩）— P5 视频区为空占位矩形，检测框叠加于无像素区域**
- 观察：`P5AiDebug` 的视频区仅 `Rectangle` 占位，无实际帧承载；真实画面需 ITEM-013/014 的渲染面（当前为桩、且媒体到 QML 运行时未接线）。
- 期望：UI 集成项把媒体帧与 `AiWorker` 遥测接入 QML 后，检测框/ROI 才具备可观察的叠加基础；在此之前“显示检测框”仅为视图模型。

**D-06（范围缺口，非本项缺陷）— P5 网格 ≤4 路假设、QML 运行期与真机 AI 未验证**
- 观察：P5 左网格 `columns:2` 且高按 2 行计，隐含 ai_enabled ≤4（当前为 4 路，恰好 2×2）；若配置更多 ai 路将溢出。板端缺 Qt（E-01b）、真机 ENV-D 未执行、检测器为 SimDetector。
- 期望：按 ai 路数计算网格；平台补齐 Qt 与模型职责接入真实 `Detector` 后，运行并录制 P5（含叠加层、不可用态）与状态区 AI 项。

### 已确认无问题/正向项

- 推理范围、优先级、预算、温度守卫与有界丢旧帧**模型齐备且有断言**；左右广角明确不入队（R1–R3）。
- “AI 不可用/推理陈旧/运行”三级语义清晰，`ai_alarm_annotation` 提供非静默标注入口（R4）。
- `AiWorker` 独立线程、`start/stop` 明确 `join`、线程安全 `snapshot()`；`Detector` 接口可插拔，`SimDetector` 如实标注为替代输入（非 RKNN）。
- AI 调试页视图字段与交互 §5.5 **逐项齐备**（框/ROI/置信度/帧率/延迟/模型版本/最近结果），叠加层**默认关闭**符合“司机视图隔离”。
- 装配链路逐键匹配（R5）；P5 颜色/字号经 `Theme.*`；`main.qml` 已把 `ai_debug` 路由到 `P5AiDebug`。
- 配置化（`app.yaml` 的 `ai:` 含 interval/depth/budget/temp_limit/models）并按具体字段路径校验。

---

## 6. 未覆盖 / 未执行（不得外推）

1. QML 运行期（P5 叠加层/指标面板、状态区 AI 项）——缺口 E-01b。
2. 真实模型/后处理（RKNN 张量、NV12 前处理、精度）——归视觉/模型职责；本轮 `SimDetector`。
3. 真机 AI 端到端（ENV-D）与真实帧。
4. AiWorker→QML 运行时接线（D-01/D-02）与实际视频渲染（D-05）。
5. 报警层对 `ai_alarm_annotation` 的消费（D-02）。
6. 目标板命令本轮未执行（由上游验证复跑覆盖）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-017 推理调度/遥测模型、工作线程与 QML 静态接线）。
- 依据：任务内嵌用户可观察点在**模型/线程层**成立——仅 ai_enabled 推理、广角排除、有界丢旧帧 + 帧间隔、优先级/预算/温度守卫、模型版本/fps/最近结果/耗时上报、AI 调试页字段齐备、停线程→AI 不可用语义——经源码审阅、装配链路逐键核对、契约对照与归档证据（板端 480/480 + ai-probe + ai-runner 线程）逐条成立。
- 明确边界：本 PASS **不表示** AI 精度或真实模型结果（替代检测器）、**不表示** QML 已运行，**不表示**“状态区 AI 项/相关报警标注不可用”在应用内可观察（D-01/D-02 未接线）。
- 后续修复/交接要求见 §5：D-01（状态区 AI 项接 `ai_status_category`）、D-02（报警标注接入报警层）为**用户可观察点闭环**项；D-03（开关可访问性）、D-04（令牌/扫描）为 UI 质量项；D-05 渲染面、D-06 网格/运行期/真机为后续交接。
