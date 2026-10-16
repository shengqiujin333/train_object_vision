# UI 实现评审证据 v3 — ITEM-020（历史回放页 P3，v2 缺陷修复轮复核）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（修复轮 commit `cbd0a93`，stamp `20261016-ITEM020-uifix-r2`）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/replay.h`、`app/src/core/src/replay.cpp`（`alarm_marks_from_rows`）、`app/src/ui/src/replay_model.cpp`（桥接用真实级别）、`app/src/ui/qml/pages/P3Replay.qml`（游标时间随动 + 面板中文名）、`app/tools/check_replay_wiring.py`
  - 构建证据: `docs/hmi/test_evidence/ITEM-020-replay/README.md`
- 被评工作项: `ITEM-020`（历史回放页；用户可观察：从报警卡进入自动定位并锁定相关摄像头、三路同步偏差超阈值提示）
- 本能力历史: `REVIEW-ITEM-020.md` **TEST_FAIL**（D-01..D-09）→ `REVIEW-ITEM-020-v2.md` **TEST_FAIL**（v2 D-01 级别色未流经桥接；D-02 游标时间；D-03 面板中文名）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §5.3；`docs/hmi/VISUAL_DESIGN.md` v2 §7.7、§8.3、§3.4；`docs/hmi/design_tokens.yaml`
- 关联上游验证: `n006 software_tester.software_verification` v4 **TEST_PASS**（`VERIFY-ITEM-020-v4.md`，583/583 + ext 115/115 + 接线扫描 59 项 mismatch=[]）
- 评审日期/环境: 2026-10-16；Windows 主机；**本轮未连接 RK3588 目标板**
- 判定: **TEST_PASS**（v2 三项缺陷已闭环；1..9 全部界面交付项在源码级满足；无运行期视觉结论）

---

## 0. 结论与口径

1. 本 v3 复核 `REVIEW-ITEM-020-v2.md` 三项缺陷，**逐项以源码为据独立核对，不采信验证报告自述**。
2. 三项**全部闭环**（§3），且 v1 轮 D-01..D-09 保持修复状态。
3. 本 capability 授权方法为 `read/ui_evidence/design_assets`；主机无 Qt、板端缺 Qt5 Quick/Qml（E-01b），故**不做运行期视觉/交互打磨结论**；真实取流/解码/关键帧图像与 NVR 可达性属媒体/设备职责（handoff）。
4. **未修改任何实现代码**；只写本评审证据并维护清单。
5. 无新增判定阻断项；残存仅属媒体 handoff 与样例数据观察项（§5）。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `cbd0a93`（uifix-r2） | core 新增 `alarm_marks_from_rows`；桥接 `timeline()` 改用 `AlarmMark` 重载（无行时回退 ts-only L3）；P3 `cursorTime.x` 随游标并标「当前」；三联面板标题恢复 `chipName(id)+" (id)"`；probe 增 `type=level-e2e`；core-tests 583 |
| UI 评审 v2 | `d6851ff` | 抓出级别色未流经桥接（恒 L3）等 3 项 |
| 交互/视觉设计 | `ef47841` / `40ccf4e` | §5.3 / §7.7·§8.3·§3.4 |
| 软件验证 | `cb9881a` | v4 TEST_PASS（含桥接级真实级别端到端） |

---

## 2. 评审方法

1. 复核 v2 D-01：核对桥接→core 调用链（实参类型→重载→级别来源）与数据源级别。
2. 复核 v2 D-02：核对 `cursorTime` 的 x 绑定与标注。
3. 复核 v2 D-03：核对三联面板标题。
4. 复核 v1 D-01..D-09 是否保持（关键帧缩略图条/缺口斜纹/播放倍速/点击目标/全量摄像头/详情黄条/日期错误/尺寸）。
5. 归档证据核对：v4 报告与 probe/ext/scan 口径；独立识别其是否覆盖桥接传级别（v3 盲区）。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | **v2 D-01 级别端到端（关键）** | `replay_model.cpp::timeline()`：`std::vector<AlarmMark> marks = railhmi::core::alarm_marks_from_rows(alarm_rows_, camera_id.toStdString());` 并以 `build_timeline(..., marks, ...)` 调用 **AlarmMark 重载**；仅当 `marks.empty()` 才回退 `alarm_ts_`+L3。`replay.cpp::alarm_marks_from_rows` 逐行取 `AlarmMark{r.ts_wall_ms, r.level}`（按 `camera_id` 过滤）。样例 `main.cpp` 注入 CAM-01 行 `mkrow(1, 1, …)`=**L1**。→ CAM-01 时间轴保护段/标记 level=1（一级红）。**已修复** |
| R2 | 重载选择核对 | `build_timeline(camera_id, day, segments_, gaps_, marks, keyframe_interval_ms)` 中 `marks` 为 `std::vector<AlarmMark>` → 命中 AlarmMark 重载（非 ts-only）。无隐式降级 |
| R3 | **v2 D-02 游标时间随动** | `cursorTime.x = Math.max(0, Math.min(parent.width - width, page.xOf(page.cursor) - width/2))`；`text: "当前 " + R.formatTime(cursor)`；`font.family: family.mono`；不再 `anchors.left: parent.left`。**已修复** |
| R4 | **v2 D-03 面板中文名** | 面板标题 `text: page.chipName(modelData) + " (" + modelData + ")"`（前方/左侧/右侧 + id）。**已修复** |
| R5 | v1 D-01 关键帧 | 桥接导出 `keyframes`（ts+`thumb`），P3 `keyframeThumb` 按 ts 定位、条高 64。保持 |
| R6 | v1 D-02 缺口斜纹 | `timelineGap` Canvas 斜线。保持 |
| R7 | v1 D-03 播放/倍速 | `playbackToggle` + `speed-0.5/1/2` + 桥接 `playback/setPlaying/setSpeed`。保持 |
| R8 | v1 D-05/06/07/08/09 | 段/缺口/标记可点选；`allCams=railhmiCameras`；详情 `detailRecordBar`+独立备注；`dateError`+44px+键盘；轨道 32px/关键帧 64px。保持 |

---

## 4. 判据逐条核对

| 项 | 现状 | 判定 |
|---|---|---|
| v2 D-01 级别色流经桥接 | `alarm_marks_from_rows` + AlarmMark 重载（回退仅无行时） | **已修复** |
| v2 D-02 游标顶部时间 | 随游标 x + 「当前」+ mono | **已修复** |
| v2 D-03 面板中文名 | `chipName(id)+" (id)"` | **已修复** |
| v1 D-01..D-09 | 保持修复（R5..R8） | **保持** |
| Item 用户可观察点 1（自动定位+锁定相关摄像头） | `onReplayRequest`→`selectAlarm`；`related_cameras` 三联 | 满足 |
| Item 用户可观察点 2（同步偏差超阈值提示） | `syncState`/`sync_snapshot`；无时钟「同步状态不可用」 | 满足 |

---

## 5. 残存与非阻断项（handoff / 观察）

1. **[媒体职责] 真实回放取流/解码、关键帧缩略图真实图像**：`KeyframeThumb.Image.source="thumb/<cam>/<ts>.bmp"` 与三联画面待媒体层产出；本项只保证引用与布局契约（handoff）。
2. **[设备职责] NVR 可达性/通道映射**：`nvr_available=false`，界面显式「NVR 录像不可用，回放本机片段」——未提供即不虚构（handoff）。
3. **[观察项，非缺陷] 无报警行相机的回退标记级别**：`alarm_marks_from_rows` 对无行的相机（如样例 CAM-02/03）返回空 → 回退 `alarm_ts_` 且固定 L3；三联同步时这两路的同一事件标记显示 L3 色（CAM-01 路为 L1）。属样例数据插桩细节，建议后续对回退标记沿用“主报警级别”或同样由事件行推导，以免三联内同一事件级别不一致。不改本次判定。
4. **[媒体层] `PlaybackState.pos_ms` 未与游标/播放推进耦合**：`play/pause/speed` 仅改状态；真实播放推进属媒体层。

---

## 6. 未覆盖 / 未执行

1. QML 运行期视觉/交互手感（无 Qt 环境）。
2. 真实媒体播放/缩略图/NVR 回放（媒体/设备职责）。
3. 真实持久化数据接入（应用未打开数据库，样例注入）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-020 历史回放页实现与静态接线 vs 任务内嵌 UI 交付清单与 `INTERACTION §5.3` / `VISUAL §7.7·§8.3·§3.4`）。

- **依据**：v2 三项缺陷（D-01 级别色未流经桥接 → 现由 `alarm_marks_from_rows`+AlarmMark 重载传递真实级别；D-02 游标顶部时间随动并标「当前」；D-03 三联面板恢复中文名）在源码级闭环；v1 的 D-01..D-09 保持修复。用户可观察点（自动定位+锁定相关摄像头、同步偏差提示）成立。
- **独立判断**：v3 验证盲区（桥接是否传真实级别）已在本轮针对性核对并确认闭环（R1/R2），非采信验证自述。
- **边界**：本 PASS **不表示**运行期视觉/交互手感通过（无 Qt 环境），**不表示**真实媒体播放/缩略图/NVR 回放可用（媒体/设备 handoff，§5）。界面修复与后续可走 Qt 环境下的 V-01 运行期复评。
