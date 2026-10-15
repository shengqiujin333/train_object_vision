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
