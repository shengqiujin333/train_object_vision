# ITEM-020 验证报告 v2 — 历史回放页（software_verification 独立验证，D-01/D-02 修复轮复核）

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-020`
- 被测版本: commit `b09fe30`（`[capability:embedded_linux_engineer.desktop_app_implementation] … returned IMPLEMENTED`，D-01/D-02 修复轮交付；上述 commit 本身只记录交付、未改动交付代码）
- 结论: **TEST_PASS**
- 与 v1 的关系: v1（`VERIFY-ITEM-020.md`，VERIFY-20261016-v1）确认两项 QML 接线缺陷 D-01（报警卡→回放定位断链）/ D-02（三联同步提示恒不可触发）并交回实现；本 v2 轮独立复核**修复状态**，所有 v1 判据保留为回归，不在未重测基础上外推。
- 验证方式: 经登记 Paramiko SSH 入口（192.168.0.103，指纹核验 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`）独立复跑交付入口 `board_build.py --mode TestRun` + 验证方独立断言 + 验证方扩展夹具 v2（新增 `related_cameras`/`sync_snapshot` 边界）+ 板端 QML/C++ 接线扫描 v2（反向断言修复接线）；主机（Windows）不做板端替代结论。

## 1. 结论要点

**全绿：core 574/574（含 7 条修复轮新断言）、探针独立断言 fails=[]、ext 84/84、接线扫描确认 D-01/D-02 修复在位，两项用户可观察点恢复可达。**

- **D-01 已修复**：`main.qml onReplayRequest` 现在使用 `cameraId`/`tsWallMs` 并调用 `railhmiReplay.selectAlarm`；P3Replay 消费 `selectedCameraId/selectedAlarmTs/relatedCameras/selectedDetail`；core 新增 `related_cameras()`（前方主摄 → 左/前/右三联，其余 → 单路），bridge 灌报警行 + 启动 `selectAlarm("CAM-01", t0+5000)` + 注入各路时钟偏移，详情按选中报警动态生成。板端源码级扫描全部命中（见 §3.3）。
- **D-02 已修复**：`[cursor, cursor, cursor]` 恒等三值字面量已不存在；`refreshSync()` 改用 `R.syncState(cursor)`（由各路真实回放时钟偏移 → `sync_snapshot` 构建）；无时钟时显示「同步状态不可用」而非谎报在同步；偏移样例 900 ms > 阈值 300 ms 时提示「同步偏差 900 ms（阈值 300 ms）」「结构上不可达」变为**可达**。
- 修复轮官方接线自检 `check_replay_wiring.py`（板端、随交付 TestRun 的 ui-scan 步骤回放）本轮 fresh 输出 `[check_replay_wiring] PASS (D-01 selection wired; D-02 dynamic sync + unavailable state)`，`REPLAY_WIRE_EXIT=0`（交付 transcript 在档）。

## 2. 验证范围与判据来源

任务描述（Runtime current task）用户可观察点同 v1：日期与摄像头选择、时间轴（连续段、报警区段显著色标、缺口斜纹、游标、关键帧缩略图）、-30s/报警时刻/+30s/+60s 四个跳转、前方报警默认左/前/右同时间轴同步播放、事件详情侧栏（时间/类型/级别/距离/置信度/处理状态/备注）；NVR 录像来源不可用时显式标注并提供本机片段回放；**从报警卡进入后自动定位报警时刻并锁定相关摄像头；三路同步偏差超出配置阈值时显示提示**。

判据来源不新增：`docs/hmi/INTERACTION_DESIGN.md` v2 §5.3、`DESKTOP_APP_DESIGN.md` §10、TEST_DESIGN TC-PLY-01/02/05。本轮 **未新增/未放宽任何验收判据**——仅把 v1 缺陷恢复为判据内要求后独立重测。

## 3. 独立板端复跑（轮 VERIFY-20261016-v2）

### 3.1 交付入口 TestRun 复跑（全部 exit=0）

`verify_replay20_fixtures.py` 以子进程方式重跑 `board_build.py --mode TestRun --keep-remote`（上传 app/ → build.sh CoreTests → 574 单测 → tokens selftest → ui scan + 交付 check_replay_wiring → sim-inject → status/ai/alarm/clip/replay 探针 → 归档取回）：

- 交付入口 `TESTRUN_EXIT=0`，fresh 归档齐全（core-tests log/xml + replat-probe + sim-inject log/json + status×2 + ai/alarm/clip/replay probes）。
- 核心单测 **574/574**：fresh log 的 [PASS] 行序列与实现者**修复轮**归档 log（`…-ITEM020-fix-r1`）逐行相同（`identical=True`，`verify_fails=0`，行数 574=574 显式校验）。
- JUnit XML 独立解析（验证方正则，不采信交付自述）：`tests=574 failures=0`；ITEM-020 判例逐条比对 **36/36 在位**（29 基线 + 7 修复轮新增：front alarm locks left/front/right、non-front alarm locks only its camera、empty camera -> no lock、no clocks -> sync unavailable、missing a locked camera clock -> unavailable、all clocks within threshold -> in sync、deviation over threshold -> prompt (reachable)；含 `-&gt;` 转义还原后逐字匹配）。
- `railhmi-replay-probe` fresh 输出与实现者修复轮归档探针日志逐行 byte-identical（`identical=True`），同机二次运行 `diff` → `IDENTICAL`（确定性）。
- 交付 ui-scan fresh 输出（交付 transcript `…-VERIFY-20261016-v2.transcript.json` 在档）：`SCAN_EXIT=0`、`REPLAY_WIRE_EXIT=0`（修复轮取证工具 PASS）。

### 3.2 探针独立断言（验证方正则逐行解析 v2 探针行序，不采信探针自述 acceptance 行）

fresh 探针（板端机内 `…VERIFY-20261016-v2.core-evidence_replay-probe-…v2.log`）逐行断言 **fails=[]**：

- 时间轴 3 段（continuous/critical/continuous）+1 缺口（[d+210s, d+250s] reason=recording_interrupted）+1 报警 +1440 关键帧；报警保护片段 [d+90s, d+200s] 标签「报警保护片段」。
- 四跳相对报警时刻 −30/+0/+30/+60 逐毫秒；seek 定位报警时刻（pos=alarm）；jump plus30=+30s。
- sync 双侧：dev=200 in_sync=true；dev=900 提示全文「同步偏差 900 ms（阈值 300 ms）」（1900/30 行 deviation 语义=存在偏差，与源码核对后断言，不按变量名反转）。
- **D-01 修复轮新行** `type=lock alarm_cam=CAM-01 locked=CAM-02,CAM-01,CAM-03` —— 精确三联顺序。
- **D-02 修复轮新行** `type=sync-snapshot in_sync_available=true dev=200 | deviation_prompt=同步偏差 900 ms（阈值 300 ms） | no_clock=同步状态不可用` —— 真实偏移驱动（120/200 阈内）、超阈提示可达、无时钟显式不可用。
- 来源回退 `source=local_clips note=NVR 录像不可用，回放本机片段`；详情 6 字段（前方轨道人员/一级/12.3 m/87%/已确认）；acceptance 行全 true + PASS。
- 解析器对实现者修复轮归档日志离线自检 FAILS=[] 后再上板（`offline_selfcheck.py` 在档）；三处 v2 自查修正：jump 分支匹配须按 `type=jump plus30 ` 前缀（否则先命中裸 type= 分支成 `jump`）、`locked` 组用 `\S+`（`\w` 不匹配连字符）。

### 3.3 板端 QML/C++ 接线扫描 v2（fresh 副本，断言修复状态而非缺陷状态）

对上传工程 fresh 副本 `src/ui/qml/{main.qml, pages/P3Replay.qml, pages/P2AlarmCenter.qml}` + `src/ui/src/main.cpp` 板端 python3 正则扫描（verify_scan.py 在档）：

- **D-01 修复取证（期望全 True）**：P2 `signal replayRequested(string cameraId, double tsWallMs)` 声明在位=True、行内发出在位=True；`onReplayRequest` 函数体使用 cameraId=True、使用 tsWallMs=True、调用 selectAlarm=True；`main.cpp` 启动 `selectAlarm("CAM-01",…)`=True、`seedAlarms`=True、`setCameraOffsets`=True；P3 消费 `selectedCameraId/selectedAlarmTs/relatedCameras/selectedDetail` 全部=True。
- **D-02 修复取证**：`[cursor, cursor, cursor]` 字面量在位=False（已移除）、`syncState` 消费=True、「同步状态不可用」状态在位=True。
- **v1 在位项保留(all True)**：`timelineTrack/timelineCursor/alarmSegment/timelineGap/alarmMarker/keyframeStrip` objectName、四跳 chip（-30s/报警时刻/+30s/+60s）、详情栏 7 字段、`sourceStatus` 来源标注引用。
- 汇总 `mismatch=[]`，SCAN2_EXIT=0。

### 3.4 验证方扩展夹具 v2（verify_replay20_ext.cpp；链本轮 fresh librailhmi_core.a + librailhmi_platform.a，板端 g++，本机 g++ 无 aarch64 目标）

**84/84 通过（EXT_EXIT=0）**：v1 全部 63 检查保留为回归（日期解析/格式化、时间轴跨日裁剪/排序/过滤、四跳/seek 边界、sync_status 边界、来源四组合、详情哨兵/舍入/标签链、阈值配置删除键报错/负值拒绝）+ 新增 21 检查：

- **related_cameras（D-01 核心）**：前方 = CAM-02,CAM-01,CAM-03 恰三联且与 `front_alarm_sync_group()` 同序；CAM-02/CAM-04/CAM-XX 非前方来源各锁单路（未知 id 不空不崩溃）；空串 → 空（不锁定）。
- **sync_snapshot（D-02 核心）**：空偏移/空锁定集/缺某路偏移/单路锁定缺己偏移 → `available=false` 且罚分提示恰为「同步状态不可用」（不谎报在同步）；位置=cursor+offset（偏移差与绝对 cursor 无关，双值核验）；阈值 300 含边界（dev=300 在同步、301 出提示且全文逐字符）；dev=900 全文恰为探针文本；单路快照 dev=0 在同步。
- **详情动态化（D-01 消费端）**：车顶报警行（CAM-04）详情与前报警行（CAM-01）逐字段不同（受电弓异常/二级/—/91%/未确认 vs 前方轨道人员/一级/12.3 m/87%/已确认）——详情确由选中报警行驱动，非静态单例；row 携带 camera_id/ts_wall_ms/time_text(camera_name) 供选择消费链。
- **阈值→快照链**：配置解析 300 直接驱动快照含边界语义（含 in/out 两侧）。

### 3.5 回归留痕（同一轮 fresh 归档在档）

sim-inject（log+json）、status-probe（maintainer+driver）、ai-probe、alarm-probe、clip-probe 全部归档且各自 EXIT=0。

### 3.6 负例 / 确定性

`--nope` → 非零非静默（stderr `unknown arg: --nope`）、`--help` exit=0、无参重跑 exit=0 且归档 diff IDENTICAL。

## 4. 结论与边界

- **判据内通过**：时间轴构成/四跳/游标/同步偏差模型（含阈值含边界，且现由各路真实偏移经 `sync_snapshot` 驱动，提示可达）、NVR 不可用显式标注回退本机片段、详情字段、配置阈值解析/校验、**报警卡进入自动定位并锁定相关摄像头（D-01 已修复）**、**无时钟明确「同步状态不可用」而非恒真在同步（D-02 已修复）**。
- **真实回放取流/解码**复用媒体层（GStreamer，属媒体/视频职责）；本项交付回放模型/时间轴/同步/来源/详情与 QML 页面骨架，同实现者边界声明。
- **数据接入**：应用进程尚未打开数据库；模拟开时灌样例（报警行 + 时钟偏移 + 段/缺口），非模拟时空数据；真实持久化接线归后续集成。时钟偏移样例 0/120/900 ms 由桥接注入，真实媒体源接入前即以「同步状态不可用」呈现，不谎报。
- **NVR 回放能力未核验**：`nvr_available=false`（通道映射/账号/协议未提供，见环境配置）；界面显示「NVR 录像不可用，回放本机片段」；NVR 可达性探测归设备维护项，不外推。
- **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（E-01b 沿 v1）；本项以 core 板端实跑（574/0）+ 探针 PASS + **QML 源码级接线扫描（v2 反向断言）** 为证据。D-01/D-02 修复在源码级确证（接线事实是源码级事实，运行期只会以同样形式呈现）；QML 运行时行为（视觉/交互手感）待 UI 环境落地后走 V-01。
- **主观画面内容与视觉体验由用户确认**；本报告不含人工视觉结论，也不冒充实机界面截图证据。

## 5. 过程轮次（v2 即首效轮）

- v2：全部业务步骤 exit=0（574/574 逐行一致 + JUnit 36/36 + 探针独立断言 fails=[] + 确定性 IDENTICAL + negatives + ext 84/84 + 接线扫描 mismatch=[] + 回归留痕在档 + cleanup_ok=True）→ **TEST_PASS**。验证侧自查（上板前离线 dry-run 对修复轮归档日志）：jump 分支须在裸 type= 前检查 `type=jump plus30 ` 前缀、`locked` 分组改 `\S+`（`\w` 不含连字符）、夹具 EventRow 聚合初始化按 persistence_model.h 15 字段补齐——三处均为验证侧期望构造，无交付侧改动。

## 6. 证据文件（本轮）

- 主记录: `VERIFY-FIXTURES-VERIFY-20261016-v2.log` / `.transcript.json`
- 板端 fresh: `build-TestRun-192_168_0_103-VERIFY-20261016-v2.core-evidence_core-tests-VERIFY-20261016-v2.{log,xml}`、`.replay-probe-…v2.log`、`.ai-probe-…v2.log`、`.alarm-probe-…v2.log`、`.clip-probe-…v2.log`、`.sim-inject-…v2.{log,json}`、`.status-probe-…v2.log`、`.status-probe-driver-…v2.log`、`build-TestRun-…-VERIFY-20261016-v2.{log,transcript.json}`
- 复现入口: `run_verify_round.sh`（默认 stamp v2）、`verify_replay20_fixtures.py`（v2）、`verify_replay20_ext.cpp`（v2，84 检查）
- 离线自查: `offline_selfcheck.py`（解析器/期望 vs 实现者修复轮归档，先于上板运行）
- 过程轮: 无（v2 为首效轮；v1 证据见 `VERIFY-ITEM-020.md` 及 `…-VERIFY-20261016-v1` 归档）
