# ITEM-007 证据 — railhmi-core 持久化与保留策略模型

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-007`
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §10（持久化）/§9.3（存储策略）/§7.3；`docs/hmi/TEST_DESIGN.md` TC-CORE-06
- 源码: `app/src/core/{persistence_model,retention,timebase}`、`app/src/platform/{sqlite_store,repository}`、`app/third_party/sqlite/sqlite3_min.h`
- 测试: `app/tests/core/test_persistence.cpp`；运行器 `app/tools/board_build.py`

## 1. 交付内容

1. **数据模型与 schema**（`railhmi-core`，纯 stdlib）：9 张表的行结构与 `CREATE TABLE IF NOT EXISTS` DDL —
   `events / event_targets / maintenance_log / device_health / recording_segments / recording_gaps / stream_events / integrations / config_kv`。
2. **统一时间基准**（`TimeBase`）：单调微秒为内部基准，启动时与 UTC 墙钟毫秒建立偏移；
   `to_wall_ms`/`to_mono_us` 成对换算（DATA-001）。事件/健康/流事件表成对存 `ts_wall_ms` + `ts_mono_us`。
3. **保留策略**（`apply_retention`，纯逻辑）：先按期限淘汰过期**普通**片段，再按容量淘汰——
   **普通最旧优先**，仅当仍超容量才淘汰**保护（critical）**片段（最旧优先）；返回确定性淘汰顺序与计数。
4. **录像缺口模型**（`add_gap`）：同相机重叠/相邻缺口合并，按 t0 排序。
5. **SQLite 仓储**（`railhmi-platform`，`RAILHMI_HAVE_SQLITE`）：`SqliteStore`（WAL/同步/busy_timeout）+ `SqliteRepository`
   （9 表写入/查询、`count`、事件回读、缺口按相机查询、`apply_retention` 删除被淘汰片段）。
6. **SQLite 依赖处理**：板端只有运行时库 `libsqlite3.so.0`（无 `libsqlite3-dev` 的 `sqlite3.h`/`.so` 链接名）。
   CMake 优先用 dev 包；否则链接运行时库并使用项目自带最小头 `app/third_party/sqlite/sqlite3_min.h`
   （在存在系统 `<sqlite3.h>` 时自动优先系统头）。**不安装/不修改系统**。

## 2. 实测（板端本机，经登记 SSH 入口；SQLite 用运行时库 + shim）

| 步骤 | 结果 | 证据 |
|---|---|---|
| `./build-core/tests/railhmi-core-tests` | **243 passed / 0 failed**（平台/配置/流/规则/持久化） | `...-ITEM007-r3.transcript.json` → `tests` |
| `railhmi-config-check ./config` | OK 退出 0 | 同上 → `config-check-good` |
| 坏配置 | 报 `thresholds.confirm_frames: required field missing` 退出 1 | 同上 → `config-check-bad` |

CMake 日志确认：`railhmi: SQLite runtime lib /lib/aarch64-linux-gnu/libsqlite3.so.0 + shim header -> persistence enabled`。

### 2.1 持久化断言（r2 原文摘录）

- 时间基准：`timebase calibrated`、`to_wall_ms truncates to ms`、`wall/mono round-trip identity (ms-aligned)`。
- SQLite：`repo journal_mode == wal`、`9 tables populated (segments=2)`、`get_config round-trip`。
- 写入-重启-读取：`write-restart-read consistent`（事件按 id 回读，含双时间戳与置信度）、`gap persisted across restart`、
  `segments read back with kind`。
- **WAL 恢复**：`copy db for WAL replay` → `open WAL copy` → `WAL replay: event recovered from -wal` + `WAL replay: gap recovered`
  （运行中复制 db + `-wal` 再打开副本，验证 WAL 重放可见已提交数据）。
- 保留淘汰：`capacity: oldest continuous evicted first`、`continuous(11,12) then oldest critical(13)`、
  `repo retention evicts continuous first`、`repo retention keeps critical segment`。
- 缺口可查：`gaps queryable by camera`、`gap merge bounds 100..300`。

## 3. 修复历史

| 运行 | 现象 | 根因 | 修复 | 复测 |
|---|---|---|---|---|
| ITEM007-r1 | 1 项失败：`wall/mono round-trip identity` | 测试期望错误：毫秒分辨率下非整毫秒的单调值往返必然截断 | 断言改为显式截断 + 毫秒对齐往返恒等 | ITEM007-r2（230/230） |
| ITEM007-r3 | 软件验证 TEST_FAIL：D-01 桥接缺口留重叠残对、D-02 合并未继承 reason | `add_gap` 只合并到第一个命中区间就 `return`；且相邻判定用严格相等（99 与 100 未视为相邻） | `add_gap` 改为迭代吸收所有可合并区间（重叠或相差≤1ms），reason 取先存在非空值；新增桥接/验证序列/相邻边界第一方断言 + 真实 SIGKILL WAL 恢复测试 | ITEM007-r3（243/243） |

## 4. 边界与后续

- 本层为**持久化模型与 SQLite 执行**；写入线程模型（单写线程）与高频写入编排由上层（录像/健康/事件运行时）接入，本层保证单连接串行写入语义。
- WAL 恢复为“运行中复制 db+wal 后重放”的可复现验证；真实断电恢复属 ENV-E 现场/嵌入式测试边界。
- 位图/快照/片段文件本身由 `SegmentFileStore`（后续工作项）落盘；本层记录元数据与缺口。
- SQLite dev 包缺失时仅靠运行时库 + shim；若目标连 `libsqlite3.so.0` 也没有，CMake 明确提示持久化禁用（不静默）。

## 5. 证据文件

- `build-CoreTests-192_168_0_103-20261015-ITEM007-r3.{transcript.json,log}`（**权威(v2 修复后)**：243/243 + config-check）
- `build-CoreTests-192_168_0_103-20261015-ITEM007-r2.{transcript.json,log}`（v1：230/230，保留追溯）
- `build-CoreTests-192_168_0_103-20261015-ITEM007-r1.{transcript.json,log}`（首次：229/230，时间基准测试期望错误，保留追溯）
- `VERIFY-ITEM-007.md`、`verify_persist_ext.cpp`、`verify_persist_fixtures.py`、`VERIFY-FIXTURES-*`（软件验证交付）

## 6. 复现

```sh
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode CoreTests \
  --outdir docs/hmi/test_evidence/ITEM-007-persistence --stamp <tag> --run-tests
# 板上直接: ./build-core/tests/railhmi-core-tests
```

## 7. v2 修复记录（响应软件验证 TEST_FAIL：D-01/D-02）

软件验证 `result_code=TEST_FAIL`，定位两个 `add_gap` 缺陷（主套件与其余判据全绿）：

- **D-01 桥接缺口留永久重叠残对**：`add_gap` 在 for 循环内合并到第一个命中区间后 `return`，未吸收后续可合并区间。
- **D-02 合并未继承 reason**：同一 early-return 导致只访问一个既有 gap，桥接时其它区间 reason 参与不到合并。

**修复**（`app/src/core/src/retention.cpp` `add_gap`）：
1. 改为**迭代吸收**：把新缺口与同相机所有“重叠或相邻”区间反复合并，直到一整轮无新增吸收——桥接的两个区间会被一并吸收，结果只留一个区间（无重叠残对）。
2. **相邻判定**改为闭合区间语义：`lo <= g.t1+1 && g.t0 <= hi+1`（相差≤1ms 视为相邻，如 [50,99] 与 [100,300]）；真正间隔≥2ms 的缺口仍分离。
3. **reason 取先存在的非空值**（扫描顺序下第一个既有非空 reason 优先，否则用本次入参），符合验证侧契约。
4. 接口签名不变；结果仍按 `(cam, t0, id)` 排序；`SqliteRepository::insert_gap` 与查询接口未受影响。

**新增第一方回归**（不复依赖验证外部夹具）：桥接合并为单区间 `[0,30]`（reason `a`）、验证侧原序列 `[100,300]+[50,99]+[99,110]`→单区间 `[50,300]`（reason `drop`）、相差 1ms 不合并/连续合并边界、首存 reason 保留；并将 D-03 观察（WAL 恢复口径）升级为第一方**真实 `fork()+SIGKILL` 写中终止**测试（未提交事务丢弃、已提交行完好、journal_mode 仍 wal）。

**复测**：ITEM007-r3 → **243 passed / 0 failed**（223→243，+13 缺口/崩溃断言）。

> 说明：本能力不自行判定验证结论对错；按验证侧可复现缺陷 D-01/D-02 修复并扩大第一方覆盖后重新提交，供验证与 Runtime 重新判定。
