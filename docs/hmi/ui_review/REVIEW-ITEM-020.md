# UI 实现评审证据 — ITEM-020（历史回放页 P3）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（修复轮 commit `b09fe30`）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/replay.h`、`app/src/core/src/replay.cpp`、`app/src/core/tools/replay_probe.cpp`、`app/tests/core/test_replay.cpp`、`app/src/ui/src/replay_model.{h,cpp}`、`app/src/ui/qml/pages/P3Replay.qml`、`app/src/ui/qml/main.qml`、`app/src/ui/src/main.cpp`、`app/config/thresholds.yaml`、`app/tools/check_replay_wiring.py`
  - 构建证据: `docs/hmi/test_evidence/ITEM-020-replay/README.md`
- 被评工作项: `ITEM-020`（历史回放页：日期与摄像头选择、时间轴〔连续段、报警区段显著色标、缺口斜纹、游标、关键帧缩略图〕、-30s/报警时刻/+30s/+60s 四个跳转、前方报警默认左/前/右同时间轴同步播放、事件详情侧栏〔时间/类型/级别/距离/置信度/处理状态/备注〕；NVR 录像来源不可用时显式标注并提供本机片段回放。用户可观察：从报警卡进入后自动定位报警时刻并锁定相关摄像头，三路同步偏差超出配置阈值时显示提示）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §5.3（P3）、§3.x 异常（PLY-008）、§7 权限；`docs/hmi/VISUAL_DESIGN.md` v2 §7.7（时间轴 Timeline）、§8.3（P3 历史录像）、§3.4（级别色彩）；`docs/hmi/design_tokens.yaml`（`touch_target_min:44`、`size.button.md:36`、`alarm.*.accent/tint_bg`）
- 关联上游验证: `n006 software_tester.software_verification` — v1 **TEST_FAIL**（`VERIFY-ITEM-020.md`，D-01/D-02）→ 修复轮 v2 **TEST_PASS**（`VERIFY-ITEM-020-v2.md`，574/574 + ext 84/84 + 接线扫描 mismatch=[]）
- 关联前序评审: `REVIEW-ITEM-018.md`（P2 行内录像三态/查看录像入口）、`REVIEW-ITEM-019.md`（缺口记录数据来源）、`REVIEW-ITEM-014/016.md`（占位层/状态三重编码）
- 评审日期/环境: 2026-10-16；Windows 主机（MINGW64 x86_64，无 C/C++/Qt/GStreamer）；**本轮未连接 RK3588 目标板**
- 判定: **TEST_FAIL**（范围：ITEM-020 回放页实现 vs 任务内嵌 UI 交付清单与 `INTERACTION`/`VISUAL` 规范；缺陷为源码可确证的界面/接线缺项，非仅运行期视觉不确定性）

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次判定针对 ITEM-020 自身列举的界面交付项（时间轴五要素、四跳转、三联、详情栏、来源标注）与用户可观察点，不是重新判定 `software_verification` 的 core 判据。
2. **关键：失败项为源码可确证的界面缺项**（如缺口斜纹缺失、关键帧条桥接未导出 `keyframes` 恒空、播放/暂停/倍速控件缺失、报警段级别色硬编码），**不属于“仅运行期视觉不可判定”**——这两类必须区分。运行期视觉/交互手感（配色观感、动画、点选手感）本轮仍不做结论。
3. **正向确认**：报警卡→回放自动定位并锁定相关摄像头（v1 D-01）、三联同步偏差提示可达且无时钟显式不可用（v1 D-02）均已修复，core 模型/探针/接线扫描证据在档。回放页“三路同步”的实际取流/解码复用媒体层，属媒体职责（handoff，非本次失败点）。
4. 本轮**未连接目标板**；板端 574/574、ext 84/84、接线扫描取上游 `software_verification` v2 权威轮。本 capability 授权方法为 `read/ui_evidence/design_assets`，主机无工具链。
5. 依赖缺口按职责交实现（界面）/媒体（真实播放）；本评审不判定其“不可修复”，不返回 BLOCKED。**本轮未修改任何实现代码**，只写本评审证据与清单登记。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `b09fe30`（修复轮） | 新增 `replay.{h,cpp}`/`replay_probe.cpp`/`test_replay.cpp` + `replay_model.{h,cpp}` + `P3Replay.qml`；`main.qml onReplayRequest` 接线 `selectAlarm`；`main.cpp` 注入样例报警行 + 时钟偏移；`thresholds.replay_sync_threshold_ms=300`；`check_replay_wiring.py` 静态取证 |
| 交互设计 | `ef47841` | v2 §5.3（日期/摄像头、时间轴+报警段+关键帧+播放/暂停/倍速/前后跳转、四跳、详情七字段、缺口显式、导出权限） |
| 视觉设计 | `40ccf4e` | §7.7（轨道 32px、连续段 accent 低饱和、报警段 3px 级别色+tint 可点选、缺口斜纹、游标 2px+顶部时间、关键帧条 64px、播放控制 44px）；§8.3；§3.4 级别色彩 |
| 软件验证 | `acb80a9` | v2 TEST_PASS；574/574 + 探针 fails=[] + 接线扫描 mismatch=[] + ext 84/84 |

`main.qml` 页面路由已指向 `pages/P3Replay.qml`（此前为占位 `PageView.qml`），P3 为本轮真实落地页面。

---

## 2. 评审方法

1. 源码逐项审阅：`P3Replay.qml`（布局/组件/绑定/交互/状态）、`replay_model.{h,cpp}`（桥接键集合与 `modelData.*` 逐键对照）、`replay.{h,cpp}`（模型字段）、`main.qml`（导航与信号接线）、`main.cpp`（注入）。
2. 契约对照：时间轴五要素（连续段/报警段显著色标/缺口斜纹/游标/关键帧缩略图）、四跳、三联同步、详情七字段、来源标注 → 对照 §5.3/§7.7/§8.3。
3. 令牌与尺寸核对：`design_tokens.yaml`（`touch_target_min:44`、`size.button.md:36`、`size.button.sm:32`、`alarm.L1/L2/L3.accent/tint_bg`）与 QML 实际取值。
4. 桥接键集合逐键核对：`timeline()` 返回键 vs `P3Replay.qml` 读取键，检测漏绑/错键。
5. 归档证据核对：574/574、replay-probe 原文、v1/v2 验证报告与过程留痕。

**方法限制**：主机无 Qt → 不能运行 P3 或截图；不得据源码宣称视觉观感/交互手感通过。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 桥接键集合核对 | `replay_model.cpp timeline()` 返回 `valid/cameraId/dayStartMs/dayEndMs/segments/gaps/alarms/hasAlarmSegment/hasGap`——**不含 `keyframes`**（D-01） |
| R2 | QML 关键帧条 | `model: page.tl.keyframes !== undefined ? Math.min(48, page.tl.keyframes.length) : 0` → 因 R1 恒为 `0`，**条带恒空**（D-01） |
| R3 | 缺口渲染 | `objectName:"timelineGap"` = `bg_base` 实底 + `degraded` 边框 + 居中文本“录像缺失”；**无斜纹图样**（D-02） |
| R4 | 播放控制 | 仅 4 个 `FilterChip`（-30s/报警时刻/+30s/+60s）；**无播放/暂停、无倍速**；core `PlaybackState{playing,speed}` 无 QML 消费（D-03） |
| R5 | 报警段色标 | `border.color: page.levelColor("L2")`（critical）、`alarmMarker` `levelColor("L1")` **硬编码**；`TimelineSegment` 无 `level` 字段，模型无法携带级别（D-04） |
| R6 | 段可点选 | 仅整轨 `MouseArea` 设游标；段/缺口/标记无独立点击处理，点选不选中段/不跳转（D-05） |
| R7 | 摄像头选择 | 顶部 chip `model: page.lockedCams`（前方报警=3 路，其余=1 路）；**无法选择其它摄像头**（D-06） |
| R8 | 详情栏录像不可用 | `detail.note = row.record_label`（录像可用性）以 `text_muted` 普通文本显示“备注：…”；**无 §8.3 黄条**（D-07） |
| R9 | 尺寸/令牌 | 轨道 `button.md=36`（规范 32）；关键帧条 `button.md=36`（规范 64）；`FilterChip` 高 `button.sm=32` < `touch_target_min:44`，无 `activeFocus`/`Keys`（D-08） |
| R10 | 非法日期 | `timeline()` 非法日期返回 `valid:false`；`reload()` 不处理 → `dayStart()=0/dayMs()=86400000`，渲染纪元空轨，**静默**（D-08） |
| R11 | D-01/D-02 修复核对 | `onReplayRequest(cameraId,tsWallMs)`→`selectAlarm`；`refreshSync()` 用 `R.syncState(cursor)`，无 `[cursor,cursor,cursor]`；`relatedCameras()`/`sync_snapshot()` 在位——**修复确认在**（正向） |

---

## 4. 判据逐条核对（ITEM-020 交付项）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 日期与摄像头选择 | 日期输入在位；摄像头仅限锁定相关路（R7） | 部分满足 |
| 时间轴-连续段 | `continuous` 段渲染（`neutral_bg`） | 满足 |
| 时间轴-报警区段显著色标 | 有（`degraded_bg` 填充 + 3px 边），但**级别硬编码 L2**（R5） | 部分满足 |
| 时间轴-缺口斜纹 | 实底+文字，**无斜纹**（R3） | **不满足** |
| 时间轴-游标 | 2px 白线在位；**无顶部时间(mono)** | 部分满足 |
| 时间轴-关键帧缩略图 | 条带**恒空**（桥接未导出），非缩略图（R1/R2） | **不满足** |
| -30s/报警时刻/+30s/+60s 四跳 | 四 chip + `jump_targets`（探针逐毫秒） | 满足 |
| 前方报警默认左/前/右同步播放 | 三联面板就位 + `related_cameras` 锁定；**实际播放为媒体层占位**（handoff） | 模型满足/播放 handoff |
| 事件详情七字段 | 时间/类型/级别/距离/置信度/处理状态/备注 全在 | 满足 |
| NVR 不可用显式标注+本机片段 | `sourceStatus` 文本“来源：本机片段（NVR 录像不可用，回放本机片段）” | 满足 |
| 播放/暂停/倍速（§5.3/§7.7） | **缺失**（R4） | **不满足** |
| 从报警卡进入自动定位+锁定 | v1 D-01 已修复（R11） | 满足 |
| 三路同步偏差超阈值提示 | v1 D-02 已修复，提示可达（R11） | 满足 |

---

## 5. 具体偏差与修复要求（交桌面实现）

**D-01（严重）关键帧缩略图条恒空：桥接未导出 `keyframes`，QML 又非缩略图**
- 观察：`replay_model.cpp::timeline()` 的 `QVariantMap` **不含 `keyframes` 键**；`P3Replay.qml` keyframe Repeater `model: page.tl.keyframes !== undefined ? Math.min(48, keyframes.length) : 0`，故**恒为 0，条带空白**。即便有数据，也只按 `index * (keyStrip.width/48)` 均匀画 1px 竖线，未使用关键帧 `ts_ms`，也非缩略图。
- 复现路径：读 `app/src/ui/src/replay_model.cpp` `timeline()` 返回键集合；读 `app/src/ui/qml/pages/P3Replay.qml` 的 `keyStrip`/keyframes Repeater。
- 期望：桥接导出 `keyframes`（含 `ts_ms` 与缩略图引用，缩略图由媒体层生成）；QML 按 `ts_ms` 定位渲染缩略图；条高按 `VISUAL §7.7` 64px。

**D-02（严重）时间轴缺口无斜纹**
- 观察：`objectName:"timelineGap"` 为 `Theme.color.bg_base` 实底 + `Theme.color.degraded` 边框 + 居中“录像缺失”文本；**无 §7.7/§8.3 要求的“斜纹留白”**。
- 复制路径：`P3Replay.qml` gaps Repeater。
- 期望：缺口用斜纹图样（`Canvas`/`ShaderEffect`/斜纹贴图）留白绘制，保留“录像缺失”提示（tooltip/文本）。

**D-03（中）播放控制缺 播放/暂停、倍速**
- 观察：播放区仅 4 个跳转 `FilterChip`；§5.3/§7.7 明确“播放/暂停/倍速/前后跳转”；core `PlaybackState{playing,speed}` 已定义但无任何 QML 消费。
- 复现路径：`P3Replay.qml` 播放控制 Row；`replay.h` `PlaybackState`。
- 期望：增补 播放/暂停、倍速（0.5×/1×/2× 等）控件并接线 `PlaybackState`；控件触控目标 ≥ `touch_target_min`（44px）。

**D-04（中）报警区段/报警标记级别色硬编码，模型不携带级别**
- 观察：`critical` 段 `border.color: page.levelColor("L2")`、`alarmMarker` `levelColor("L1")` 固定；`TimelineSegment`/`build_timeline` 无 `level` 字段（`RecordingSegmentRow` 亦无），故一/三级报警的回放段与标记都显示二级/一级色，违反 `VISUAL §3.4` 级别色彩语义。
- 复现路径：`P3Replay.qml` segments/alarms Repeater；`replay.h` `TimelineSegment`；`persistence_model.h` `RecordingSegmentRow`。
- 期望：段/标记携带实际 `level`（core 由关联事件/alarm 行推导），QML 用 `Theme.alarm.<Lvl>.accent` + `tint_bg` 渲染“半透明 tint + 3px 级别色”。

**D-05（中）段/缺口/标记不可点选**
- 观察：§7.7“报警区段…可点选”；实现仅整轨 `MouseArea` 设游标，段/缺口/标记无独立点击处理（点击不选中段、不定位到该报警）。
- 期望：段/缺口/标记可点选，点选后定位游标并联动右侧详情（选中该报警）。

**D-06（中）摄像头选择被限制为“相关路”**
- 观察：顶部摄像头 chip `model: page.lockedCams`，前方报警只列 3 路、其余 1 路；§5.3“日期与摄像头选择”应有完整摄像头选择。
- 期望：提供完整摄像头列表选择（本项目 6 路），`relatedCameras` 仅作为进入时的默认锁定/定位。

**D-07（中）详情栏“录像不可用”未用黄条显著标注；备注被录像可用性占用**
- 观察：§8.3“录像不可用事件在详情栏黄条标注”；实现把 `row.record_label` 塞入 `note`，以 `text_muted` 普通文本显示“备注：[录像不可用]”，**无黄条**且无独立备注字段。
- 期望：录像不可用用 `Theme.color.degraded`/`degraded_bg` 黄条显著标注；备注应为独立字段（来自事件备注），不与录像可用性混用。

**D-08（中）日期输入非法静默；FilterChip 32px 无键盘可达**
- 观察：`dateInput`（120px）无校验/错误反馈；`timeline()` 非法日期返回 `valid:false`，`reload()` 不处理 → `dayStart()=0/dayMs()=86400000`，渲染纪元空轨，**静默失败**。`FilterChip` 高 `size.button.sm=32` < `touch_target_min:44`，无 `activeFocus`/`Keys`，键盘不可达（与 `REVIEW-ITEM-018.md` D-03 同类）。
- 期望：非法日期显示错误提示或还原为上次有效值；交互控件达 44px 触控目标并提供可见键盘焦点环（`Enter`/`Space` 触发）。

**D-09（轻）游标顶部时间缺失；轨道/关键帧条高度偏离规范**
- 观察：§7.7 游标“顶部时间(mono)”未实现（时间显示在播放行）；轨道高 `size.button.md=36`（规范 32）；关键帧条 36（规范 64）。
- 期望：按 `VISUAL §7.7` 尺寸与游标顶部时间呈现。

### 已确认无问题/正向项（实现无需返工）

- **v1 D-01 已修复**：`main.qml::onReplayRequest(cameraId, tsWallMs)` 使用双参并调用 `railhmiReplay.selectAlarm`；`P3Replay.reload()` 消费 `selectedCameraId/selectedAlarmTs/relatedCameras/selectedDetail`；`related_cameras()` 前方主摄→左/前/右三联、其余→单路（探针 `type=lock locked=CAM-02,CAM-01,CAM-03`）。
- **v1 D-02 已修复**：`refreshSync()` 改用 `R.syncState(cursor)`（`sync_snapshot` 由各路真实时钟偏移构建），无时钟→`available=false`→“同步状态不可用”；偏移 900ms>300ms 时提示可达（`objectName:"syncStatusText"`）。
- 时间轴数据模型与四跳（含 `seek` 窗口裁剪、`jump_targets` 逐毫秒）；连续段/游标/报警标记渲染；详情七字段齐全。
- NVR 来源回退：`replay_source_status` nvr 不可用→本机片段 + 显式标注，均不可用→“录像不可用”。
- 配置阈值 `replay_sync_threshold_ms` 解析/校验（负值/缺键报错）。
- `objectName`（`timelineTrack/timelineCursor/alarmSegment/timelineGap/alarmMarker/keyframeStrip/replayPanel-*/replayDetail/syncStatusText`）齐备，利于自动化定位。

---

## 6. 未覆盖 / 未执行（不得外推）

1. QML 运行期视觉/交互（配色观感、动效、点选手感）——主机无 Qt，板端缺 Qt5 Quick/Qml（E-01b）。
2. 三联真实同步播放/取流/解码（媒体层职责，handoff）。
3. 关键帧缩略图真实图像（媒体层生成）。
4. 数据接入（应用未打开数据库；`seed` 仅 `app.simulation=true` 时灌样例）。
5. NVR 可达性/通道映射（环境未提供，显示“不可用”正确）。

---

## 7. 判定

**TEST_FAIL**（范围：ITEM-020 历史回放页实现 vs 任务内嵌 UI 交付清单与 `INTERACTION` §5.3 / `VISUAL` §7.7·§8.3）。

- **失败依据（源码可确证，非运行期视觉不确定性）**：
  1. 任务明确要求的“**缺口斜纹**”缺失（D-02）；
  2. 任务明确要求的“**关键帧缩略图**”实际恒空且非缩略图（桥接未导出 `keyframes`，D-01）；
  3. §5.3/§7.7 要求的“**播放/暂停/倍速**”控件缺失（D-03）；
  4. “报警区段**显著色标**”级别硬编码 L2 且模型不携带级别，与 §3.4 级别语义冲突（D-04）；
  5. 另有多项 §5.3/§7.7/§8.3 偏差（段不可点选、摄像头选择受限、详情录像不可用无黄条、非法日期静默、尺寸/焦点，D-05..D-09）。
- **边界**：本 FAIL **不否定** v1 D-01/D-02 的修复（已确认在位），也**不表示** core 模型/探针有问题（574/574、探针全 PASS）。修复要求见 §5；界面修复交桌面实现能力，Runtime 控制 loop。
- 运行期视觉/交互手感仍留待 Qt 环境落地后评审（§6），但本 FAIL 不依赖运行期——所列缺项均为源码级事实。
