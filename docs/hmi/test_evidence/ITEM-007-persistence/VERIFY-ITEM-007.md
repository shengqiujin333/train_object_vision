# VERIFY-ITEM-007 — railhmi-core 持久化与保留策略模型（软件验证 TestEvidence）

- 被测交付: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-007`（commit **fe28250**，`embedded_linux_engineer.desktop_app_implementation` IMPLEMENTED）
- 被测对象: `app/src/core/{include/railhmi/core/persistence_model.h, include/railhmi/core/retention.h, include/railhmi/core/timebase.h, src/persistence_model.cpp, src/retention.cpp, src/timebase.cpp}`、`app/src/platform/{CMakeLists.txt, include/railhmi/platform/{sqlite_store.h, repository.h}, src/{sqlite_store.cpp, repository.cpp}}`、`app/third_party/sqlite/sqlite3_min.h`、`app/tests/core/test_persistence.cpp`
- 判据来源: 工作项描述 + TEST_DESIGN **TC-CORE-06**（ENV-A0）；DESKTOP_APP_DESIGN §10.3（存储策略）/§11（持久化设计）；DATA-001
- 验证端: RK3588 板端本机（192.168.0.103，aarch64，gcc 11.4；经登记 Paramiko SSH 入口）+ 主机静态核对
- 结论: **TEST_FAIL**（本范围独立证据：主套件与设计符合性全绿，但保留/缺口模型存在 2 个已复现缺陷 D-01/D-02；复现输入、实际观察、预期与建议修复边界见 §3）

## 1. 验证方法（独立于实现的证据链）

1. **静态核对**：逐行审阅 commit fe28250 的 `retention.cpp`/`repository.cpp`/`persistence_model.cpp`/`timebase.cpp`/`test_persistence.cpp` 与 DESKTOP_APP_DESIGN §11.1 九表列清单（见 §2 DDL 断言行）。
2. **板端独立复跑**（验证自有运行器 `verify_persist_fixtures.py` 打包上传 HEAD `app/` 快照 65 文件至板上独立目录 `/tmp/railhmi_verify007_board`，`bash build.sh CoreTests`，全超时 1200s；板端无 libsqlite3-dev，走运行时库 `libsqlite3.so.0` + shim 头路径，CMake 日志确认 `persistence enabled`）：构建 `BUILDEXIT=0`；`railhmi-core-tests` **230 passed / 0 failed（TESTEXIT=0）**，[PASS] 行与实现者 r2 log（`build-CoreTests-...-ITEM007-r2.log`）**230/230 逐行一致**（程序化逐行对比）；`config-check good → CFG_EXIT=0`、`bad → CFG_EXIT=1` 报 `thresholds.confirm_frames: required field missing`（与 r2 逐字一致）。
3. **验证自有扩展夹具**（源码 `verify_persist_ext.cpp`，g++ 直编 `persistence_model.cpp retention.cpp timebase.cpp sqlite_store.cpp repository.cpp`，链接 `/lib/aarch64-linux-gnu/libsqlite3.so.0`，`-Wall -Wextra` 零告警）：**51 断言，49 通过 / 2 失败**，失败即 §3 D-01/D-02 的可复现缺陷证据（v1–v5 过程轮详见 §6）。
4. **运行器安全版式**：受信任 known_hosts 指纹不匹配即中止（RejectPolicy，无 AutoAddPolicy）；connect/auth/banner/SFTP 全显式超时；凭据只读环境变量 `TRAIN_RK3588_SSH_CREDENTIAL` 不落日志；全部 `/tmp` 目录按名清理并在 transcript 留痕。

## 2. 判据逐条核对（工作项描述 / TC-CORE-06）

| 判据 | 结果 | 证据 |
|---|---|---|
| 9 张表建表与读写一致 | ✅ | 主套件 `9 tables populated (segments=2)`、`write-restart-read consistent` 板端独立复现（230/230，逐行一致）；夹具 `fx ddl: 9 tables with design §11.1 column counts`（events 15/event_targets 6/maintenance_log 7/device_health 8/recording_segments 8/recording_gaps 5/stream_events 6/integrations 8/config_kv 4 列）核对 DDL 与 §11.1 一致 |
| 写入→重启→读取一致 | ✅ | `reopen after restart` + `write-restart-read consistent`（含双时间戳、置信度回读）；夹具 `fx repo: explicit event id preserved` / `record_available=false round-trips` / `duplicate PK reported as failure` |
| SQLite（WAL） | ✅ | `repo journal_mode == wal`；夹具真实 **SIGKILL 写中终止** 复现（见 D-03 备注） |
| 写中进程终止（WAL 恢复） | ✅（测试口径已超第一方） | 主套件为「运行中复制 db+wal 后重放」的间接口径；夹具以真实 `fork()+SIGKILL` 补强：未提交事务行不可见、已提交行完好、journal_mode 仍 `wal`（3+2 断言全 PASS） |
| UTC 毫秒 + 单调微秒双时间戳成对 | ✅ | 主套件 timebase 8 断言 + `ts_wall_ms/ts_mono_us` 成对插入读取；夹具 DDL 断言 4 张事件类表成对列存在（设计 §11.1 口径） |
| 保护片段优先、普通按容量/周期淘汰顺序正确 | ✅ | 主套件 age/capacity/critical 优先全 PASS；夹具严格边界（恰达期限不淘汰/+1ms 淘汰、恰达容量不淘汰、同 t0 tie 淘汰顺序按 id、days=0、空输入、incomplete 片段同规则）全 PASS |
| 保留淘汰顺序正确（DB 执行） | ✅ | `repo retention evicts continuous first` / `keeps critical segment` + 夹具 live DB age 策略 5 断言 |
| 缺口可查 | ✅（查询）/ ❌（合并语义见 D-01） | `gaps queryable by camera`、`gap persisted across restart`；夹具空过滤器全量有序 + 相机过滤隔离 PASS；**但合并后存在重叠残对（D-01）** |

## 3. 缺陷（按 TEST_FAIL 证据要求：版本、复现输入、实际观察、预期、建议修复边界）

**版本**: commit fe28250（`fe282507b5bc6a49c767aad70308c038c04613dd`，64 文件 app 快照上传至板端；能力交付引用构建产物时间 2026-10-15 23:37 见实现者 r2 transcript）。
**复现**: `docs/hmi/test_evidence/ITEM-007-persistence/VERIFY-FIXTURES-verify-item007-persist-20261015-v5.transcript.json` → `run-ext`；夹具源 `verify_persist_ext.cpp` `test_gap_edges()`。

### D-01 `add_gap` 只合并到第一个重叠/相邻区间，桥接缺口留下永久重叠残对（缺陷，修复责任: 实现）
- **复现输入**: 同相机 `C1` 依次 `add_gap(100,300)`、`add_gap(50,99)`、`add_gap(99,110)`（第三条与既有两条 gap 分别重叠/相邻）。
- **预期**（`retention.h` 声明契约"缺口按相机合并重叠/相邻区间，保持按 t0 排序"+ 模型语义"任何录像中断区间合并"）: 第 1 个 gap，`[50,300]`，reason 取先存在的非空值。
- **实际**: **2 个 gap**，`[50,99]` 与「合并了 99..110 的 `[100,300]`」并存在案——同一相机时间轴上 `[99,100]` 之后仍记录两条互相重叠的缺口。
- **影响**: P3 时间轴斜纹取重叠并集呈现与缺口区间查询语义失真（TC-REC-03 / FI-10 直接消费 `recording_gaps`，回放页 ITEM-021 为下游）；时间轴留白按 gap 区间画会因重叠残对产生重复计数/错误覆盖关系。
- **根因**（源码定位）: `app/src/core/retention.cpp:62-86` `add_gap` 在 for 循环内 merge 到第一个命中区间后 `return`，未继续扫描并吸收后续可合并区间。
- **建议修复边界**: 收敛为两遍循环/单遍递归吸收（merge 后不 return，从合并后的区间继续吸收后续命中，直到无重叠），并保持按 t0/cam 排序不变量；不影响 `SqliteRepository::insert_gap` 与查询接口签名。

### D-02 合并未继承第二条既有区间的 reason（缺陷加重 D-01，修复责任: 实现）
- **复现输入**: 同 D-01 场景；两条既有 gap 的 reason 分别为 `drop`（[100,300]，先存）与 `x`（[50,99]，后插入）。
- **预期**: 合并结果 `[50,300]`，reason 保留先存在的非空语义标记 `drop`。
- **实际**: 桥接 merge 只吸收了 `[100,300]` 一侧，`[50,99]` 的 `x` 未被继承或丢弃（结果仍为 2 gaps 时两 reason 各为一侧；合并成功路径下先到先得）。
- **影响**: 缺口原因标注在 bridge 合并后语义不连续（回放页/维护排查按 reason 过滤会漏记一侧）。
- **根因**: 同 D-01 的 early-return 结构——只访问一个既有 gap，另一个的 reason 永不参与合并。
- **建议修复边界**: 与 D-01 同一修复点；合并时聚合参与合并区间的既有 reason（如保留最先存在的非空值）。

### D-03（观察，非缺陷）主套件 WAL 恢复为采样复制口径
- 主套件 `WAL replay` 是"连接打开期间复制 db+-wal 再开副本"的采样视角，验证了**已提交**数据在 WAL 中可重放，但未覆盖「未提交事务写中终止」的不可见性与真实 kill。夹具（真实 `fork()+SIGKILL`）补强后未提交事务不可见、已提交行完好、`journal_mode` 仍 wal 全部成立，故判定为**口径差异而非缺陷**。真实断电恢复仍属 ENV-E 现场/嵌入式边界（TEST_DESIGN TC-RECOVERY-04）。

### O-02（观察，非缺陷）`recording_segments.kind`/`recording_gaps.reason` 列类型核对一致，两表无 UNIQUE/外键约束
- 按设计 §11.1 列清单核对一致（kind 枚举 round-trip 有断言）；跨表约束非本期判据，留设计确认事项，不改变本层结论。

## 4. 验证侧自查更正（保持证据链诚实）

- v1/v2/v3 轮 GXX_EXIT=1 均为**验证侧夹具编译错误**（`std::raise` 不存在、`using namespace railhmi` 后漏 `core`），非被测对象问题，逐轮留痕 §6；v4 轮 3 失败中 2 处为**验证侧断言期望错误**（capacity `retained_bytes >= 29` 单位/停止条件弄错、bridge reason 检查）“先修”——本轮 v5 措辞按合约定案后仅剩余 D-01/D-02 两失败，全部与源码 `add_gap` early-return 逐一对应。
- cleanup 步骤 exit=2 为末尾 `ls` 对已删目录的确认性输出（`fixture_cleanup_done` 在前，`ls` 报 no such file 即清理成功证据），非清理失败。

## 5. 复现

```sh
# 验证自有运行器（独立复现 CoreTests 230/230 + config-check 好/坏 + 扩展夹具 49/51）
# 需先加载 TRAIN_RK3588_SSH_CREDENTIAL；MSYS 前缀禁用路径转换
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" python \
  docs/hmi/test_evidence/ITEM-007-persistence/verify_persist_fixtures.py \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --expected-fingerprint "SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU" \
  --app-root app \
  --cpp docs/hmi/test_evidence/ITEM-007-persistence/verify_persist_ext.cpp \
  --outdir docs/hmi/test_evidence/ITEM-007-persistence \
  --stamp verify-item007-persist-20261015-v5 \
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

## 6. 证据文件与轮次留痕

| 文件 | 说明 |
|---|---|
| `VERIFY-ITEM-007.md` | 本文（TestEvidence，TEST_FAIL：D-01/D-02 已复现缺陷） |
| `verify_persist_fixtures.py` | 验证自有板端运行器（指纹校验/全超时/环境变量凭据/按名清理） |
| `verify_persist_ext.cpp` | 验证自有扩展夹具源码（51 断言：SIGKILL WAL 恢复/保留边界/缺口合并/DDL 一致性/repo 语义） |
| `VERIFY-FIXTURES-verify-item007-persist-20261015-v5.{transcript.json,log}` | 本轮权威轮 transcript（指纹/逐条命令/退出码/清理确认） |
| `VERIFY-FIXTURES-...-v1..v4.{transcript.json,log}` | 过程轮留痕（v1–v3 验证侧编译错误、v4 验证侧期望更正，非被测对象结论） |
| `build-CoreTests-…-ITEM007-r1/r2.…`（实现者） | 实现者交付证据（r2 权威：230/230 + config-check），引用为本轮逐行一致参照 |

## 7. 判据维持说明（不改变验收口径）

- 验证采用与实现者 r2 同版本 HEAD（commit fe28250 `app/` 快照，上传 65 文件）+ 相同构建层级（板端 aarch64 本机）。未新增也未放宽验收判据；D-01/D-02 判定依据分别是 `retention.h` 声明的缺口合并契约与设计 §11.1 持久化口径。
- 主套件 230 [PASS] 行经程序化逐行对比与实现者 r2 完全一致，作为独立第三方复现记录成立；工作项「写入-重启-读取一致、WAL 恢复、保留淘汰顺序正确」三大项在两侧证据中均成立——**缺口合并语义（D-01）是全项唯一不满足面**，且它直接是工作项文本"录像缺口记录模型"与 `retention.h` 声明契约的可复现违反，故本范围整体按契约返回 TEST_FAIL 交回实现修复 D-01/D-02。
