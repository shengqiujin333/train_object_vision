# UI 实现评审证据 — ITEM-021（AI 调试页与司机视图隔离）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `efdabac`，stamp `20261016-ITEM021-r2`）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/ui/qml/pages/P5AiDebug.qml`、`app/src/ui/qml/main.qml`、`app/src/ui/qml/components/FilterChip.qml`、`app/src/ui/src/main.cpp`、`app/src/core/include/railhmi/core/inference.h`、`app/src/core/src/inference.cpp`、`app/src/core/src/navigation.cpp`、`docs/hmi/design_tokens.yaml`（+ 生成 `Theme.qml`/`Tokens.qml`/`tokens.json`）、`app/tools/check_ai_debug_wiring.py`
  - 构建证据: `docs/hmi/test_evidence/ITEM-021-ai-debug/README.md`
- 被评工作项: `ITEM-021`（AI 调试页与司机视图隔离：展示检测框/ROI/置信度/距离/帧率/端到端延迟/模型版本/最近结果时间；调试叠加层开关默认关闭；调试描边独立高对比色避免与报警红混淆；司机角色不渲染本页与任何叠加。用户可观察：维护/研发可逐项开关叠加层；司机登录后主界面无检测框与参数；直达本页显示无权限）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §5.5、§2.3（角色矩阵）、§7.4、§9.10；`docs/hmi/VISUAL_DESIGN.md` v2 §8.5、§7.5；`docs/hmi/design_tokens.yaml`
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-021-ai-debug/VERIFY-ITEM-021.md`，core 586/586 + ai-probe 公式断言 + ext 40/40 + 接线扫描 33 项 mismatch=[]）
- 关联前序评审: `REVIEW-ITEM-011.md`（外壳/角色导航，D-01/D-02 残留）、`REVIEW-ITEM-017.md`（P5 叠加开关/字段）、`REVIEW-ITEM-015.md`（P4 网格定尺）
- 评审日期/环境: 2026-10-16；Windows 主机（MINGW64 x86_64，无 C/C++/Qt/GStreamer）；**本轮未连接 RK3588 目标板**
- 判定: **TEST_PASS**（范围：ITEM-021 交付项与 `INTERACTION §5.5` / `VISUAL §8.5`·§7.5 `3.4` 逐项满足；无运行期视觉结论）

---

## 0. 结论与口径

1. 本次判定针对 ITEM-021 的交付项与用户可观察点（逐项开关叠加层、独立高对比描边、司机不渲染本页与叠加、直达无权限）。
2. **关键**：所有交付项均可在**源码级**确证在位（字段齐备、开关默认 false、令牌独立、角色过滤与双重守卫、P1 无叠加）。**运行期视觉/交互（描边实际观感、虚线是否呈现、叠加像素对齐）本轮不做结论**（主机无 Qt、板端缺 Qt5 Quick/Qml，E-01b）。
3. 真实检测框/掩码/ROI 像素由媒体/模型层生成；本项消费 `DetectionResult` 做展示/叠加（handoff，见 §5）。
4. 本轮**未连接目标板**；板端 586/586、ai-probe、ext、扫描取上游 `software_verification`。本 capability 授权方法为 `read/ui_evidence/design_assets`。
5. **未修改任何实现代码**；只写本评审证据并维护清单。残存均为观察项/运行期待验，不构成失败判据（§5）。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `efdabac` | `DetectionBox.distance_m` + `AiDebugChannel.distance_text`；P5 四叠加开关默认 false + 「全部关闭」；`color.debug_box=#00E5FF`/`color.debug_roi=#FFD400` 入令牌并生成 Theme/Tokens；P5 司机守卫 + `aiDebugDenied`；`inference.cpp ai_debug_visible`；`check_ai_debug_wiring.py` |
| 交互设计 | `ef47841` | v2 §5.5（字段清单/四叠加开关/默认关/权限）、§2.3 角色矩阵、§9.10 |
| 视觉设计 | `40ccf4e` | §8.5（检测框青 `#00E5FF`、ROI 虚线黄 `#FFD400`、指标面板）、§7.5（P5 外不叠加调试框） |
| 软件验证 | `474f59e` | TEST_PASS；586/0 + ai-probe + ext 40/40 + 扫描 33 项 mismatch=[] |

---

## 2. 评审方法

1. 源码审阅：`P5AiDebug.qml`（字段/开关/颜色/守卫/布局）、`main.qml`（导航过滤/DeniedView/路由）、`main.cpp`（`railhmiNav`/`railhmiRole`/`railhmiAiDebug` 注入键）、`navigation.cpp`（`role_can_access`/`default_page`）、`inference.cpp`（`ai_debug_visible`）、`config.cpp`（`to_string(Role)`）。
2. 令牌核对：`design_tokens.yaml` → `Theme.qml`/`Tokens.qml` 的 `debug_box`/`debug_roi`；与报警红 `error_fg=#FF6B6B` 区分。
3. 隔离链路追踪：`default_role` → `role_can_access` 过滤 `railhmiNav` → `main.qml allowed(id)`/`DeniedView` → P5 二次守卫。
4. 清洁核对：P1 及全 QML 搜索 `debug_box/debug_roi/aiBox/aiRoi/aiMask/aiLabel` 仅 P5 消费。
5. 归档证据核对：v4 验证报告与 ext/扫描口径。

**方法限制**：主机无 Qt → 不能运行 P5 或截图。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 字段完整性 | P5 展示：检测框(`aiBox`)、ROI 虚线(`aiRoi`)、掩码(`aiMask`)、文字(`aiLabel` 含置信度+距离)；指标面板每路：`模型 modelVersion`、`FPS fpsText`、`端到端延迟 latencyMs`、`最近结果 lastResultText`、`置信度 confidenceText`、`距离 distanceText`。`§5.5` 8 项字段齐备 |
| R2 | 叠加层默认关闭 | `overlayBoxes/Roi/Labels/Masks` 四个 `property bool = false`；`overlayAny`；`allOff()` + 「全部关闭」chip；页面文案「默认关闭」 |
| R3 | 独立高对比描边 | 框 `border.color: Theme.color.debug_box`(#00E5FF)、ROI `Canvas.strokeStyle: Theme.color.debug_roi`(#FFD400)、标签 `color: debug_box`；令牌在 `design_tokens.yaml`→`Theme.qml`/`Tokens.qml` 一致；≠ 报警红 `#FF6B6B` |
| R4 | 令牌仅 P5 消费 | 全 QML 搜索：`debug_box/debug_roi` 仅 `Theme.qml`(生成)/`Tokens.qml`(生成)/`P5AiDebug.qml`（P5 行 8 为注释）；`aiBox/aiRoi/aiMask/aiLabel` 仅 P5 → P1/P2/P3 无调试叠加（`VISUAL §7.5`） |
| R5 | 司机导航过滤 | `main.cpp` `navList` 按 `role_can_access(role,p)` 过滤 → `railhmiNav`；`navigation.cpp` `Driver` 白名单仅 `Realtime/AlarmCenter/Replay`（无 `AiDebug`）→ 司机导航无 P5 入口 |
| R6 | 直达无权限 | `main.qml onNavigate`/`onStatusNavigate`：`denied = !allowed(pageId)` → `DeniedView`（标题「无权限」+「返回」）；`DeniedView` 文案「当前角色无权访问：…」 |
| R7 | P5 二次守卫 | P5 `allowed = railhmiRole !== "driver"`（`config.cpp to_string(Driver)="driver"` 一致）；`aiDebugDenied` 文案「无权限：AI 调试仅研发/维护/管理员可见」；内容 `visible: page.allowed` → 司机不渲染任何叠加/参数 |
| R8 | 控件可达性 | 叠加开关用 `FilterChip`（现高 `touch_target_min=44`、`activeFocusOnTab`、Return/Enter/Space）→ 逐项开关可触控/键盘操作 |
| R9 | 桥接键对齐 | `main.cpp` `aiList`（`railhmiAiDebug`）键：`cameraId/name/profile/profileLabel/modelVersion/available/unavailableReason/fpsText/latencyMs/lastResultText/confidenceText/distanceText/boxes{x,y,w,h,confidence,label,distanceM}/roi{name,x,y,w,h}` —— 与 P5 读取键逐一对应，无漏绑/错键 |

---

## 4. 判据逐条核对（ITEM-021 交付项）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 展示检测框 | `aiBox`（overlayBoxes，默认关） | 满足 |
| 展示 ROI | `aiRoi` 虚线 + `aiMask` 半透明（默认关） | 满足 |
| 展示置信度 | `aiLabel`/`confidenceText` | 满足 |
| 展示距离 | `distanceText`/`distanceM` | 满足 |
| 展示帧率 | `fpsText` | 满足 |
| 展示端到端延迟 | `latencyMs` | 满足 |
| 展示模型版本 | `modelVersion` | 满足 |
| 展示最近结果时间 | `lastResultText` | 满足 |
| 叠加层逐项开关 | 框/ROI/文字/掩码 四 chip + 全部关闭 | 满足 |
| 叠加层默认关闭 | 四属性默认 false | 满足 |
| 独立高对比描边、避免报警红 | `#00E5FF`/`#FFD400` ≠ `#FF6B6B` | 满足 |
| 司机不渲染本页 | 导航过滤（无入口）+ DeniedView + P5 守卫 | 满足 |
| 司机主界面无检测框与参数 | P1 无任何叠加标识（R4） | 满足 |
| 直达本页显示无权限 | DeniedView「无权限」+ P5「无权限」 | 满足 |

---

## 5. 偏差/观察项（非阻断；handoff 或运行期待验）

**D-01（观察，运行期待验）ROI「虚线」依赖 `ctx.setLineDash` 可用性**
- 观察：`aiRoi` 用 `Canvas.onPaint` 的 `if (ctx.setLineDash) ctx.setLineDash([6,4])` 画虚线；若运行期 Qt Quick Canvas 不支持 `setLineDash`，将**静默回退为实线**边框，与 `VISUAL §8.5`「ROI 虚线」不符。
- 期望：在 Qt 环境（V-01）确认虚线呈现；如不支持，改用分段绘制或贴图虚线，勿静默降级。

**D-02（观察，本地残留）`DeniedView.onBack` 未复位 `currentPageId`**
- 观察：`main.qml onBack: shell.denied = false`——返回后 `currentPageId` 仍为受限页；司机直达 P5 并点「返回」后，`Loader` 重新载入 P5，由 P5 自身 `aiDebugDenied` 占位显示「无权限」（不崩溃、无叠加），但停留在非导航项页面（沿用 `REVIEW-ITEM-011.md` D-01/D-02）。建议返回到该角色的默认页（`railhmiDefaultPage`）。

**D-03（观察，健壮性）P5 网格按 2 列 2 行定尺**
- 观察：`Grid.columns=2`，面板 `height=(grid.height-spacing)/2` 假定恰好 2 行；当前 `ai_enabled` 为 4 路（前/顶/底/司机室）恰好 2×2。若配置使 `ai_enabled` >4，面板将超高裁切（同类于 `REVIEW-ITEM-015.md` 网格定尺）。建议按通道数自适应行高/滚动。

**D-04（观察，单一真相）`ai_debug_visible` 与 `role_can_access` 并行**
- 观察：`inference.cpp ai_debug_visible(role)=role_can_access(role,Page::AiDebug)`；运行期导航过滤实际直接用 `role_can_access`，QML 又用字符串 `railhmiRole!=="driver"`。三者语义一致但存在多源；`ai_debug_visible` 似无运行期消费点。建议统一由角色权限函数驱动 QML 守卫（避免字符串口径漂移）。

**D-05（handoff，媒体层）真实检测框/掩码/ROI 像素**
- 观察：P5 消费 `DetectionResult`（框/ROI/置信度/距离/耗时/模型版本）；真实框/掩码图像与 `AiWorker` 接入 QML 运行时尚缺（`app.simulation=false` 时页面按「AI 不可用/等待结果」如实呈现）。属媒体/集成职责。

### 已确认无问题/正向项

- 8 项展示字段与四叠加开关齐备，开关**默认全关**并有「全部关闭」。
- 描边色 `#00E5FF`/`#FFD400` 经令牌管线生成；**不与报警红混淆**；P5 外不叠加（`VISUAL §7.5`）。
- 司机隔离三重：导航过滤（C++）+ 直达 DeniedView（外壳）+ P5 二次守卫；P1 无任何叠加/参数。
- 桥接键与 QML 读取键逐一对应，无漏绑；`FilterChip` 44px + 键盘可达（ROP 无障碍）。
- core `distance_text`（最小有效距离，`%.1f m`/`—`）与 `ai_debug_visible` 有板端单测（586/0）与 ext 40/40 覆盖。

---

## 6. 未覆盖 / 未执行

1. QML 运行期视觉/交互（描边观感、虚线呈现、叠加像素对齐）——无 Qt 环境。
2. 真实检测框/掩码图像与 `AiWorker`→QML 运行期接入（媒体/集成）。
3. 真机角色登录/切换（本应用按 `default_role` 配置驱动，无运行期登录）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-021 交付项与静态接线 vs `INTERACTION §5.5` / `VISUAL §8.5`·§7.5）。
- 依据：展示字段（检测框/ROI/置信度/距离/帧率/端到端延迟/模型版本/最近结果时间）齐备；调试叠加层框/ROI/文字/掩码**逐项开关且默认关闭**；描边用独立高对比令牌（`#00E5FF`/`#FFD400`）**避免与报警红混淆**；司机角色**导航无入口 + 直达 DeniedView「无权限」+ P5 二次守卫**且 P1 主界面无任何检测框/参数——均在源码级确证（R1..R9）。
- 边界：本 PASS **不表示**运行期视觉/交互手感通过（无 Qt 环境，§6），**不表示**真实检测框/掩码像素可用（媒体 handoff，D-05）。观察项 D-01（虚线运行期呈现）、D-02（返回未复位页）、D-03（网格定尺）、D-04（权限多源）供后续实现/运行期复评参考，不改变本次判定。
