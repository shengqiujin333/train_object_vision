# UI 实现评审证据 — ITEM-003（分层 CMake 工程骨架与三档构建入口）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `5e7746a`，2026-10-15 22:07:03 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/CMakeLists.txt`、`app/build.sh`、`app/src/{core,platform,media,ui}`、`app/tests/core`、`app/tools/board_build.py`
  - 构建证据: `docs/hmi/test_evidence/ITEM-003-build/README.md`
- 被评工作项: `ITEM-003`（建立分层 CMake 工程骨架与三档构建入口：`railhmi-core` 仅 C++ 标准库 + SQLite 可选、`railhmi-platform`、`railhmi-ui+media` target 专用 Qt/GStreamer；`target/preview/CoreTests` 三开关与 `build.sh`；无工具链时明确缺失依赖提示而非静默失败）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2（`ef47841`）、`docs/hmi/VISUAL_DESIGN.md` v2 + `docs/hmi/design_tokens.yaml` v2（`40ccf4e`）、`docs/hmi/DESKTOP_APP_DESIGN.md` v2 §5.2/§15.1（`e691118`）
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（commit `c85987c`；`docs/hmi/test_evidence/ITEM-003-build/VERIFY-ITEM-003.md`）
- 评审日期/环境: 2026-10-15；Windows 主机（MINGW64 x86_64，无 C/C++ 工具链），Python 3.13.9；**本轮未连接 RK3588 目标板**（板端事实采用上游软件验证的独立板端复跑，见 §0.3）
- 判定: **TEST_PASS（范围严格限定为 ITEM-003 构建骨架交付物；不构成任何 UI 视觉/交互或 UI 可运行结论）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-003**（分层工程骨架与构建入口）。判定对象是任务内嵌用户可观察点：具备对应工具链时 CoreTests / Preview 编译成功、板端 target 编译成功并产出二进制；无工具链时构建脚本给出明确缺失依赖提示而非静默失败；分层符合设计（core 无 Qt/GStreamer/相机）。
2. **本次未对任何界面做视觉/交互还原评审**：本项交付的是**工程骨架**，`app/src/ui/qml/main.qml` 仅为一张骨架窗口（居中品牌文字），无页面、无状态、无交互；`app/README.md` 自述“尚未交付完整应用”。因此本文不对布局、组件、字体、图标、颜色语义、焦点、空/错/恢复状态做任何 PASS 结论（§5 D-05）。
3. **本轮未重新连接目标板**：板端四模式构建已由上游软件验证独立复跑（`…-verify-20261015-v1`，指纹与实现者一致）。本 capability 的授权方法为 `read/ui_evidence/design_assets`；本轮以**主机复跑 `build.sh` 无工具链路径 + 源码/证据审阅**为独立证据，并明确标注引用关系。
4. 依赖缺口（板端 Qt5 Quick/Qml 开发包缺失 E-01b、SQLite3 dev 缺失、主机 C/C++ 工具链缺失 E-00）按职责交平台/rootfs 与环境；本评审不判定其“不可修复”，不返回 BLOCKED。
5. 本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `5e7746a`（2026-10-15 22:07） | 新增分层 CMake（core/platform/media/ui/tests）、`build.sh` 五入口、`board_build.py` 板端构建运行器、ITEM-003 证据全量归档；`app/README.md` 状态更新；无范围外改动 |
| 交互设计 | `ef47841` | v2；本项只消费“UI 可构建/可预览”的工程前提，不核验界面 |
| 视觉设计 / 令牌 | `40ccf4e` | v2；骨架 `main.qml` 已引用与令牌一致的色值，但硬编码（见 §5 D-04） |
| 桌面应用设计 | `e691118` | §5.2 分层/目录、§15.1 构建入口为本项判据来源 |
| 软件验证 | `c85987c` | ITER2 TEST_PASS；板端四模式独立复跑 + 静态分层审查成立 |

实现自述与仓库证据一致：`app/README.md` 状态表、`ITEM-003-build/README.md` 与板端日志一致；**未把板端 aarch64 构建写成“主机预览成功”，未把缺依赖写成成功**。

---

## 2. 评审方法

1. 源码审阅：`app/CMakeLists.txt`（开关与子目录编排）、`app/build.sh`（模式解析/依赖自检/退出码）、`app/src/{core,platform,media,ui}/CMakeLists.txt`、`app/src/ui/{src/main.cpp,qml/main.qml,qml.qrc}`、`app/tests/core/{CMakeLists.txt,main.cpp}`、`app/tools/board_build.py`。
2. 文档审阅：`app/README.md`、`docs/hmi/test_evidence/ITEM-003-build/README.md`、`docs/hmi/test_evidence/ITEM-003-build/VERIFY-ITEM-003.md`。
3. 归档证据核对：板端 CoreTests/MediaCheck/Release/Preview 日志与 transcript。
4. 独立复现（本机实机执行）：在 Windows 主机运行 `bash app/build.sh {check,CoreTests,Preview,Bogus}`，核对无工具链时的提示与退出码。
5. 判据逐条对账 + UI 一致性抽检（`main.qml` 色值 vs `design_tokens.yaml`）。

**方法限制**：主机无 C/C++ 工具链与 Qt → 不能独立编译任何目标；无 UI 产物 → 不能做界面评审。板端构建结论采用上游软件验证 ITER2 复跑证据。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | `bash app/build.sh check` | `MISSING BUILD DEPENDENCIES (mode=check): cmake; C++ compiler`，**退出码 1**，非静默 |
| R2 | `bash app/build.sh CoreTests` | 同上逐项列出 cmake/编译器缺失，**退出码 1**（本机无最小工具链，按判据“无工具链→明确提示”成立） |
| R3 | `bash app/build.sh Preview` | 逐项列出缺失，**退出码 1**，非静默 |
| R4 | `bash app/build.sh Bogus` | 打印 `unknown mode 'Bogus' (expected Release|Preview|CoreTests|check)`，**退出码 2**；**提示的期望模式清单漏了已文档化的 `MediaCheck`**（见 §5 D-01） |
| R5 | 审阅 `app/tools/board_build.py` | `load_system_host_keys()`（默认 RejectPolicy，无 AutoAddPolicy）；连接/命令/SFTP 全超时；凭据仅取环境变量；`cleanup`（`rm -rf`，非 `--keep-remote`）——安全与清理边界与项目规则一致 |
| R6 | 板端 CoreTests 复跑日志核对 | `BUILD_EXIT=0`、`== 9 passed, 0 failed ==`、`TEST_EXIT=0`、产物 `build-core/tests/railhmi-core-tests`（41312 B）、`[SKIP] sqlite tests`、`SQLite3 dev not found -> SqliteStore disabled`（明确提示）；指纹与实现者一致 |
| R7 | 板端 Preview 复跑日志核对 | `mode=preview host=aarch64`、`BUILD_EXIT=1`、列出缺 `Qt5 Quick/Qml dev for host preview (qtbase5-dev qtdeclarative5-dev qml-module-qtquick2)`（见 §5 D-02/D-03） |
| R8 | UI 一致性抽检 | `main.qml` 使用 `#0E1116`/`#E6EDF3`/`#9BA7B4`/`#6E7A88`，与 `design_tokens.yaml` 的 `bg_base`/`text_primary`/`text_secondary`/`text_muted` 逐一相符；但为硬编码（§5 D-04） |
| R9 | 分层抽检 | `app/src/core` 内 `grep` 无 Qt/GStreamer/sqlite 头文件引用 → core 层纯净 |

---

## 4. 判据逐条核对（ITEM-003 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 具备对应工具链时 CoreTests 编译成功 | 板端 aarch64（最小工具链 C++/CMake）构建成功，测试 9/9 通过（R6，上游复跑一致） | **满足** |
| 具备对应工具链时 Preview 编译成功 | **本环境未核验**：板端与 Windows 主机均缺 Qt5 Quick/Qml 开发包（E-01b/E-00） | 未核验（环境缺口，非实现缺陷） |
| 板端 target 编译成功并产出二进制 | **本环境未达成**：同一 Qt5 Quick/Qml 开发包缺口阻塞 UI 层；`build.sh` 行为正确（明确 MISSING + 退出 1）；媒体层已证可编译（MediaCheck 成功） | 未核验（环境缺口，非实现缺陷） |
| 无工具链时给出明确缺失依赖提示而非静默失败 | 主机 R1–R3 与板端 target/preview 日志均逐项列出缺失并退出 1 | **满足** |
| 分层：core 仅 C++ 标准库、无 Qt/GStreamer/相机 | `app/src/core` 无外部头引用；`src/core/CMakeLists.txt` 不引入外部依赖；platform SQLite 条件编译；media 仅 target/media 档 | **满足** |
| 三档开关 + build.sh | `RAILHMI_BUILD_{TARGET,PREVIEW,CORE_TESTS,MEDIA}`；`build.sh` `Release/Preview/CoreTests/MediaCheck/check`；未选模式时 CMake FATAL | **满足** |

---

## 5. 具体偏差与修复要求

**D-01（可复现的小缺陷）— `build.sh` 未知模式提示漏列已文档化的 `MediaCheck`**
- 观察：`./build.sh Bogus` 打印 `unknown mode 'Bogus' (expected Release|Preview|CoreTests|check)`，但脚本头、`usage()` 与 README 均把 `MediaCheck` 作为正式入口（本评审 R4 实测）。
- 复现路径：`bash app/build.sh Bogus`（退出码 2）；对照 `app/build.sh` 头部用法注释与 `app/README.md` 构建段。
- 期望：提示清单包含 `MediaCheck`（及 `media` 别名），与头部用法/README 一致，避免用户误以为没有媒体层检查入口。

**D-02（UI 相关依赖提示不完整）— `build.sh` 的 Qt 安装提示缺 `Controls2`/`Layouts`**
- 观察：`build.sh` 在 target/preview 缺 Qt 时提示 `qtbase5-dev qtdeclarative5-dev qml-module-qtquick2`（板端日志 R7 原文），**未含** `qml-module-qtquick-controls2` 与 `qml-module-qtquick-layouts`；而 `app/src/ui/CMakeLists.txt` 的 FATAL 信息**已包含**这两项，且 `VISUAL_DESIGN.md` 的组件/布局层依赖 QtQuick Controls2 与 Layouts。
- 影响：操作者按 `build.sh` 提示安装后，target 可能通过编译，但 QML 在运行期因缺 `Controls2`/`Layouts` 导入失败 —— 会把“缺包”问题推迟到运行时。
- 复现路径：`read app/build.sh`（三处 `MISSING+=("Qt5 Quick/Qml ...")`）；`read app/src/ui/CMakeLists.txt`（FATAL 提示集合）；`read docs/hmi/VISUAL_DESIGN.md` §6/§9。
- 期望：把 build.sh 的 Qt 安装提示与 `ui/CMakeLists.txt` 的 FATAL 集合对齐，补齐 `qml-module-qtquick-controls2`、`qml-module-qtquick-layouts`（如设计后续还需 `qtquick-window`/`qtquick-dialogs`，一并列出），使“缺失依赖提示”覆盖 UI 真正需要的包集。

**D-03（证据口径，UI 开发路径未打通）— `Preview` 从未在 x86_64 上运行，且板端“preview”证据 host=aarch64 易误读**
- 观察：`Preview` 档设计目标是 **x86_64 主机预览**（`RAILHMI_BUILD_PREVIEW + RAILHMI_SIMULATION`），但当前唯一“Preview”证据是在 **aarch64 板端**跑 `build.sh Preview` 得到的（R7 `host=aarch64`），且因缺 Qt 直接失败。因此**没有任何环境真正构建/运行过预览界面**。
- 影响：UI 工作（ITEM-004+）目前缺少可运行的开发/预览载体；若不补齐宿主 Qt 工具链与 x86_64 预览路径，界面开发与后续 UI 实现评审核将长期无运行素材。
- 期望：①环境/平台侧按 E-00 缺口提供宿主 Qt5 Quick/Qml 工具链（或明确 x86_64 预览的替代执行环境）；②`board_build.py`/证据命名对“在板端尝试 Preview”显式标注为“aarch64 宿主尝试，非 x86_64 开发预览”，避免与真正的宿主预览混淆。

**D-04（UI 一致性，暂可接受但须收敛）— 骨架 QML 硬编码设计令牌值**
- 观察：`app/src/ui/qml/main.qml` 直接写 `#0E1116`/`#E6EDF3`/`#9BA7B4`/`#6E7A88`，注释标注“后续由令牌生成层接管，ITEM-004”。当前数值与 `design_tokens.yaml` 一致（R8）。
- 期望：ITEM-004 落地令牌生成层后，QML 从令牌读取，不在界面散落硬编码色值（`VISUAL_DESIGN.md` §12 实现规则第 1 条）；在接替前若调整令牌，须同步 `main.qml`，避免过渡期颜色漂移。

**D-05（范围缺口，非本项缺陷）— 无可评审的 UI**
- 观察：`main.qml` 仅居中品牌文字骨架，无页面/状态/交互；无截图/录屏。
- 期望：ITEM-004 起产出可运行 UI 后，UI 实现评审按 `INTERACTION_DESIGN` §4–§11 与 `VISUAL_DESIGN` §7–§12 逐状态核对，并要求运行录屏/交互记录；静态图不足以证明交互行为。

### 已确认无问题/正向项

- 最小工具链 CoreTests 真实构建 + 9/9 通过（R6），SQLite 用例按条件 SKIP 且打印明确 note（非静默）。
- MediaCheck 媒体层（GStreamer + librga）编译成功，证明 target 媒体层可构建。
- 分层清晰：core 纯净、platform SQLite 可选、media 仅 target/media 档、ui 依赖 Qt 并在缺失时 FATAL。
- 无工具链时 target/preview/coretests/check 均逐项报错退出 1，未静默失败（R1–R3）。
- `board_build.py` 安全边界（RejectPolicy/全超时/凭据 env）与板端 `/tmp` 清理（R5、R6 `cleaned`）符合项目规则。
- `main.qml` 色值与令牌一致（R8）。

---

## 6. 未覆盖 / 未执行（不得外推）

1. 目标板任何命令本轮**未执行**；板端构建结论采用上游软件验证 ITER2 复跑证据。
2. 任何界面视觉/交互还原（骨架窗口，无 UI 产物）。
3. target（aarch64 应用二进制）与 preview（x86_64 预览）**均未在任何环境编译成功**（Qt5 Quick/Qml 开发包缺口 E-01b；宿主 Qt 缺口 E-00）。
4. 运行期行为（QML 加载、Controls2/Layouts 导入、令牌渲染）— 归 ITEM-004+。
5. SQLite 持久化用例 — 板端缺 `libsqlite3-dev`，SKIP；归 ITEM-007/008。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-003 构建骨架交付物）。
- 依据：任务内嵌用户可观察点中可核验的部分——CoreTests 编译成功且 9/9 通过（板端复跑一致）、无工具链时逐项明确报错而非静默失败（主机 R1–R3 与板端日志）、分层符合设计（core 无 Qt/GStreamer/相机）、三档开关与 `build.sh` 存在且可用——经源码审阅、主机复跑与归档证据核对逐条成立。
- 明确边界：本 PASS **不表示** target/preview 已编译成功（受已登记 Qt5 Quick/Qml 开发包缺口与环境缺口阻塞，非实现缺陷），**不表示**任何 UI 可运行或视觉/交互通过。
- 后续修复要求见 §5：D-01（未知模式提示漏 MediaCheck）、D-02（Qt 安装提示缺 Controls2/Layouts）为可低成本补齐的具体项；D-03 需打通 x86_64 预览路径并澄清板端“preview”证据口径；D-04 待 ITEM-004 令牌层收敛；D-05 待 ITEM-004 产生可运行 UI 后再评审。
