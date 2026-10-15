# ITEM-009 证据 — 模拟输入与故障注入系统

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-009`
- 依据: `docs/hmi/TEST_DESIGN.md` §3（故障注入目录 FI-01..10）、TC-SIM-01；`docs/hmi/DESKTOP_APP_DESIGN.md` §5.3/§16（模拟输入）
- 源码: `app/src/core/include/railhmi/core/simulation.h`、`app/src/core/src/simulation.cpp`、`app/src/core/tools/sim_inject.cpp`
- 测试: `app/tests/core/test_simulation.cpp`；入口 `app/tests/run_core_tests.sh`；板端 `app/tools/board_build.py --mode TestRun`

## 1. 交付内容

1. **模拟时钟确定性运行器** `SimulationRunner`（纯逻辑，无 Qt/GStreamer/相机）：
   - 模拟帧源节拍（`step_ms`）与 `onset_ms`/`recovery_ms`，故障区间内停帧/报错/降级，区间外自动**恢复**；
   - **可重置的模拟检测序列按 `ai_profile` 注入**：front→person(in_roi)、pantograph→Abnormal、
     wheel_rail→wheel_abnormal、cab_door→Ajar、none→不产生；重新实例化即重置（确定性，逐行可复现）。
2. **FI-01..FI-05、FI-10 软件开关**（`FaultKind`，`parse_fault` 支持 `01`/`fi-01`/`stream_cut` 等）：
   | FI | 名称 | 注入效果（结构化观测） |
   |---|---|---|
   | FI-01 | 断流 | `stream_state state=reconnecting` → 计数递增 → 达阈值 `state=offline`；恢复后 `state=live` |
   | FI-02 | 冻结 | 停帧超时 → `state=frozen`；恢复后 `state=live` |
   | FI-03 | AI 退出 | `ai status=unavailable`，不再产生检测/报警 |
   | FI-04 | 磁盘不足 | `storage low=true record_available=false`；恢复后 `low=false` |
   | FI-05 | 云台超差 | `ptz tolerance_ok=false target_zoom=.. actual_zoom=..`；恢复后 `true` |
   | FI-10 | 录像中断 | `gap cam=.. t0_ms=.. t1_ms=.. gap_count=..`（缺口合并） |
3. **结构化日志**：`Observation{t_ms,type,detail}`，`format_observation` 输出 `t=<ms> type=<t> key=value...`；
   注入参数（camera/profile/faults/onset/recovery/duration/step）与逐帧观测、`summary` 全部记录。
4. **CLI** `railhmi-sim-inject`（无 Qt）：`--fi 01,02,... --duration --step --onset --recovery --out --json --list`；
   打印结构化日志并写 `.log`/`.json`；退出码 0/2。

## 2. 实测（板端本机，经登记 SSH 入口）

| 步骤 | 结果 | 证据 |
|---|---|---|
| `bash tests/run_core_tests.sh` | `TESTRUN_EXIT=0`；**274 passed / 0 failed**；`RESULT: PASS` | `...-ITEM009-r2.core-evidence_core-tests-...-r2.log` |
| JUnit XML | `<testsuites tests="274" failures="0">` | `...-ITEM009-r2.core-evidence_core-tests-...-r2.xml` |
| `railhmi-sim-inject --fi 01,02,03,04,05,10` | `SIM_EXIT=0`；结构化日志 63 行，含各故障观测与 `summary final_state=live ... gaps=1` | `...-ITEM009-r2.core-evidence_sim-inject-...-r2.log/.json` |

关键断言（r2 原文节选）：`FI-01 -> reconnecting`、`FI-01 -> offline escalation`、`FI-01 recovers to live`、
`FI-02 -> frozen`、`FI-03 no detections -> no alarms`、`FI-04 record_available=false`、`FI-05 ptz tolerance fail`、
`FI-10 -> recording gap logged`、`simulation is deterministic/resettable`、`no fault -> front alarm produced`。

结构化日志样例：
```
t=0    type=injection  camera=CAM-01 profile=front faults=FI-01,FI-02,FI-03,FI-04,FI-05,FI-10 onset_ms=1500 recovery_ms=5000 duration_ms=6000 step_ms=500
t=1500 type=stream_state state=reconnecting reconnect_count=1 presentable=false cam=CAM-01
t=2500 type=stream_state state=offline reconnect_count=3 presentable=false cam=CAM-01
t=1500 type=storage free_bytes=2147483647 total_bytes=107374182400 low=true record_available=false
t=1500 type=ptz tolerance_ok=false target_zoom=10.000000 actual_zoom=10.000000
t=6000 type=summary final_state=live reconnect_count=0 alarms=2 gaps=1 events_recorded=2
```

## 3. 边界与后续

- 本层在**模型层**驱动状态/规则/录像缺口/存储/云台容差，并可经 `railhmi-sim-inject` 无相机运行；
  这是“驱动全部页面与状态”的**数据/状态侧**。板端缺 Qt5 Quick/Qml 开发包，QML 页面无法构建，
  页面绑定（同一状态模型）需平台/rootfs 补齐 Qt 后由 UI 任务接入（见 `build_env_check.md` §3.1）。
- 真实像素级模拟（录制文件/合成视频）属 media 层与 ITEM-002 spike；本层不做图像。
- 注入观测已可写入 SQLite（`stream_events`/`recording_gaps`/`events`）——运行时接线归后续录像/健康任务。
- 不宣称端到端页面已联调；不把本层结果外推为板上渲染/相机行为。

## 4. 证据文件

- `build-TestRun-192_168_0_103-20261015-ITEM009-r2.transcript.json` / `.log`（运行器 transcript/日志）
- `...-ITEM009-r2.core-evidence_core-tests-...-r2.log` / `.xml`（测试归档，274/274）
- `...-ITEM009-r2.core-evidence_sim-inject-...-r2.log` / `.json`（故障注入结构化日志/JSON）

## 5. 复现

```sh
# 板端/最小工具链本机
bash app/tests/run_core_tests.sh
./build-core/tools/railhmi-sim-inject --fi 01,02,03,04,05,10 --duration 6000 --out sim.log --json sim.json
# 经登记 SSH 入口（上传 app/ 后运行并取回归档）
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-009-simulation --stamp <tag>
```
