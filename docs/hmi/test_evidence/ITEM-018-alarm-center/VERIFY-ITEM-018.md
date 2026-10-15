# ITEM-018 验证报告 — 报警中心页面（software_verification 独立验证）

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-018`
- 被测版本: commit `c293c96`（`[capability:embedded_linux_engineer.desktop_app_implementation] … returned IMPLEMENTED`）
- 结论: **TEST_PASS**
- 验证方式: 经登记 Paramiko SSH 入口（192.168.0.103，指纹核验 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`）独立复跑交付入口 `board_build.py --mode TestRun` + 验证方独立断言 + 验证方扩展夹具；主机（Windows）只做静态归档核对，不替板端下结论。

## 1. 验证范围与判据来源

任务用户可观察点（Runtime current task）：列表字段含级别/类型/时间/来源摄像头/描述/处理状态/录像可用性；默认按级别降序再时间降序；支持确认、批量确认、查看录像、查看快照与按日期/时间段/摄像头/类型/级别/确认状态筛选；录像不可用时行内显著标注；筛选组合后列表与空态正确；批量确认后各行状态即时更新并写维护日志。

判据来源不新增：`docs/hmi/INTERACTION_DESIGN.md` §5.2、`docs/hmi/VISUAL_DESIGN.md` §8.2、`docs/hmi/DESKTOP_APP_DESIGN.md` §9（ack 只改状态不删事件）、测试设计 TC-ALM-02（ack 即时生效 + 维护日志 HLT-007 + 事件存在性不变）、PLY-008（录像不可用显式标注）、TC-CORE-05（ack 不删事件层依据）。

## 2. 独立板端复跑（权威轮 VERIFY-20261016-v3）

### 2.1 交付入口 TestRun 复跑

`verify_ai18_fixtures.py` 以子进程方式重跑 `board_build.py --mode TestRun --keep-remote`（上传 app/ → build.sh CoreTests → railhmi-core-tests → tokens selftest → ui scan → sim-inject → status/ai/alarm 探针 → 归档取回）。逐项 exit=0：

- 核心单测 **512/512（impl 逐行 [PASS] 序列与本轮 fresh log 逐一相同，identical=True）**；JUnit XML 独立解析 `tests=512 failures=0`；报警中心域用例 83 条（≥40 阈值）。
- `railhmi-alarm-probe` fresh 输出与实现者归档 r1 探针日志**逐行 byte-identical**（确定性复现）；同轮二次运行 `diff` IDENTICAL。
- 回归留痕同轮在档：sim-inject（log+json）、status-probe（maintainer+driver）、ai-probe 全部 exit=0。

### 2.2 报警探针独立断言（不相信探针自述 acceptance 行，验证方自行解析）

对 fresh `alarm-probe-VERIFY-20261016-v3.log` 独立解析：

- 列表 4 行默认排序 `[1,4,3,2]` = L1(id1 户00:00:05) → L1(id4 00:00:01) → L2(id3) → L3(id2)：级别降序→同级时间降序 ✅
- 行字段齐全：level/type/type token/time(UTC 文本)/cam id+中文名/ack/record/snapshot/desc ✅
- 录像三态 `available/incomplete/unavailable`，id3 行内 `[录像不完整]`、id2 行内 `[录像不可用]` 显式标注（PLY-008 非静默）✅
- 四维筛选独立计数：level_L1=2、unacked=3、window[2000,6000)=2（半开窗已由 ext 证明）、combo(L3&CAM-01)=0 且 empty=true（空态）✅
- 批量确认 `changed=3`；确认后 4 行仍在（**事件未删除**），全部 `ack_label=已确认`（即时更新）✅
- 维护日志 `action=ack_alarm target=1,2,3 user=maintainer detail=count=3`（HLT-007）✅
- options：cameras=4 types=4 levels=3 ✅
- 负例：`--nope` → exit 2（非静默）✅

### 2.3 验证方扩展夹具（verify_ai18_ext.cpp，链本轮 fresh librailhmi_core.a，板端 g++）

**53/53 通过（EXT_EXIT=0）**，覆盖交付测试/探针之外的可证伪边界：

- 时间窗 `[from,to)` 半开语义（from 含、to 不含、内部区间双排除）
- `day_start_utc_ms` 负值向下取整（-1ms → 前一日）；`parse_date` 月/日越界、短串、尾随杂字拒绝，闰日 2024-02-29 接受
- 默认排序稳定性：同级同时刻保持插入序；L1 与输入顺序无关居首；同 level 时间降序
- 确认：重复 id 每一个匹配行翻转（changed=3）、重复 ack 计数 0、`to=Unacked` 回退并重新打标签、未知 id 无操作
- 行派生：level>3 收敛 L3、三态中文标签一致、未知 type 串回退 `设备故障`、空 snapshot → `snapshot_available=false`、ts≤0 → "—"
- 筛选：camera+ack / camera+level 组合命中与空；type 标记往返；options 去重且 names 与 ids 平行
- 纯度：`query_alarms` 不改源向量顺序；`build_ack_log` 空 id 列表 count=0、多 id 连接、大 id 字符串化

### 2.4 QML/UI 桥接静态核对（板端 QML 运行期不可编译为已知边界 E-01b）

QML 页面源与桥接源逐项核对（主机静态，不冒充实运行）：`P2AlarmCenter.qml` 行内消费全部 7 个展示字段（levelLabel/typeLabel/time/cameraName/description/ackLabel/recordLabel 徽标 `visible: !modelData.recordOk`）；空态“暂无符合条件报警 + 清除筛选”；`replayRequested/snapshotRequested` 两入口；`ackOne/ackAllUnacked` 单/批量；六维筛选 chip（级别×4、状态×3、类型/摄像头动态、日期文本从/至）；`alarm_model.cpp` query/options/ack 全部委托 core（过滤/排序/确认单一真相）；`main.cpp` 上下文属性注册 + `simulation` 门控灌样（默认空态，不伪真实事件）。与 ITEM-010 起一致的 ui-scan（板端 exit=0）本场在档。

## 3. 结论与边界（与实现者 README §3 一致，验证方独立复核认可）

- **判据内全部通过**：字段/排序/筛选/空态/批量确认/维护日志/录像标注/快照与录像入口的 core 层与桥接层均有板端实测证据。
- **真事件持久化接线未实现**（设计上属后续集成项）：`RailhmiAlarmModel.seedEvents` 接口存在但应用进程未打开数据库，默认列表为空态；`app.simulation=true` 才灌样例。本项验收的是页面与模型，接线缺口已在实现者边界声明，与本次任务范围一致，不改变结论。
- **QML 运行期未在板端执行**（板端缺 Qt5 Quick/Qml 开发包 E-01b，历史登记）；本项证据 = core 板端实跑 512/0 + 探针 PASS + QML/桥接静态核对。真实运行期行为（触屏交互、样式）待 Preview/Release 环境落实后的后续验证项。
- **“查看录像”跳转 P3 回放页**的信号已接线，回放页本体属后续工作项。

## 4. 观察项（交实现参考，不改本次验收结论）

- **O-1 日期解析宽松**：`parse_date_utc_ms` 仅校验 月≤12/日≤31，`2023-02-29` 被规整为 2023-03-01 而非拒绝（ext 夹具实测 `parse_date_utc_ms("2023-02-29") == parse_date_utc_ms("2023-03-01")`）。设计/PRD 未定义逐月天数校验，不构成验收失败；筛选框若输入此类日期，窗口边界会平移规整天。建议后续在需求明确时加逐月校验。
- `build_ack_log` 默认 `result="ok"`（契约值以实现为准，同 ITEM-012 教训——不自行假设默认为空）。

## 5. 过程轮次（按 Runtime loop 记录）

- v1：夹具 kv 解析弱化（`detail=count=3` 被拆分）+ ext 夹具 check 参数序失配误报 —— 验证侧方法修正（沿用 ITEM-017 “探针 kv 值可含 =”教训）。
- v2：ext 两处预期自查修正（`build_ack_log` 默认值实为 "ok" 以实现为准；2023-02-29 宽松接受转为 O-1 观察项不越权新增判据）。
- v3：**权威轮，全部 exit=0**。

## 6. 证据文件（本轮）

- 主记录: `VERIFY-FIXTURES-VERIFY-20261016-v3.log` / `.transcript.json`
- 板端 fresh: `build-TestRun-192_168_0_103-VERIFY-20261016-v3.core-evidence_core-tests-20261016-v3.{log,xml}`、`.alarm-probe-…v3.log`、`.ai-probe-…v3.log`、`.sim-inject-…v3.{log,json}`、`.status-probe-…v3.log`、`.status-probe-driver-…v3.log`、`build-TestRun-…-VERIFY-20261016-v3.{log,transcript.json}`
- 过程轮: v1/v2 对应同名 `VERIFY-FIXTURES-*` 与 `build-TestRun-*-VERIFY-20261016-v1/v2.*`（保留失败轮原样）
- 验证工具（复现入口）: `run_verify_round.sh`、`verify_ai18_fixtures.py`、`verify_ai18_ext.cpp`、`verify_local_checks.py`（主机静态归档核对）
