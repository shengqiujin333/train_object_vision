# VERIFY-ITEM-025 — Independent Software Verification Record

- Capability: `software_tester.software_verification`（Restlessdream Runtime，RESUME_SYNC）
- 被验证对象: 实现方 fix 轮 `20261017-ITEM025-fix-r1`（commit `ee0a741`），声明工件 `app/README.md`，证据 `docs/hmi/test_evidence/ITEM-025-packaging/README.md`
- 工作项: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-025`（目标运行包、systemd 常驻自启与故障自检自证）
- 验证轮: 上轮 `VERIFY-20261017-v1`（TEST_FAIL: D-01/D-02）→ v2/v3（夹具修复终轮，维持 FAIL）→ 本轮 `VERIFY-20261017-v4`（全量步骤，唯一失败为验证方负控制夹具缺陷）→ `VERIFY-20261017-v4b`（修复夹具的负控制板端补证）
- 结论: **TEST_PASS**

## 0. 上轮失败与本轮变化

上轮 D-01/D-02 结论与修复边界（`git show` 1face14 轮 VERIFY-ITEM-025.md §3/§4）：
- D-01（严重）`packaging/railhmi.service` `ExecStartPre=/opt/railhmi/bin/railhmi-version-check` 引用不存在且无人构建/安装的二进制 → 实机必启动失败并 Crash-Loop。
- D-02（严重）单元传 `--config /opt/railhmi/config` 但 `main.cpp` 不解析 argv、配置目录被编译期钉死为构建时源码树路径 → 部署后启动即 exit 4 → Crash-Loop。
- 建议交付物 `check_packaging.py` 增补两类断言。

fix-r2（ee0a741）逐项对照修复边界：
- D-01 选建议 (b)：删除 ExecStartPre 行；`UPGRADE.md` 相应改为“应用启动时校验配置与版本并写日志”，不再引用不存在的工具。
- D-02 选建议（解析参数）：core 新增 `parse_config_dir()`（`--config <dir>` / `--config=<dir>`，无值/空值回退缺省），`main.cpp` 以 `load_config_dir(configDir)` 真正消费；新增 7 条单测。
- 交付检查器增补 D-01（单元引用的 `/opt/railhmi/bin/*` 必须出现在 install.sh）与 D-02（单元传 `--config` 时 main.cpp 必须含 `parse_config_dir` + `load_config_dir(configDir)`）断言。

## 1. 独立复验方法（不信任实现方自检）

与上轮同一套独立方法，参数按 fix-r2 更新：

1. 指纹校验 SSH 连接（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，RejectPolicy，凭据仅走环境变量）。
2. 用被交付的入口 `app/tools/board_build.py --mode TestRun` 以验证方 stamp `VERIFY-20261017-v4` 独立复跑板端全量（上传 `app/`+`packaging/`，板端重建、670 单测、tokens selftest、7 项接线扫描、sim-inject、10 探针、归档取回）。
3. 逐行比对复跑 core-tests `[PASS]` 列表 vs 实现方 fix-r1 归档（要求完全一致）；独立解析 JUnit XML 670/0，断言 31 条故障/注入域用例 + 7 条 fix-r2 新增 `parse_config_dir` 用例名逐字存在（共 38 条）。
4. `railhmi-fault-probe` 16 行 oracle 独立断言 + 与 fix-r1 归档逐字节跨轮比对（fix-r2 未触及故障目录，应逐字节一致）+ 板上重跑 diff（确定性）。
5. `railhmi-sim-inject` 六故障注入 oracle + JSON 结构断言 + 与 fix-r1 逐字节比对。
6. 验证方 ext C++ 夹具现场编译链接**本轮新构建的** `librailhmi_core.a`/`librailhmi_platform.a`：目录/自检契约、判窗语义、六故障序列、确定性，并新增 **G 段（fix-r2 D-02 参数契约）**：`parse_config_dir` 七种参数形态（含单元实际形态 `--mode x --config /opt/railhmi/config`、无值/空值回退、`nullptr` argv 安全回退）。
7. 验证方部署契约扫描（`verify_packaging25_scan.py`，本轮键语义更新）：P1..P16 全 True + **D-01/D-01b/D-02 三键必须为 True**（=缺陷缺席；上轮为 False），并要求 `D02_parses_arg=True`（真实解析路径，而非仅编译期前缀回退）。
8. 交付检查器负控制板端补证：健康副本 0，新鲜注入 D-01（加重 ExecStartPre railhmi-version-check）→ 1 且点名 provenance 消息，注入 D-02（main.cpp 停止消费 `--config`）→ 1 且点名 parse/consume 消息。
9. 临时目录按名清理并 `ls` 确认。

夹具/驱动（验证方所有，未修改任何产品源码——`verify_packaging25_ext.cpp` 等为验证方夹具，本轮仅追加 G 段）：
`verify_packaging25_fixround.py`（本轮全量驱动）、`run_fix_round.sh`、`verify_packaging25_negfix_only.py`（v4b 补证驱动）、`run_negfix_only.sh`、`pretest_negfix_local.py`（夹具修复的主机预检）、既有 `verify_packaging25_scan.py`（键语义更新）、`verify_packaging25_ext.cpp`（追加 G 段）。

## 2. 独立复验通过项

| # | 断言 | 结果 |
|---|---|---|
| R1 | 独立复跑 TestRun 入口成功；core-tests **670/670**，`[PASS]` 列表与实现方 fix-r1 **逐行一致** | ✅（v4 log-compare identical=True） |
| R2 | JUnit 独立解析 670/0；31 条故障/注入域用例 + 7 条 fix-r2 `parse_config_dir` 用例全部逐字存在（38/38） | ✅ |
| R3 | fault-probe 16 行 oracle 全中 + 与 fix-r1 **逐字节一致** + 板上重跑 `diff` → `IDENTICAL`（确定性） | ✅ |
| R4 | sim-inject 六故障注入：onset 断流/AI 不可用/存储低/云台超差/缺口同时注入，t=2500 offline 升级（阈值 3），t=5000 恢复 live 清零，`final_state=live alarms=2 gaps=1 events_recorded=2`；log/JSON 与 fix-r1 逐字节一致 | ✅ |
| R5 | ext 夹具（本轮静态库，含 G 段）：6 条 FI 目录四字段非空、SC-01..10 注入命令可解析、SC-BOOT 引用启用单元名 `railhmi` 与 `Restart=always`、判窗半开/永不恢复、六故障模型序列、确定性、**`parse_config_dir` 七种参数形态全过** | ✅（v4 板上 GXX_EXIT=0, RESULT: PASS passed=85 failed=0） |
| R6 | fault-probe 验收 `fi_covered=true boot_step=true prompts_nonempty=true result=PASS` | ✅ |
| R7 | 部署契约扫描：P1..P16 全 True；**D-01/D-01b/D-02 均 True**（缺陷已修复）且 `D02_parses_arg=True` | ✅（主机 + 板端新鲜副本两处扫描一致） |
| R8 | 交付检查器负控制（板端 v4b）：健康副本 PASS；D-01 重注入 → `[FAIL] systemd unit executes /opt/railhmi/bin/railhmi-version-check but install.sh never installs it`（rc=1）；D-02 重注入 → `[FAIL] unit passes --config but main.cpp does not parse/consume it`（rc=1） | ✅（NEG_HEALTHY_EXIT=0 / NEG_D01_EXIT=1 / NEG_D02_EXIT=1 / NEG_DRIVER_EXIT=0） |
| R9 | 交付 check_packaging.py 主机反例自检 `negcheck_packaging_fix.py`（实现方交付）：0/1/1 PASS | ✅ |
| R10 | 打包核对 PASS（TestRun 内 PACK_EXIT=0，含 fix-r2 新断言版） | ✅ |

## 3. 本轮验证方夹具缺陷（不构成业务失败）

- v4 全量轮唯一失败步骤 `negative-control`：`FileExistsError ... /neg/d01` — 夹具 `staging()` 对已创建目录重复 `os.makedirs`，属**验证方夹具缺陷**（与 v1/v2 轮的三次夹具缺陷同类），未触及交付物。修复后在主机预检（`pretest_negfix_local.py`，含真实退出码断言 0/1/1 与点名消息）并以 `verify_packaging25_negfix_only.py` 板端补证（v4b，rc=0）。
- 同步修正验证方扫描夹具键语义：D-01b（UPGRADE.md 登记 `bin/` 工具）改为“无登记即无缺陷”（fix-r2 后 `doc_tools=(none)`），与验收语义一致；R7 结果不受影响。

## 4. 边界说明（不影响 TEST_PASS 结论）

- 验证方未执行 `systemctl enable`/整机重启（ENV-E 现场操作；上轮边界一致）。D-01/D-02 为静态可证伪的交付契约，修复在 Referenced-binary / Parsed-args 层面已由三类独立证据（扫描键翻转、ext G 段、负控制重注入被点名拦下）证实；`parse_config_dir` 行为另有 7 条板端单测 + ext G 段双覆盖。
- “界面出现显式提示与恢复路径”的用户可观察点由故障目录/自检证据（fi_covered/prompts_nonempty、SC-BOOT Restart=always、P6 自检卡扫描 P8）与预验证轮通过的 UI 评审证据链支撑；画面内容本身仍属用户视觉确认范畴（既有边界），不在本轮自动证据范围。
- 6 路真实摄像头取流不属于 ITEM-025 工作项范围（属取流/推理工作项），本轮未测。

## 5. 证据清单（本轮验证方生成）

- `VERIFY-FIXTURES-VERIFY-20261017-v4.log` / `.transcript.json`（全量步骤：指纹、TestRun 复跑、670 逐行比对、JUnit 38/38、oracle、跨轮字节一致、确定性、扫描 D 键 True、负控制夹具 FileExistsError、清理确认）
- `VERIFY-NEGFIX-VERIFY-20261017-v4b.log` / `.transcript.json`（修复夹具后的板端负控制补证：0/1/1 + 点名两条缺陷消息 + 按名清理）
- `build-TestRun-192_168_0_103-VERIFY-20261017-v4.log` + 14 份 `core-evidence_*` 归档（独立复跑产物）
- `run_fix_round.sh` / `run_negfix_only.sh`（有限等待驱动；指纹校验、凭据仅经环境变量）
- `verify_packaging25_fixround.py` / `verify_packaging25_negfix_only.py` / `pretest_negfix_local.py`（验证方驱动/夹具，含本轮修正）
- `verify_packaging25_scan.py`（键语义更新版）/ `verify_packaging25_ext.cpp`（追加 G 段版）

## 6. 复现

```sh
# 主机（离线）：夹具期望预检 + 扫描 + 实现方反例自检
python docs/hmi/test_evidence/ITEM-025-packaging/pretest_negfix_local.py
python docs/hmi/test_evidence/ITEM-025-packaging/verify_packaging25_scan.py --app app --pkg packaging
python docs/hmi/test_evidence/ITEM-025-packaging/negcheck_packaging_fix.py
# 板端（经登记 SSH 入口，约 25 分钟 + 补证 2 分钟）
bash docs/hmi/test_evidence/ITEM-025-packaging/run_fix_round.sh       # 全量（负控制步骤本轮由 v4b 补证）
bash docs/hmi/test_evidence/ITEM-025-packaging/run_negfix_only.sh    # 负控制板端补证
```
