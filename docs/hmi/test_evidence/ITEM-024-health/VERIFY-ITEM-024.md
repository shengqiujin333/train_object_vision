# VERIFY-ITEM-024 — 健康监控、结构化日志与断电自恢复（software_verification 复核轮）

- 工单: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-024`
- 能力: `software_tester.software_verification`
- 审核对象: 实现 commit `fd9dde4`（实现者 stamp `20261016-ITEM024-uifix-r1`，UI 评审 D-01..D-04 修复轮）
- 修复链: 首轮验证 `VERIFY-ITEM-024.md`（commit `93e625a`）TEST_PASS（core 层）→ `web_frontend.ui_implementation_review` TEST_FAIL（`REVIEW-ITEM-024.md`：D-01 P6 网格溢出裁切 / D-02 恢复·日志未接 `changed()` 不刷新 / D-03 来源·关键字查询 UI 不可达 / D-04 恢复路径未演示）→ 本轮对 uifix-r1 修复的独立复核
- 验证章: `VERIFY-20261017-v1`（TestRun 全量独立复跑 + 双 ext 夹具 + 扫描 v3 + 板端负例对照 + 确定性）
- 结论: **TEST_PASS**

## 1. 本轮判据（不新增验收，只复核评审偏差的修复）

工单验收不变（8 步顺序恢复/进度上屏/单路非关键相机异常不退出/日志按日滚动且可结构化查询/健康采样上屏）。本轮针对 UI 评审 4 项偏差逐项独立取证：

| 偏差 | 修复交付（fd9dde4） | 本轮独立判据 |
|---|---|---|
| D-01 P6 网格溢出裁切 | 卡片区包 `Flickable`（`clip: true`、`contentHeight: grid.height + 2*s3`） | 扫描 R1（Flickable/clip/contentHeight/contentWidth 四要素）+ R2 反向断言（旧顶层 `Grid { anchors.fill: parent }` 形态必须消失）+ 交付 checker 对合成 pre-fix 树必须 FAIL（板端负例对照） |
| D-02 恢复/日志不刷新 | `RecoveryOverlay` 改缓存属性 + `reload()` + `Connections{onChanged}`；P6 新增 `healthSt/logRows` + `reloadHealth/reloadLogs` + health Connections | 扫描 T11/T12（正向）+ R3 反向断言（旧一次性方法调用绑定 `R ? R.recovery()` 与 `recovery().text/.steps` 必须消失）；ext v3 对桥接演示状态机逐 tick 断言 |
| D-03 来源/关键字不可达 | 结构化日志卡新增 `logSourceInput/logKeywordInput`（`onEditingFinished → reloadLogs()`） | 扫描 T3（输入框存在 + 编辑完成触发重查 + 桥接 `logs(fLevel,fSource,fKeyword)` 三参贯通） |
| D-04 恢复路径未演示 | 桥接新增 `resetRecovery/advanceRecovery` Q_INVOKABLE；浮层 Timer 600ms 推进；P6 `recoveryDemo` chip | 扫描 T13/T14（推进算法语义：非流步骤 `complete_step(ok)`、流步骤 `camera_result(false,false)` 降级、全终态当 tick `finish`）+ **ext v3（60 项）板端实跑演示状态机** |

边界保持既有登记：QML 运行期未执行（板端缺 Qt5 Quick/Qml，E-01b），UI 级结论为**源码接线证据**（交付 checker + 验证侧扫描 + 板端负例对照）；core 层为板端实跑。不因扫描全绿声称运行期视觉验证。

## 2. 独立复跑结果（VERIFY-20261017-v1，全绿）

| # | 步骤 | 结果 |
|---|------|------|
| 1 | 指纹校验 | `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU` 与登记一致 |
| 2 | 交付入口 `board_build.py --mode TestRun --keep-remote` 独立复跑 | `TESTRUN_EXIT=0`、`SCAN_EXIT=0`、`HEALTH_WIRE_EXIT=0`、`HEALTH_EXIT=0` 及全部探针 EXIT=0 |
| 3 | core 单测逐行比对 | impl(uifix-r1) 663 行 == verifier 663 行，`identical=True`，fails=0；且与 r2 权威轮 PASS 列表逐行一致（core 未被 UI 修复轮改动，双口径确认） |
| 4 | JUnit 独立解析 | `tests=663 failures=0`；ITEM-024 精确用例名 22/22 命中 |
| 5 | health-probe oracle | 15 条精确行 `fails=[]` + 进度数学独立复算 `[12,25,37,50,62,75,87,100]`；与 uifix-r1 归档**逐字节一致**（core 未动） |
| 6 | ext v2（回归基线，未改） | 板端 g++ 链接本轮 `librailhmi_core.a`：**103/0**（健康阈值严格 `>`、哨兵 -1、JSON 转义、严重级序、半开窗、关键字三路、滚动日原子、恢复枚举/进度/camera_result 语义全保留） |
| 7 | **ext v3（本轮新增，60 项）** | 桥接演示恢复路径 oracle（`DemoDriver` 逐字段转写 `advanceRecovery` 对真实 `RecoveryTracker` 驱动）：9 tick 序列 progress 0→12→25→37→**50（降级步，`degraded_cameras=1`，`alive=true`，detail=`CAM-02 启动失败（降级）`）**→62→75→87→100 + 当 tick `finish` → `恢复完成（部分降级）`；完成后推进为无操作（无二次 finish/不再降级）；重按 chip 全重置；中途重置干净重启；关键失败态 `alive=false`（浮层"关键步骤失败"提示背后的模型状态）；浮层颜色映射所依赖的 5 个 status token + 2 个 step label 逐一核对 |
| 8 | 接线扫描 v3（20 键，板端对 fresh 副本，验证侧自有判据） | `mismatch=[]` 全 True：T1-T9（v1 全保留回归）+ **T11 浮层反应性 + T12 P6 反应性 + T13 演示路径 + T14 演示语义** + N2 降级≠失败双处着色 + T10 无硬编码色 + **R1 可滚动 + R2 反向（无锚定 Grid）+ R3 反向（无一次性绑定）+ R4 演示可达（seed 已完成态时演示路径存在）+ R5 新输入框 Theme token 存在** |
| 9 | 板端负例对照 | 交付 checker：fresh 副本 `NEG_HEALTHY_EXIT=0`；合成 pre-fix 变异树（锚定 Grid/去输入框/去演示/去 Connections）`NEG_MUTATED_EXIT=1` 且输出点名 Flickable/logSourceInput/logKeywordInput/recoveryDemo/D-02——证明 checker 的 D-01..D-04 断言真实咬合，本轮 PASS 非空转 |
| 10 | 确定性 | health-probe 二跑 `diff` → `IDENTICAL` |
| 11 | 回归留痕 | 同轮 core-evidence 含 sim-inject/status(+driver)/ai/alarm/clip/replay/maintenance/permissions/health 探针全套 |
| 12 | 清理 | `/tmp/railhmi_verify024` 与 `/tmp/railhmi_build` 按名删除，`cleanup_ok=True`（ls exit=2 为删除确认输出） |

## 3. 工单判据逐条核对（用户可观察点，复核轮口径）

| 工单要求 | 证据 | 结论 |
|---|---|---|
| 周期采样 NPU/CPU/温度/网络错误/磁盘 | 同 v1：`evaluate_health` 五维 + ext 边界（回归基线全保留） | 模型层 ✅ |
| 落库 | `device_health`/`insert_health` 已交付并被单测覆盖；应用未开库前桥接内存列表（O-1 观察项不变） | 集成层观察项 |
| 上屏 | P6 健康/日志/恢复卡 + **本轮修复：日志/恢复卡随模型 `changed()` 刷新（T11/T12/R3）** | 接线 ✅（刷新缺陷已修） |
| 日志按日滚动 | 同 v1（ext v2 回归）+ P6 日志卡 | ✅ |
| 结构化查询 | core 查询（ext v2 回归）+ **本轮修复：来源/关键字输入 UI 可达（T3）** | ✅（可达性缺陷已修） |
| 启动按顺序恢复 + 界面显示进度 | 8 步序列（同 v1）+ **本轮修复：Flickable 可滚动（R1/R2/负例）+ 反应性刷新（T11/T12）+ 进度可演示推进至完成并隐藏浮层（ext v3 全序列 + T9）** | ✅（裁切/不刷新缺陷已修） |
| 单路非关键相机异常不退出 | `camera_result(false,false)` → 降级、`alive=true`（ext v2 + ext v3 tick4 + 探针） | ✅ |
| 重启后相机自动重连/AI 恢复/云台回预置位 | 恢复序列语义同 v1；真实重启链路（TC-RECOVERY-01..03）属 ENV-E 现场档，未执行不声称 | 序列语义 ✅ / 现场档如实登记 |

## 4. 观察项（不阻塞，沿用既有登记）

- **O-1**：健康采样/结构化日志 SQLite 落库待应用开库集成（同 v1）。
- **O-2**：真实采样源与断电/systemd 自启（TC-RECOVERY-01/02/03、TC-HLT-05/07 现场档）待 ENV-E（同 v1）。
- **O-3（本轮登记）**：QML 运行期行为（Flickable 手感、浮层动画、输入框焦点/键盘、Timer 推进的视觉节奏）在板端 Qt5 Quick/Qml 环境就绪后仍需运行期核验（E-01b 既有边界）；本轮结论限于源码接线 + 板端静态/负例取证。

## 5. 过程轮记录（验证侧夹具错误，非产品缺陷）

上板前三件套离线自查（`offline_selfcheck0/1/2_v3.py`）抓出并修正 6 处验证侧构造错，全部未影响上板轮：
1. selfcheck0 include 断言捕获组与带引号常量比较（恒假）；
2. 扫描 v3 T11 期望字符串漏 `st: ({` 的括号；
3. R1/R2 正则 `[^}]*` 会跨入嵌套卡片（改 `[^{}]*` 圈定块头）；
4. 几何复算首版按卡片高度求和（2272px）而非按行取 max（1292px，溢出 284px——与评审 §R4 口径一致，溢出结论不变）；
5. selfcheck2 A1 守卫把替换结果丢弃（`old_grid if ... else p6` 逻辑自败）；
6. checker 负例树缺 `qml/main.qml`（checker 需读该文件，FileNotFoundError 而非判据 FAIL）。

板端轮一次通过，无过程失败。

## 6. 权威证据文件

- `VERIFY-FIXTURES-VERIFY-20261017-v1.log` / `.transcript.json` — 本轮全量（TestRun 复跑 + 663 逐行 + JUnit + oracle + 双 ext + 扫描 v3 + 板端负例 + 确定性 + 回归 + 清理）
- `build-TestRun-192_168_0_103-VERIFY-20261017-v1.core-evidence_*` — 本轮独立复跑归档（core-tests 663 行 + JUnit XML + health-probe + 回归全套）
- `verify_health24_ext_v3.cpp`（本轮新增 60 项演示路径 oracle）/ `verify_health24_fixtures_v2.py` / `run_verify_round_v2.sh` — 本轮验证侧夹具与司机
- `verify_health24_ext.cpp`（v2 回归基线 103 项，未改）/ v1 轮夹具与离线自查 — 前轮留痕
- `offline_selfcheck0/1/2_v3.py` — 本轮上板前离线自查三件套（全绿后上板）
- 实现者归档（审核对象）: `build-TestRun-192_168_0_103-20261016-ITEM024-uifix-r1.*`
- 前轮报告: `VERIFY-ITEM-024.md`（commit 93e625a core 权威轮）；`docs/hmi/ui_review/REVIEW-ITEM-024.md`（D-01..D-04 来源）

**终态 TEST_PASS**（范围：ITEM-024 交付项 vs 工单判据 + UI 评审 D-01..D-04 修复复核；QML 运行期视觉/交互仍受 E-01b 边界约束，见 O-3）。
