# VERIFY-ITEM-025 — Independent Software Verification Record

- Capability: `software_tester.software_verification`（Restlessdream Runtime，RESUME_SYNC）
- 被验证对象: implementer 轮 `20261016-ITEM025-r1`（commit `6d1787b`），声明工件 `app/README.md`，证据 `docs/hmi/test_evidence/ITEM-025-packaging/README.md`
- 工作项: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-025`（目标运行包、systemd 常驻自启与故障自检自证）
- 验证轮 stamp: `VERIFY-20261017-v1`（全量步骤）→ `VERIFY-20261017-v2`（首次 ext+反例重跑：驱动自身 JUnit `&apos;` 未反转义、ext 头注释非注释、NEG 反例缺显式参数——三次失败均经离线复现定位为**验证方夹具缺陷**，未触及交付物）→ `VERIFY-20261017-v3`（修复后终轮，最终板上证据以此 stamp 归档）
- 结论: **TEST_FAIL**

## 1. 独立复验方法（不信任实现方自检）

1. 指纹校验 SSH 连接（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，RejectPolicy，凭据仅走环境变量不落日志）。
2. 用被交付的入口 `app/tools/board_build.py --mode TestRun` 以验证方 stamp 独立复跑（上传 `app/`+`packaging/` 到板端 `/tmp/railhmi_build`，本机重建 CoreTests、跑 663 单测、tokens selftest、7 项接线扫描、sim-inject 六故障注入、10 个探针、归档取回）。
3. 逐行比对：复跑 core-tests `[PASS]` 列表 vs 实现方 r1 归档（663 条，要求完全一致）。
4. 独立解析 JUnit XML（不经过实现方汇总行）：663/0，且 31 条故障域用例名逐字存在。
5. 对 `railhmi-fault-probe` 用验证方 16 行正则 oracle 独立断言 + 与实现方 r1 log 跨轮逐字节比对 + 板上重跑 diff（确定性）。
6. 对 `railhmi-sim-inject`（六故障同时注入）独立断言起点/恢复/汇总观测 + JSON 结构 + 跨轮逐字节比对。
7. 验证方 ext C++ 夹具现场编译链接**本轮新构建的** `librailhmi_core.a`：目录/自检契约、注入窗口语义（[onset,recovery) 半开、recovery≤onset 不恢复）、六故障模型状态序列、确定性。
8. 验证方部署契约扫描（超出交付的 token 检查）：systemd 单元/install.sh/UPGRADE/VERSION/main.cpp/CMake 交叉一致性，包括 **ExecStartPre 目标存在性**（D-01）与 **--config 参数被消费**（D-02）。
9. 板上反例对照：交付的 `check_packaging.py` 对新鲜副本 PASS、对合成畸变副本 FAIL（证明该检查器在其声称的层面有效）。
10. 全部临时目录按名清理并 `ls` 确认（exit=2 为"不存在"确认，非失败）。

夹具/驱动（验证方所有，未修改任何产品源码）：`verify_packaging25_fixtures.py`、`verify_packaging25_ext.cpp`、`verify_packaging25_scan.py`、`verify_packaging25_ext_rerun.py`、`offline_selfcheck0/1/2.py`、`run_verify_round.sh`、`run_ext_rerun.sh`。

## 2. 独立复验通过项（与业务交付相符的部分）

| # | 断言 | 结果 |
|---|---|---|
| R1 | 独立复跑 TestRun 入口成功；core-tests **663/663**，`[PASS]` 列表与实现方 r1 **逐行一致** | ✅（v1 log-compare identical=True） |
| R2 | JUnit 独立解析 663/0；31 条故障/注入域用例全在（FI-01..05/10 状态序列、注入参数记录、确定性重置、级别映射） | ✅（v1 junit-parse tests=663 failures=0；注：v1 驱动自身 `&apos;` 未反转义报 28/31，离线修正后确认 31/31） |
| R3 | fault-probe 16 行 oracle 全中 + 与 r1 **逐字节一致** + 板上重跑 `diff` → `IDENTICAL`（确定性） | ✅ |
| R4 | sim-inject 六故障注入：t=1500 起断流重连/AI 不可用/存储低/云台超差/缺口，t=2500 offline 升级（阈值 3），t=5000 恢复 live 清零，汇总 `final_state=live alarms=2 gaps=1 events_recorded=2`；log 与 JSON 与 r1 逐字节一致 | ✅ |
| R5 | ext 夹具（本轮静态库）：6 条 FI 目录项四字段非空、SC-01..10 注入命令可被 `parse_fault` 解析、SC-BOOT 重启的是 install.sh 启用的单元名 `railhmi` 且恢复路径引用 `Restart=always`、窗口半开/永不恢复语义、六故障模型序列、确定性 | ✅（v3 板上 GXX_EXIT=0, RESULT: PASS passed=78 failed=0） |
| R6 | `railhmi-fault-probe` 验收 `fi_covered=true boot_step=true prompts_nonempty=true result=PASS`（TEXT_DESIGN FI 目录全量覆盖） | ✅ |
| R7 | Unit `Restart=always`/`DISPLAY=:0`/install 目录/单元安装启用/VERSION/UPGRADE 回滚/RESOURCES 许可登记/P6 自检卡/main.cpp `railhmiFaults` 注入、单测域覆盖、探针/CLI 均为真实构建目标 | ✅（扫描 P1..P16 全 True） |
| R8 | 反例对照：交付 `check_packaging.py` 新鲜副本 PASS / 畸变副本 FAIL（token 层健全） | ✅（v3 NEG_HEALTHY_EXIT=0 / NEG_MUTATED_EXIT=1，点名 `[FAIL] systemd unit missing auto-restart (Restart=always)` 与 `cpu-performance` 两条） |

## 3. 缺陷（构成 TEST_FAIL 的业务失败）

### D-01（严重） ExecStartPre 引用不存在且无人构建/安装的二进制 → 实机部署必启动失败并 Crash-Loop

`packaging/railhmi.service:15`：

```
ExecStartPre=/opt/railhmi/bin/railhmi-version-check
```

取证（三路独立证据）：
1. **构建系统**：`app/src/core/CMakeLists.txt` 全部 11 个工具目标为 config-check/sim-inject/status/ai/alarm/clip/replay/maintenance/permissions/health/**fault**-probe——无任何 `version-check` 目标；仓库内 `add_executable` 全扫无 `railhmi-version-check`。
2. **安装器**：`packaging/install.sh` 只安装 `$SRC/app/build-target/railhmi` 与 `VERSION`，不产生/安装 `railhmi-version-check`。
3. **文档**：`packaging/UPGRADE.md:7` 把 `railhmi-version-check`（`bin/` 内）登记为"启动前自检入口"——文档所指工具不存在。

systemd 语义：`ExecStartPre` 非零退出直接导致启动失败；`Restart=always`（`RestartSec=3`）会使其进入无限 Crash-Loop，**永远不会进入 active**。因此任务的用户可观察点"**系统重启后应用随系统自动拉起并进入可用状态**"在真实部署（ENV-E）下**不成立**。板上验证扫描 D-01 键 = `False`（v1 与 v3 两轮板上独立确认）；反例对照证明交付的 `check_packaging.py` 检不出该缺陷（它只匹配 `ExecStart=` 子串前缀，未校验 ExecStartPre 目标存在性）——这就是交付证据 `PACK_EXIT=0` 与该缺陷并存的原因。

### D-02（严重） 单元传 `--config` 但二进制不解析；配置目录是编译期源码树路径 → `/opt/railhmi/config` 部署契约不成立

`packaging/railhmi.service:16`：

```
ExecStart=/opt/railhmi/bin/railhmi --config /opt/railhmi/config
```

取证：
1. `app/src/ui/src/main.cpp:36-45`：`main()` 对 argv 不做任何解析（`QGuiApplication` 吞掉未知参数），配置来自 `load_config_dir(RAILHMI_CONFIG_DIR)`（main.cpp:45）。
2. `app/src/ui/CMakeLists.txt:48`：`target_compile_definitions(railhmi PRIVATE RAILHMI_CONFIG_DIR="${CMAKE_SOURCE_DIR}/config")`——**编译期**把配置目录钉死为构建时源码树的绝对路径。
3. 两相结合：部署到 `/opt/railhmi/bin/railhmi` 的二进制会在 `load_config_dir` 时指向构建时的源码树位置（例如 TestRun 的 `/tmp/railhmi_build`，该目录验证后即删除）。`/opt/railhmi/config`（install.sh 安装的拷贝）**不会被读取**；既有路径不存在时 `load_config_dir` 失败 → `main()` **exit 4**（main.cpp:51）→ 同样 `Restart=always` Crash-Loop。

板上验证扫描 D-02 键 = `False`，并逐条打印 `RAILHMI_CONFIG_DIR` 定义（无任何 `/opt/railhmi` 出现）。

### 边界说明（不影响 TEST_FAIL 结论）

- 验证方**未执行** `systemctl enable` 或整机重启（ENV-E 需现场授权且涉及重启整机；与实现方边界声明一致）。D-01/D-02 属**静态可证伪的交付缺陷**：部署契约在 Referenced-binary / Parsed-args 层面即可证不成立，板端取证未尽事项在此不构成对缺陷的豁免。
- 故障域（FI 目录、提示/恢复文案、模拟注入、自检清单）与运行目录/版本/升级回滚文档本体经独立复验为真（§2 R1-R8）；TEST_FAIL 由部署可用性缺陷 D-01/D-02 构成，修复边界明确（见 §4）。

## 4. 修复边界建议（交实现方）

1. **D-01**：三选一——(a) 新增 `railhmi-version-check` 工具目标并在 install.sh 安装；(b) 从单元移除 ExecStartPre 行并同步修订 UPGRADE.md 第 7 行；(c) 若版本自检语义保留为应用内启动步骤（main.cpp 已校验配置并落日志），删除 ExecStartPre 重复环节。
2. **D-02**：让 `main.cpp` 解析 `--config <dir>`（无参数时回退编译期缺省），或将编译期定义改为部署前缀 `/opt/railhmi/config`。无论哪种，单元 `/opt/railhmi/config` 与 `railhmi-config-check ./config` 探针口径（启动校验入口）应统一。
3. 建议交付物 `check_packaging.py` 增补两条断言（ExecStart* 目标在 provenance 集合内；`--config` 有消费路径），否则同类缺陷仍会漏检。

## 5. 证据清单（本轮验证方生成）

- `VERIFY-FIXTURES-VERIFY-20261017-v1.log` / `.transcript.json`（全量步骤：指纹、TestRun 复跑、逐行比对、oracle、跨轮字节一致性、确定性、扫描板端 D-01/D-02=False）
- `VERIFY-FIXTURES-VERIFY-20261017-v3.log` / `.transcript.json`（修复夹具后的终轮板上证据：ext 夹具 GXX_EXIT=0 → `RESULT: PASS passed=78 failed=0`、反例对照 0/1、D-01/D-01b/D-02=False 复核、按名清理确认）
- `build-TestRun-192_168_0_103-VERIFY-20261017-v1.log` + 14 份 `core-evidence_*` 归档（独立复跑产物）
- `build-TestRun-192_168_0_103-VERIFY-20261017-v3.log` + 归档（v3 终轮 TestRun 复跑产物；注：跑动优先归档，先前失败轮未留存，stamp 内逻辑为同一步骤集的最终成功执行）
- `RUN-ROUND-console.log` / `RUN-RERUN-console.log` / `RUN-RERUN3-console.log`（驱动控制台；含离线自检 PASS 与各步 exit 码）
- `offline_selfcheck0/1/2/3.py` 输出内嵌于控制台日志（零板耗预期的期望构造自检）；`offline_negcheck.py` / `offline_negcheck_diag.py`（v2 反例异常 rc=2 的离线复现与 stderr 定位——定位到驱动 `fresh_args[:-1]` 切片把 `--packaging` 的值切掉，属验证方夹具缺陷）

## 6. 复现

```sh
# 主机（离线）：静态契约扫描 + 交付检查器 + 反例 + 夹具期望构造
python docs/hmi/test_evidence/ITEM-025-packaging/offline_selfcheck1.py
python docs/hmi/test_evidence/ITEM-025-packaging/offline_selfcheck3.py
# 板端（经登记 SSH 入口，约 20 分钟）
bash docs/hmi/test_evidence/ITEM-025-packaging/run_verify_round.sh      # 全量
bash docs/hmi/test_evidence/ITEM-025-packaging/run_ext_rerun.sh        # ext+反例终轮（归档 stamp 内逻辑）
```
