# VERIFY-ITEM-007 — railhmi-core 持久化与保留策略模型（软件验证 TestEvidence，v2 修复轮）

- 被测交付: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-007`（commit **7f517b8**，v2 修复轮，`embedded_linux_engineer.desktop_app_implementation` IMPLEMENTED）
- 被测对象: `app/src/core/{include/railhmi/core/persistence_model.h, include/railhmi/core/retention.h, include/railhmi/core/timebase.h, src/persistence_model.cpp, src/retention.cpp, src/timebase.cpp}`、`app/src/platform/{CMakeLists.txt, include/railhmi/platform/{sqlite_store.h, repository.h}, src/{sqlite_store.cpp, repository.cpp}}`、`app/third_party/sqlite/sqlite3_min.h`、`app/tests/core/test_persistence.cpp`
- 判据来源: 工作项描述 + TEST_DESIGN **TC-CORE-06**（ENV-A0）；DESKTOP_APP_DESIGN §10.3（存储策略）/§11（持久化设计）；DATA-001
- 验证端: RK3588 板端本机（192.168.0.103，aarch64，gcc 11.4；经登记 Paramiko SSH 入口）+ 主机静态核对
- 结论: **TEST_PASS**（本范围独立证据：上一轮 TEST_FAIL 的 D-01/D-02 缺陷已修复并复现验证；主套件 243/243、扩展夹具 55/55 全绿；详细逐条核对见 §2，过程轮与自查更正见 §4/§6）

## 1. 验证方法（独立于实现的证据链）

1. **静态核对**: 逐行审阅 commit 7f517b8 相对 commit fe28250 的修复 diff（`retention.cpp` `add_gap`、`retention.h` 契约声明、`test_persistence.cpp` 新增回归）与上一轮 TEST_FAIL 证据 `VERIFY-ITEM-007.md`（前版，编号沿用本文档）的 D-01/D-02 复现输入——修复方案（迭代吸收 + reason 先存非空 + 闭合区间相邻 ≤1ms）与建议修复边界一致，接口签名未变。
2. **板端独立复跑**（验证自有运行器 `verify_persist_fixtures.py` 打包上传 HEAD `app/` 快照 65 文件至板上独立目录 `/tmp/railhmi_verify007_board`，`bash build.sh CoreTests`，全超时 1200s；板端无 libsqlite3-dev，走运行时库 `libsqlite3.so.0` + shim 头路径，CMake 日志确认 `persistence enabled`）: 构建 `BUILDEXIT=0`；`railhmi-core-tests` **243 passed / 0 failed（TESTEXIT=0）**，[PASS]/[FAIL] 行与实现者 r3 log（`build-CoreTests-...-ITEM007-r3.log`）**243/243 程序化逐行对比完全一致**；`config-check good → CFG_EXIT=0`、`bad → CFG_EXIT=1` 报 `thresholds.confirm_frames: required field missing`（逐字一致）。板端 host SSH 指纹与实现者 r3 transcript 一致（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`）。
3. **验证自有扩展夹具**（源码 `verify_persist_ext.cpp` v7，g++ 直编 `persistence_model.cpp retention.cpp timebase.cpp sqlite_store.cpp repository.cpp`，链接 `/lib/aarch64-linux-gnu/libsqlite3.so.0`，`-Wall -Wextra` 零告警）: **55 断言，55 通过 / 0 失败**，覆盖 SIGKILL WAL 恢复 / 保留边界与 tie / 缺口合并契约（v2 语义）/ DDL 一致性 / repo 语义。
4. **运行器安全版式**: 受信任 known_hosts 指纹不匹配即中止（RejectPolicy，无 AutoAddPolicy）；connect/auth/banner/SFTP 全显式超时；凭据只读环境变量 `TRAIN_RK3588_SSH_CREDENTIAL` 不落日志；全部 `/tmp` 目录按名清理并在 transcript 留痕（cleanup 步骤 exit=2 为末尾 `ls` 对已删目录的确认性输出，非失败）。

## 2. 判据逐条核对（工作项描述 / TC-CORE-06）

| 判据 | 结果 | 证据 |
|---|---|---|
| 9 张表建表与读写一致 | ✅ | 主套件 `9 tables populated (segments=2)`、`write-restart-read consistent` 板端独立复现（243/243，逐行一致）；夹具 `fx ddl: 9 tables with design §11.1 column counts`（events 15/event_targets 6/maintenance_log 7/device_health 8/recording_segments 8/recording_gaps 5/stream_events 6/integrations 8/config_kv 4 列）核对 DDL 与 §11.1 一致 |
| 写入→重启→读取一致 | ✅ | `reopen after restart` + `write-restart-read consistent`（含双时间戳、置信度回读）；夹具 `fx repo: explicit event id preserved` / `record_available=false round-trips` / `duplicate PK reported as failure` / `config_kv upsert overwrites` |
| SQLite（WAL） | ✅ | `repo journal_mode == wal`；夹具 + 主套件均为**真实 SIGKILL 写中终止**口径（见下行） |
| 写中进程终止（WAL 恢复） | ✅ | 上一轮夹具已用真实 `fork()+SIGKILL` 补强（未提交事务行不可见、已提交行完好、journal_mode 仍 `wal`）；本轮实现者按 D-03 建议把它升级为第一方覆盖（`crash: reopen after SIGKILL` 等 4 断言）+ 上committed 后 kill 的 WAL replay 路径；夹具独立复现 8+3 断言全 PASS |
| UTC 毫秒 + 单调微秒双时间戳成对 | ✅ | 主套件 timebase 8 断言 + `ts_wall_ms/ts_mono_us` 成对插入读取；夹具 DDL 断言 4 张事件类表成对列存在（设计 §11.1 口径） |
| 保护片段优先、普通按容量/周期淘汰顺序正确 | ✅ | 主套件 age/capacity/critical 优先全 PASS；夹具严格边界（恰达期限不淘汰/+1ms 淘汰、恰达容量不淘汰、同 t0 tie 淘汰顺序按 continuous-first→id、days=0、空输入、incomplete 片段同规则）全 PASS |
| 保留淘汰顺序正确（DB 执行） | ✅ | `repo retention evicts continuous first` / `keeps critical segment` + 夹具 live DB age 策略 5 断言 |
| 缺口可查 | ✅ | `gaps queryable by camera`、`gap persisted across restart`；夹具空过滤器全量有序 + 相机过滤隔离 PASS |
| 缺口合并语义（上一轮 D-01/D-02） | ✅ **已修复** | `retention.h` v2 契约（重叠或相邻=相差≤1ms 即合并、**吸收所有**可合并区间、reason 先存非空）；主套件新增 4 组回归 PASS；夹具 `fx gaps: bridging interval merges ALL pieces`（桥接 + 被包含区间→单区间无残对）、`up-to-1ms proximity merges`、`>=2ms hole stays separate`、`bridge absorbs three pieces`、`merged reason = first non-empty in scan order`、`empty existing reason filled` 全 PASS |

## 3. 上一轮缺陷修复验证（对照前版 TEST_FAIL 证据）

**版本**: commit 7f517b8（`7f517b860432ee296fe7aed361b2515bfda91766`，65 文件 app 快照上传至板端）。

### D-01 `add_gap` 只合并到第一个重叠/相邻区间，桥接缺口留下永久重叠残对 → **已修复**
- 前轮复现输入: 同相机 `C1` 依次 `add_gap(100,300)`、`add_gap(50,99)`、`add_gap(99,110)`，预期单缺口 `[50,300]`，实际 2 个互相重叠的缺口。
- 本轮验证: 复现序列被实现者写为第一方回归（`verify sequence: single merged gap` + bounds `[50,300]` + reason `drop`，r3 中 PASS）；验证夹具用双向扩展独立复现——桥接（相邻 `[50,98]` + 包含 `[100,300]`）、三段桥接（`[0,10]/[20,30]/[40,50]` 被 `[5,45]` 桥接为单区间 `[0,50]`）→ 均为**单区间、无重叠残对**，且按 t0 排序不变。
- 根因修复核对（源码定位）: `app/src/core/retention.cpp` `add_gap` 改为迭代吸收（`while changed` 反复扫描所有同相机区间直至一轮无新增吸收），early-return 已消除；结果按 `(cam, t0, id)` 重排。

### D-02 合并未继承第二条既有区间的 reason → **已修复**
- 前轮复现输入: 同 D-01 场景，合并后 reason 不连续。
- 本轮验证: reason 保序规则被实现为「先存在的非空值优先，否则用入参」；验证独立复现三条：桥接三段取第一段非空 reason `a`（并发起段为空也正确）；桥接两段取 t0 序第一非空 reason `x`；同相机既有区间 reason 均空时由新入参 `later` 填充。合并后单区间单 reason，语义连续，两侧残对的 reason 丢失问题不再存在。
- 口径说明（观察项 O-03，见 §5）: `retention.h` 文本「最先存在的非空值」在桥接场景对「存在次序」（创建序 vs t0 序）有措辞歧义；实现者 README §7 已明确为「扫描顺序下第一个既有非空」，第二、三条夹具断言验证了两种场景下结果均为确定性输出且语义连续。按声明口径核对一致；创建序还是扫描序更符合产品语义为设计确认事项，不构成本期业务缺陷。

### D-03（前轮观察项，非缺陷）主套件 WAL 恢复为采样复制口径 → 实现者已升级
- 第一方新增 `test_wal_crash_recovery`（真实 `fork()+SIGKILL`，未提交事务丢弃、已提交行完好、journal_mode 仍 wal），夹具独立复现一致。前轮口径差异已消除。

## 4. 验证侧自查更正（保持证据链诚实）

- 本轮扩展夹具从 v5（51 断言）扩展为 55 断言（v2 契约新语义下重建缺口边界用例）。**v6 过程轮 53/55**，2 失败均为**验证侧断言期望构造错误**（对照源码与实现者 README §7 声明口径确认，非被测对象问题，v6 留痕 §6）:
  1. `empty existing reason filled` 用了相差 2ms 的 `[10]..[12]` 造合并场景，但 v2 契约下相差 ≥2ms 本应保持分离（实现行为正确）——改为相差 1ms 的 `[11]..[20]` 后正确覆盖位相语义。
  2. `first existing reason` 在桥接场景期望了**创建序**的 reason（`drop`），而声明口径是**扫描（t0）序**（`x`）——按 README §7 口径修正期望，并把契约措辞歧义登记为 O-03。
- v1–v3 轮已在前版 VERIFY 登记过（验证侧编译错误，非板面结论）。本轮无其它过程轮。
- cleanup 步骤 exit=2 为末尾 `ls` 对已删目录的确认性输出（`fixture_cleanup_done` 在前），非清理失败。

## 5. 验证范围与方法边界 / 观察项（非缺陷，交设计或实现参考）

- **O-03**: `retention.h` add_gap 注释「reason 取最先存在的非空值」未写明「存在次序」的时间定义；README §7 已自洽（扫描序）。建议实现者在下一轮注释中把该定义写入 `retention.h`，或由设计确认创建序语义后由实现方调整（改动点局限 `add_gap` 单函数，不影响存储/查询）。
- **O-04**: `recording_segments.kind`/`recording_gaps.reason` 列类型与设计 §11.1 核对一致，两表无 UNIQUE/外键约束；跨表约束非本期判据，留设计确认事项，不改变本层结论。
- 验证范围 = railhmi-core 持久化与保留模型（ENV-A0 板端本机 aarch64）；多相机/多进程并发写、 `/tmp` 之外的电量/断电场景、真实断电持久化属于 TC-RECOVERY-04（ENV-E 实机边界），不在本范围——同类范围注释沿用 TEST_DESIGN ENV 分层口径，不对本层结论外推。

## 6. 复现

```sh
# 验证自有运行器（独立复现 CoreTests 243/243 + config-check 好/坏 + 扩展夹具 55/55）
# 需先加载 TRAIN_RK3588_SSH_CREDENTIAL；MSYS 前缀禁用路径转换
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" python \
  docs/hmi/test_evidence/ITEM-007-persistence/verify_persist_fixtures.py \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --expected-fingerprint "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" \
  --app-root app \
  --cpp docs/hmi/test_evidence/ITEM-007-persistence/verify_persist_ext.cpp \
  --outdir docs/hmi/test_evidence/ITEM-007-persistence \
  --stamp verify-item007-persist-20261015-v7 \
  --workdir /tmp/railhmi_verify007
# 板上直接: cd /tmp/railhmi_verify007_board/app && ./build-core/tests/railhmi-core-tests
#           g++ -std=c++17 -O0 -Wall -Wextra -Isrc/core/include -Isrc/platform/include \
#           -Ithird_party/sqlite -DRAILHMI_HAVE_SQLITE=1 \
#           verify_persist_ext.cpp src/core/src/{persistence_model,retention,timebase}.cpp \
#           src/platform/src/{sqlite_store,repository}.cpp \
#           -L/lib/aarch64-linux-gnu -l:libsqlite3.so.0 \
#           -o verify_persist_ext && ./verify_persist_ext
# 收尾（运行器已自动按名清理；如残留）
rm -rf /tmp/railhmi_verify007 /tmp/railhmi_verify007_board /tmp/railhmi_cfg_bad
```

## 7. 证据文件与轮次留痕

| 文件 | 说明 |
|---|---|
| `VERIFY-ITEM-007.md` | 本文（TestEvidence，v2 修复轮重写，TEST_PASS） |
| `verify_persist_fixtures.py` | 验证自有板端运行器（指纹校验/全超时/环境变量凭据/按名清理） |
| `verify_persist_ext.cpp` | 验证自有扩展夹具源码 v7（55 断言：SIGKILL WAL 恢复/保留边界/缺口合并 v2 契约/DDL 一致性/repo 语义） |
| `VERIFY-FIXTURES-verify-item007-persist-20261015-v7.{transcript.json,log}` | 本轮权威轮 transcript（指纹/逐条命令/退出码/清理确认；CoreTests 243/243 与实现者 r3 程序化逐行一致 + 扩展夹具 55/55） |
| `VERIFY-FIXTURES-...-v6.{transcript.json,log}` | 过程轮留痕（53/55，2 失败为验证侧期望构造错误，§4 已更正；非被测对象结论） |
| `VERIFY-FIXTURES-...-v1..v5.{transcript.json,log}`、前版 `VERIFY-ITEM-007.md` | 上轮（fe28250）验证过程与 TEST_FAIL 留痕（被本文档取代为当前版本判据；保留追溯）  |
| `build-CoreTests-…-ITEM007-r1/r2/r3.…`（实现者） | 实现者交付证据（r3 权威：243/243 + config-check；两轮逐行一致） |

## 8. 判据维持说明（不改变验收口径）

- 验证采用与实现者 r3 同版本 HEAD（commit 7f517b8 `app/` 快照，上传 65 文件）+ 相同构建层级（板端 aarch64 本机）。未新增也未放宽验收判据；本轮判定依据是 `retention.h` 修订后的声明契约（v2）、设计 §11.1 持久化口径与工作项文本三者的并集，与上一轮 D-01/D-02 判据同源（上一轮判据是缺口合并契约的"吸收所有可合并区间 + reason 连续"面，本轮按修订后契约语义核对同一行为面）。
- 主套件 243 [PASS] 行经程序化逐行对比与实现者 r3 完全一致，作为独立第三方复现记录成立；工作项「写入-重启-读取一致、WAL 恢复、保留淘汰顺序正确且缺口可查」全项成立（缺口合并面在 v2 修复轮已由第一方 + 验证夹具双向覆盖）。
- 板端 SSH host 指纹五轮一致（实现者 r1/r2/r3 与验证 v6/v7），证据链完整。
