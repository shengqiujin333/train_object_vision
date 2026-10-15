# ITEM-012 软件验证 TestEvidence — 配置驱动摄像头注册表与 UI 接入（VERIFY-20261016-v3）

- 被验证对象: `n005_embedded_linux_engineer_desktop_app_implementation` 交付（commit **0747577**）。
  本验证轮以当前仓库工作树（与 0747577 一致，git status clean，本轮开始前复核）为被测快照。
- 交付入口: `app/tests/run_core_tests.sh`（Default = CoreTests 构建 + 测试 + tokens-selftest + ui-scan + sim-inject）。
- 判据来源（不改判据、不新增判据）: 工作项 ITEM-012 文本（配置驱动注册表渲染槽位与排序、位置名、
  连接状态、检测职责徽标仅 P4/P6 且司机 P1 不显示、NVR 集成项登记 地址/已登记/状态/通道映射=未配置、
  仅改配置增删/改名一路相机即改变视图槽位与徽标、删除必填字段启动报出具体缺失项）；
  `docs/hmi/TEST_DESIGN.md` TC-SMOKE-06 / TC-BADGE-01 / TC-INTG-01；
  `docs/hmi/INTERACTION_DESIGN.md` §7.2/§7.3（注册表/字段）、§11#13（NVR 不虚构）；
  `docs/hmi/VISUAL_DESIGN.md` §7.11/§7.12；`docs/hmi/DESKTOP_APP_DESIGN.md` §8.1/§12。
- 验证范围: **软件验证**——注册表/徽标/NVR 集成项模型层 + QML 源码消费链静态核对；
  不承担 MCU/电气测试。板端 Qt5 Quick/Qml 开发包缺失（E-01b，`build_env_check.md` §3.1、
  ITEM-010 `verify_board_qt_check.py` 板端实测），QML **运行期画面**本期无法在板上构建/启动；
  按"当前包实测（模型层 + 静态链）与 mock 分记"，不以模型结论外推 UI 运行期渲染（实现者 README §3
  边界一致，如实沿用）。
- 验证者自有工具链（本目录，全部超时/凭据约束与既往轮一致）:
  `verify_registry_ext.cpp`（验证自有 54 断言，纯 railhmi-core 无 Qt）、
  `verify_registry_fixtures.py`（Paramiko 板端运行器：指纹校验不符即中止 + 全超时 +
  凭据仅取 `TRAIN_RK3588_SSH_CREDENTIAL` + 按名清理 + 与实现者归档日志程序化逐行比对）、
  `run_verify_round.sh`（启动脚本）。

---

## 1. 判据总表

| # | 判据 | 来源/类型 | 实测 | 结论 |
|---|---|---|---|---|
| C-1 | 基线注册表 6 槽位，按 cameras.yaml 顺序 CAM-01..06，order 0..5 连续，position six-value 全对 | 工作项 TEXT | 板端 348/348 含 25 条注册表域 + 夹具 d4/order matrix | ✅ |
| C-2 | 位置名/ai_enabled/徽标来自配置：`badge front/none/pantograph/wheel rail/cab door` 五值精确且两两不同 | 工作项/视觉 §7.12 | 板端 PASS + 夹具 d1/d1b/d6 全槽位逐一致 | ✅ |
| C-3 | 徽标作用域: 仅 P4/P6，司机 P1 及 P2/P3/P5 不显示 | 工作项 | 板端 PASS + 夹具 d3 六页全真值表（仅 AllCameras/Maintenance true）| ✅ |
| C-4 | 仅改 `ai_profile` 徽标即变（不改代码）: CAM-02 none→front 变"前方障碍/人员"、CAM-01 front→none 变"广角·无检测" | TC-BADGE-01 | 夹具 d2a..d2g 板端 PASS（实现者 self-test 未覆盖此配置驱动徽标变化维度）| ✅ |
| C-5 | 配置即视图: 改名（CAM-04）后 slot.name 变且 order/position 保留；删首位/删中间 5 槽位 order 重编号 0..4、相邻槽位拼接；CAM-07 插入 CAM-06 前 7 槽位占据配置位置；六路完全倒序注册表完全跟随 | TC-SMOKE-06 | 板端 PASS + 夹具 r1..r5（含 r5d 全倒序跟随）| ✅（模型层; 结构变更需重启+界面提示归运行期核） |
| C-6 | 删除必填字段启动报出**具体缺失项**: 非零退出 + 字段路径 | 工作项 | 板端 `cameras[0].rtsp_url`（实现者）+ 夹具 b1/b2/b3 验证轮换槽位换字段: `cameras[5].rtsp_url`/`cameras[2].name`/`cameras[1].credential_env` 全部逐字路径命中 | ✅ |
| C-7 | NVR 集成项: id=NVR/`192.168.1.4`/已登记/状态 unconfigured/通道映射"未配置"/协议"—"，**无虚构通道号** | 工作项/交互 §11#13 | 板端 PASS + 夹具 n0c 全字段逐项核对 | ✅ |
| C-8 | NVR 集成项对配置变化语义: 补全 protocol+channel_mapping → unverified + 通道映射转"已配置"；registered=false → unverified 且映射仍"未配置"；删整段 → `applied_defaults` 记录 + 空列表 | 夹具自有（契约内, camera_registry.cpp 语义） | 夹具 n1c/n2c/n3d 板端 PASS | ✅ |
| C-9 | UI 接线静态链: `main.cpp` 注入 `railhmiCameras`/`railhmiIntegrations`（由 `build_registry`/`build_integrations` 配置派生）; `CameraTile`(title/state/badge/aiEnabled/showBadge) 被 P1(`showBadge:false`)/P4/P6(`showBadge:true`) 消费; `DeviceCard`(devName/address/registered/integrationState/protocol/channelMapping) 被 P6 消费; `main.qml` 按 id 选择页面 | 工作项/源码审阅 | `app/src/ui/src/main.cpp:58-96`、`P1Realtime.qml`(`showBadge:false`)、`P4AllCameras.qml`、`P6Maintenance.qml`、`main.qml:30-38` 逐项核对；**运行期渲染**留 E-01b 边界 | ✅（静态链; 无运行期结论） |
| C-10 | UI 源码无硬编码色值/字号 | 工作项/ITEM-010 口径 | 板端 `check_no_hardcoded_ui.py --root src/ui`: **12 文件，SCAN_EXIT=0**（含本轮新增 5 个 QML）| ✅ |
| C-11 | 与实现者归档完全一致（回归不因新一轮实现修改而漂移） | REG 口径 | 板端复跑 348/348，348 条 PASS 行与 `...ITEM012-r2...log` **逐行一致**（程序化比对 identical=True）；sim-inject 63 行与实现者归档 **完全一致** | ✅ |
| C-12 | 徽标独立于运行时字段: `stream_state` 为配置无关占位（"connecting"），不随配置变化 | 模型契约 | 夹具 d5 板端 PASS | ✅ |

**结论: 全部判据满足（模型层为主，QML 运行期呈现按 §5 边界如实标注）→ TEST_PASS。**

## 2. 独立板端复跑（v3, 2026-10-16 官方判据轮）

Run-method: 独立 SSH 会话（Paramiko，指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`
与实现者 ITEM-011/ITEM-012/前多轮一致），**独立目录** `/tmp/railhmi_verify012_board/app`，
上传 app 快照（93 文件）+ design_tokens.yaml，**不经实现者 build 目录直接执行交付入口**
`bash tests/run_core_tests.sh`（实测 348/348；Full产业链）:

| 步骤 | 结果 |
|---|---|
| `run_core_tests.sh` 交付入口 | 构建成功（含 camera_registry.cpp），**348 passed / 0 failed**，`RESULT: PASS`，`ENTRY_EXIT=0`；JUnit 独立解析 `tests="348" failures="0"` |
| 与实现者归档对比 | 348 PASS 行**逐行一致**（`log-compare identical=True`，程序化比对） |
| `gen_tokens.py --selftest` | `TOKENS_EXIT=0`（8/8） |
| `check_no_hardcoded_ui.py --root src/ui` | 扫过 **12 文件**（含本轮新增 CameraTile/DetectionBadge/DeviceCard/P1Realtime/P4AllCameras/P6Maintenance），`SCAN_EXIT=0` |
| `railhmi-sim-inject`（FI-01..05,10） | `SIM_EXIT=0`；63 行 log **与实现者归档 diff 逐字节一致**（SIM_LOGS_IDENTICAL） |

**验证者自有扩展夹具（verify_registry_ext.cpp, 54 断言）— 结果: 54/54 PASS（板端, EXT_EXIT=0）**
超实现者 test_registry.cpp 之外的独立断言（`VERIFY-FIXTURES-…-v3.log` §run-ext）:
- **d1/d1b 徽标文本全值身份**: 5 枚举值（front/pantograph/wheel_rail/cab_door/none）→
  精确文本 + 两两不同；d6 逐槽位 badge == f(ai_profile) 全 6 槽一致。
- **d2 配置驱动徽标变化（TC-BADGE-01 关键维度, 实现者 self-test 未覆盖）**: 仅改 YAML——
  CAM-02 none→front 徽标变"前方障碍/人员"、CAM-01 front→none 变"广角·无检测"，徽标跟随
  ai_profile 而非任何其他字段。
- **d3 六页全真值表**: Realtime/AlarmCenter/Replay/AiDebug=false、AllCameras/Maintenance=true。
- **r1..r5 配置即视图矩阵**: 改名（order/position 保留）、删首位（5 槽 order 0..4 重编号）、
  删中间（相邻拼接）、CAM-07 插入（占据配置位置, 后继 order 后移）、六路完全倒序注册表完全跟随。
- **r6 派生确定性**: 同基线两轮 build_registry 逐字段（id/name/position/ai_enabled/ai_profile/
  badge/stream_state/rtsp_url/credential_env/order）相等。
- **n1/n2/n3 NVR 配置语义矩阵**: 补全 protocol+channel_mapping → unverified + "已配置"；
  registered=false → unverified + 仍"未配置"；删整段 → `applied_defaults` 记录 `app.integrations`
  + 空列表。
- **b1/b2/b3 缺失必填负例（换实现者未测的槽位与字段, 逐字路径）**:
  `cameras[5].rtsp_url`、`cameras[2].name`、`cameras[1].credential_env` 全部精确命中且非零退出。

## 3. 证据文件（本轮新增/全部为验证者自产）

- `VERIFY-FIXTURES-VERIFY-20261016-v3.transcript.json` — SSH 指纹/逐条命令/退出码/清理确认
- `VERIFY-FIXTURES-VERIFY-20261016-v3.log` — 总过程留痕（含夹具全 54 断言行 + log-compare + SIM_LOGS_IDENTICAL）
- `VERIFY-FIXTURES-VERIFY-20261016-v3.core-evidence_core-tests-…-v3.log` / `.xml` — 板端 348/348 + JUnit 独立解析
- `VERIFY-FIXTURES-VERIFY-20261016-v3.core-evidence_sim-inject-…-v3.log` / `.json` — 与实现者归档逐字节一致
- `verify_registry_ext.cpp` / `verify_registry_fixtures.py` / `run_verify_round.sh` — 验证自主夹具与启动脚本
- 实现者参考资料（不作判据）: `build-TestRun-192_168_0_103-20261015-ITEM012-r2.*`

## 4. 过程留痕（非判据）

- **v1 轮（VERIFY-20261016-v1）**: 交付入口×独立板端复跑已 348/348 全 PASS；扩展夹具 0/1 失败——
  **验证侧运行方式错误**（ext 二进制从 /tmp 启动，`RAILHMI_CONFIG_DIR="config"` 相对路径导致基线不可读；
  且夹具运行器 log-compare 存在 bool/path 类型笔误），板面模型未跑，不计判据。留痕 `VERIFY-FIXTURES-…-v1.log`。
- **v2 轮（VERIFY-20261016-v2）**: 348/348 + identical=True 复现；扩展夹具 52/54，2 失败均为
  **验证侧期望构造错误**（① r2b: 删 CAM-01 后未同步清 roi 引用，产品按设计拒绝"未知 roi camera"，
  属 fail-fast 正确行为——夹具改用 `roi: []` 变体；② n1c: 我方临时假设"channel_mapping 原值透传"，
  但 camera_registry.cpp 契约为布尔区分显示"已配置"——原值透传是我方发明，非项目判据，按规则不新增
  验收判据，改回契约值）。两项均可复算，板面产品行为一致。留痕 `VERIFY-FIXTURES-…-v2.log`。
- **v3 轮（VERIFY-20261016-v3）**: 54/54 全绿 + 348/348 identical + sim-inject 一致 → 判据轮。
- 板端产物复现方式: 独立实现目录 + 自有夹具；`/tmp/railhmi_verify012*` 夹具目录由验证轮**按名清理**，
  cleanup 步骤 exit=2 仅为 `ls` 对已删除目录报错（删除本身 `fixture_cleanup_done` 成功），与既往轮口径一致。
- 连接 192.168.0.103 SSH 正常（fingerprint exit=0），本轮未涉及相机/NVR 流量。

## 5. 边界（维持实现者边界口径，逐条如实）

- **QML 未在板上编译/运行**: 板端缺 Qt5 Quick/Qml 开发包（E-01b）。本验证覆盖（a）注册表/徽标/NVR
  模型（板端 348/348 + 验证自有 54/54）、（b）UI 源码无硬编码（12 文件扫描 PASS）、（c）QML ↔ C++
  注入字段静态接线核对。**运行期渲染**（P4/P6 徽标可见、P1 不可见的实际画面表现、配置即视图的
  运行时网格更新）需平台补齐 Qt 后由后续 UI 验证承担；不以模型层结论外推。
- **结构变更（增删相机）需重启后生效** + 界面提示属运行期行为，同上边界。
- 连接状态 `stream_state` 为配置无关占位（C-12 契约锚定"connecting"），实时连接状态/帧率由取流
  状态机（ITEM-005 及取流接入项）接入后核。
- 不接入红外；不虚构 NVR 通道号（C-7 实测"未配置"呈现）。

## 6. 复现

```sh
# 凭据在用户启动的同一 PowerShell 中点加载: runtime/local_credentials.ps1
VERIFY_STAMP=VERIFY-20261016-v3 bash docs/hmi/test_evidence/ITEM-012-registry/run_verify_round.sh
# 板上直接复现模型域: cd <独立目录>/app && bash tests/run_core_tests.sh；标准输出 348 PASS 行
#   与 docs/hmi/test_evidence/ITEM-012-registry/VERIFY-FIXTURES-VERIFY-20261016-v3.core-evidence_core-tests-...-v3.log 一致
# 扩展夹具: g++ -std=c++17 -O1 -Isrc/core/include /tmp/railhmi_verify012/verify_registry_ext.cpp \
#   build-core/src/core/librailhmi_core.a -o /tmp/railhmi_verify012/verify_registry_ext && \
#   cd <board_wd>/app && /tmp/railhmi_verify012/verify_registry_ext   # 54 PASS 行, EXT_EXIT=0
```
