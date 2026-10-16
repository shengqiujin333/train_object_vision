# VERIFY-ITEM-024 — 健康监控、结构化日志与断电自恢复（software_verification 权威轮）

- 工单: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-024`
- 能力: `software_tester.software_verification`
- 审核对象: 实现 commit `93e625a`（实现者 stamp `20261016-ITEM024-r2`，板端 663/0）
- 验证章: `VERIFY-20261016-v1`（TestRun 全量独立复跑）+ `VERIFY-EXT-v2`（ext 夹具复核；v1 过程轮 4 处验证侧期望错，见 §5）
- 结论: **TEST_PASS**

## 1. 验收范围与判据来源

工单要求：周期采样 NPU/CPU/温度/网络错误/磁盘并落库与上屏；日志按日滚动且可结构化查询；启动按顺序恢复配置→数据库→录像→取流→AI→云台归位→前方预置位并显示恢复进度；用户可观察：重启后各相机自动重连、AI 恢复、云台回前方预置位、界面显示恢复步骤与完成状态、单路非关键相机异常不导致应用退出。

判据来源：`docs/hmi/DESKTOP_APP_DESIGN.md` §15.3/§15.4（恢复序列 8 步 + 失败记录 + 显式告警 + 不阻塞）、§13（健康采样源与 `device_health`）、`docs/hmi/TEST_DESIGN.md` G7（TC-RECOVERY-01..06，其中 01/02/03 标注 ENV-E 现场档）、交互方案 §13。不新增判据。

**范围分层**（沿用设计 §16 与实现 README §3 声明，非放水）：
- 本项交付**健康/日志/恢复的模型、评估、查询、进度语义与 UI 接线**；真实采样源（rknpu load、/proc/stat、thermal、snmp、statvfs）与真实断电/systemd 自启（TC-RECOVERY-01/02/03）属 ENV-E 现场/平台档，本设计登记为待实现/现场执行项，不在本轮判据内。
- **落库**：`DeviceHealthRow`/`insert_health`（ITEM-007 `device_health` 表 + `SqliteRepository`）已存在于 platform 层并被 ITEM-007 单测覆盖；应用尚未打开数据库，本轮桥接以内存列表驱动（与 ITEM-022 v3→v6 的"维护日志 SQLite 持久化属后续集成"同一观察项口径，登记为观察项 O-1，不新增/不豁免判据）。
- QML 运行期未执行（板端缺 Qt5 Quick/Qml 开发包，E-01b）：UI 级用户可观察点以**源码接线扫描**（对板端 fresh 副本独立扫描）取证，非运行时证据。

## 2. 独立复跑结果（v1 全绿 + ext v2 全绿）

| # | 步骤 | 结果 |
|---|------|------|
| 1 | 指纹校验 | `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU` 与登记一致 |
| 2 | 交付入口 `board_build.py --mode TestRun --keep-remote` 独立复跑 | `TESTRUN_EXIT=0`、`HEALTH_WIRE_EXIT=0`、`HEALTH_EXIT=0`、其余探针 EXIT=0 |
| 3 | core 单测逐行比对 | impl(r2) 663 行 == verifier 663 行，`identical=True`，fails=0，`RESULT: PASS passed=663 failed=0` |
| 4 | JUnit 独立解析 | `tests=663 failures=0`；ITEM-024 精确用例名 22/22 命中（healthy->ok / temp+net warn / very high temp->error / low disk->warn / log json with fields / query by source·min severity·keyword·time window / log day key / day-rolling keeps latest day / recovery has 8 steps / all ok->completed / step label / single non-critical camera failure does not exit app / degraded camera counted / streaming step degraded, rest continue / recovery still completes / critical step failure->app not alive / config step failed recorded / progress reflects terminal steps） |
| 5 | health-probe 独立 oracle 断言 | 15 条精确行（健康 ok/warn 文本、log-json 字段、查询计数 by_source=1/by_keyword=1/by_severity=2/day_rolled=2、8 步恢复序列 12→100%、summary `degraded_cameras=1 alive=true`、critical `alive=false progress=25`、acceptance PASS）`fails=[]`；**进度数学独立复算** `i*100//8 = [12,25,37,50,62,75,87,100]` 与探针一致 |
| 6 | health-probe 跨轮字节一致 | 与实现者 r2 归档逐行 `identical=True`；二跑 diff `IDENTICAL` |
| 7 | ext 夹具（v2） | 板端 g++ 链接本轮 `librailhmi_core.a` 编译运行：**103/0**（健康阈值边界严格 `>`、哨兵 -1 跳过、JSON 转义、severity 升序、半开时间窗、关键字三路命中 message/键/值、按日滚动日原子+最新日+哨兵 0/负值全保留、8 步枚举 token、进度数学、camera_result 排序语义、关键步=仅 config/db、复 ok 复活语义文档化断言） |
| 8 | 接线扫描（11 项，上板对 fresh 副本，验证侧自有判据） | mismatch=[] 全 True：浮层结构/接入 main.qml/P6 日志卡（3 级筛选 chips + 行渲染）/P6 恢复卡/桥接委托 core/main 注入 seed 8 步/板端管线含 health 探针+接线检查/桥接 filter 映射 min_severity/浮层完成隐藏+失败提示/降级≠失败双处着色/无硬编码色值 |
| 9 | 回归留痕 | 同轮 core-evidence 含 status-probe(+driver)/sim-inject/alarm-probe/clip-probe/replay-probe/ai-probe/maintenance-probe/permissions-probe/health-probe 全套 |
| 10 | 清理 | `/tmp/railhmi_verify024`（v1+v2）与 `/tmp/railhmi_build` 按名删除，`cleanup_ok=True`（ls exit=2 为删除确认） |

## 3. 工单判据逐条核对（用户可观察点）

| 工单要求 | 证据 | 结论 |
|---|---|---|
| 周期采样 NPU/CPU/温度/网络错误/磁盘 | `evaluate_health` 五维评估 + 阈值边界（ext：严格 `>`、-1 哨兵、磁盘百分比、disk_total=0 守卫）+ `HealthSample` 字段齐备；周期采样源属 ENV-E 登记边界 | 模型层 ✅ |
| 落库 | `device_health` 表 + `insert_health` 已交付并被 ITEM-007 单测覆盖；应用未开库前桥接内存列表（O-1 观察项） | 集成层观察项 |
| 上屏 | P6 `maintHealthCard`（ITEM-022 `healthText`）+ 本轮 `railhmiHealth.health()` 桥接 + 扫描 T5/T6 | 接线 ✅ |
| 日志按日滚动 | `roll_health_logs_by_day`（日原子、保留最新 N 日、0/负哨兵全保留）+ ext 日界毫秒断言 + `log_day_key` UTC 复算 | ✅ |
| 结构化查询 | `query_logs` 级别/来源/关键字/时间窗 + ext 三路关键字与半开窗边界 + P6 日志卡三档筛选 | ✅ |
| 启动按顺序恢复 + 界面显示进度 | 8 步固定顺序（token+label 双断言）、进度 12→100%、`恢复中 k/8` 中间态、`RecoveryOverlay` + P6 恢复卡接线扫描全 True | ✅ |
| 单路非关键相机异常不退出 | `camera_result(false,false)` → StartStreaming 降级、`degraded_cameras=1`、`alive=true`、其余步骤继续、`恢复完成（部分降级）`；ext 排序语义（ok 不复位降级、降级累计、关键相机 Failed 亦不杀应用——相机域非致命） | ✅ |
| 关键步失败应用不可用 | `recovery_step_critical`=LoadConfig/OpenDatabase、`alive=false`、`progress=25`、浮层"关键步骤失败"显式提示（T9） | ✅ |
| 重启后相机自动重连/AI 恢复/云台回预置位 | 恢复序列 step=StartStreaming/StartAi/PtzHome/FrontPreset 状态与进度可见；真实重启链路（kill -9/断电）属 TC-RECOVERY-01..03 ENV-E 现场档，未执行即不声称 | 序列语义 ✅ / 现场档未执行（如实登记） |

## 4. 观察项（不阻塞，交后续集成）

- **O-1**：健康采样与结构化日志的 SQLite 落库（`insert_health`/日志目录）待应用打开数据库的后续集成项接通；当前 `DeviceHealthRow`/DDL/`insert_health` 与按日滚动语义均已被单测覆盖，桥接层内存列表为过渡态。与 ITEM-022 维护日志持久化观察项同口径。
- **O-2**：真实采样源与断电/systemd 自启（TC-RECOVERY-01/02/03、TC-HLT-05/07 现场档）待 ENV-E；本设计原登记，非本轮缺项。

## 5. 过程轮记录（验证侧夹具错误，非产品缺陷）

- v1 ext 98/4——四处均为验证侧期望构造错：① `j.find(...)==0` 未算 `{"` 前缀（改 `rfind(...,0)==0`）；② `start()` 后 text 是"恢复中 0/8"，"未开始"仅在 start() 前；③ `camera_result(critical=true)` 断言 `alive=false` 与交付契约矛盾（recovery.h:6 + 设计 §15.4：仅 config/db 致命，相机丢失是报警域非退出域）；④ camera_result 已使 StartStreaming 终态，后续 complete_step 计数 2/8 而非 1/8。修正后 ext 103/0（`VERIFY-EXT-v2` 归档）。
- 上板前离线自查三件套（零成本抓错）：`offline_selfcheck0.py`（JUnit 663/0 + 22 判例名 + probe oracle + 进度数学 + ext token 清单）、`offline_selfcheck1.py`（SCAN_PY 本地 remap dry-run，11 键全 True）、`offline_selfcheck2.py`（ext 静态审计：check 签名/printf/`\xHH` 贪婪/LogRecord 5 字段聚合/重名/includes，55 checks）。

## 6. 权威证据文件

- `VERIFY-FIXTURES-VERIFY-20261016-v1.log` / `.transcript.json` — v1 全量轮（TestRun 复跑 + 663 逐行 + oracle + 扫描 + 确定性 + 回归留痕）
- `VERIFY-EXT-v2-VERIFY-20261016-v2.log` / `.transcript.json` — v2 ext 复核轮（103/0）
- `build-TestRun-192_168_0_103-VERIFY-20261016-v1.core-evidence_*` — 本轮独立复跑归档（core-tests 663 行 + JUnit XML + health-probe + 回归全套）
- `verify_health24_ext.cpp`（103 项）/ `verify_health24_fixtures.py` / `verify_health24_ext_rerun.py` / `run_verify_round.sh` / `run_ext_rerun.sh` / `cleanup_board.py` / `offline_selfcheck{0,1,2}.py` — 验证侧夹具与司机
- 实现者归档（审核对象）: `build-TestRun-192_168_0_103-20261016-ITEM024-r2.*`

**终态 TEST_PASS。**
