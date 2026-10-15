# UI 实现评审证据 — ITEM-015（六路并发与全摄像头页）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `db1bae7`，2026-10-16 03:08:42 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/grid.h`、`app/src/core/src/grid.cpp`、`app/src/core/include/railhmi/core/channel_view.h`、`app/src/core/src/channel_view.cpp`、`app/src/media/include/railhmi/media/multi_stream.h`、`app/src/media/src/multi_stream.cpp`、`app/src/media/tools/multi_probe.cpp`、`app/src/ui/qml/pages/P4AllCameras.qml`、`app/src/ui/qml/components/CameraTile.qml`、`app/src/ui/src/main.cpp`、`app/tests/core/test_grid.cpp`
  - 构建证据: `docs/hmi/test_evidence/ITEM-015-grid/README.md`
- 被评工作项: `ITEM-015`（六路同时取流并在 2x3/3x2 网格显示；每格显示摄像头名、连接状态角标、实时 fps、最近一帧时间、检测职责徽标；断流/冻结格保留位置不重排；用户可观察：断开一路后该格显示占位与最近帧时间、其余五路继续刷新、网格不跳位）
- 参照设计: `docs/hmi/VISUAL_DESIGN.md` v2 §3.5/§7.5/§7.12/§8.4（`40ccf4e`）、`docs/hmi/INTERACTION_DESIGN.md` v2 §5.4/§7.1/§7.2（`ef47841`）、`docs/hmi/TEST_DESIGN.md` TC-STREAM-07
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-015-grid/VERIFY-ITEM-015.md`）
- 关联前序评审: `REVIEW-ITEM-012.md` D-01（P4 网格按 6 路硬编码不自适应）、D-02（状态无三重编码）；`REVIEW-ITEM-013.md` D-01/D-02/D-03（质量标志未暴露、最近帧不可显示、UI 未消费 presentable）；`REVIEW-ITEM-014.md` D-01（渲染面为桩）
- 评审日期/环境: 2026-10-16；Windows 主机（MINGW64 x86_64，无 C/C++/Qt/GStreamer）；**本轮未连接 RK3588 目标板**
- 判定: **TEST_PASS（范围严格限定为 ITEM-015 网格/每格视图模型、六路并发媒体层与 QML 静态接线；不构成 QML 运行期视觉结论；运行期接线与质量角标见 §5）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-015**。判定对象是任务内嵌用户可观察点：六路同时取流、2x3/3x2 网格、每格名/状态/fps/最近帧/职责徽标、断流格保留位置与最近帧、其余五路继续。
2. **本次未做 QML 运行期视觉评审**：板端缺 Qt5 Quick/Qml（E-01b），QML 无法运行；本文以**网格/视图模型 + 媒体并发源码 + QML 静态接线 + 归档证据**为证据（§5 D-06）。
3. **多项前序 UI 发现在本轮闭合**：P4 网格随窗口宽高切 3x2/2x3（闭合 ITEM-012 D-01 的 6 路自适应）；每格提供中文状态标签/fps/最近帧/重连字段且 `presentable` 驱动占位（部分闭合 ITEM-013 D-02/D-03）。
4. **运行期接线仍未完成**：`main.cpp` 以 `build_channel_views(registry, {}, 0)`（空运行时）注入，应用内六格均为“未启动”占位；`MultiStreamManager` 未接入 QML 运行时（实现自述与验证边界均载明）。因此**六路实时数据到界面的端到端呈现不可在应用内演示**（§5 D-01）。
5. **本轮未重新连接目标板**：板端 411/411 与六路并发探针由上游软件验证复跑。本 capability 授权方法为 `read/ui_evidence/design_assets`，主机无工具链；结论以源码/契约与归档证据为准。
6. 依赖缺口按职责交平台/环境；本评审不判定其“不可修复”，不返回 BLOCKED。本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `db1bae7`（2026-10-16 03:08） | 新增 `grid.{h,cpp}`/`channel_view.{h,cpp}` + `test_grid.cpp`；`MultiStreamManager` + `multi_probe.cpp`；`P4AllCameras` 改为 `railhmiChannels`+`railhmiGrid`；`CameraTile` 增 fps/最近帧/重连；`main.cpp` 注入 channels/grid；证据归档 |
| 视觉设计 | `40ccf4e` | §3.5 状态三重编码、§7.5 CameraTile、§7.12 徽标、§8.4 全摄像头 3×2 |
| 交互设计 | `ef47841` | §5.4 全摄像头页、§7.1 六态/占位、§7.2 字段 |
| 软件验证 | `62abcc8` | TEST_PASS；411/411（GRID 域≥12）+ 三组断流 media 探针全 true + ext 81/81 + QML 接线 34/34 |

实现自述与仓库证据一致：`ITEM-015-grid/README.md`、板端日志与断言相符；**明确声明真机六路 RTSP 与 QML 运行期未执行、媒体到 QML 的运行时接线未完成**，未外推。

---

## 2. 评审方法

1. 源码审阅：`grid.{h,cpp}`（形状/稳定槽位/单元格/不重叠）、`channel_view.{h,cpp}`（每格视图/标签/格式/占位）、`multi_stream.h`、`P4AllCameras.qml`、`CameraTile.qml`、`main.cpp`（channels/grid 注入）、`test_grid.cpp`。
2. 装配链路核对：C++ 注入 map 键（`id/name/position/badge/aiEnabled/slot/state/stateLabel/fpsText/lastFrameText/reconnectText/presentable/placeholder`；`wideColumns/wideRows/tallColumns/tallRows`）与 QML `modelData.*`/`G.*` 引用**逐键比对**。
3. 契约对照：状态标签与 §3.5、fps/最近帧与 §7.5、徽标与 §7.12、网格与 §8.4/交互 §5.4。
4. 归档证据核对：411/411、三组断流 media 探针（cut3/cut1/cut0）与 ext 81/81。

**方法限制**：主机无 Qt/GStreamer → 不能运行界面或六路取流；QML 行为以静态接线与契约分析为准。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 网格形状对照 §8.4/交互 §5.4 | `choose_grid_shape`：宽≥高→**3x2**，否则 **2x3**；QML `cols/rows` 随 `width>=height` 切换（消费 `railhmiGrid`） |
| R2 | 稳定槽位/不重排 | `grid_slots(i)={row=i/cols,col=i%cols}` 只依赖形状与序号；`test_grid` 断言 cut 后 `size==6`、id/slot 不变（归档三组探针 `order_unchanged=true`） |
| R3 | 每格字段装配逐键 | 视图提供 `name/stateLabel/fpsText/lastFrameText/reconnectText/badge/aiEnabled/presentable`；QML `P4AllCameras` 逐字段消费，`CameraTile.state: modelData.stateLabel` → **中文标签**（闭合 ITEM-012 D-02 的“原始 token”面） |
| R4 | 占位/实时互补 | `CameraTile.live: modelData.presentable`；`VideoSurface` 实时层 `visible: live`、占位层 `visible: !live`（互补） |
| R5 | 状态标签与格式 | `channel_state_label`：live→实时、connecting→连接中、reconnecting→重连中、frozen→画面冻结、offline→离线、degraded→降级、unstarted→未启动；`format_last_frame`→“N 秒前 / <1 秒前 / —”；`format_fps`→“25.0 fps / —” |
| R6 | 断流可观察点（归档） | cut 格 `reconnecting`/`presentable=false`/`重连 N 次`/冻结最近帧；其余五路 `live`+`presentable=true` 且 `last_frame_ms` 推进；`cells=6` 恒定（R2） |
| R7 | 运行期接线核对 | `main.cpp` 以 `build_channel_views(registry, {}, 0)` 注入 → 应用内全为“未启动”占位；无 `MultiStreamManager` 到 QML 的桥接（D-01） |
| R8 | 质量标志/渲染面核对 | `StreamStats`/`ChannelRuntime`/`ChannelView` 均无 `QualityFlags`（D-05）；`VideoSurface.videoLayer` 仍为空 `Item`（延续 ITEM-014 D-01） |

---

## 4. 判据逐条核对（ITEM-015 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 六路同时取流 | media 探针 `cells=6 live=6 presentable=6`，逐路 fps≈25（归档） | **满足（媒体层）** |
| 2x3/3x2 网格显示 | 形状/槽位/单元格模型 + QML 切换（R1/R2） | **满足（模型/静态）** |
| 每格名/状态角标/fps/最近帧/职责徽标 | `ChannelView` 字段齐备 + QML 消费（R3/R5） | **满足（字段/静态）** |
| 断流格保留位置不重排 | 稳定槽位 + 三组探针 order_unchanged（R2/R6） | **满足** |
| 断开一路→该格占位+最近帧时间、其余五路继续、网格不跳位 | 模型/媒体层成立（R6）；应用内因未接线只显示“未启动”（D-01） | **模型/媒体满足；应用运行期未接线** |

---

## 5. 具体偏差与修复要求

**D-01（端到端接线缺口）— 媒体并发未接入 QML 运行时，应用内六格恒为“未启动”占位**
- 观察：`main.cpp` 用 `build_channel_views(registry, {}, 0)`（空 `ChannelRuntime`）注入 `railhmiChannels`，故产品运行界面无法体现“断开一路后该格占位+最近帧、其余五路刷新”；`MultiStreamManager` 仅存在于 media 层与探针，无运行时桥接（R7）。
- 影响：工作项的**用户可观察点在应用内不可演示**；目前证据为 core/media 与静态接线。
- 复现路径：`read app/src/ui/src/main.cpp`（`build_channel_views(registry, {}, 0)`）；`read app/src/media/include/railhmi/media/multi_stream.h`（无 QML 桥接）。
- 期望：由后续集成项把 `MultiStreamManager::stats()` 周期映射为 `ChannelRuntime`（并绑定 `presentable`/fps/最近帧/重连），通过属性/模型通知刷新 `railhmiChannels`；在此之前，UI 评审不得宣称六路实时呈现通过。

**D-02（视觉还原，继续 ITEM-012 D-02）— 连接状态为纯文本标签，未按 §3.5 做“颜色+形状+文字”三重编码**
- 观察：`CameraTile` 右上 `text: tile.state`（P4 传入 `stateLabel`，已中文化），`VideoSurface` 占位层同；无状态点/形状与状态色（§3.5 `stream_state` 令牌）。
- 期望：引入 `StatusChip`（形状字形+状态色+文字）消费 `Theme.stream_state`，CameraTile/占位层统一；这样才满足“颜色非唯一编码 + 异常可辨”。

**D-03（残留边界）— 网格按“恰好 6 格”定尺，配置 >6 路时溢出**
- 观察：`P4AllCameras` 的 `cols/rows` 由方向决定（3x2 或 2x3，恒 6 格），单元格高 = `(grid.height-(rows-1)*gap)/rows`；若注册表 >6（如 ITEM-012 的 add→7 slots），第 7 格落在第 3 行但按 2 行高度计，将溢出/裁切。
- 期望：按 `railhmiChannels.length` 计算 `rows`（或允许滚动/分页），使 1..N 路均不裁切；或明确“P4 上限 6 路，多余不显示”并登记。

**D-04（规范对齐）— 状态标签与最近帧格式与视觉规范字面不一致（需设计确认）**
- 观察：`channel_state_label("degraded")="降级"`，而 `VISUAL_DESIGN` §3.5 为“画面降级”；`format_last_frame` 输出“N 秒前/分 秒前”，而 §7.5 文案为“最近帧 HH:MM:SS”（交互 §7.2 `last_frame_ts`）。
- 说明：相对时间是我在 ITEM-013 D-02 中允许的可选口径之一，但需与视觉设计正文对齐（或修订 §7.5 说明“相对时间亦可”）。
- 期望：与 `ui_visual_design` 对齐后固化文案（含 degraded 标签、最近帧显示口径）；无运行期结论前按规范字面优先。

**D-05（UI 数据契约，继续 ITEM-005/013 D-01）— 质量标志仍未进入视图/统计模型**
- 观察：`StreamStats`、`ChannelRuntime`、`ChannelView` 均无 `QualityFlags`；P4/P6 无法显示质量角标（过暗/过曝/遮挡/模糊），DEGRADED 仅显示“降级”文字。
- 期望：在 `ChannelRuntime`/`ChannelView` 增加质量标志（或位掩码）字段，媒体层保存当前标志，供 `DetectionBadge`/质量角标渲染。

**D-06（范围缺口，非本项缺陷）— QML 运行期与真机六路 RTSP 未验证**
- 观察：板端缺 Qt5 Quick/Qml（E-01b）；ENV-D 六路 RTSP 未执行（凭据未核验）；`VideoSurface.videoLayer` 仍为空（延续 ITEM-014 D-01）。
- 期望：平台补齐 Qt 并完成 D-01 接线后运行并录制：六路同屏刷新、断流格占位+最近帧+重连、其余五路继续、网格不跳位、方向切换 3x2/2x3；真机档待 `read_camera_stream` 授权后补测。

### 已确认无问题/正向项

- **闭合 ITEM-012 D-01（P4 6 路自适应）**：P4 网格随窗口宽高切 3x2/2x3，槽位由 `railhmiChannels` 固定序决定，无按状态重排代码路径。
- **稳定槽位**：`grid_slots` 仅依赖形状与序号；断流/冻结只改格内容，`size/id/slot` 不变（R2/R6）。
- **闭合 ITEM-012 D-02 的“原始 token”面**：每格状态经 `channel_state_label` 输出中文标签（实时/连接中/重连中/画面冻结/离线/降级/未启动），QML 消费 `stateLabel`（R3/R5）。
- **闭合 ITEM-013 D-02/D-03 的显示面**：每格提供 fps/最近帧/重连文本，`presentable` 驱动实时层与占位层互补（R3/R4）。
- **装配链路逐键匹配**：C++ 注入键与 QML `modelData.*`/`G.*` 无未定义引用（R3）。
- 配置驱动：删/改名/顺序跟随配置（ext uc0/uc1/uc2），无摄像头 ID/名/位置字面量；媒体层 `MultiStreamManager` 单路失败不影响其余路（六路探针）。
- 判据有鉴别力：ext 对伪造重叠/乱序槽位判 false；`grid_rects_no_overlap` 实跑。

---

## 6. 未覆盖 / 未执行（不得外推）

1. QML 运行期六路呈现、方向切换、断流格占位（板端缺 Qt5 Quick/Qml E-01b）。
2. 媒体层到 QML 的运行时接线（D-01）。
3. 真机六路 RTSP（ENV-D；凭据未核验）与 file 源素材。
4. 六路全模型推理（归推理接入项）。
5. 质量标志 UI 呈现（D-05）与实际视频画面（ITEM-014 D-01 渲染面为桩）。
6. 目标板命令本轮未执行（由上游验证复跑覆盖）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-015 网格/每格视图模型、六路并发媒体层与 QML 静态接线）。
- 依据：任务内嵌用户可观察点在**模型/媒体层**成立——六路并发、2x3/3x2 网格与稳定槽位、每格名/状态标签/fps/最近帧/重连/徽标、断流格保留位置与最近帧、其余五路继续、网格不跳位——经源码审阅、装配链路逐键核对、契约对照与归档证据（板端 411/411 + 三组 media 探针 + ext 81/81 + QML 接线 34/34）逐条成立；并闭合了 ITEM-012 D-01 与 ITEM-013 D-02/D-03 的显示面。
- 明确边界：本 PASS **不表示** QML 已运行或六路实时画面在应用内呈现通过（D-01 未接线）；**不表示**状态三重编码（D-02）、质量角标（D-05）、实际视频渲染（ITEM-014 D-01）已实现。
- 后续修复/交接要求见 §5：D-01（媒体→QML 运行期接线）为端到端关键项；D-02（状态三重编码）与 D-05（质量角标）为视觉还原项；D-03 网格 >6 路边界、D-04 文案对齐待设计确认；D-06 待 Qt 与真机授权后补测。
