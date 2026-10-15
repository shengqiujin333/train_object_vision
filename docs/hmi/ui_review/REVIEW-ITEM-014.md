# UI 实现评审证据 — ITEM-014（视频渲染与实时监控页三联布局）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `6cb9122`，2026-10-16 02:46:23 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/layout.h`、`app/src/core/src/layout.cpp`、`app/src/ui/qml/pages/P1Realtime.qml`、`app/src/ui/qml/components/VideoSurface.qml`、`app/src/ui/qml/components/CameraTile.qml`、`app/src/ui/src/main.cpp`、`app/tests/core/test_layout.cpp`
  - 构建证据: `docs/hmi/test_evidence/ITEM-014-layout/README.md`
- 被评工作项: `ITEM-014`（把配置选定的渲染路径（默认 cpu，gl 可开关）接入 QML；左-前-右三联且前方最大；1920×1080 与 1280×720 两档 P0 元素不滚动即可见；一级报警横幅不遮挡前方中央可视区；用户可观察：两档 P0 核对、缩放后三联顺序与比例保持）
- 参照设计: `docs/hmi/VISUAL_DESIGN.md` v2 §8.1（三联 26/48/26、前方最大）、§7.5（CameraTile 占位层/最近帧）、§3.5（状态三重编码）（`40ccf4e`）、`docs/hmi/INTERACTION_DESIGN.md` v2 §4.1/§4.2/§7.1（`ef47841`）、`docs/hmi/DESKTOP_APP_DESIGN.md` §6.1/§7.1、`docs/hmi/TEST_DESIGN.md`
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-014-layout/VERIFY-ITEM-014.md`）
- 关联前序评审: `REVIEW-ITEM-012.md` D-02（连接状态无 §3.5 三重编码/占位层）、`REVIEW-ITEM-013.md` D-03（UI 占位层与字段消费缺口）
- 评审日期/环境: 2026-10-16；Windows 主机（MINGW64 x86_64，无 C/C++/Qt/GStreamer）；**本轮未连接 RK3588 目标板**
- 判定: **TEST_PASS（范围严格限定为 ITEM-014 三联布局几何与渲染路径接线；不构成 QML 运行期视觉结论；视频渲染面为桩，见 §5 D-01）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-014**。判定对象是任务内嵌用户可观察点：两档分辨率下 P0 元素一屏可见、三联左-前-右且前方最大、缩放后顺序与比例保持、一级横幅不遮挡前方中央可视区。
2. **本次未做 QML 运行期视觉评审**：板端缺 Qt5 Quick/Qml（E-01b），QML 无法运行；本文以**核心布局几何模型 + QML 源码/装配 + 设计对照**为证据（§5 D-06）。
3. **视频渲染面为桩**：`VideoSurface` 的实时画面层为空 `Item`，无 cpu（librga BGR 图元/Image）或 gl（`GstGLVideoItem`）实现；`P1Realtime` 三格硬编码 `live: false`，故实时层永不显示（§5 D-01）。工作项的**用户可观察点为布局**（已在几何层成立），但“视频渲染”本身尚未实现。
4. **本轮未重新连接目标板**：板端 377/377（含布局用例）与扫描由上游软件验证复跑。本 capability 授权方法为 `read/ui_evidence/design_assets`，主机无工具链；结论以源码/几何与归档证据为准。
5. 依赖缺口按职责交平台/环境；本评审不判定其“不可修复”，不返回 BLOCKED。本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `6cb9122`（2026-10-16 02:46） | 新增 `layout.{h,cpp}` + `test_layout.cpp`；`VideoSurface.qml`；`CameraTile` 内嵌渲染面；`P1Realtime` 改为 railhmiLayout 驱动三联；`main.cpp` 注入 `railhmiLayout`/`railhmiRenderPath`；证据归档 |
| 视觉设计 | `40ccf4e` | §8.1 三联 26/48/26、前方面积最大；§7.5 占位层；§3.5 状态三重编码 |
| 交互设计 | `ef47841` | §4.1 顶栏、§4.2 三联布局、§7.1 非 LIVE 硬约束 |
| 软件验证 | `e015c7c` | TEST_PASS；板端 377/377（布局用例）+ 令牌自检 + UI 扫描 + sim-inject |

实现自述与仓库证据一致：`ITEM-014-layout/README.md`、板端日志与布局断言相符；**明确声明 QML 运行期未执行、视觉截图待补**，未外推。

---

## 2. 评审方法

1. 源码审阅：`layout.{h,cpp}`（`compute_layout` 与 5 个判据函数）、`test_layout.cpp`、`P1Realtime.qml`、`VideoSurface.qml`、`CameraTile.qml`、`main.qml`（Loader/页面路由）、`src/main.cpp`（layout/render path 注入）。
2. 几何契约对照：三联比例、前方最大、P0 fit、横幅遮挡、比例保持与视觉 §8.1、交互 §4.1/§4.2 逐项对照；独立复算两档像素几何。
3. 渲染路径接线核对：`render_path`→`railhmiRenderPath`→`VideoSurface.renderPath` 链路；`live` 数据来源核对（原生 Grep `GstGLVideoItem|PaintedItem|registerType|Image`）。
4. 归档证据核对：板端 377/377 关键断言与布局表。

**方法限制**：主机无 Qt → 不能运行 QML；QML 行为以源码/装配分析为准，不作运行期结论。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 复算 1920×1080 几何 | margin 24；row 1872×856；left 487 / front 898 / right 487；front 占比 **0.480**；used=48+856+64+64+48=**1080** ≤1080 |
| R2 | 复算 1280×720 几何 | margin 16；row 1248×512；left 324 / front 600 / right 324；front 占比 **0.481**；used=48+512+64+64+32=**720** ≤720 |
| R3 | 三联顺序/邻接/无空隙 | left/front/right x 递增且两两相邻、右缘齐 row 右缘（test_layout 断言一致） |
| R4 | 横幅遮挡判据 | `banner={0,48,W,64}`（y48..112）与 `front_center`（y≈243..757）不重叠 → 不遮挡前方中央（顶部与 row 顶 72px 有重叠带，属允许的顶部条带） |
| R5 | 比例保持 | `ratios_preserved(1920×1080, 1280×720/960×540)`：16:9 下 left/front/right 比例偏差 ≤0.02（断言通过） |
| R6 | 渲染路径接线 | `app.render_path`→`to_string`→`railhmiRenderPath`→`VideoSurface.renderPath`（默认 cpu、gl 可开关），label 显示 `render:<path>` |
| R7 | 渲染实现核对（原生 Grep） | 产品 QML **无** `GstGLVideoItem`/`QQuickPaintedItem`/`Image`/`registerType` 视频承载；`videoLayer` 为空 `Item`；`P1Realtime` 三格 `live: false` → 实时层不可见（D-01） |

---

## 4. 判据逐条核对（ITEM-014 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 配置选定渲染路径接入 QML（默认 cpu，gl 可开关） | 路径字符串已注入并显示；**无实际渲染实现**（D-01） | **仅接线** |
| 左-前-右三联且前方面积最大 | 26/48/26，front 0.48，front>两侧（R1/R2/R3） | **满足** |
| 1920×1080 与 1280×720 两档 P0 不滚动即可见 | used 分别 1080/720 ≤ 高（R1/R2） | **满足（几何）** |
| 一级报警横幅不遮挡前方中央可视区 | banner 与 front_center 无重叠（R4） | **满足（几何）** |
| 缩放时三联顺序与比例保持 | 16:9 缩放 ratio 偏差 ≤0.02（R5） | **满足（几何）** |

---

## 5. 具体偏差与修复要求

**D-01（关键：item 标题的“视频渲染”为桩）— 无实际视频渲染面，`live` 硬编码 false**
- 观察：`VideoSurface.qml` 的 `videoLayer` 是**空 `Item`**（无 cpu 路径的 BGR 图像项、无 gl 路径的 `GstGLVideoItem`）；`renderPath` 只驱动右下角 `render:<path>` 文本。`P1Realtime.qml` 三格全部 `live: false`，且 `CameraTile.live` 未被任何运行时数据（`presentable`）绑定，故实时画面层永不显示、仅显示占位层。产品 QML 无任何视频承载类型（R7）。
- 影响（UI 可见）：三联是三个空占位框；“渲染路径接入”“视频渲染”目前不具备可观察的实时画面，且 CPU/GL 两条路径均无实现。
- 复现路径：`read app/src/ui/qml/components/VideoSurface.qml`（空 `videoLayer`）；`read app/src/ui/qml/pages/P1Realtime.qml`（`live: false`）；原生 Grep 无 `GstGLVideoItem`/`PaintedItem`/`Image` 承载。
- 期望：在 `videoLayer` 内实现两条渲染路径——cpu：`QQuickPaintedItem`/`Image` 消费 media 层 librga 输出的 BGR 帧；gl：`GstGLVideoItem`（或经 `qmlglsink`）——并由流运行时把 `presentable`（ITEM-013）绑定到 `live`、把 `last_frame`/`reconnect_count` 绑定到占位层信息；在 Qt 就绪前至少提供可运行的模拟帧源以验证路径切换。

**D-02（继续 ITEM-012 D-02）— 连接状态仍为原始 token 纯文本，未按 §3.5 三重编码**
- 观察：`CameraTile` 右上 `text: tile.state`（原始 `connecting/live/...`）；`VideoSurface` 占位层 `stateText: tile.state` 亦为原始 token。§3.5 要求“文字+颜色+形状”状态徽标。
- 期望：引入 `StatusChip` 状态→{label,color,shape} 单点映射（消费 `Theme.stream_state`），`CameraTile`/占位层统一使用；placeholder 显示最近帧时间（需 ITEM-013 D-02 的墙钟口径）与重连次数。

**D-03（方法/单一来源）— 布局“单一来源”只覆盖比例/高度，QML 仍自行 anchor 推导几何**
- 观察：`compute_layout` 计算像素 rect 并由单测断言 P0 fit；但 `P1Realtime.qml` 只消费 `leftRatio/frontRatio/rightRatio/statusStripHeight/alarmBarHeight/margin*`，几何由 anchors 重新推导。二者当前一致，但 `p0_fits_without_scroll` 的像素结论并未直接在 QML 层被强制；未来 anchors/margins 调整可能偏离单测。
- 期望：或将 rect 直接注入 QML（或用同一函数在 QML 侧计算），或在文档记录“QML 只消费比例、几何由 anchors 保证”，并让 UI 还原评审在 Qt 就绪后实测两档。

**D-04（交接）— 一级报警横幅浮层未实现（当前只有几何带）**
- 观察：`alarm_banner_obscures_front_center` 只校验几何带（top_bar 下 64px）与 `front_center` 不重叠；实际 L1 横幅（§7.4，常驻脉冲、跨全宽）由报警项实现。当前横幅带（y48..112）与三联行顶（y=72）有 40px 重叠，仅顶部条带、不触及中央可视区，符合规范。
- 期望：报警项实现横幅时沿用该几何带与 z-index（`Theme.z_index.alarm_banner`），并保持不覆盖 `front_center`；UIS 评审据此核对。

**D-05（观察）— `P1Realtime` 三联为按位置固定取相机，且 `railhmiCameras` 位置缺失时回退标题**
- 观察：`camByPosition` 取 left/primary/right；缺配置时回退“左侧/前方/右侧”与“连接中”。与交互 §4.2 固定三联一致（记录，非缺陷）。

**D-06（范围缺口，非本项缺陷）— QML 运行期未验证**
- 观察：板端缺 Qt5 Quick/Qml（E-01b）；无截图/录屏，两档 P0 可见性与缩放行为仅有几何单测。
- 期望：平台补齐 Qt 后运行并录制：1920×1080 与 1280×720 两档 P0 一屏、缩放时比例保持、L1 横幅不遮前方中央；UI 实现评审再做还原审阅（并覆盖 D-01/D-02）。

### 已确认无问题/正向项

- 三联比例 **26/48/26** 与视觉 §8.1/交互 §4.2 **精确一致**，前方面积最大（0.48）（R1/R2）。
- P0 元素两档“不滚动即可见”的像素和式成立（1080/720 恰好用满），且三联行邻接无空隙（R1–R3）。
- 一级横幅几何带与前方中央可视区无重叠（R4），且 `front_center` 限定在前方面板内部（断言）。
- 缩放比例保持判据（同 16:9，容差 2%）成立（R5）。
- 渲染路径由**配置**驱动（默认 cpu、gl 可开关）并接入 QML（R6），未硬编码；UI 无硬编码色值/字号（扫描 PASS）。
- `compute_layout` 纯几何、可单测，为后续页面复用提供单一比例来源。

---

## 6. 未覆盖 / 未执行（不得外推）

1. QML 运行期两档 P0 可见性、缩放行为与视觉呈现（板端缺 Qt5 Quick/Qml E-01b）。
2. **实际视频渲染**（cpu/gl 两条路径）与实时画面显示（D-01）。
3. 真实帧源（ENV-D 六路 RTSP）与六路并发合成（后续档）。
4. 一级报警横幅浮层与 z-order（归报警项）。
5. 目标板命令本轮未执行（由上游验证复跑覆盖）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-014 三联布局几何与渲染路径接线）。
- 依据：任务内嵌**用户可观察点**（两档 P0 不滚动即可见、三联左-前-右且前方最大、缩放保持顺序与比例、一级横幅不遮前方中央）在几何层经源码审阅、独立复算与归档断言（板端 377/377）逐条成立；渲染路径由配置接入 QML。
- 明确边界：本 PASS **不表示** QML 已运行或视觉呈现通过；**不表示“视频渲染”已实现**——实时画面层为空、`live` 硬编码 false（D-01），须由实现补齐后方可宣称该交付物完整。
- 后续修复/交接要求见 §5：D-01（cpu/gl 渲染面 + `live` 绑定）为**必须补齐**项；D-02（状态三重编码，继续 ITEM-012 项）为视觉还原项；D-03 单一来源方法、D-04 横幅浮层交接、D-06 待 Qt 就绪后运行期与还原审阅。
