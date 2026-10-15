# UI 实现评审证据 — ITEM-008（railhmi-core 单元测试与可复现验证入口）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `cd1d888`，2026-10-16 00:15:32 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/tests/run_core_tests.sh`（单命令入口）、`app/tests/core/{main.cpp,test_util.cpp,test_util.h}`（harness + JUnit）、`app/tools/board_build.py`（`--mode TestRun`）、既有 `test_config/stream/rules/persistence/platform.cpp`
  - 构建证据: `docs/hmi/test_evidence/ITEM-008-coretests/README.md`
- 被评工作项: `ITEM-008`（交付 railhmi-core 单元测试与可复现验证入口：一条命令运行并输出可归档结果；最小工具链（C++ 编译器 + SQLite）下可运行，不依赖 Qt/GStreamer/相机；用户可观察：运行测试命令打印逐项通过/失败与退出码，测试文件可独立复现）
- 参照设计: `docs/hmi/TEST_DESIGN.md` §0/§5（TC-CORE-01..07）、`docs/hmi/DESKTOP_APP_DESIGN.md` §15.1；交互/视觉设计为被覆盖契约来源（`INTERACTION_DESIGN` v2 `ef47841`、`VISUAL_DESIGN` v2 `40ccf4e`）
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-008-coretests/VERIFY-ITEM-008.md`）
- 评审日期/环境: 2026-10-15/16；Windows 主机（MINGW64 x86_64，无 C/C++ 工具链）；**本轮未连接 RK3588 目标板**（板端事实采用上游软件验证的独立板端复跑）
- 判定: **TEST_PASS（范围严格限定为 ITEM-008 测试/验证入口；不构成任何界面视觉/交互或 UI 可运行结论）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-008**（core 单测与可复现验证入口）。判定对象是任务内嵌用户可观察点：一条命令运行、打印逐项 `[PASS]/[FAIL]` 与退出码、结果可归档（log + JUnit XML）、最小工具链下可运行且不依赖 Qt/GStreamer/相机、测试文件可独立复现。
2. **本次未对任何界面做视觉/交互还原评审**：本项交付的是**测试与验证入口**，无 UI 产物；测试覆盖的是 core/platform 逻辑（ENV-A0 等价面），不含 UI/取流/推理。因此本文不对布局、颜色、字形、焦点、反馈、空/恢复状态做任何 PASS 结论（§5 D-05）。
3. **本轮未重新连接目标板**：板端 243/243、入口退出码负例、JUnit 独立解析与验证自有 14 条夹具已由上游软件验证独立复跑。本 capability 授权方法为 `read/ui_evidence/design_assets`，主机无 C/C++ 工具链；本轮以**源码审阅 + 判据/产物核对 + 归档证据核对**为独立证据，并标注引用关系。
4. 依赖缺口（主机无 C/C++ 工具链 E-00；板端无 `libsqlite3-dev`，走运行时库 + shim）按职责交平台/环境；本评审不判定其“不可修复”，不返回 BLOCKED。
5. 本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `cd1d888`（2026-10-16 00:15） | 新增 `run_core_tests.sh`、harness JUnit（`test_util.*`）、`main.cpp` RESULT/`--junit`、`board_build.py --mode TestRun`；ITME-008 证据归档；无范围外改动 |
| 交互设计 | `ef47841` | v2；本轮核对其契约是否被测试覆盖（结论见 §4） |
| 视觉设计 / 令牌 | `40ccf4e` | v2；令牌/状态字符串与 core 枚举的一致性无自动守门（见 D-03） |
| 软件验证 | `1683c6f` | TEST_PASS；243/243 + 入口负例 2/3 + JUnit 解析 + 扩展夹具 14/14 |

实现自述与仓库证据一致：`ITEM-008-coretests/README.md`、板端 r1 归档 log/XML/transcript 与验证件相互印证；**未把主机结论当板端结论，未把 UI/相机行为纳入本入口**。

---

## 2. 评审方法

1. 源码审阅：`run_core_tests.sh`（参数/构建/运行/归档/退出码/PIPESTATUS）、`test_util.{h,cpp}`（`check` 记录与 `write_junit` 转义）、`tests/core/main.cpp`（RESULT 行与退出码）、`board_build.py`（TestRun 分支）。
2. 判据核对：一条命令、逐项打印、退出码 0/1/2/3 路径、JUnit XML、最小工具链、独立复现。
3. 契约覆盖对照：测试文件是否覆盖配置校验、六态状态机、规则/级别映射、抑制、持久化、保留策略（UI 所依赖的 core 契约面）。
4. 归档证据核对：板端 r1 归档 log（243 PASS 行、`RESULT: PASS`）、XML（`tests=243 failures=0`）、transcript；`VERIFY-ITEM-008.md`。
5. 仓库卫生核对：入口默认输出目录位置与仓库忽略规则。

**方法限制**：主机无 C/C++ 工具链 → 不能本地运行测试；板端结论采用上游软件验证复跑证据。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 审阅 `run_core_tests.sh` | 参数 `--outdir/--stamp/--no-build`；未知参 exit 2；构建失败 exit 3；二进制缺失 exit 3；`RC=${PIPESTATUS[0]}` **未被 `tee` 污染**；log/XML 由入口统一归档 —— 退出码语义正确 |
| R2 | 审阅 harness | `check()` 逐条打印 `[PASS]`/`[FAIL]` 并计数；`write_junit` 对 `& < > " '` 转义并对控制字符替换；`main.cpp` 打印 `RESULT: PASS|FAIL passed=N failed=M` 且退出码与 `g_fail` 一致 |
| R3 | 契约覆盖对照 | 五组测试齐备：platform（版本/脱敏/原子文件/SQLite）、config（合法逐项 + 10 类非法报具体字段 + 流时序阈值）、stream（六态 + 升级 + `may_present_as_live` + `classify_quality`）、rules（级别映射/抑制/track/ack/ROI）、persistence（9 表/WAL/写入-重启-读取/SIGKILL/保留/缺口）—— 与 UI 依赖的 core 契约面一致（R5 计数） |
| R4 | 归档证据核对 | 板端归档 log `243 passed, 0 failed`、`RESULT: PASS passed=243 failed=0`；XML `tests="243" failures="0"`；transcript 与验证件一致 |
| R5 | 分组计数核对 | 验证件分组：platform 16 + config 32 + stream 38 + rules 97 + persistence/timebase 60 = **243**；XML testcase 243、failure 0 |
| R6 | 仓库卫生核对 | `find **/.gitignore` **无命中**；`app/tests/` 下无 `evidence/` 目录；入口默认 `--outdir app/tests/evidence` 会写入**源码树内**（见 §5 D-01） |

---

## 4. 判据逐条核对（ITEM-008 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| tests/core 覆盖六域（配置/六态/规则与级别/抑制/持久化/保留） | R3/R5 五组齐备、243 断言 | **满足** |
| 一条命令运行 | `run_core_tests.sh` 内完成构建+运行+归档 | **满足** |
| 打印逐项通过/失败 | `check()` 逐条 `[PASS]/[FAIL]`（失败含 detail），板端 243 行可见 | **满足** |
| 退出码可归档（log + JUnit XML） | log + XML 归档；退出码 0/1/2/3 路径（R1） | **满足** |
| 最小工具链（C++ 编译器 + SQLite），不依赖 Qt/GStreamer/相机 | 板上 `Qt=no` 仍构建；SQLite 运行时库 + shim；g++ 直编夹具零库成功 | **满足**（工具链实含 CMake，见 D-04） |
| 测试文件可独立复现 | 验证未用实现者构建目录，独立上传/构建，243 行逐行一致 | **满足** |

---

## 5. 具体偏差与修复要求

**D-01（仓库卫生，UI/工程实践）— 入口默认输出目录在源码树内，且仓库无 `.gitignore`**
- 观察：`run_core_tests.sh` 默认 `--outdir "$SCRIPT_DIR/evidence"` = `app/tests/evidence`；主机 `find **/.gitignore` **无命中**（仓库无任何忽略规则）。在开发机直接运行入口会在源码树生成 `core-tests-*.log/.xml` 未跟踪文件。
- 影响：与公司规则“自动提交前仓库必须干净/不得卷入无关改动”及 `file_manifest.txt` 文件登记纪律冲突；若被 `git add -A` 扫入，会污染能力提交。
- 复现路径：`read app/tests/run_core_tests.sh`（默认 OUTDIR）；`find **/.gitignore`（无）；`ls app/tests`（无 evidence 目录）。
- 期望：默认输出改为构建目录（如 `app/build-core/evidence`）或新增 `.gitignore`（忽略 `app/**/build-*/`、`app/tests/evidence/`、SQLite 临时 `*.db*`），并在 README 说明；避免默认运行脏工作树。

**D-02（CI 可消费性，轻量）— JUnit XML 存在重名 testcase，唯一名 242/243**
- 观察：`offline cannot present as live` 在 `test_stream.cpp` 出现两次（OFFLINE 初始态与达阈值后），JUnit `classname` 均为 `railhmi-core`，按 `name` 定位失败用例时会歧义（验证件 O-1 同源）。
- 期望：为重复断言名加区分后缀（如 `(initial)`/`(after-escalation)`），使 `classname+name` 唯一；属第一方用例命名改进，不影响当前 pass。

**D-03（设计一致性守门，UI 契约）— UI 面向的枚举 token 无自动断言，依赖人工评审防漂移**
- 观察：测试虽断言了部分字符串身份（如 `integration_state string is 'unconfigured'`），但**未系统断言** core `to_string` 与 `design_tokens.yaml`/交互视觉设计的一致性（如 `StreamState` 六 token、`AlarmLevel` `L1/L2/L3`、`ImagingMode` `visible/ir`、`RecordingSource` `local/nvr/both`、`IntegrationState` 五态）。目前这些一致性由本系列 UI 评审逐轮人工核对（REVIEW-ITEM-004/005/006/007）。
- 期望：增加一组轻量“契约身份”断言，把 core 枚举 token 与设计约定字符串逐值绑定（可引用 `docs/hmi/design_tokens.yaml` 的键名清单作为被测常量），使 UI/设计/实现三端漂移在 CoreTests 即可发现，减少人工评审负担。

**D-04（文档口径）— 工作项描述的“最小工具链”未包含 CMake**
- 观察：ITEM-008 描述为“C++ 编译器 + SQLite”，但入口经 `build.sh CoreTests` 构建需要 **CMake**（`build.sh` 亦在缺 CMake 时报错）。
- 期望：README/证据中把最小工具链表述为“C++ 编译器 + CMake（+ SQLite 可选/运行时库）”，与 `build.sh` 依赖自检一致；纯文档对齐，不改行为。

**D-05（范围缺口，非本项缺陷）— 本入口不含 UI，无可评审界面**
- 观察：入口只覆盖 core/platform；UI/取流/推理/相机行为不在本入口，仓库仍无可运行 UI。
- 期望：待页面/组件落地后，UI 实现评审按 `INTERACTION_DESIGN` §4–§11 与 `VISUAL_DESIGN` §7–§12 逐状态核对；本入口可作为 UI 契约层（core）的回归基线。

### 已确认无问题/正向项

- 单命令完成“构建 + 运行 + 归档 + 退出码”，`PIPESTATUS[0]` 取值正确未被 `tee` 污染（R1）。
- JUnit XML 转义与结构正确（`& < > " '` + 控制字符），`tests=243 failures=0` 可被 CI 独立解析（R2/R4）。
- 退出码 0/1（真实 FAIL）/2（参数）/3（缺二进制）四路径均有实测；失败用例含 detail。
- 覆盖与 TEST_DESIGN TC-CORE-01..07 对应，五组分域计数闭合 243；SQLite 缺失自动 `[SKIP]` 并可退化运行。
- 最小工具链不含 Qt/GStreamer/相机依赖，g++ 直编夹具零库可运行（不在板也不需板）。
- 验证独立、逐行可比；板端结论与主机结论分列，未外推。

---

## 6. 未覆盖 / 未执行（不得外推）

1. 目标板任何命令本轮**未执行**；板端结论采用上游软件验证复跑与夹具证据。
2. 任何界面视觉/交互还原（无 UI 产物）。
3. UI/取流/推理/相机行为（不在本入口，也不在本证据范围）。
4. 主机（Windows）运行（无 C/C++ 工具链 E-00，历史缺口未变）。
5. 目标无 SQLite 运行时库时的退化 `[SKIP]` 路径（板端存在运行时库，未实测）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-008 测试/验证入口）。
- 依据：任务内嵌用户可观察点——一条命令运行、打印逐项 `[PASS]/[FAIL]` 与退出码、结果可归档（log + JUnit XML）、最小工具链下可运行且不依赖 Qt/GStreamer/相机、测试文件可独立复现——经源码审阅、判据/产物核对与归档证据核对逐条成立；板端 243/243 与验证 14 夹具一致。
- 明确边界：本 PASS **不表示**任何界面可运行或视觉/交互通过；本入口仅覆盖 core/platform 逻辑（ENV-A0 等价面）。
- 后续修复/交接要求见 §5：D-01（默认输出目录/仓库忽略）为工程卫生项；D-02（JUnit 重名）为 CI 可消费性项；D-03（token 契约身份断言）为减少 UI 设计漂移的建议；D-04 文档口径；D-05 待可运行 UI 后再评审。
