# VERIFY-ITEM-022 — 设备维护页（software_verification 权威轮·修复复核）

- 工单: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-022`
- 能力: `software_tester.software_verification`
- 审核对象: 实现 fix 轮 commit `f6acafb`（实现者 stamp `20261016-ITEM022-fix-r1`，板端 617/0）
- 验证章: `VERIFY-20261016-v6`（权威轮；本单历史: v3 权威轮 TEST_FAIL（D-01/D-02）→ 实现 fix 轮 → v4/v5 复核过程轮（验证侧夹具两处期望出错，非产品缺陷，见 §5）→ v6 全绿）
- 上一轮失败项复查: **D-01 手动重连入口、D-02 按日滚动维护日志 → 修复确认满足**
- 结论: **TEST_PASS**

## 1. 验收范围与判据来源

ITEM-022：设备维护页——摄像头在线/码流/帧率/最近帧/质量、AI 状态与模型版本、录像进程与剩余容量、网络错误计数与温度、云台 Pan/Tilt/Zoom 实际值与目标值及容差校验、重连入口与按日滚动的维护日志；NVR 以设备卡显示登记/可达/未配置状态与通道映射（未提供时显示未配置，不虚构通道号，不提供自动修改入口）；一键归位与重新调用前方预置位并二次确认；调用后显示目标值与实际值及容差结果，超差显示失败并令前方 AI 未就绪，操作写入维护日志。判据来源: 工单原文 + `docs/hmi/INTERACTION_DESIGN.md` v2 §5.6（含"重连：支持手动重连"；"维护日志：…记录时间/用户/结果"）+ §7.4 + `docs/hmi/VISUAL_DESIGN.md` v2 §8.6 + `docs/hmi/TEST_DESIGN.md`（TC-PTZ-01/02、TC-HLT-*、TC-INTG-01）。对 ui-review 轮及 v3 已核判据全量保留回归，未放松任何判据。

## 2. 上一轮（v3 TEST_FAIL）缺陷修复核对

- **D-01（P2）手动重连入口 — 已修复**：`P6Maintenance.qml` 每路摄像头行新增 `FilterChip objectName:"reconnect-<id>"`（"重连"）→ `doReconnect(cameraId)` → `Q_INVOKABLE RailhmiMaintenanceModel::reconnect(camera_id)`（maintenance_model.{h,cpp}）：执行 + 计数 + 写维护日志 `action=reconnect target=<cam> result=ok detail="手动重连"`，P6 同步消费 `reconnectText` 并显示 `reconnectNotice` 反馈。源码级 + 板端扫描 `D1_manual_reconnect_entry_present=True`；日志行为独立公式断言命中。
- **D-02（P3）按日滚动维护日志 — 已修复**：core 新增 `log_day_key(ts)`（UTC `YYYY-MM-DD`，复用 `format_time_text` 前 10 位）与 `roll_logs_by_day(logs, max_days)`（仅保留最近 N 个日键，保持时间序）；桥接 `Q_INVOKABLE logsByDay(max_days=7)` 按日分组（day/count/entries）+ `logDayCount`；P6 日志卡标题改为"维护日志（按日滚动）"，`maintLogDay` 日头（day+count）+ 内层逐行。源码级 + 板端扫描 `D2_log_day_rolling_present=True`；probe 独立断言 `type=log-days total=4 rolled=3 max_days=1 day_key=2025-10-09`（1760000000000 ms = 2025-10-09 UTC，旧条目 2025-10-04 被滚动淘汰）。
- 修复余项沿用 v3 结论：core 语义（PTZ 容差含 ≤ 边界、动作三态日志、前方 AI 真值表、维护行 stale/缺失运行时/质量组合、录像/健康格式化）经 v6 ext 116 项全量回归无变化（§4）。

## 3. 独立复跑结果（v6 全绿，FIXTURES_RC=0）

| # | 步骤 | 结果 |
|---|------|------|
| 1 | 指纹校验 | `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU` 与登记一致 |
| 2 | 交付入口 `board_build.py --mode TestRun --keep-remote` 独立复跑 | `TESTRUN_EXIT=0`、`MAINT_WIRE_EXIT=0`、`MAINT_EXIT=0`、其余探针 EXIT=0 |
| 3 | core 单测逐行比对 | impl(fix-r1) 617 行 == verifier 617 行，`identical=True`，fails=0，`RESULT: PASS passed=617 failed=0` |
| 4 | JUnit 独立解析 | `tests=617 failures=0`；ITEM-022 精确用例名 16/16 命中（含 fix 新增 5 例: log day key (D-02)、roll keeps latest 2 days (D-02)、rolled entries are the most recent days、max_days=0 keeps all、generic maintenance log (reconnect)） |
| 5 | maintenance-probe 公式断言（预言独立推导，非转录实现者文本） | fails=[]（ptz-check Δ0.20/0.30/0.05→容差内、Δ4.00→超差；两动作 confirm=true applied=true；home/preset_front/reconnect 日志行；`log-days total=4 rolled=3 max_days=1 day_key=2025-10-09`；front-ai READY/NOT_READY；cam 行实时/fps/最近帧/质量；recording 75%; health 文本；acceptance 五布尔含 reconnect_logged=true log_day_rolling=true；反伪守卫: rolled=4 与 reconnect_logged=false 必须缺席） |
| 6 | maintenance-probe 跨轮字节一致 | 与实现者 fix-r1 归档逐行 `identical=True`；二跑 diff `IDENTICAL` |
| 7 | 验证侧 ext 夹具（`verify_maint22_ext.cpp` 链接本轮自产 `librailhmi_core.a`） | **116/116**（v3 全部 83 项回归 + fix 新增 33 项: 通用维护日志列契约/不篡改 action 与 result、与 build_ptz_log 等价性、log_day_key 验证侧独立 civil-from-days 历法复算（2025-10-09/ leap 2024-02-29/ 年界/ 日界首毫秒尾毫秒/ ts≤0 占位/ 与 parse_date_utc_ms 回程一致）、roll_logs_by_day 整日滚动（6 条 4 日 max_days=2→3 条仅含 15/16 日、13/14 日整日剔除、保留时间序、max_days=1 只留当日、0/-1 全保留无误删、00:00.000 归次日语义、probe 场景形状同构）） |
| 8 | QML/桥接接线扫描（25 项，上板对 fresh 副本） | mismatch=[] 全 True（v3 22 项回归 + T4 改判 logsByDay 按日分组渲染 + T8_reconnect_text_consumed_in_p6=True + D1/D2 双成立；NVR 无自动修改入口、tokens-only 配色不变） |
| 9 | 确定性 | 重跑 maintenance-probe diff `IDENTICAL` |
| 10 | 回归留痕 | 同轮 core-evidence 含 status-probe(+driver)/sim-inject/alarm-probe/clip-probe/replay-probe/ai-probe/maintenance-probe 全套 |
| 11 | 清理 | `/tmp/railhmi_verify022`、`/tmp/railhmi_build`、`/tmp/railhmi_clips` 按名删除，`cleanup_ok=True`（ls exit=2 为删除确认） |

本单两条 TEST_FAIL 缺陷已按工单原文核对满足；其余判据（PTZ 目标 vs 实际 + 容差、超差 → 前方 AI NOT_READY、写日志、NVR 未配置呈现 + 无自动修改入口、分区卡全字段）经本轮与 v3 双轮证据保持成立。

## 4. 复核用例要点（ext fix 新增，全部为验证侧独立推导）

- **等价性**：`build_ptz_log(home,…)` 与 `build_maintenance_log(home.kind, "ptz", …)` 逐列相等——修复仅是列透传，无新行为分支。
- **不篡改**：`build_maintenance_log("param_change",…, "unverified",…)` 的 action/result 原样透传；空 user 保持空。
- **log_day_key 历法复算**：验证侧自带 Howard Hinnant civil-from-days 算法（不信任交付 formatter）；覆盖 2025-10-09（probe 时间戳）、2024-02-29 闰日、2025-12-31→2026-01-01 年界、某日 23:59:59.999→前一日、00:00:00.000→当日、`ts<=0` 返回占位符（与其 time_text 口径一致）并文档化；与交付 `parse_date_utc_ms` 回程一致。
- **整日滚动**：滚动以"日"为原子——最多去 Days 而非去 Rows；day16 首毫秒条目 `00:00.000` 与日尾毫秒 `86399999` 同属该日并被同日保留；`max_days=0` 与负值全保留（文档化哨兵，无误删风险）。
- **日志显示层**：P6 `logsByDay(7)` 分组渲染（day+count 表头 + 行内 action/user/result/detail 四列），接线扫描逐字段断言。

## 5. 复核过程轮记录（D-注记：全部为验证侧夹具错误，非产品缺陷）

- v4：ext 新函数 `t_log_day_key`/`t_roll_logs_by_day` 直接调用 `parse_date_utc_ms` 但未包含 `railhmi/core/alarm_center.h`（GXX_EXIT=1，与交付核心测试无涉——交付测试显式 include 了该头）。补 include 后复跑。
- v5：ext 116/1——`roll 6 logs/4 days max_days=2 -> 4 rows` 是验证侧算术错误：day15 有 1 行 + day16 有 2 行 = **3 行**，非 4。板端单点失败证据 `FAIL: roll 6 logs/4 days max_days=2 -> 4 rows`，修正后离线自查 8/8 再上板。两轮均无产品缺陷触发。
- 上板前离线自查（`offline_selfcheck4.py`，零成本）：① oracle 接受 fix-r1 归档 probe 日志；② oracle 对 pre-fix r1 归档负例复现 3 处 missing line（证明断言对缺失项敏感反向可用）；③ SCAN_PY 本地 remap dry-run（`open()` 映射 + 单命名空间，25 键全 True 含 D1/D2）;④ 逐位置审计验证侧 `\xHH` 转义无贪婪吞并隐患。v4 的 include 缺失与 v5 的行数算错均不在其离线可检范围（缺少板端编译/链接与本地 C++ 运行时），如实登记。

## 6. 权威证据文件

- `VERIFY-FIXTURES-VERIFY-20261016-v6.log` / `.transcript.json` — 权威轮逐步输出/指纹
- `build-TestRun-192_168_0_103-VERIFY-20261016-v6.*` — 本轮独立复跑归档（core-tests 617 行 + JUnit XML + maintenance-probe + 回归全套）
- `verify_maint22_ext.cpp`（116 项含 fix 复核 33 项）/ `verify_maint22_fixtures.py` / `run_verify_round.sh` / `offline_selfcheck4.py` — 验证侧夹具与司机
- `VERIFY-ITEM-022.md` 上一版内容并入本 §1/§2/§5（三权威轮历史: v3 TEST_FAIL → fix → v6 TEST_PASS；过程轮 v4/v5 留痕同目录）
- 过程轮留痕: v4/v5 FIXTURES 日志（含 compile-ext 失败与 run-ext 单点失败输出）

**终态 TEST_PASS。** 维护日志持久化到 SQLite（`maintenance_log` 表）仍属实现声明的"后续集成"边界，与 v3 同记为观察项，不在本工单验收层放水或加判据。
