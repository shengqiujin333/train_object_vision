# ITEM-024 证据 — 健康监控、结构化日志与断电自恢复

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-024`
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §15.3（健康采样 NPU/CPU/温度/网络/磁盘 -> `device_health` + StatusModel；HLT-004/005、DATA-003）、§15.4（启动自恢复序列 1..8 且 UI 显示进度；任一步失败记录+告警，不阻塞其余步骤，单路非关键异常不导致整体退出）、§7（LogService 结构化按日滚动）；`docs/hmi/INTERACTION_DESIGN.md` §13（断电/重启自动恢复并显示进度）
- 源码: `app/src/core/include/railhmi/core/health.h`、`app/src/core/src/health.cpp`、`app/src/core/include/railhmi/core/recovery.h`、`app/src/core/src/recovery.cpp`、`app/src/core/tools/health_probe.cpp`、`app/tests/core/test_health.cpp`、`app/src/ui/src/health_model.{h,cpp}`、`app/src/ui/qml/components/RecoveryOverlay.qml`、`app/src/ui/qml/pages/P6Maintenance.qml`、`app/src/ui/qml/main.qml`、`app/src/ui/src/main.cpp`、`app/tools/check_health_wiring.py`、`app/tools/board_build.py`

## 1. 交付内容

1. **`railhmi-core` 健康采样**（`health.{h,cpp}`）：`HealthSample`（NPU/CPU/温度/网络错误/磁盘）+ `HealthThresholds` + `evaluate_health` → 等级 `Ok/Warn/Error`、告警项与概要文本（NPU/CPU/温度/网络错误/磁盘低阈值）。
2. **结构化日志**：`LogRecord{ts,severity,source,message,fields}` + `log_record_json`（单行 JSON）+ `query_logs`（按最低级别/来源/关键字/时间窗）+ `roll_health_logs_by_day`（按日滚动，仅保留最近 N 天）。
3. **启动自恢复序列**（`recovery.{h,cpp}`）：8 步固定顺序 `加载配置→打开数据库(WAL)→启动录像/存储→启动相机取流→启动 AI→云台归位→调前方预置位→前方 AI READY`；`RecoveryTracker` 记录每步 `Pending/Running/Ok/Degraded/Failed` 与进度；**单路非关键相机异常 -> StartStreaming 降级且继续**（`camera_result(ok=false,critical=false)`），关键步（配置/数据库）失败 -> `alive=false`。
4. **UI 桥接** `RailhmiHealthModel`：`health()`、`recovery()`（步骤/进度/文本/存活）、`logs(level,source,keyword)`、`logJson`。
5. **QML**：
   - `components/RecoveryOverlay.qml`：启动自恢复浮层（顺序步骤 + 状态 + 进度 + 关键失败提示），接入 `main.qml`。
   - `P6Maintenance.qml`：新增**结构化日志卡**（级别筛选 + 逐行）+ **恢复进度卡**（步骤逐项）。
6. **静态取证工具** `app/tools/check_health_wiring.py`。

## 2. 实测（板端 RK3588 本机；TestRun）

`board_build.py --mode TestRun`（stamp `20261016-ITEM024-r2`）→ `TESTRUN_EXIT=0` `SCAN_EXIT=0` `HEALTH_WIRE_EXIT=0` `HEALTH_EXIT=0`。核心单测 **663 passed / 0 failed**（含 ITEM-024 新增健康/日志/恢复断言）。\n
`railhmi-health-probe`（板端原文）：
```
type=health level=ok text="NPU 42.0% / CPU 33.0% / 温度 58.0°C / 网络错误 3 / 正常" alerts=0
type=health level=warn alerts=1 first="温度 80.0°C > 75.0°C"
type=log-json {"ts_ms":1792108801000,"level":"error","source":"stream","message":"CAM-04 断流","fields":{"cam":"CAM-04"}}
type=log-query by_source=1 by_keyword=1 by_severity=2 day_rolled=2
type=recovery step=load_config label=加载配置 status=完成 progress=12
type=recovery step=start_streaming label=启动相机取流 status=降级 progress=50
...
type=recovery-summary completed=true alive=true degraded_cameras=1 progress=100 text="恢复完成（部分降级）"
type=recovery-critical config_failed_alive=false progress=25
type=acceptance health_ok_warn=true log_query_and_rolling=true recovery_completed=true noncritical_isolated=true critical_stops_alive=true result=PASS
[check_health_wiring] PASS (ITEM-024 health/log/recovery wired)
```

判据核对（用户可观察点）：
- 重启后按顺序恢复并显示进度（8 步 `加载配置…前方 AI READY`，`progress` 12→100%）✅
- **单路非关键相机异常不导致应用退出**：`start_streaming=降级`、`degraded_cameras=1`、`alive=true`、其余步骤继续并 `completed=true` ✅
- 健康采样落库/上屏口径：`evaluate_health` 等级+告警（温度/NPU/CPU/网络/磁盘）✅
- 日志按日滚动且可结构化查询（级别/来源/关键字/JSON）✅
- 关键步失败显式提示（`alive=false`、RecoveryOverlay “关键步骤失败：应用不可用”）✅

## 3. 边界

- **真实采样源**（`/sys/kernel/debug/rknpu/load`、`/proc/stat`、thermal、`/proc/net/snmp`、`statvfs`）与**真实重启自恢复**（systemd、断电流水线）属目标平台/现场；本项交付采样/评估/日志/恢复**模型与进度语义**，桥接以确定性样例驱动。
- **落库**：`HealthSample`/`LogRecord` 复用 ITEM-007 `device_health` 表与结构化日志目录（`LogService` 按日滚动）；应用尚未打开数据库，桥接维护内存列表。
- **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（E-01b）；证据为 core 板端实跑（663/0）+ 健康探针 + 源码/静态扫描。

## 4. 证据文件

- `build-TestRun-...-ITEM024-r2.core-evidence_health-probe-...-r2.log`（健康/日志查询与滚动/恢复序列 PASS）
- `...-ITEM024-r2.core-evidence_core-tests-...-r2.log` / `.xml`（663 passed / 0 failed）
- `...-ITEM024-r2.transcript.json` / `.log`（构建 + 单测 + 探针 + 扫描/接线 + 取回）
- r1 留痕（`log_day_key` 与 ITEM-022 重复定义导致链接失败）——r2 复用既有定义修复

## 5. 复现

```sh
# 板端
bash app/build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-health-probe
python3 tools/check_health_wiring.py --root src/ui
# 经登记 SSH 入口
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-024-health --stamp <tag>
```
