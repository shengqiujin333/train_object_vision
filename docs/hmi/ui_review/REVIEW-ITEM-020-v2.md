# UI 实现评审证据 v2 — ITEM-020（历史回放页 P3，D-01..D-09 修复轮复核）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（修复轮 commit `a718a53`，stamp `20261016-ITEM020-uifix-r1`）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/replay.h`、`app/src/core/src/replay.cpp`、`app/src/ui/src/replay_model.{h,cpp}`、`app/src/ui/qml/pages/P3Replay.qml`、`app/src/ui/qml/components/FilterChip.qml`、`app/src/ui/src/main.cpp`
  - 构建证据: `docs/hmi/test_evidence/ITEM-020-replay/README.md`
- 被评工作项: `ITEM-020`（历史回放页：日期与摄像头选择、时间轴〔连续段、报警区段显著色标、缺口斜纹、游标、关键帧缩略图〕、-30s/报警时刻/+30s/+60s 四个跳转、前方报警默认左/前/右同时间轴同步播放、事件详情侧栏、NVR 不可用显式标注并提供本机片段回放。用户可观察：自动定位并锁定相关摄像头，三路同步偏差超阈值提示）
- 上一轮（本能力）: `REVIEW-ITEM-020.md` **TEST_FAIL**（D-01..D-09）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §5.3；`docs/hmi/VISUAL_DESIGN.md` v2 §7.7、§8.3、§3.4；`docs/hmi/design_tokens.yaml`
- 关联上游验证: `n006 software_tester.software_verification` v3 **TEST_PASS**（`VERIFY-ITEM-020-v3.md`，579/579 + ext 100/100 + 接线扫描 46 项 mismatch=[]）
- 评审日期/环境: 2026-10-16；Windows 主机；**本轮未连接 RK3588 目标板**
- 判定: **TEST_FAIL**（D-01/D-02/D-03/D-05/D-06/D-07/D-08/D-09 已修复；**D-04 仅部分修复——级别色在真实 UI 链路上仍不反映实际报警级别**，源码可确证）

---

## 0. 结论与口径

1. 本 v2 复核上一轮 `REVIEW-ITEM-020.md` 的 9 项缺陷修复状态。**逐项以源码为据独立核对，不采信验证报告的自述结论**。
2. **8 项已修复**（D-01/D-02/D-03/D-05/D-06/D-07/D-08/D-09，见 §3 正向项）。
3. **D-04 仅部分修复**：core 新增 `AlarmMark`/`TimelineSegment.level`，QML 已按 `modelData.level` 上色，但**桥接 `timeline()` 仍用仅时间戳重载**（`std::vector<long long>`），core 的 ts-only 便捷重载固定填 `AlarmLevel::L3` → **所有报警段/标记的级别恒为 L3**，真实级别从不流经桥接。L1 报警（样例 person_on_rail）在时间轴上将显示 L3（三级/黄）色，违反 `VISUAL §3.4/§7.7` 级别色彩语义。
4. 本轮**未连接目标板**；core 板端数据沿用上游 v3。本 capability 授权方法为 `read/ui_evidence/design_assets`。**未修改任何实现代码**。
5. 依赖缺口按职责交实现（桥接建线）/媒体（真实取流）；不返回 BLOCKED。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `a718a53`（uifix-r1） | 修复轮：core 新增 `AlarmMark`/段·标记 `level`/关键帧 `thumb`/`build_event_detail` 备注参数与 `record_label|record_ok`；桥接新增 `playback/setPlaying/setSpeed`；P3 重写（斜纹 Canvas、关键帧缩略图条 64px、播放/倍速、级别色映射、点击目标、完整摄像头、详情黄条、日期错误、44px 焦点）；FilterChip 44px + 键盘 |
| 交互设计 | `ef47841` | v2 §5.3 |
| 视觉设计 | `40ccf4e` | §7.7（轨道 32px、报警段 3px **级别色** + tint、缺口斜纹、游标 2px+顶部时间、关键帧条 64px、播放控制 44px）；§8.3；§3.4 级别色彩 |
| 软件验证 | `68d365c` | v3 TEST_PASS；579/579 + ext 100/100 + 接线扫描 46 项 mismatch=[]（其 D-04 结论仅覆盖 core `AlarmMark` 语义与 QML 映射，未覆盖桥接是否给真实级别） |

---

## 2. 评审方法

1. 逐项复核上轮 D-01..D-09：读 `P3Replay.qml`、`replay_model.{h,cpp}`、`replay.{h,cpp}`、`FilterChip.qml`、`main.cpp`。
2. **桥接→core 调用链核对**（关键）：`RailhmiReplayModel::timeline()` 实参类型 → `build_timeline` 重载选择 → 级别来源。
3. 数据源核对：`main.cpp` 注入的报警数据（是否携带级别）。
4. 令牌核对：`Theme.color.focus_ring`、`Theme.typography.family.mono`、`size.status_strip_height/touch_target_min`、`alarm.L*.accent/tint_bg` 是否可用（避免绑定失败）。
5. 归档证据核对：v3 报告与 ext/probe 口径。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | D-01 关键帧 | 桥接 `timeline()` 现导出 `keyframes`（`ts`+`thumb="thumb/<cam>/<ts>.bmp"`）；P3 `keyframeThumb` 按 `xOf(modelData.ts)` 定位、条高 `size.status_strip_height=64`、`Image source: modelData.thumb`。**已修复**（缩略图图像本身待媒体层，handoff） |
| R2 | D-02 缺口斜纹 | `timelineGap` 内含 `Canvas.onPaint`，以 `degraded` 色 `lineWidth=1` 每 8px 画斜线。**已修复** |
| R3 | D-03 播放/倍速 | `playbackToggle` + `speed-0.5/1/2` chip 调 `R.setPlaying/setSpeed`；桥接 `playback()/setPlaying/setSpeed` + `onPlaybackChanged`。**已修复**（真实播放推进/`pos_ms` 属媒体层） |
| R4 | **D-04 级别色（关键）** | **桥接 `timeline()` 传 `alarm_ts_`（`std::vector<long long>`）→ 命中 ts-only 重载 → `AlarmMark{ts, AlarmLevel::L3}`**；`replay_model.h` 仅存 `std::vector<long long> alarm_ts_`，`seed()` 签名也只收 ts；`main.cpp` 注入 `alarms={t0+5000}` 无级别。→ **所有报警段 `level`、标记 `level` 恒为 3**；QML `levelColorOf(3)` 恒取 L3 色。**未修复**（详见 D-01） |
| R5 | D-05 点击目标 | 段 `MouseArea seekTo(t0)`、缺口 `MouseArea acceptedButtons:LeftButton`、标记覆盖 `Item+MouseArea selectAlarmAt(ts)`。**已修复** |
| R6 | D-06 完整摄像头 | P3 `allCams = railhmiCameras`（`main.cpp` `camList` 6 路，含 `id`/`name`），Repeater `model: allCams`。**已修复** |
| R7 | D-07 详情黄条+独立备注 | `detailRecordBar`（`degraded_bg` 底+`degraded` 边）绑定 `detail.recordLabel/recordOk`；桥接导出 `recordLabel/recordOk/note`；`build_event_detail(..., note)` 备注独立。**已修复**（备注暂无数据源，显示“—”） |
| R8 | D-08 日期错误/44px/键盘 | `dateError` 在 `res.valid===false` 时置“日期无效（YYYY-MM-DD）”并染红边框；`dateInput` 高 `touch_target_min=44`；`FilterChip` 高 44、`activeFocusOnTab`、`focus_ring`、Return/Enter/Space 触发。**已修复** |
| R9 | D-09 尺寸/游标时间 | 轨道 `size.button.sm=32`；关键帧条 64；`cursorTime` 用 `family.mono`。**已修复**（时间读数位于轨道左端而非游标正上方，见 D-03 残留） |
| R10 | 令牌可用性 | `focus_ring`、`family.mono`、`status_strip_height`、`touch_target_min`、`alarm.L*.tint_bg` 均在 `Theme.qml` 存在。无绑定缺失 |
| R11 | 媒体面板标签 | P3 三联面板 `text: modelData + " "`（仅显示 `CAM-0x`），丢失中文名（前方/左侧/右侧）（D-04 残留） |

---

## 4. 判据逐条核对（上轮 D-01..D-09）

| 上轮偏差 | 现状 | 判定 |
|---|---|---|
| D-01 关键帧缩略图条 | 桥接导出 keyframes + 64px + 按 ts 定位 | **已修复** |
| D-02 缺口斜纹 | Canvas 斜线 | **已修复** |
| D-03 播放/暂停/倍速 | 控件 + 桥接接线在位 | **已修复** |
| D-04 报警段/标记级别色 | 桥接恒填 L3（真实级别不流经） | **未修复** |
| D-05 段/缺口/标记可点选 | MouseArea 全在位 | **已修复** |
| D-06 摄像头选择受限 | 使用全量 `railhmiCameras` | **已修复** |
| D-07 详情录像黄条 | `detailRecordBar` + 独立备注 | **已修复** |
| D-08 非法日期静默/32px 无键盘 | 错误提示 + 44px + 焦点/键 | **已修复** |
| D-09 游标顶部时间/尺寸偏离 | 尺寸合规 + 等宽时间 | **已修复**（时间位置见残留） |

---

## 5. 偏差与修复要求

**D-01（严重，上轮 D-04 未闭环）报警段/标记级别色在真实 UI 链路上恒为 L3**
- 观察（源码可确证）：
  - `app/src/ui/src/replay_model.cpp::timeline()` 调用
    `railhmi::core::build_timeline(camera_id, day, segments_, gaps_, alarm_ts_, keyframe_interval_ms)`；
    `alarm_ts_` 类型为 `std::vector<long long>`（`replay_model.h`），只能命中 **仅时间戳重载**。
  - `app/src/core/src/replay.cpp` 的 ts-only 重载：
    `for (long long ts : alarm_ts) marks.push_back(AlarmMark{ts, AlarmLevel::L3});`
    → 所有标记级别恒为 L3(=3)，进而 `TimelineSegment.level = level_in(...) = 3`。
  - `app/src/ui/src/main.cpp` 注入 `std::vector<long long> alarms = {t0 + 5000}`（无级别）；`seed()` 也只收 ts。
  - 结果：P3 `levelColorOf(3)`/`levelTintOf(3)` 恒取 L3（黄 `#E3B341` / `#332A12`）。样例报警 id1 `person_on_rail` 为**一级(L1)**，时间轴保护段与报警标记将显示**三级黄**而非**一级红**（`#FF6B6B`/`#3A1518`），违反 `VISUAL §3.4` 级别色彩语义与 §7.7「报警区段 3px 级别色」。
- 复现路径：
  1. 读 `replay_model.h`（`std::vector<long long> alarm_ts_;` 与 `seed(...)` 签名）；
  2. 读 `replay_model.cpp::timeline()` 的 `build_timeline(..., alarm_ts_, ...)`；
  3. 读 `replay.cpp` ts-only 重载 `AlarmMark{ts, AlarmLevel::L3}`；
  4. 读 `main.cpp` 注入 `alarms={t0+5000}`（无级别）。
- 期望：桥接把**真实级别**传给 `build_timeline` 的 `AlarmMark` 重载——例如 `seed` 改收/新增 `std::vector<AlarmMark>`（或由已灌入的 `alarm_rows_`(带 `level`) 与段窗口关联推导），并在 `timeline()` 使用级别重载；样例注入携带 L1/L2/L3。修复后须有端到端断言（**桥接输出**的段/标记 `level` 等于对应报警级别），而非仅 core `AlarmMark` 单测。

**D-02（轻，残留）游标“顶部时间”位置**
- 观察：§7.7「游标：2px 白线 + 顶部时间(mono)」；实现 `cursorTime` 锚定 `timelineArea.left`，为**固定左端读数**，未随游标 x 移动。
- 期望：时间读数跟随游标 x（或明确为“当前时间”读数并标注），与设计一致。

**D-03（轻，残留/回归）三联面板标签丢失中文名**
- 观察：面板标题 `text: modelData + " "` 仅显示 `CAM-0x`；上轮为 `chipName()`（前方/左侧/右侧）。
- 期望：恢复 `前方/左侧/右侧` 等中文名（或 id+名并列），与 §8.3 示意一致。

**D-04（观察项，非本次失败点）播放状态与游标/媒体未耦合**
- `playback_.pos_ms` 恒 0；`togglePlay/setSpeed` 仅改状态，无播放推进/游标联动（真实播放属媒体层）。登记为媒体/集成交接，不改变本次判定。

---

## 6. 未覆盖 / 未执行

1. QML 运行期视觉/交互（主机无 Qt；板端缺 Qt5 Quick/Qml，E-01b）。
2. 三联真实同步播放/取流/解码、关键帧缩略图真实图像（媒体层）。
3. 数据接入（应用未打开数据库；样例注入）。
4. NVR 可达性/通道映射（未提供，界面显示“不可用”正确）。

---

## 7. 判定

**TEST_FAIL**（范围：ITEM-020 历史回放页 vs 任务内嵌 UI 交付清单与 `INTERACTION §5.3` / `VISUAL §7.7·§8.3·§3.4`）。

- **失败依据（源码可确证）**：上轮 D-04 仅部分修复——core 的 `AlarmMark`/`level` 与 QML 级别映射到位，但**桥接 `timeline()` 使用仅时间戳重载，core 固定填 `AlarmLevel::L3`**，真实报警级别从不流经 UI 链路；样例一级报警在时间轴上显示三级色（§3.4 级别色彩语义被破坏）。上游 v3 的 D-04 结论只验证了 core `AlarmMark` 语义与 QML 映射（probe 直接构造 `AlarmMark`），**未覆盖桥接是否传递真实级别**，故该结论不能证明 UI 级别色正确。
- **已确认修复**：D-01 关键帧缩略图条、D-02 缺口斜纹、D-03 播放/倍速、D-05 点击目标、D-06 完整摄像头选择、D-07 详情录像黄条+独立备注、D-08 日期错误路径/44px/键盘、D-09 尺寸与等宽时间。修复要求见 §5；界面/桥接修复交桌面实现能力。
- 运行期视觉/交互手感仍留待 Qt 环境（§6），但本 FAIL 不依赖运行期——D-01 为源码级事实。
