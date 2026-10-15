# ITEM-020 验证报告 v3 — 历史回放页（software_verification 独立验证，UI-review D-01..D-09 修复轮复核）

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-020`
- 被测版本: commit `a718a53`（`[capability:embedded_linux_engineer.desktop_app_implementation] … returned IMPLEMENTED`，UI-review D-01..D-09 修复轮交付 stamp `20261016-ITEM020-uifix-r1`；上述 commit 本身只记录交付、未改动交付代码）
- 结论: **TEST_PASS**
- 与 v2 的关系: v2（`VERIFY-ITEM-020-v2.md`，VERIFY-20261016-v2）确认 D-01/D-02 修复在位；随后 `web_frontend.ui_implementation_review` 以 TEST_FAIL 提出 P3 页 9 项 UI 缺陷（REVIEW-ITEM-020.md D-01..D-09：关键帧缩略图/缺口斜纹/播放与倍速/级别配色/时间轴点击目标/完整摄像头列表/详情录像条/日期错误路径/触控键盘与尺寸）。实现方以 uifix-r1 交付修复；本 v3 轮独立复核**修复状态**，v2 全部判据保留为回归，不在未重测基础上外推。
- 验证方式: 经登记 Paramiko SSH 入口（192.168.0.103，指纹核验 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`）独立复跑交付入口 `board_build.py --mode TestRun` + 验证方独立断言 + 验证方扩展夹具 v3（新增级别传播/缩略图引用/备注与录像三态独立断言）+ 板端 QML/C++/桥接接线扫描 v3（46 项断言覆盖 D-01..D-09）；主机（Windows）不做板端替代结论。

## 1. 结论要点

**全绿：core 579/579（逐行与 uifix-r1 归档一致，含 5 条 uifix 新断言）、探针独立断言 fails=[]、ext 100/100、接线扫描 46 项 D-01..D-09 全命中 mismatch=[]，9 项 UI 缺陷修复在位。**

- **D-01 关键帧缩略图**：bridge `timeline()` 每时间点导出 `"keyframes"`（ts + 相机限定 `"thumb/CAM-01/<ts>.bmp"`），P3 `keyframeThumb` Repeater 按 `x: page.xOf(modelData.ts)` 定位、条带高 `status_strip_height`（64px 语义）——bridge+P3+尺寸扫描全 True；ext 专注 core 侧：hourly 24 帧、缩略图字符串逐字相机限定、ts↔ref 逐点一致、`kind=keyframe` 与 `kind=alarm` 分离且报警点 thumb 为空。
- **D-02 缺口斜纹**：P3 `timelineGap` 为 Canvas 且 `onPaint` 以斜线 `strokeStyle` 绘制（不再是纯色矩形）——源码级在位。
- **D-03 播放/倍速接线**：bridge 导出 `playback()/setPlaying/setSpeed`，P3 有 `playbackToggle`（暂停/播放态切换）、`speed-0.5/1/2` 三档 chip 调 `setSpeed(` + `togglePlay`、`onPlaybackChanged` 同步 `playState`——全部命中。
- **D-04 级别配色**：bridge 段 `sm["level"]`、标记 `am["level"]`；P3 `levelColorOf/levelTintOf` 映射 `Theme.alarm.L1/L2/L3.accent`、段/标记按 `modelData.level` 取色——ext 侧核实 core 传播语义（见 §3.4）。
- **D-05 时间轴点击目标**：段点击 `seekTo(modelData.t0)`、缺口 `acceptedButtons: Qt.LeftButton` 可点、标记 `selectAlarmAt` 联动详情——全部命中。
- **D-06 完整摄像头列表**：P3 使用全局 `railhmiCameras` 构造 `allCams`（6 路全量，不再截断为样例子集）。
- **D-07 详情录像条**：`detailRecordBar` + `recordLabel` + `recordOk` 三态（可回放/录像不完整/录像不可用）、降级态 `degraded_bg` 显式区分；bridge `build_event_detail` 备注 param 独立于录像可用性（ext 10 项核查，见 §3.4）。
- **D-08 日期错误路径**：`res.valid === false` 时 `dateError` 显式提示并可恢复（ext 亦核核心 `parse_date_utc_ms` 全部非法形态返回 -1）；chip 触控目标 `touch_target_min`（44px）+ `activeFocusOnTab` + Return/Enter/Space 键盘操作。
- **D-09 尺寸密度**：轨道高 `Theme.size.button.sm`（32px）、游标顶部 `family.mono` 时间读数（`cursorTime`）——在位。
- 交付方 uifix-r1 自证（旧 `check_replay_wiring.py`）在本轮 fresh TestRun 的 ui-scan 步骤亦 PASS（交付 transcript 在档），与验证方独立扫描结论一致。

## 2. 验证范围与判据来源

用户可观察点同 v1/v2（时间轴、四跳、锁定相关摄像头同步播放、详情侧栏、来源回退、同步偏差提示），本轮把 UI-review 提出的 D-01..D-09（P3 历史回放页信息完整性/可交互性/可用性缺陷）恢复为判据内要求后独立重测；判据来源不新增：`docs/hmi/INTERACTION_DESIGN.md` v2 §5.3、`DESKTOP_APP_DESIGN.md` §10、TEST_DESIGN TC-PLY-01/02/05 + REVIEW-ITEM-020.md 缺陷清单（逐项与设计判据对齐，无放宽）。**未新增/未放宽任何验收判据。**

## 3. 独立板端复跑（轮 VERIFY-20261016-v3）

### 3.1 交付入口 TestRun 复跑（全部 exit=0）

`verify_replay20_fixtures_v3.py` 以子进程方式重跑 `board_build.py --mode TestRun --keep-remote`：

- 交付入口 exit=0，fresh 归档齐全（core-tests log/xml + replay-probe + sim-inject log/json + status×2 + ai/alarm/clip probes 取回，见 §6）。
- 核心单测 **579/579**：fresh log 的 [PASS] 行序列与实现者 uifix-r1 归档 log（`…-ITEM020-uifix-r1`）逐行相同（`identical=True`，`verify_fails=0`，579=579）。
- JUnit XML 独立解析（验证方正则，不采信交付自述）：`tests=579 failures=0`；ITEM-020 判例 **41/41 在位**（v2 的 36 条 + uifix 新 5 条：`alarm segment carries real level (D-04)`、`alarm markers carry level (D-04)`、`keyframe carries thumbnail ref (D-01)`、`detail record status (D-07)`、`detail note independent from record status (D-07)`；含 `-&gt;` 转义还原后逐字匹配，`missing=[]`）。
- `railhmi-replay-probe` fresh 输出与实现者 uifix-r1 归档探针日志逐行 byte-identical（`identical=True`），同机二次运行 `diff` → `IDENTICAL`（确定性）。

### 3.2 探针独立断言（验证方正则逐行解析，不采信探针自述 acceptance 行）

fresh 探针逐行断言 **fails=[]**（离线 dry-run `offline_selfcheck_v3.py` 先对 uifix-r1 归档自检 FAILS=[] 后再上板；v3 探针新增 `level=`/`keyframe_thumb=`/`record_ok=` 字段解析）：

- 时间轴 3 段 + 1 缺口 + 1 报警 + 1440 关键帧；四跳 −30/+0/+30/+60 逐毫秒；seek/jump 边界。
- `type=sync` dev=200 in_sync=true / dev=900 提示全文「同步偏差 900 ms（阈值 300 ms）」；`type=lock` 精确三联 CAM-02,CAM-01,CAM-03；`type=sync-snapshot` 三态（在同步/提示/不可用）。
- **uifix 探针行**：段/标记行含 `level=`（级别非配置默认）、条带行含 `keyframe_thumb=thumb/CAM-01/<ts>.bmp`、详情行含 `record=可回放 record_ok=true`——独立解析通过。
- 来源回退 `source=local_clips note=NVR 录像不可用，回放本机片段`；acceptance 全 true + PASS。

### 3.3 板端接线扫描 v3（fresh 副本，46 项断言，含桥接 C++）

对上传工程 fresh 副本 `src/ui/qml/{main.qml, pages/P3Replay.qml, pages/P2AlarmCenter.qml, components/FilterChip.qml}` + `src/ui/src/main.cpp` + `src/ui/src/replay_model.cpp` 板端 python3 正则扫描（verify_scan_v3.py 在档）：全 46 项期望逐项为 True（W1×7 / W2×3 / V2×5 / V3×31，见 §1 逐缺陷映射），汇总 `mismatch=[]`，SCAN2_EXIT=0。扫描逻辑先在主机对交付源码离线复演（`offline_scancheck_v3.py`），期望与源码一致后才上板；一处自查修正为 speed chip 判据（`"speed-0.5"/"speed-1"/"speed-2"` 子串存在性，不再对 `0.5×` 标签再切分）。

### 3.4 验证方扩展夹具 v3（verify_replay20_ext_v3.cpp；板端 g++ 链本轮 fresh core 库）

**100/100 通过（EXT_EXIT=0）**：v2 全部 84 检查保留为回归（两处 D-07 契约变化随契约更新：默认备注为空、可用性标签由 `record_label` 承载，不再别名 `note`）+ 新增 16 检查分三组：

- **级别传播（D-04 core 语义，6 检查）**：报警点在临界窗口内 → `TimelineSegment.level` 推导为该报警级别（L1→1）；窗口外报警只着色自身标记；连续段 level=0；按 ts 排序的标记保持各自级别；**日末被裁剪的临界段保留窗口内首个有效报警级别**（裁剪窗口 [d+10s, d+1d]，段 level=L2，验证 level 推导在裁剪后窗口上重算）；无报警临界段 level=0；ts-only 便捷重载默认 L3（标记与段均 3）。
- **缩略图引用（D-01 core 侧，4 检查）**：hourly=24 帧；`thumb/CAM-01/1792108800000.bmp` 相机限定逐字；`keyframes[10].ts == d+10h` 且 ref 内嵌同一 ts；`kind=keyframe` 与 `kind=alarm` 分离、报警点 `thumb.empty()`。
- **备注/录像三态（D-07 契约，6 检查）**：显式备注逐字保留（维护备注甲/乙/丙）且与 `record_label/record_ok` 互不覆盖；三种 RecordAvailability 下 `record_ok` 三态（true/false/false）与 `record_label`（可回放/录像不完整/录像不可用）逐字；**默认重载备注为空**（UI 以 dash 呈现，不再复用可用性文案）；空串备注原样透传。

### 3.5 回归留痕（同一轮 fresh 归档在档）

sim-inject（log+json）、status-probe（maintainer+driver）、ai-probe、alarm-probe、clip-probe 归档且各自 exit=0。

### 3.6 负例 / 确定性

`--nope` → 非零非静默（stderr `unknown arg: --nope`）、`--help` exit=0、无参重跑 exit=0 且 diff `IDENTICAL`；板端 `/tmp/railhmi_*` 工作目录清理核验 `cleanup_ok=True`。

## 4. 结论与边界

- **判据内通过**：v2 全部结论保持（四跳/同步偏差模型/锁定链/来源回退/详情/配置阈值），且 D-01..D-09 九项 UI 缺陷在源码级（QML/C++ 桥接接线扫描）+ core 级（ext 100 检查）+ 单测级（41 判例）三重确证修复在位。
- **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（沿 v1/v2 边界声明）；本项证据为 core 板端实跑（579/0）+ 探针 PASS + QML/桥接源码级扫描。接线事实是源码级事实；运行期视觉/交互手感待 UI 环境落地后走 V-01。
- **真实回放取流/解码**复用媒体层（GStreamer，媒体/视频职责）；NVR 回放能力未核验（`nvr_available=false`，界面显式标注「NVR 录像不可用，回放本机片段」），与实现者边界声明一致。
- **数据接入**：模拟开时灌样例（报警行 + 时钟偏移 + 段/缺口）；真实持久化接线归后续集成。
- **主观画面内容与视觉体验由用户确认**；本报告不含人工视觉结论。

## 5. 过程轮次（v3 三轮上板，均为验证侧夹具自查修正，无交付侧改动）

- **轮 1**（07:13–07:20）：业务步骤全绿，仅 ext 编译失败 GXX_EXIT=1——v3 夹具 6 处 `build_timeline` 调用在 `AlarmMark` 双重载引入后用花括号初始化列表实参（如 `{}, {}, 0`）无法在 `std::vector<AlarmMark>` 与 `std::vector<long long>` 重载间消歧。改为具名类型向量后进轮 2。
- **轮 2**（07:24–07:25）：仍 GXX_EXIT=1——三处残余：t_level_propagation 便捷重载调用漏 gaps 参数、t3 把 none_gaps 误放 segments 位、一处局部 `no_segs` 重复声明。修正 arity/位置/重复声明后进轮 3。
- **轮 3**（07:29–07:37）：GXX_EXIT=0 但 100 检查中 1 项**语义期望构造错误**：「critical 覆盖 d+0 的 L2 标记」——段起于 d+10s，d+0 的标记本就在窗口外，core 语义（`level_in` 只看段窗口内报警）判定 level=0 是正确行为，属验证侧期望错误而非交付缺陷。改为断言真实边界语义（被裁剪临界段保留窗口内 L2 报警级别 + 日缘标记不被覆盖）后全绿：**579/579 逐行一致 + 41/41 + fails=[] + ext 100/100 + 扫描 mismatch=[] + negatives/确定性/回归留痕/cleanup_ok** → TEST_PASS。
- 三轮均为验证侧夹具问题（期望构造/类型消歧），教训已并入验证流程记忆（花括号列表实参不再用于重载歧义位置；上板前对每个新期望做逐行离线核对）。

## 6. 证据文件（本轮）

- 主记录: `VERIFY-FIXTURES-VERIFY-20261016-v3.log` / `.transcript.json`（末轮全绿；前三轮 transcript 覆写故只保留主记录，过程轮描述见 §5）
- 板端 fresh: `build-TestRun-192_168_0_103-VERIFY-20261016-v3.core-evidence_core-tests-VERIFY-20261016-v3.{log,xml}`、`.replay-probe-…v3.log`、`.ai-probe-…v3.log`、`.alarm-probe-…v3.log`、`.clip-probe-…v3.log`、`.sim-inject-…v3.{log,json}`、`.status-probe-…v3.log`、`.status-probe-driver-…v3.log`、`build-TestRun-…-VERIFY-20261016-v3.{log,transcript.json}`
- 复现入口: `run_verify_round_v3.sh`（stamp VERIFY-20261016-v3）、`verify_replay20_fixtures_v3.py`（v3 driver）、`verify_replay20_ext_v3.cpp`（v3，100 检查）
- 离线自查: `offline_selfcheck_v3.py`（探针解析器/期望 vs uifix-r1 归档）、`offline_scancheck_v3.py`（接线扫描期望 vs 交付源码）
- 过程基准: 实现者 uifix-r1 归档 `build-TestRun-…-ITEM020-uifix-r1.*`（逐行一致基准，在档）
- 历史: v1 `VERIFY-ITEM-020.md` + `…-VERIFY-20261016-v1` 归档；v2 `VERIFY-ITEM-020-v2.md` + `…-VERIFY-20261016-v2` 归档
