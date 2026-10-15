# ITEM-020 验证报告 — 历史回放页（software_verification 独立验证）

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-020`
- 被测版本: commit `56c8498`（`[capability:web_frontend.ui_implementation_review] … returned TEST_PASS` 之后的工作树；实现由 `1ff7549` `[capability:embedded_linux_engineer.desktop_app_implementation] … returned IMPLEMENTED` 交付）
- 结论: **TEST_FAIL**
- 验证方式: 经登记 Paramiko SSH 入口（192.168.0.103，指纹核验 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`）独立复跑交付入口 `board_build.py --mode TestRun` + 验证方独立断言 + 验证方扩展夹具；主机（Windows）不做板端替代结论。

## 1. 结论要点

**core 模型层全部通过；QML 接线层存在两项已确认缺陷（D-01、D-02），直接击中本工作项用户可观察点，因此整单 TEST_FAIL 交回实现。**

用户可观察点中「从报警卡进入后自动定位报警时刻并锁定相关摄像头」与「三路同步偏差超出配置阈值时显示提示」在当前交付中不可达：

- **D-01 报警卡→回放定位断链**：`P2AlarmCenter.qml:24` 声明 `signal replayRequested(string cameraId, double tsWallMs)`、`P2AlarmCenter.qml:220` 行内发出 `replayRequested(modelData.cameraId, modelData.tsWallMs)`，但唯一接收方 `main.qml:39-42` `function onReplayRequest(cameraId, tsWallMs) { shell.currentPageId = "replay"; shell.denied = !shell.allowed("replay") }` **把两个参数全部丢弃**——既不在页面间传递，也不写入 `railhmiReplayDetail`（`railhmiReplayDetail` 是 `main.cpp` 启动时灌的**固定第一条模拟事件**，静态、每次相同）。结果：从任意报警卡点击「查看录像」，P3 均打开同一详情、同一游标位置、同一摄像头（CAM-01），不随被点报警变化。
- **D-02 三联同步提示恒不可触发**：`P3Replay.qml:37-41` `refreshSync()` 给 `R.syncStatus([cursor, cursor, cursor], R.syncThreshold())` 喂**恒等三值**；core `sync_status` 对空差集合必返回 `in_sync=true`。真实三路回放时钟并不存在（媒体层未接），而 UI 桥接也未给验证提供任何注入真实偏移的通道，因此「同步偏差 N ms」提示在本页**结构上不可能出现**（core 层判据本身正确，ext 夹具全部通过）。

## 2. 验证范围与判据来源

任务描述（Runtime current task）用户可观察点：日期与摄像头选择、时间轴（连续段、报警区段显著色标、缺口斜纹、游标、关键帧缩略图）、-30s/报警时刻/+30s/+60s 四个跳转、前方报警默认左/前/右同时间轴同步播放、事件详情侧栏（时间/类型/级别/距离/置信度/处理状态/备注）；NVR 录像来源不可用时显式标注并提供本机片段回放；**从报警卡进入后自动定位报警时刻并锁定相关摄像头；三路同步偏差超出配置阈值时显示提示**。

判据来源不新增：`docs/hmi/INTERACTION_DESIGN.md` v2 §5.3（自动锁定相关摄像头 PLY-005、四跳 PLY-004、缺口显式、详情字段）、`DESKTOP_APP_DESIGN.md` §10（统一墙钟时间轴、漂移超阈值提示）、TEST_DESIGN TC-PLY-01/02/05。

## 3. 独立板端复跑（轮 VERIFY-20261016-v1）

### 3.1 交付入口 TestRun 复跑（全部 exit=0）

`verify_replay20_fixtures.py` 以子进程方式重跑 `board_build.py --mode TestRun --keep-remote`（上传 app/ → build.sh CoreTests → 566 单测 → tokens selftest → ui scan → sim-inject → status/ai/alarm/clip/replay 探针 → 归档取回）：

- 核心单测 **566/566（实现者 r1 归档 log 的 [PASS] 序列与本轮 fresh log 逐行相同，identical=True，verify_fails=0）**；JUnit XML 独立解析 `tests=566 failures=0`，ITEM-020 判例名逐条比对 29/29 全部在位（含转义还原后「deviation over threshold -> prompt」）。
- `railhmi-replay-probe` fresh 输出与实现者归档 r1 探针日志**逐行 byte-identical**；同机二次运行 `diff` IDENTICAL（确定性）；`--help` exit=0、未知参数 `--nope` exit=2（非静默）。
- 探针独立断言（验证方正则逐行解析，不采信探针自述 acceptance 行）：时间轴组合 3 段（continuous/critical/continuous）+1 缺口 +1 报警 +1440 关键帧、报警保护片段 [d+90s, d+200s] 标签「报警保护片段」、缺口 [d+210s, d+250s] reason、四跳相对报警时刻 ±30/+30/+60 偏移逐毫秒、seek 定位报警时刻、jump plus30=+30s、sync 双侧（dev=200 in_sync / dev=900 提示全文「同步偏差 900 ms（阈值 300 ms）」）、来源回退 local_clips 注「NVR 录像不可用，回放本机片段」、详情 6 字段——**fails=[]**。

### 3.2 验证方扩展夹具（verify_replay20_ext.cpp，链本轮 fresh librailhmi_core.a + librailhmi_platform.a，板端 g++）

**63/63 通过（EXT_EXIT=0）**，覆盖交付测试/探针之外的可证伪边界：

- 日期解析/格式化：`2026-10-16`=1792108800000 与独立期望常量一致；次日 +86400000；长短/非数字/月 00-13/日 00/32 拒绝；`ts<=0`→「—」；`day-1`→「2026-10-15 23:59:59」
- 时间轴裁剪/排序/过滤：跨日首段裁到 day_start、跨日末段（Critical）裁到 day_end、双侧窗外与零长段排除、他相机排除、乱序输入排序输出；报警标记 ts==day_start 保留、ts==day_end 丢弃（半开窗）；关键帧间隔==日长 → 恰 1 个在 day_start；间隔 0/负 → 无
- 跳转/游标：四跳偏移逐毫秒；seek 在精确 day_end 允许（<= clamp）、越界双侧 clamp；`jump_to` 不 clamp（按实现登记契约）
- 同步边界：空列表 dev=0 默认在同步；单点 dev=0；阈值 0 与 dev=0 → in_sync、dev=1 → 提示；**dev==阈值含边界 in_sync、dev==阈值+1 提示**；三值 hi-lo 跨度提示全文逐字符（含全角括号）
- 来源：nvr-only 空注、NVR 优先、本机回退注全文、「都不可用」不可用、`to_string` 三值
- 详情：L2/L3 标签、未确认/已确认、`%.1f m` 舍入（3.14159→3.1）、置信度半入舍入（0.8749→87%、0.875→88%、0→0%）、距离/置信度负哨兵→「—」、note==录像可用性标签链（可回放/录像不完整/录像不可用）
- **配置接线**（交付 core-tests 无 replay_sync 断言）：基线 `thresholds.replay_sync_threshold_ms=300` 解析成功；删除该键 → 报错路径恰为 `thresholds.replay_sync_threshold_ms`（必填不静默）；负值 →「must be >= 0」拒绝；配置阈值 300 驱动 `sync_status` 含边界语义核对

### 3.3 QML 静态接线扫描（板端 fresh 副本；D-01/D-02 取证）

对上传工程 `src/ui/qml/{main.qml, pages/P3Replay.qml, pages/P2AlarmCenter.qml}`（fresh 副本，栈上验证）逐项：

- **W1（D-01 取证）**：`signal replayRequested(string cameraId, double tsWallMs)` 存在=True、行内发出存在=True、`onReplayRequest` 函数体内使用 cameraId=False、使用 tsWallMs=False（正则提取函数体，板端 python3 实测）
- **W2（D-02 取证）**：`[cursor, cursor, cursor]` 字面量喂 syncStatus=True（真实三路偏移无注入通道）
- **W3（铸件在位确认）**：`timelineTrack/timelineCursor/alarmSegment/timelineGap/alarmMarker/keyframeStrip` objectName 齐、四跳 chip（-30s/报警时刻/+30s/+60s）齐、详情栏 7 字段（时间/类型/级别/距离/置信度/处理状态/备注）齐、`sourceStatus` 来源标注引用在位——全部 True
- 日期切换 TextInput、单路/三联切换数据面在位（数据面为 Reload/Repeater 技术合法形式）

### 3.4 回归留痕（同一轮 fresh 归档在档）

sim-inject（log+json）、status-probe（maintainer+driver）、ai-probe、alarm-probe、clip-probe 全部归档且 exit=0。

## 4. 失败证据（按契约给出）

- **当前版本**：工作树 @ 56c8498 之后（实现 1ff7549 交付；review 56c8498 后未改）
- **复现输入**：板端 `python3 verify_scan.py`（fresh 上传副本）输出 W1/W2 值；任选 P2 列表任意两行不同 camera/时刻的报警各点「查看录像」
- **实际观察**：P3 打开后 `railhmiReplayDetail` 恒为 main.cpp:300-313 灌入的 CAM-01 id=1 (t0=1760000000000+120000) 详情；游标 `cursor` 仅在 `<=0` 时初始化为该常量事件时刻（P3Replay.qml:34）；`syncInfo.hasPrompt` 恒为 false（三值恒等）
- **预期**：进入回放页直接定位被点报警的时刻与摄像头（交互 §5.3「从报警详情进入时自动锁定相关摄像头」）且三路延迟差>300ms 时出现提示
- **建议修复边界**（实现侧；验证不判断可修复性）：① `main.qml` 的 `onReplayRequest` 把 cameraId/tsWallMs 传给 P3Replay（属性或 `railhmiReplay.selectAlarm(cameraId, ts)` 桥接方法），P3 `reload()`/游标/`syncGroup` 过滤消费之；② `railhmiReplayDetail` 改为按选中报警动态生成（alarm_model 已有 rowToVariant 字段可复用）；③ `refreshSync` 的 positions 改为三联各路真实回放时钟（或为验证/无真实源时提供可注入通道），无源时显示「同步状态不可用」而不显示恒真在同步。

## 5. 结论与边界（与实现者 README §3 对照，验证方独立复核）

- **判据内通过**（模板/逻辑/探针/单测层）：时间轴构成、四跳、游标 seek/clamp、同步偏差模型（含阈值含边界）、NVR 不可用显式标注回退本机片段、详情字段、配置阈值解析/校验——全部有板端实测证据。
- **判据内不通过**（QML 接线层，D-01/D-02）：报警卡进入自动定位与三联同步提示在当前交付**不可达**，两项均为交互 §5.3 的明确用户可观察点。
- **真实回放取流/解码**复用媒体层（GStreamer，属媒体/视频职责）；本项交付回放模型/时间轴/同步/来源/详情与 QML 页面骨架，同实现者边界声明。
- **数据接入**：应用进程尚未打开数据库，模拟时灌入样例（seed），非模拟时空数据；真实持久化接线归后续集成。
- **NVR 回放能力未核验**：`nvr_available` 当前为 false（通道映射/账号/协议未提供，见环境配置），界面显示「NVR 录像不可用，回放本机片段」；NVR 可达性探测归设备维护项。
- **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（E-01b）；本项以 core 板端实跑（566/0）+ 探针 PASS + **QML 静态接线扫描（新）** 为证据；D-01/D-02 在静态扫描层面确证（参数被丢弃/恒等三值喂入是源码级事实，运行期只会以同样形式呈现）；QML 运行时行为（视觉/交互手感）待 UI 环境落地后走 V-01。
- **主观画面内容与视觉体验由用户确认**；本报告不含人工视觉结论。

## 6. 过程轮次（v1 即首效轮）

- v1：全部业务步骤 exit=0（566/566 逐行一致 + probe 逐行 identical + 独立断言 fails=[] + 确定性 IDENTICAL + negatives exit2/0 + ext 63/63 + 回归留痕在档 + cleanup_ok=True），但 D-01/D-02 两项用户可观察点缺陷经板端静态接线扫描确证 → **TEST_FAIL**。验证侧自查：探针解析器经实现者归档日志刀测修正（seg/gap 键名 t0/t1、-type 正式行、sync 行保留 deviation 语义不反转断言；详见夹具注释），无交付侧改动。

## 7. 证据文件（本轮）

- 主记录: `VERIFY-FIXTURES-VERIFY-20261016-v1.log` / `.transcript.json`
- 板端 fresh: `build-TestRun-192_168_0_103-VERIFY-20261016-v1.core-evidence_core-tests-VERIFY-20261016-v1.{log,xml}`、`.replay-probe-…v1.log`、`.ai-probe-…v1.log`、`.alarm-probe-…v1.log`、`.clip-probe-…v1.log`、`.sim-inject-…v1.{log,json}`、`.status-probe-…v1.log`、`.status-probe-driver-…v1.log`、`build-TestRun-…-VERIFY-20261016-v1.{log,transcript.json}`
- 过程轮: 无（v1 为首效轮）
- 验证工具（复现入口）: `run_verify_round.sh`、`verify_replay20_fixtures.py`、`verify_replay20_ext.cpp`
