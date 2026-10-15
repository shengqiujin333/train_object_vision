# ITEM-020 证据 — 历史回放页

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-020`
- 依据: `docs/hmi/INTERACTION_DESIGN.md` v2 §5.3（日期/摄像头、时间轴、四个跳转、三联同步、事件详情、NVR 不可用标注）、PLY-004/005/007/008；`docs/hmi/VISUAL_DESIGN.md` v2 §8.3（时间轴+关键帧+播放控制+右侧详情；缺口斜纹；录像不可用黄条）；`docs/hmi/DESKTOP_APP_DESIGN.md` §10（统一墙钟时间轴驱动多路 seek；漂移超阈值显示“同步偏差”）
- 源码: `app/src/core/include/railhmi/core/replay.h`、`app/src/core/src/replay.cpp`、`app/src/core/tools/replay_probe.cpp`、`app/tests/core/test_replay.cpp`、`app/src/ui/src/replay_model.{h,cpp}`、`app/src/ui/qml/pages/P3Replay.qml`、`app/src/ui/qml/main.qml`、`app/src/ui/src/main.cpp`、`app/config/thresholds.yaml`、`app/src/core/{include/railhmi/core/config.h,src/config.cpp}`、`app/tools/board_build.py`

## 1. 交付内容

1. **`railhmi-core` 回放模型**（无 Qt/SQLite）：
   - `build_timeline`：某相机某日的时间轴——连续段 / **报警保护段（Critical，显著色标）** / 缺口 / 报警标记 / 关键帧；段与缺口裁剪到当日窗口，按时间排序。
   - `jump_targets`：四个跳转点 **-30s / 报警时刻 / +30s / +60s**。
   - `PlaybackState` + `seek`（窗口裁剪）+ `jump_to`：统一墙钟游标。
   - `sync_status`：三路（左/前/右）位置的**最大偏差**与阈值判定，超阈值产出“同步偏差 N ms（阈值 M ms）”提示；`front_alarm_sync_group` = {CAM-02 左, CAM-01 前, CAM-03 右}。
   - `replay_source_status`：NVR 可用→NVR；否则回退**本机片段**并给出显式标注“NVR 录像不可用，回放本机片段”；都不可用→“录像不可用”。
   - `build_event_detail`：事件详情（时间/类型/级别/距离/置信度/处理状态/备注）。
2. **UI 桥接** `RailhmiReplayModel`：`timeline/jumpTargets/syncStatus/sourceStatus/formatTime` 委托 core（单一真相）。
3. **QML** `P3Replay.qml`：日期/摄像头/三联同步开关；三联回放画面；时间轴（连续段、报警段显著、缺口“录像缺失”、游标可点选）+ 关键帧条 + 播放控制（四个跳转）；右侧事件详情栏；NVR 不可用来源标注；同步偏差提示。
4. **配置**：`thresholds.replay_sync_threshold_ms`（默认 300）解析并校验。
5. **`railhmi-replay-probe`**（core，无 Qt）。

## 2. 实测（板端 RK3588 本机；TestRun）

`board_build.py --mode TestRun` → `TESTRUN_EXIT=0` `REPLAY_EXIT=0`。核心单测 **566 passed / 0 failed**（含 ITEM-020 新增回放断言）：
```
[PASS] timeline only CAM-01 segments / continuous segment / critical/alarm segment prominent / gap per camera / has_gap
[PASS] two alarm markers / keyframe markers present / day window duration
[PASS] jump -30s / jump alarm moment / jump +30s / jump +60s / seek within window / seek clamps low / seek clamps high / jump_to sets position
[PASS] sync group left/front/right / in sync within threshold / deviation over threshold -> prompt / prompt text
[PASS] nvr available / nvr unavailable -> local clips with explicit note / no source unavailable
[PASS] detail type / detail level / detail distance / detail confidence / detail ack / detail time
```

`railhmi-replay-probe`（板端原文）：
```
type=timeline cam=CAM-01 day=1792108800000 duration_ms=86400000 segments=3 gaps=1 alarms=1 keyframes=1440
seg kind=continuous t0=...+10000 t1=...+80000 label=连续录像
seg kind=critical   t0=...+90000 t1=...+200000 label=报警保护片段
seg kind=continuous t0=...+260000 t1=...+300000 label=连续录像
gap t0=...+210000 t1=...+250000 reason=recording_interrupted
flags has_alarm_segment=true has_gap=true
type=jumps minus30=... alarm=... plus30=... plus60=...
type=sync in_sync=true dev=200 | deviation=true dev=900 prompt=同步偏差 900 ms（阈值 300 ms）
type=source source=local_clips label=本机片段 usable=true note=NVR 录像不可用，回放本机片段
type=detail time=2026-10-16 00:02:00 type=前方轨道人员 level=一级 distance=12.3 m confidence=87% ack=已确认
type=acceptance alarm_segment=true gap=true jumps_ok=true sync_prompt_on_deviation=true nvr_fallback_local=true result=PASS
```

判据核对（用户可观察点）：
- 从报警卡进入后**自动定位报警时刻并锁定相关摄像头**：`jump_targets.alarm = 报警时刻`，`front_alarm_sync_group` 锁定左/前/右，`seek`/`jump_to` 定位到该时刻 ✅
- 时间轴含连续段、报警段（显著色标）、缺口（“录像缺失”）、游标、关键帧 ✅
- 四个跳转 -30s/报警时刻/+30s/+60s ✅
- **三路同步偏差超阈值显示提示**（`dev=900 > 300 → 同步偏差 900 ms`）✅
- NVR 来源不可用时**显式标注并提供本机片段回放**（`source=local_clips note=NVR 录像不可用，回放本机片段`）✅
- 事件详情侧栏字段齐全 ✅

## 2.5 修复轮（D-01 / D-02，software_verification TEST_FAIL 交回）

验证反馈（`VERIFY-ITEM-020.md` v1）：core 层全通过，但 QML 接线层两项击中用户可观察点：
- **D-01 报警卡→回放定位断链**：`main.qml onReplayRequest` 丢弃 `cameraId/tsWallMs`，P3 恒开固定详情/游标。
- **D-02 三联同步提示恒不可触发**：`P3Replay.refreshSync` 喂恒等三值 `[cursor, cursor, cursor]`，提示结构上不可达。

修复内容：
1. **core** 新增 `related_cameras(alarm_camera)`（前方主摄→左/前/右；其余→仅该路）与 `sync_snapshot(locked, cursor, camera_offsets, threshold)`（由**各路真实回放时钟偏移**构建；缺时钟→`available=false`）。
2. **桥接** `RailhmiReplayModel` 新增 `selectAlarm/selectedCameraId/selectedAlarmTs/relatedCameras/selectedDetail/syncState` 与 `seedAlarms/setCameraOffsets`；`main.cpp` 按样例事件灌入报警行并注入三联时钟偏移，启动即 `selectAlarm(CAM-01, t0+5000)`。
3. **QML** `P3Replay.qml`：`reload()` 消费 `selectedCameraId/selectedAlarmTs/relatedCameras/selectedDetail`，`refreshSync()` 改用 `syncState(cursor)`；无时钟显示“同步状态不可用”（`objectName=syncStatusText`），不再出现恒等三值。`main.qml onReplayRequest` 调用 `railhmiReplay.selectAlarm(cameraId, tsWallMs)`。
4. **静态取证工具** `app/tools/check_replay_wiring.py`（板端可跑）：核对 `onReplayRequest` 使用两参数并调用 `selectAlarm`、P3 消费四个选择/同步接口、无恒等三值、含“同步状态不可用”。

修复轮板端实测（`board_build.py --mode TestRun`，stamp `20261016-ITEM020-fix-r1`）→ `TESTRUN_EXIT=0` `REPLAY_WIRE_EXIT=0`：
```
== 574 passed, 0 failed ==   （较修复前 566 新增 related_cameras/sync_snapshot/配置阈值断言）
type=lock alarm_cam=CAM-01 locked=CAM-02,CAM-01,CAM-03
type=sync-snapshot in_sync_available=true dev=200 | deviation_prompt=同步偏差 900 ms（阈值 300 ms） | no_clock=同步状态不可用
type=acceptance ... lock_related_cameras=true sync_unavailable_without_clock=true ... result=PASS
[check_replay_wiring] PASS (D-01 selection wired; D-02 dynamic sync + unavailable state)
```
`sync_prompt_on_deviation=true`：提示现在**可达**（真实偏移 dev=900 > 阈值 300）；无时钟时明确显示“同步状态不可用”，不再谎报在同步。

## 2.6 UI 评审修复轮（D-01..D-09，web_frontend.ui_implementation_review TEST_FAIL 交回）

UI 评审 `REVIEW-ITEM-020.md` 确认 v1 D-01/D-02 已修复，但列出 9 项界面缺项（源码可确证）：

| 偏差 | 修复 |
|---|---|
| D-01 关键帧条恒空/非缩略图 | 桥接 `timeline()` 导出 `keyframes`（`{ts,thumb}`）；`build_timeline` 关键帧带 `thumb` 引用；P3 按 `ts` 定位渲染 64px 缩略图槽 |
| D-02 缺口无斜纹 | P3 缺口用 `Canvas` 绘制斜纹 + “录像缺失” |
| D-03 缺播放/暂停、倍速 | 桥接 `playback()/setPlaying()/setSpeed()` 消费 core `PlaybackState`；P3 增播放/暂停 + 0.5×/1×/2× |
| D-04 报警级别硬编码 | `AlarmMark{ts,level}`；`TimelineSegment.level`/`TimelineMarker.level` 由关联报警推导；桥接导出；P3 按 `Theme.alarm.<lvl>` 上色（tint+3px） |
| D-05 段/缺口/标记不可点选 | P3 段/缺口/标记各有 `MouseArea`：段/缺口 seek，标记 `selectAlarm` |
| D-06 摄像头选择受限 | P3 顶部用完整 `railhmiCameras`（6 路）；`relatedCameras` 仅作进入默认锁定 |
| D-07 详情无录像黄条/备注混用 | `ReplayEventDetail` 新增 `record_label/record_ok`，`note` 独立；P3 `detailRecordBar` 黄条 + “备注”独立字段 |
| D-08 非法日期静默/无键盘可达 | P3 `dateError` 提示（非法日期不静默）；`FilterChip` 高 `touch_target_min=44`、`activeFocusOnTab`、Enter/Space 触发、焦点环 |
| D-09 尺寸/游标时间 | 轨道 `button.sm=32`、关键帧条 `status_strip_height=64`；游标顶部等宽（mono）时间 |

修复轮板端实测（`board_build.py --mode TestRun`，stamp `20261016-ITEM020-uifix-r1`）→ `TESTRUN_EXIT=0` `SCAN_EXIT=0` `REPLAY_WIRE_EXIT=0`：
```
== 579 passed, 0 failed ==   （新增 segment/marker level、keyframe thumb、detail record 断言）
seg kind=critical level=1 ... label=报警保护片段
flags ... alarm_marker_level=1 keyframe_thumb=thumb/CAM-01/1792108800000.bmp
type=detail ... record=可回放 record_ok=true
[check_replay_wiring] PASS (D-01..D-09 + v1 D-01/D-02 wired)
```

## 2.7 UI 评审 v2 修复轮（D-01/D-02/D-03 残留）

v2 复核（`REVIEW-ITEM-020-v2.md`）：D-01/02/03/05/06/07/08/09 已修复；**v2 D-01（上轮 D-04 未闭环）**——桥接 `timeline()` 仍用仅时间戳重载，core 固定填 `AlarmLevel::L3`，真实报警级别从不流经 UI（样例一级报警显示三级黄）。两项轻残留：游标顶部时间为固定左端读数；三联面板标签丢失中文名。

修复：
1. **core** 新增 `alarm_marks_from_rows(rows, camera_id)`（由带真实级别的报警行推导标记）；桥接 `timeline()` 改用 `build_timeline` 的 `AlarmMark` 重载（仅当无行时回退 ts-only）。
2. **P3Replay**：游标顶部时间随游标 x 移动并标注“当前”；三联面板标题恢复 `chipName()`（前方/左侧/右侧）。
3. **静态取证** `check_replay_wiring.py` 新增断言：桥接使用 `alarm_marks_from_rows`、P3 含 `chipName(modelData)` 与游标跟随时间。

修复轮板端实测（stamp `20261016-ITEM020-uifix-r2`）→ `TESTRUN_EXIT=0` `SCAN_EXIT=0` `REPLAY_WIRE_EXIT=0`：
```
== 583 passed, 0 failed ==   （新增 alarm_marks_from_rows + 端到端段/标记级别断言）
type=level-e2e alarm_level=1 segment_level=1 marker_level=1
type=acceptance ... level_e2e=true ... result=PASS
[check_replay_wiring] PASS (D-01..D-09 + v1 D-01/D-02 wired)
```
真实级别 L1 现已端到端流经桥接至 UI（一级红），不再恒为 L3。

## 3. 边界

- **真实回放取流/解码**复用媒体层（GStreamer 播放链，属媒体/视频职责）；本项交付回放**模型/时间轴/同步/来源/详情**与 QML 页面骨架。
- **数据接入**：`RailhmiReplayModel::seed(segments, gaps, alarm_ts, nvr, local)` 已就绪消费来自持久化（`recording_segments`/`recording_gaps`/`events`）与保护片段（ITEM-019）的数据；应用进程尚未打开数据库，默认空数据，仅 `app.simulation=true` 时灌入样例。真实数据接线归后续集成。
- **NVR 回放能力未核验**：`nvr_available` 当前为 false（通道映射/账号/协议未提供，见环境配置），因此界面显示“NVR 录像不可用，回放本机片段”；NVR 可达性探测归设备维护项。
- **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（E-01b）；本项以 core 板端实跑（566/0）+ 探针 PASS + QML 源码/静态扫描 PASS 为证据。

## 4. 证据文件

- `build-TestRun-...-ITEM020-r1.core-evidence_replay-probe-...-r1.log`（时间轴/跳转/同步/来源/详情原文 + PASS）
- `...-ITEM020-r1.core-evidence_core-tests-...-r1.log` / `.xml`（566 passed / 0 failed）
- `...-ITEM020-r1.transcript.json` / `.log`（构建 + 单测 + 探针 + 取回）
- 回归留痕：`alarm-probe` / `clip-probe` / `ai-probe` / `status-probe` / `sim-inject`

## 5. 复现

```sh
# 板端
bash app/build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-replay-probe
# 经登记 SSH 入口
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-020-replay --stamp <tag>
```
