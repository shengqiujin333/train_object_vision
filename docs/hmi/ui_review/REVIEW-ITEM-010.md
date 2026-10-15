# UI 实现评审证据 — ITEM-010（设计令牌主题层）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `b15db1d`，2026-10-16 01:16:52 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/tools/gen_tokens.py`、`app/tools/check_no_hardcoded_ui.py`、生成物 `app/resources/tokens.json`、`app/src/ui/qml/{Theme,Tokens}.qml`、`app/src/ui/qml/qmldir`、消费方 `app/src/ui/qml/main.qml`、`app/src/ui/CMakeLists.txt`（`railhmi_tokens` 生成目标）、`app/src/ui/qml.qrc`
  - 构建证据: `docs/hmi/test_evidence/ITEM-010-tokens/README.md`
- 被评工作项: `ITEM-010`（构建期由 `docs/hmi/design_tokens.yaml` 生成 `tokens.json`/`Tokens.qml`；运行时 `Theme` 单例提供颜色/字号/间距/圆角/z-index，含 `alarm L1/L2/L3`、`stream_state`、`mode`、`integration_state`、`detection_badge`；QML 与 C++ UI 不出现硬编码色值/字号；用户可观察：改令牌→重新生成→重启界面变化，静态扫描无硬编码十六进制色值）
- 参照设计: `docs/hmi/design_tokens.yaml` v2（`40ccf4e`）、`docs/hmi/VISUAL_DESIGN.md` v2 §12 实现规则（`40ccf4e`）、`docs/hmi/DESKTOP_APP_DESIGN.md` §5.2、`docs/hmi/TEST_DESIGN.md` TC-TOKEN-01/TC-SMOKE-15
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-010-tokens/VERIFY-ITEM-010.md`）
- 关联前序评审: `docs/hmi/ui_review/REVIEW-ITEM-003.md` D-04（骨架 QML 硬编码令牌值）、`REVIEW-ITEM-008.md` D-03（core 枚举 token 无自动身份断言）
- 评审日期/环境: 2026-10-16；Windows 主机（MINGW64 x86_64，Python + PyYAML 可用，无 C/C++/Qt 工具链）；**本轮未连接 RK3588 目标板**（板端事实采用上游软件验证复跑；本项的生成器/扫描器在本机可独立复现，见 §3）
- 判定: **TEST_PASS（范围严格限定为 ITEM-010 令牌生成/主题层；不构成 QML 运行期视觉或交互结论）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-010**（设计令牌主题层）。判定对象是任务内嵌用户可观察点：由 `design_tokens.yaml` 构建期生成 `tokens.json`/`Theme.qml`；`Theme` 单例覆盖所需分组；UI 源码无硬编码色值/字号；改令牌→重生成→界面变化；静态扫描无硬编码十六进制色值。
2. **本次未做 QML 运行期视觉/交互评审**：板端缺 Qt5 Quick/Qml（E-01b），QML 无法构建运行；“重启后界面随令牌变化”的**运行期呈现**未验证。本文只评审生成物、消费方源码与扫描/生成契约（§5 D-06）。
3. **本项可在主机独立复现**：生成器与扫描器为 Python，本轮**实际在本机执行**了 `gen_tokens.py --selftest`、生成到临时目录并与已提交生成物逐字节比对、`check_no_hardcoded_ui.py` 及对抗性覆盖探针（§3），无需板端。
4. 依赖缺口（主机无 C/C++/Qt 工具链 E-00；板端无 Qt5 Quick/Qml E-01b）按职责交平台/环境；本评审不判定其“不可修复”，不返回 BLOCKED。
5. 本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `b15db1d`（2026-10-16 01:16） | 新增 `gen_tokens.py`/`check_no_hardcoded_ui.py` 与生成物；`main.qml` 改为消费 `Theme.*`；`ui/CMakeLists.txt` 加构建期生成目标；qrc 收录 Theme/Tokens/qmldir；无范围外改动 |
| 令牌 | `40ccf4e` | `design_tokens.yaml` v2（alarm/stream_state/mode/integration_state/detection_badge 等） |
| 视觉设计 | `40ccf4e` | §12 实现规则第 1 条“颜色/字号/间距/圆角/状态色从 design_tokens.yaml 读取，不得硬编码” |
| 软件验证 | `e54c869` | TEST_PASS（主机/板端 gen-selftest、ui-scan、274 回归一致） |

实现自述与仓库证据一致：`ITEM-010-tokens/README.md`、生成物与源码与 §3 独立复现一致；**未宣称 QML 已在板端运行**。

---

## 2. 评审方法

1. 源码审阅：`gen_tokens.py`（类型推断/分组/确定性/变更传播/`REQUIRED_GROUPS`）、`check_no_hardcoded_ui.py`（扫描规则/排除生成物）、`Theme.qml`/`Tokens.qml`/`qmldir`、`main.qml`、`ui/CMakeLists.txt`、`qml.qrc`。
2. 独立复现（本机实机执行）：`gen_tokens.py --selftest`；生成到临时目录并与已提交生成物逐字节比对；`check_no_hardcoded_ui.py`；对抗性覆盖探针。
3. 契约核对：`tokens.json` 与 `design_tokens.yaml` **深度值比对**；`Theme.qml` 分组/级别存在性；`main.qml` 无硬编码。
4. 归档证据核对：板端 r2 TestRun（gen-selftest、ui-scan、274 回归、sim-inject）、`VERIFY-ITEM-010.md`。

**方法限制**：主机无 Qt → 不能编译/运行 QML；运行期视觉呈现采用“生成物 + 消费方源码”静态核对，不作运行结论。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | `python app/tools/gen_tokens.py --selftest --yaml docs/hmi/design_tokens.yaml` | **9/9 全部 PASS**（含 10 组齐备、确定性、JSON 可解析、Theme 探针、变更传播），退出码 0 |
| R2 | 生成到临时目录并与已提交生成物逐字节比对 | `tokens.json`、`Theme.qml`、`Tokens.qml`、`qmldir` **全部 SAME**（字节一致）→ 确定性成立且提交物与生成器同步 |
| R3 | `tokens.json` 与 `design_tokens.yaml` 深度值比对 | 顶层 18 键一致、**值差异 0** → 生成物忠实反映令牌，无漏字段/改值 |
| R4 | `python app/tools/check_no_hardcoded_ui.py --root app/src/ui` | **PASS**（扫描 2 文件，无硬编码十六进制色值/字号）；另独立 grep 产品 UI（排除生成物）`Qt.rgba`/hex/命名色命中 **0** |
| R5 | `Theme.qml` 分组/级别存在性 | 10 组（color/alarm/stream_state/mode/integration_state/detection_badge/typography/spacing/radius/z_index）与 `alarm.L1/L2/L3` **全部存在** |
| R6 | 对抗性扫描探针（自建样例） | 命中 `#fff`/`#FF0000`/`#11223344` 与 `pixelSize/pointSize: <数字>`；**未命中** `color: Qt.rgba(1,0,0,1)` 与 `border.color: "white"`（见 D-01） |

---

## 4. 判据逐条核对（ITEM-010 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 构建期由 `design_tokens.yaml` 生成 `tokens.json`/`Tokens.qml` | 生成器 + CMake 目标 + qrc 收录（R2/R3） | **满足** |
| 运行时 `Theme` 单例提供颜色/字号/间距/圆角/z-index（含所列分组） | R5 全部分组与 alarm L1-L3 存在 | **满足** |
| QML/C++ UI 不出现硬编码色值/字号 | 产品 UI 扫描 PASS 且独立 grep 0 命中；`main.qml` 全用 `Theme.*` | **满足（十六进制/字号，见 D-01 覆盖范围）** |
| 改令牌→重新生成→重启界面变化 | `token change propagates to outputs` PASS（生成层）；运行期呈现未验证 | **生成层满足**；运行期未核验（D-06） |
| 静态扫描无硬编码十六进制色值 | R4 PASS；R6 证明 3/6/8 位十六进制均可检出 | **满足**（`Qt.rgba`/命名色不在当前扫描范围，见 D-01） |

---

## 5. 具体偏差与修复要求

**D-01（扫描覆盖缺口，UI 契约）— 仅检测十六进制与数字字号，未覆盖 `Qt.rgba(...)`/命名色与数字尺寸**
- 观察：`check_no_hardcoded_ui.py` 以 `HEX = #(3..8 hex)` 与 `(pixelSize|pointSize|fontSize)\s*[:=]\s*[0-9]` 为规则。对抗性样例（R6）显示 `color: Qt.rgba(1, 0, 0, 1)`、`border.color: "white"` 均**不产生违规**；同理 `width/height/spacing/radius/z` 的数字硬编码也不在范围。而工作项目标为“QML 与 C++ UI 不出现硬编码**色值/字号**”，`Qt.rgba`/命名色正是常见硬编码色值形态。
- 复现路径：`read app/tools/check_no_hardcoded_ui.py`；自建 `Probe.qml` 含上述两种写法后运行 `--root`，仅十六进制行被报。
- 期望：扩展扫描规则——增加 `Qt.rgba(`/`Qt.hsva(`、QML 命名色白名单（white/black/red/... 若确不允许）、以及尺寸类属性数字（可选）；或明确记录“本扫描只覆盖十六进制色值/字号，其余形态由评审复核”，使“无硬编码色值”的判据与工具一致。

**D-02（构建期生成健壮性）— CMake 回退分支未校验 PyYAML，缺 PyYAML 时会构建失败而非“跳过”**
- 观察：`ui/CMakeLists.txt` 仅以 `Python3_Interpreter_FOUND AND EXISTS design_tokens.yaml` 作为建目标的守卫；未检查 PyYAML。若目标机有 `python3` 但无 `PyYAML`，`gen_tokens.py` 因 `import yaml` 失败 **exit 2**，`railhmi_tokens` 目标失败；且 `railhmi` 通过 `add_dependencies(railhmi railhmi_tokens)` 依赖它，将导致 **UI 构建失败**，与注释“缺失时跳过并保留已生成物（不静默改设计）”不符。
- 复现路径：`read app/src/ui/CMakeLists.txt`（守卫缺 PyYAML 探测）；`read app/tools/gen_tokens.py`（`import yaml` 失败 `raise SystemExit(2)`）。
- 期望：用 `execute_process(COMMAND python -c "import yaml")` 探测 PyYAML，失败则进入“使用已提交生成物”分支（或把目标设为可选/非致命），让回退语义与注释一致。

**D-03（仓库卫生/生成物同步）— 生成物既提交入库，又在构建期写回源码树**
- 观察：`app/resources/tokens.json`、`app/src/ui/qml/{Theme,Tokens}.qml`、`qmldir` 均已提交；`railhmi_tokens` 目标在构建时把输出写到 `${CMAKE_SOURCE_DIR}/resources` 与 `${CMAKE_CURRENT_SOURCE_DIR}/qml`（源码树内）。当前生成确定（R2 字节一致），故无脏树；但一旦修改 `design_tokens.yaml` 而未提交重生成物，构建会就地改写受跟踪文件，产生未提交差异，与“提交前仓库必须干净”冲突。
- 期望：文档化“改令牌后必须重生成并提交”的步骤；或生成到构建目录并复制/QRC 引用；或对生成物加来源标记与同步校验（CI 比对 `tokens.json` 与 YAML）。与 REVIEW-ITEM-008 D-01 同源。

**D-04（观察，冗余/漂移风险）— `Tokens.qml` 与 `Theme.qml` 内容重复**
- 观察：两者由同一 `tokens` 映射生成，仅 `id` 不同（`theme` vs `tokens`），长度差 1 字节（R2 比对可见）。当前 `main.qml` 只用 `Theme`。
- 期望：明确二者职责（如 `Tokens`=原始数据、`Theme`=消费入口/别名），或删除未使用的 `Tokens` 单例，避免双写漂移；若保留，请在文档说明何时用哪个。

**D-05（测试精度）— 生成器自检对 Theme.qml 的分组探针不完整**
- 观察：`selftest` 的 QML 探针仅含 `bg_base`、`L1`、`stream_state`、`integration_state`、`detection_badge`；`mode`、`alarm.L2/L3`、`typography/spacing/radius/z_index` 仅在 YAML 层校验组存在，未在生成 QML 中断言。
- 期望：扩展为对**每个分组/级别**的生成断言；更佳做法是断言 `tokens.json`/`Theme.qml` 与 YAML 的深度值一致（本评审 R3 已手工验证），从而同时覆盖 REVIEW-ITEM-008 D-03 提出的“令牌身份一致性”守门。

**D-06（范围缺口，非本项缺陷）— QML 运行期呈现未验证**
- 观察：板端缺 Qt5 Quick/Qml（E-01b），QML 未编译/运行；“重启后界面随令牌变化”的运行期呈现无运行素材（无截图/录屏）。
- 期望：平台补齐 Qt 后，由 UI 任务运行 `railhmi`（或 preview）并录制“改令牌→重生成→重启”前后对比；UI 实现评审再按 `VISUAL_DESIGN` §7–§12 做还原审阅。

### 已确认无问题/正向项

- 生成器确定性且**提交物与生成器逐字节一致**（R2），`tokens.json` 与 `design_tokens.yaml` 深度值 **0 差异**（R3）。
- `Theme.qml` 覆盖工作项要求的全部分组与 `alarm L1/L2/L3`（R5）。
- `main.qml` 完全改为 `Theme.color.* / Theme.spacing.* / Theme.typography.scale.*`，无硬编码（R4）；直接关闭了 REVIEW-ITEM-003 D-04。
- 静态扫描已能检出 3/6/8 位十六进制与 `pixelSize/pointSize` 数字；产品 UI 当前 0 命中（R4）。
- 变更传播可用（R1 selftest），支持“改令牌→重生成”工作流。
- 生成物被 qrc 收录、CMake 目标接入构建依赖，避免“改了令牌但没进构建”的错位。

---

## 6. 未覆盖 / 未执行（不得外推）

1. QML 编译/运行与运行期视觉呈现（板端缺 Qt5 Quick/Qml E-01b；主机无 Qt）。
2. 目标板命令本轮未执行（生成器/扫描器已在本机独立复现，板端由上游验证覆盖）。
3. C++ UI 侧对 `tokens.json` 的消费（当前 C++ UI 仅骨架，未见令牌消费点）。
4. `Qt.rgba`/命名色/尺寸类硬编码（当前扫描未覆盖，见 D-01）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-010 令牌生成/主题层）。
- 依据：任务内嵌用户可观察点——构建期由令牌生成 `tokens.json`/`Tokens.qml`、`Theme` 单例覆盖所列分组、UI 源码无硬编码十六进制色值/字号、静态扫描通过、令牌变更可传播到输出——经源码审阅、**主机独立复现**（生成器 selftest 9/9、生成物字节一致、YAML↔JSON 深度值 0 差异、扫描 PASS）与归档证据核对逐条成立。
- 明确边界：本 PASS **不表示** QML 已运行或界面对令牌变化的运行期呈现通过，**不表示**扫描工具已覆盖 `Qt.rgba`/命名色等全部硬编码形态。
- 后续修复/交接要求见 §5：D-01（扫描覆盖）、D-02（PyYAML 守卫）为可低成本补齐项；D-03（生成物同步/仓库卫生）、D-04（Theme/Tokens 冗余）、D-05（自检探针/深度一致性）为工程改进；D-06 待 Qt 就绪后做运行期与还原审阅。
