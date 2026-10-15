# ITEM-011 软件验证 TestEvidence — 应用外壳与角色化导航（VERIFY-20261016-v2）

- 被验证对象: `n005_embedded_linux_engineer_desktop_app_implementation` 交付（commit **00d60ce**）
  本验证轮以当前仓库工作树（与 00d60ce 一致，git status clean）为被测快照。
- 交付入口: `app/tests/run_core_tests.sh`（Default=CoreTests + tokens-selftest + ui-scan + sim-inject）
- 判据来源（不改判据、不新增判据；运行期呈现项以实现证据 §3 边界如实标注）:
  `docs/hmi/INTERACTION_DESIGN.md` §8（权限矩阵/§8 列表行 1“无权限说明与返回”）、§11#10；
  `docs/hmi/DESKTOP_APP_DESIGN.md` §7.2；`docs/hmi/TEST_DESIGN.md` TC-SMOKE-07 / TC-ROLE-*；
  工作项 ITEM-011 文本（六页固化/角色过滤/模式位/受限直达/无切换控件）。
- 验证范围: **软件验证**——桌面应用外壳逻辑层（navigation 契约 + QML 外壳源码消费链）；
  不承担 MCU/电气测试。板端 Qt5 Quick/Qml 开发包缺失（E-01b，`build_env_check.md` §3.1、
  ITEM-010 `verify_board_qt_check.py` 实测），QML **运行期画面**本期无法在板上构建/启动，
  按“当前包实测 + mock（模型层）”分记，不以 mock 结论外推 UI 运行期呈现。
- 验证者自有工具链（本目录，全部超时/凭据约束与既往轮一致）:
  `verify_nav_ext.cpp`（验证自有断言）、`verify_nav_fixtures.py`（Paramiko 板端运行器，
  指纹校验不符即中止 + 全超时 + 凭据仅取 `TRAIN_RK3588_SSH_CREDENTIAL`）、`run_verify_round.sh`。

---

## 1. 判据总表

| # | 判据 | 来源/类型 | 实测 | 结论 |
|---|---|---|---|---|
| C-1 | 六页固定顺序与 id：`realtime/alarm_center/replay/all_cameras/ai_debug/maintenance`，`all_pages()` 6 项、首 Realtime 末 Maintenance | 工作项 TEXT | 板端 v2 测试 280-281 PASS + 夹具 order/subset | ✅ |
| C-2 | 司机仅可见实时/报警/回放：`visible_pages(Driver)`=3 项有序三元组，不含 all_cameras/ai_debug/maintenance | 交互 §8 | v2 板端 PASS + 夹具 `visible driver exact ordered triple` | ✅ |
| C-3 | 维护/研发/管理员可见全部 6 页 | 交互 §8 | v2 板端 PASS + 夹具 24 组合矩阵 + `maintainer visible == all_pages` | ✅ |
| C-4 | 受限直达返回 denied：司机直达 AI 调试 → `resolve_navigation.allowed=false` 且 `requested` 原样回显，UI 面为“无权限+返回”（`DeniedView.qml` 文“无权限/当前角色无权访问：X/返回”） | 交互 §8/§11#10 | 板端 PASS + 夹具 48 组合 echo/matrix | ✅（模型+QML 静态链） |
| C-5 | 默认落地页: 司机→实时 / 维护→全摄像头 / 研发→AI调试 / 管理员→设备维护，且默认页必在自身可见集内（夹具不变量） | README §1 设计 | 四角色 PASS + `maintainer default outside driver set` | ✅ |
| C-6 | 模式位固定: `primary=可见光`、`secondary=红外未接入`、`interactive=false`（无切换控件）；`TopStatusBar.qml` 模式位仅文本，无任何点击控件 | 工作项/交互 §11#14 | 板端 PASS + 夹具 repeat 稳定 + 源码审阅 | ✅ |
| C-7 | 外壳 QML 全部消费 `Theme.*`，无硬编码色值/字号 | 工作项/ITEM-010 口径 | 板端 `ui-scan` 6 文件 `SCAN_EXIT=0` + 主机源码审阅 | ✅ |
| C-8 | 外壳接线（qrc 注入 4 组件/页面、`main.cpp` 注入 railhmiNav/AllPages/Role/DefaultPage/Mode*、`main.qml` denied→DisabledView）与导航模型一致 | 源码审阅 | `main.cpp:41-67`、`main.qml:15-77`、`qml.qrc`、`CMakeLists` 核对 | ✅（静态链，无运行期结论） |
| C-9 | 回归: 既有 274 断言域不满、不退 | REG 口径 | 板端 319/319（旧域 274 全 PASS + 新导航域 45） | ✅ |
| C-10 | denied→返回复位、NavBar active 高亮、visible 过滤绑定 | 源码审阅 | `NavBar.qml` `navigation: navigate(pageId)`、`main.qml:52,58` 核对（静态） | ✅（受限直达/返回逻辑） |

**结论: 全部判据满足（模型层为主，QML 运行期呈现按 §5 边界如实标注）→ TEST_PASS。**
`app/config/app.yaml` `default_role: driver` 基线 → 司机角色下
`railhmiNav` 注入集 = {实时监控, 报警中心, 历史录像}，AI 调试/设备维护/全摄像头入口不渲染。

## 2. 独立板端复跑（v2, 2026-10-16）

Run-method: 独立 SSH 会话（Paramiko，指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`
与实现者 ITEM-011/前多轮一致），**独立目录** `/tmp/railhmi_verify011_board/app`，上传 app 快照 + design_tokens.yaml，
**不经实现者 build 目录直接执行交付入口** `bash tests/run_core_tests.sh`：

| 步骤 | 结果 |
|---|---|
| `run_core_tests.sh` 交付入口 | 构建成功（含 navigation.cpp 新链入），**319 passed / 0 failed**，`RESULT: PASS`，`ENTRY_EXIT=0`（12.25s） |
| 与实现者归档对比 | 319 PASS 行**逐行一致**（程序化比对，`…ITEM011-r1.core-evidence_core-tests…log`）；JUnit 双方均 319/0 |
| `gen_tokens.py --selftest` | `TOKENS_EXIT=0`（8/8） |
| `check_no_hardcoded_ui.py --root src/ui` | 扫过 6 文件（shell QML 全在），`SCAN_EXIT=0` |
| `railhmi-sim-inject`（FI-01..05,10） | `SIM_EXIT=0`；63 行 log + 63 项 JSON **与实现者归档完全一致** |

**验证者自有扩展夹具（verify_nav_ext.cpp, 137 断言）— 结果: 137/137 PASS（板端）**
超实现者 test_navigation.cpp 之外的独立断言（`VERIFY-FIXTURES-…-v2.log` §run-ext）：
- **权限矩阵全组合（4×6=24）** 逐对核验并与模型一致（司机 3 白名单，其余 3 角色全开）；
- `visible_pages` 不变量×4 角色: 保序、全为 all_pages 子集、非空；司机 = 精确有序三元组
  {实时/报警/回放}；维护列表恒等 all_pages。
- 默认页不变量×4: `default_page(r)` 任一角色均可达；维护默认（全摄像头）不在司机集合内。
- `resolve_navigation` 24 组合: `requested` 原样回显 + `allowed` 与矩阵一致（含司机直连 AI 调试 denied）。
- `parse_page` 负例矩阵: 全 6 id 往返 + 大写 `REALTIME`/空串/前导空格/尾随空格/`realtime_x`/`alarm`/`IR` 全拒绝。
- id/标题契约: 6 个 id 全小写 snake、两两不同；6 个中文标题两两不同、非空。
- `mode_display` 连续两次调用结果相等（契约稳定性）。

## 3. 证据文件（本轮新增/全部为验证者自产）

- `VERIFY-FIXTURES-VERIFY-20261016-v2.transcript.json` — SSH 指纹/递归 timeout/逐条命令/清理确认
- `VERIFY-FIXTURES-VERIFY-20261016-v2.log` / `.transcript.json` — 总过程留痕（含夹具全 137 断言行）
- `VERIFY-FIXTURES-VERIFY-20261016-v2.core-evidence_core-tests-…-v2.log` / `.xml` — 板端 319/319 + JUnit 独立解析
- `VERIFY-FIXTURES-VERIFY-20261016-v2.core-evidence_sim-inject-…-v2.log` / `.json` — 与实现者归档逐行一致
- `verify_nav_ext.cpp` / `verify_nav_fixtures.py` / `run_verify_round.sh` — 验证自主夹具与启动脚本
- 实现者参考资料（不作判据）: `build-TestRun-192_168_0_103-20261015-ITEM011-r1.*`

## 4. 过程留痕（非判据）

- **v1 轮（VERIFY-20261016-v1）**: 交付入口×独立板端复跑已 319/319 全 PASS；扩展夹具 12/126 失败
  **全部为验证侧夹具期望表达式构造错误**（把“司机白名单/维护全开”两段式条件写成了
  `(r==Driver) || 白名单页` 的析取，司机×受限页与维护×受限页期望被算反；12 失败 12 项全部
  自查可复算），板面模型一致。v1 留痕文件 `VERIFY-FIXTURES-…-v1.log`（**已随本能力 committed**、
  transcript、归档 core-tests/v2 同轮归档）保留作为证据链；判据轮为 v2。
- 板端产物复现方式: 独立实现目录 + 自有夹具；`/tmp/railhmi_verify011*` 夹具目录由验证轮**按名清理**，
  cleanup 步骤自证 `ls` 已不存在（left non-empty residue=0）。

## 5. 观察项与边界（交实现/后续 UI 验证参考，不影响本轮结论）

| # | 观察/边界 | 说明 |
|---|---|---|
| O-1 | **QML 运行期呈现未在板上实测** | 板端缺 Qt5 Quick/Qml 开发包（E-01b，历史实测）。“司机启动即不渲染受限入口”“直接导航显示无权限页 + 返回按钮可返回”“TopStatusBar 实际外观”属**运行期 UI 行为**，本轮仅在源码/接线层面核验（§1 C-4/C-7/C-8），**不改变判据，也不以主机 mock 结论冒充板上运行效果**；与实现证据 §3 边界一致。QML 运行期验证归入后续 UI 工作项（Qt 包补齐后）。 |
| O-2 | `check_no_hardcoded_ui.py` 扫描面 | 静态扫描 6 文件已包括外壳与全部组件/页面源码；"页面内容由后续工作项实现"占位文案属 PageView 设计内文本。 |
| O-3 | JUnit 重名 testcase | `maintainer/developer/admin can all pages` 循环生成 3 个重名 `testcase`、`offline cannot present as live` 重名继承自既有套件（ITEM-008 轮已观察并移交实现），本轮归属仍以前轮不足为准，不新增。 |
| O-4 | `default_role` 基线 | `app/config/app.yaml:5` `default_role: driver`；配置生效链在 ITEM-004/实现源码（main.cpp 启动即校验+注入），本轮不改配置。 |

## 6. 复现

```sh
# 用户在启动框架的同一 PowerShell 中先加载凭据（TRAIN_RK3588_SSH_CREDENTIAL），
# 再以 Git Bash 执行（脚本内不打印凭据值，只透传）:
VERIFY_STAMP=<stamp> bash docs/hmi/test_evidence/ITEM-011-shell/run_verify_round.sh
# 板上直接（可选）:
#   cd <app> && bash tests/run_core_tests.sh --outdir core-evidence --stamp <s>
#   python3 tools/check_no_hardcoded_ui.py --root src/ui
#   g++ -std=c++17 -Isrc/core/include verify_nav_ext.cpp build-core/src/core/librailhmi_core.a -o v && ./v
```

---

**Conclusion: TEST_PASS**（本文件即软件验证交付 `test` Evidence 的可读判据文档；结构化归档见 §3 同目录）
