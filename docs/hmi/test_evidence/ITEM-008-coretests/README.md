# ITEM-008 证据 — railhmi-core 单元测试与可复现验证入口

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-008`
- 依据: `docs/hmi/TEST_DESIGN.md` §0（ENV-A0）/§5（TC-CORE-01..07）；`docs/hmi/DESKTOP_APP_DESIGN.md` §15.1（`tests/core` 单命令入口）
- 源码: `app/tests/core/*`、`app/tests/run_core_tests.sh`、`app/tools/board_build.py`
- 运行器（板端）：`app/tools/board_build.py --mode TestRun`

## 1. 交付内容

1. **单命令入口** `app/tests/run_core_tests.sh`：一条命令完成 **构建 + 运行 + 归档 + 退出码**。
   - `--outdir DIR`（默认 `app/tests/evidence`）、`--stamp TAG`、`--no-build`。
   - 归档：`core-tests-<stamp>.log`（完整 stdout，含逐项 `[PASS]/[FAIL]`、汇总、`RESULT:` 行）与
     `core-tests-<stamp>.xml`（JUnit XML，可被 CI 消费）。
   - 退出码：0 全通过；1 有失败；2 参数错误；3 构建/二进制缺失。
2. **可归档结构化输出**：测试 harness 记录每条断言并在 `--junit <path>` 时写 JUnit XML；
   末尾打印 `RESULT: PASS|FAIL passed=N failed=M`（脚本据此与退出码归档）。
3. **测试覆盖**（`app/tests/core/`，仅标准库 + `railhmi_core`/`railhmi_platform`；SQLite 可选）：
   - 配置校验：`test_config.cpp`（合法逐项 + 10 类非法报具体字段 + 流时序阈值）；
   - 六态状态机与质量判定：`test_stream.cpp`（全六态 + 重连升级 + 非 LIVE 接口 + classify_quality）；
   - 规则/级别映射/多帧确认/抑制/track/ack/ROI：`test_rules.cpp`；
   - 持久化（9 表/WAL/写入-重启-读取/SIGKILL 恢复）：`test_persistence.cpp`；
   - 保留策略与缺口合并：`test_persistence.cpp`；
   - 平台基础：`test_platform.cpp`（版本/脱敏/原子文件/SQLite）。
4. **最小工具链**：C++ 编译器 + CMake（SQLite 存在则启用持久化测试，缺失自动跳过并打印 `[SKIP]`）；
   **不依赖 Qt / GStreamer / 相机**。

## 2. 实测（板端本机，经登记 SSH 入口）

| 步骤 | 结果 | 证据 |
|---|---|---|
| `bash tests/run_core_tests.sh --outdir core-evidence --stamp <tag>` | `TESTRUN_EXIT=0`；日志 `== 243 passed, 0 failed ==`、`RESULT: PASS passed=243 failed=0` | `...-ITEM008-r1.core-evidence_core-tests-...-r1.log` |
| JUnit XML | `<testsuites tests="243" failures="0">`，243 `<testcase>`、0 `<failure>` | `...-ITEM008-r1.core-evidence_core-tests-...-r1.xml` |

CMake 日志确认 SQLite 走运行时库 + shim（`persistence enabled`），持久化测试实际执行（非跳过）。

## 3. 运行方式

```sh
# 单命令（板端/最小工具链本机；先加载凭据仅用于 SSH 远程方式）
bash app/tests/run_core_tests.sh --outdir app/tests/evidence
# 或经登记 SSH 入口（上传 app/ 后板上运行并取回归档）
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-008-coretests --stamp <tag>
# 已构建过可跳过构建：
bash app/tests/run_core_tests.sh --no-build
```

## 4. 边界

- 本入口只覆盖 **railhmi-core/platform 逻辑**（ENV-A0 等价面）；UI/取流/推理/相机行为不在本入口。
- SQLite 缺失时持久化用例自动 `[SKIP]`，入口仍可运行（最小工具链退化为“仅 C++ 编译器”）。
- 主机（Windows）无 C/C++ 工具链，故本轮在板端本机执行；结论记录为板端本机（aarch64），不外推为主机可运行。

## 5. 证据文件

- `build-TestRun-192_168_0_103-20261015-ITEM008-r1.transcript.json` / `.log`（运行器 transcript/日志）
- `build-TestRun-...-ITEM008-r1.core-evidence_core-tests-...-r1.log`（归档完整 stdout）
- `build-TestRun-...-ITEM008-r1.core-evidence_core-tests-...-r1.xml`（归档 JUnit XML）

## 6. 复现

```sh
MSYS_NO_PATHCONV=1 MSYS2_ARG_CONV_EXCL="*" python app/tools/board_build.py \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-008-coretests --stamp <tag>
```
