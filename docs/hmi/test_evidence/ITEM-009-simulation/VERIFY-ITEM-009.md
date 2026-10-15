# VERIFY-ITEM-009 — 模拟输入与故障注入系统（软件验证 TestEvidence）

- 被测交付: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-009`（commit **2a0215b**，`embedded_linux_engineer.desktop_app_implementation` IMPLEMENTED）
- 被测对象: `app/src/core/{include/railhmi/core/simulation.h, src/simulation.cpp, tools/sim_inject.cpp}`（SimulationRunner/FaultKind/parse_fault/Observation + CLI）、`app/tests/core/test_simulation.cpp`、CMake 接线（`app/src/core/CMakeLists.txt` railhmi-sim-inject、`app/tests/core/CMakeLists.txt` test_simulation.cpp 入轮、`app/tests/core/main.cpp` run_simulation_tests）与 `app/tools/board_build.py` TestRun 分支（sim-inject 复现与取回）
- 判据来源: 工作项描述（按 ai_profile 注入可重置检测序列、FI-01..05/FI-10 软件开关/CLI、故障后进入对应状态且可恢复、注入参数与观测写入结构化日志、无需相机）+ TEST_DESIGN §3 FI 目录与 TC-SIM-01（模型层档）
- 验证端: RK3588 板端本机（192.168.0.103，aarch64，gcc 11.4；经登记 Paramiko SSH 入口）+ 主机静态核对
- 结论: **TEST_PASS**（本范围独立证据：交付入口独立复现 274/274 与实现者归档近期逐行一致；sim-inject 权威参数复现 63 行逐行一致；CLI 负例/列表/确定性/自定义参数 5 项实测；验证自有扩展夹具 49/49 全绿；逐节见 §2）
- 实现者归档核对: r2 transcript（mkdir/upload 70 文件/testrun 274 passed/sim-inject 63 行/download×4/cleanup 全 exit=0）、log/XML/sim-inject log+JSON 四件套均存在且内部一致（主机复验见 §3）

## 1. 验证方法（独立于实现的证据链）

1. **静态核对**（主机）: ①`simulation.cpp`——`fault_active` 窗口 `[onset, recovery)` 语义、`recovery<=onset` 不解除、FI-01 优先于 FI-02（`frz = !cut && …`）、FI-03 停检测、FI-04 阈值-1 与 `record_available` 联动、FI-05 容差观测、FI-10 逐 tick `add_gap` 相邻合并（复用 ITEM-007 已独立验证的 `retention.cpp` 实测合并语义）、断电确定性（模拟时钟无 wall 依赖）；②`sim_inject.cpp`——参数解析、`--list`/`--help`、退出码 0/2、`--out`/`--json` 双路输出、JSON 转义；③CMake 接线确认 simulation.cpp/test_simulation.cpp/sim-inject 三处均入构建（非死代码）；④发现并登记 O-1/O-2（见 §5）。
2. **板端独立复跑**（验证自有运行器 `verify_sim_fixtures.py`，上传 HEAD `app/` 快照 98 856 字节压缩 → 板上独立目录 `/tmp/railhmi_verify009_board/app`，执行交付入口 `bash tests/run_core_tests.sh --outdir core-evidence --stamp VERIFY-20261016-v3`，全超时 1200s）: `ENTRY_EXIT=0`；入口归档 274 [PASS]/0 [FAIL]、`RESULT: PASS passed=274 failed=0`；**274 条逐项行 + 头尾 summary 与实现者 r2 归档程序化逐行对比完全一致**（含顺序，277 行对比）；取回 JUnit XML 独立解析 `<testsuites tests="274" failures="0">`、testcase 274、failure 0。SSH host 指纹 `SHA256:NzHFHZ0Z…Z5qU` 与实现者 r2 及前五轮一致。
3. **sim-inject 权威轮复现**（实现者同参数 `--fi 01,02,03,04,05,10 --duration 6000`）: `SIM_EXIT=0`；**63 行结构化日志与实现者归档逐行一致**（程序化 diff 无差异）；JSON 独立解析 63 entries、8 种 type（injection/stream_state/ai/storage/ptz/gap/alarm/summary）。
4. **CLI 行为负例/正例**（板上，同快照二进制）: ①`--fi 01,99` → `invalid --fi list`，exit **2**；②`--bogus-arg` → `unknown arg`，exit **2**；③`--list` 列出全部 6 个 FI 且 exit 0；④**CLI 边界确定性**：同参数两次运行产物 sha256 完全一致（`2c9642…8eb`）；⑤自定义参数（`--camera CAM-42 --profile pantograph --onset 0 --recovery 0 --fi 05 --no-detections`）exit 0 且注入行如实记录 camera/profile/faults/时间参数、观测反映注入。
5. **验证自有扩展夹具**（`verify_sim_ext.cpp`，g++ `-std=c++17 -O1 -Wall -Wextra` 零告警编译，链接入口刚构建的 `librailhmi_core.a`，无 Qt/GStreamer/SQLite）: **49 断言 49 通过 / 0 失败**。按 33 组时间线上逐 tick 解析观测：
   - `parse_fault` 全别名/负例矩阵（7 条近义拒收）；
   - FI-01 窗口收敛：onset 前 live+presentable、onset 即 RECONNECTING(count1)、第二错升级 OFFLINE(count2=阈值)（与断言的 8-tick 窗口一致）、窗口内 `presentable=false` 全程、恢复边界回 live、窗口内零报警、summary 报警数与观测行数一致、未注 FI-10 时 gaps=0；
   - **FI-01 与 FI-02 同时注入时 cut 优先**：全程无 frozen（登记为已确定语义，见 §2 判据表备注）；
   - FI-02 单独注入完整状态进程：frozen(1250)→reconnect(1750)→connecting(2000)→offline(2500)→恢复 live(3000)，逐态时间与 `StreamThresholds` 推导一致；
   - FI-10 合并包络：窗口 [1000,3000) 内 8 次相邻注入合并为单一缺口 [1000,3000)、gap_count 恒 1、synthesize `gaps=1`；
   - FI-04/FI-05 逐 tick on/off 精确划分（4/8/5 与 4/8/5，无遗漏行）；
   - FI-03 `recovery<=onset` 全程不解除：17/17 tick ai unavailable、0 报警、stream 全程 live（AI 退出不影响流）、其余通道保持健康；
   - 第二配置（step 100，5 故障）双冷实例 byte-identical（确定性）、注入行 imp_id 配置顺序；
   - 观测格式契约：每行 `t=<ms> type=<t>`、注入行记录全部注入参数、末行为 t=duration 的 summary。
6. **运行器安全版式**: known_hosts 指纹不匹配即中止（RejectPolicy）；connect/auth/banner/SFTP 全显式超时（10s/120s/1200s 级别）；板端目录与 /tmp 夹具产物按名清理（cleanup 步骤 exit=2 为末尾 `ls` 对已删目录的确认输出，与前几轮一致，非失败）。

## 2. 判据逐条核对（工作项描述 / TC-SIM-01 模型层档）

| 判据 | 结果 | 证据 |
|---|---|---|
| 模拟帧源节拍 + 故障注入进入对应状态 | ✅ | 扩展夹具 FI-01 断流→reconnecting→offline、FI-02 冻结→frozen→…、FI-03 ai unavailable、FI-04 storage low、FI-05 tolerance fail、FI-10 gap 落点逐 tick 实测（49/49） |
| 故障后可恢复 | ✅ | 每类故障在 recovery 边界后回 live/low=false/tolerance_ok=true/合并不残留；summary `final_state=live`（56 处夹具断言 + CLI 复现 6000ms summary） |
| FI-01..FI-05、FI-10 软件开关/CLI 参数 | ✅ | `FaultKind` 六项 + `parse_fault` 多形式；CLI `--fi` 接受 `01,02,…`,`fi-XX`,别名；`--list` 实测 6 项 |
| 无需相机即可驱动状态 | ✅ | 板上独立目录构建运行，无相机、无 Qt（Qt=no）、无 GStreamer 链路；`SIM_EXIT=0` |
| 注入参数与观测写入结构化日志 | ✅ | 注入行记录 camera/profile/faults/onset/recovery/duration/step；63 行 8 类观测；`.log`+`.json` 双格式 |
| 可重置的模拟检测序列按 ai_profile 注入 | ✅ | 首方 274 内含 pantograph→L2、none→0 报警、front→person_on_rail；验证轮 CLI 复现与实现者逐行一致；两次运行 byte-identical（CLI 边界）+ 夹具内第二配置双实例 identical（模型层） |
| 结构化日志可驱动后续联调（隐含验收） | ✅ | JSON 独立逐元素解析成功；格式契约夹具核对 |

> 边界声明（与本验证结论一致，不外推）：本工作项按 TEST_DESIGN 的**模型层档**验收（与实现 README §3 一致）——QML 页面接线板端 Qt5 Quick/Qml 缺失（E-01b 已登记）不在本层、端到端页面联调不在本工作项；本验证不宣称六页面已可用。

## 3. 实现者归档核对（BUILD 证据 vs 实物）

| 项 | 实现 r2 归档 | 主机/板端核对 | 一致 |
|---|---|---|---|
| transcript 步骤 | mkdir/upload 70 文件/testrun/sim-inject/download×4/artifacts/cleanup 全 exit=0 | transcript.json 逐项相符；upload 70 与本地 `app/` 实树逐文件复算一致 | ✅ |
| 完整测试 stdout | 归档 log 274 PASS 行 + RESULT + junit 行 | 板上独立复现 274 [PASS] 行程序化逐行一致 | ✅ |
| JUnit XML | tests=274 failures=0 | 独立解析相同 | ✅ |
| sim-inject log+JSON | 63 行 + 63 entries 8 类 | 板上同参数复现 63 行逐行一致 + JSON 可独立解析 | ✅ |
| 入口命令/参数 | TestRun mode + `--fi 01,02,03,04,05,10 --duration 6000` | 验证以同级参数复跑，行为一致 | ✅ |

## 4. 验证侧自查更正（保持证据链诚实）

- **v1 过程轮**（留痕 `VERIFY-FIXTURES-VERIFY-20261016-v1.*`）：运行器板端路径错误（`cd %s` 漏 `/app`，入口与二进制全程 127），无板面结论，不作判据。
- **v2 过程轮**（留痕 `*-v2.*`）：运行器路径修正后入口/sim-inject/负例全部复现成立；扩展夹具编译失败（`count_obs` 无时间窗重载），无夹具结论，不作判据。
- **v3 权威轮**：`count_obs` 增加窗口重载 + 显式 `long long` 字面量后零告警一次通过；v1/v2 的失败均为**验证侧运行器/夹具构造错误**，非被测对象问题。

## 5. 观察与边界（不改变 PASS 结论；交实现参考）

- **O-1（观察，首方断言质量问题）**: `test_simulation.cpp:152` `no fault summary alarms=1` 用 `summary.find("alarms=1")`——子串匹配同样命中 `alarms=10..19`（实测 6000ms/500ms 步长无故障场景按 conf=1/suppress=500 语义产生 >1 条报警，该弱断言仍 PASS）。属第一方自身用例精确度，非本工作项判据缺口；建议改用 atoi 精确比较（交实现参考）。
- **O-2（观察，CLI 参数校验不对称）**: `sim_inject.cpp:63` 忽略 `parse_ai_profile` 返回值——非法 `--profile` 静默落到默认 `front`（与 `--fi` 非法时 exit 2 不对称）。注入行如实记录实际生效的 profile（非静默掩盖），但按本项目"不静默默认"惯例建议补返回值检查并 exit 2（交实现参考）。
- **边界**: 本层为模型层验证（TEST_DESIGN TC-SIM-01 的 ENV-A0/模型档），不覆盖 UI/渲染/真实相机；`chip` 级页面驱动留待平台 Qt 补齐后的 UI 工作项。板端本机（aarch64）实测不代表主机可运行；主机 Windows 无 C/C++ 工具链（历史 E-00 缺口）未变。
- **性能口径**: 夹具运行 33 组配置 + 入口 274 测试均在秒级完成；无长时运行，无遗留后台任务或无主进程（述明：本轮开创的全部板端进程在 cleanup 步骤已按名结束，transcript 留痕）。

## 6. 复现

```sh
# 验证运行器（板上独立目录 + 交付入口本身 + sim-inject 复现 + CLI 负例/确定性/自定义 + 扩展夹具 + 按名清理）
bash docs/hmi/test_evidence/ITEM-009-simulation/run_verify_round.sh
#   内部调用:
#   python docs/hmi/test_evidence/ITEM-009-simulation/verify_sim_fixtures.py \
#     --host 192.168.0.103 --user admin --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
#     --expected-fingerprint SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU \
#     --cpp docs/hmi/test_evidence/ITEM-009-simulation/verify_sim_ext.cpp \
#     --outdir docs/hmi/test_evidence/ITEM-009-simulation --stamp VERIFY-20261016-v3
# 仅复现实现者运行器口径:
#   python app/tools/board_build.py --host 192.168.0.103 --user admin \
#     --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
#     --outdir docs/hmi/test_evidence/ITEM-009-simulation --stamp <tag>
```

## 7. 证据文件

- `VERIFY-FIXTURES-VERIFY-20261016-v3.{log,transcript.json}` — 权威轮原始日志/逐命令 transcript（指纹一致、ENTRY_EXIT=0、SIM_EXIT=0、负例 2/2、列表、确定性 sha256、自定义参数、EXT 49/49、清理确认）
- `VERIFY-FIXTURES-VERIFY-20261016-v3.core-evidence_core-tests-VERIFY-20261016-v3.{log,xml}` — 板上交付入口自身归档的完整 stdout 与 JUnit XML（274/0；与实现者 r2 逐行一致）
- `VERIFY-FIXTURES-VERIFY-20261016-v3.core-evidence_sim-inject-VERIFY-20261016-v3.{log,json}` — sim-inject 复现 63 行 log/JSON（与实现者逐行一致）
- `VERIFY-FIXTURES-VERIFY-20261016-v{1,2}.{log,transcript.json}` — 过程轮留痕（运行器路径错误与夹具编译错误，§4）
- `verify_sim_ext.cpp` / `verify_sim_fixtures.py` / `run_verify_round.sh` — 验证自有夹具与运行器
- `build-TestRun-192_168_0_103-20261015-ITEM009-r2.*` — 实现者归档（实现轮产物，未修改）
