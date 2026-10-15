# UI 实现评审证据 — ITEM-004（railhmi-core 配置与状态契约层）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `a2ade63`，2026-10-15 22:28:50 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/{config.h,yaml.h}`、`app/src/core/src/{config.cpp,yaml.cpp}`、`app/src/core/tools/config_check.cpp`、`app/config/{cameras,app,thresholds,roi}.yaml`、`app/tests/core/test_config.cpp`、`app/src/ui/src/main.cpp`（启动校验接入）
  - 构建证据: `docs/hmi/test_evidence/ITEM-004-config/README.md`
- 被评工作项: `ITEM-004`（加载并校验 `cameras.yaml/app.yaml/thresholds.yaml/roi.yaml`；字段缺失或非法报出具体字段名、绝不静默用默认值；暴露交互方案 v2 §7.2 契约字段含 `ai_enabled`/`ai_profile`/`integration_state(probing/reachable/unreachable/unconfigured/unverified)`/`mode(visible/ir 未接入)`/`recording_source(local/nvr/both)`）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §7.2/§7.3（`ef47841`）、`docs/hmi/VISUAL_DESIGN.md` v2 §3.6/§7.11/§7.12 + `docs/hmi/design_tokens.yaml` v2（`40ccf4e`）、`docs/hmi/DESKTOP_APP_DESIGN.md` v2 §7/§12（`e691118`）
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-004-config/VERIFY-ITEM-004.md`）
- 评审日期/环境: 2026-10-15；Windows 主机（MINGW64 x86_64，无 C/C++ 工具链）；**本轮未连接 RK3588 目标板**（板端事实采用上游软件验证的独立板端复跑，见 §0.3）
- 判定: **TEST_PASS（范围严格限定为 ITEM-004 配置与静态状态契约层；不构成任何 UI 视觉/交互或 UI 可运行结论）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-004**（配置与状态契约层）。判定对象是任务内嵌用户可观察点：CoreTests 对合法/非法配置逐项断言；非法配置启动报出具体字段；暴露交互方案 v2 §7.2 的静态契约字段（`ai_enabled`/`ai_profile`/`integration_state`/`mode`/`recording_source`）；必填字段缺失不静默用默认值。
2. **本次未对任何界面做视觉/交互还原评审**：本项交付的是**无 GUI 的 core 配置层**与一个非 Qt 的 `railhmi-config-check`；UI（`src/ui`）仅接入同源启动校验，仍无可运行界面。因此本文不对布局、组件、字体、图标、焦点、空/错/恢复状态做任何 PASS 结论（§5 D-05）。
3. **本轮未重新连接目标板**：板端 40/40 与 `config-check` good/bad 已由上游软件验证独立复跑（含 5 个验证自有负例夹具，`VERIFY-ITEM-004.md`）。本 capability 授权方法为 `read/ui_evidence/design_assets`；本轮以**主机独立数据一致性核对 + 源码/证据审阅**为独立证据（主机无 C/C++ 工具链，无法本地编译），并标注引用关系。
4. 依赖缺口（板端无 `libsqlite3-dev`/Qt5 Quick/Qml，主无工具链）按职责交平台/环境；本评审不判定其“不可修复”，不返回 BLOCKED。
5. 本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `a2ade63`（2026-10-15 22:28） | 新增 `yaml.{h,cpp}`（无依赖 YAML 子集）、`config.{h,cpp}`（模型 + 字段级校验）、`config_check.cpp`、`app/config/*.yaml`（4 件）、`test_config.cpp`；`src/ui/src/main.cpp` 接入同源启动校验；ITME-004 证据归档；无范围外改动 |
| 交互设计 | `ef47841` | v2 §7.2 字段契约、§7.3 摄像头职责为本项直接判据 |
| 视觉设计 / 令牌 | `40ccf4e` | v2 §3.6 集成/模式状态、§7.11 NVR 卡、§7.12 检测徽标；枚举命名需与本层一致 |
| 桌面应用设计 | `e691118` | §7 状态契约 / §12 配置为本项判据来源 |
| 软件验证 | `e7f1275` | TEST_PASS；板端 40/40 + good/bad + 5 独立负例一致；观察 O-1（`mode: ir` 可写）记录在案 |

实现自述与仓库证据一致：`app/README.md`、`ITEM-004-config/README.md`、板端日志（40/40、good exit 0、bad exit 1）相互印证；**未把运行时字段冒充已实现，未把主机结论当板端结论**。

---

## 2. 评审方法

1. 源码审阅：`config.h`（枚举/结构/契约）、`config.cpp`（`req_*` 必填校验、跨字段一致性、`applied_defaults`、`initial_integration_state`、`mode_ir_connected`）、`yaml.{h,cpp}`、`config_check.cpp`、`src/ui/src/main.cpp`、`test_config.cpp`。
2. 文档/证据审阅：`ITEM-004-config/README.md`、`VERIFY-ITEM-004.md`、板端 r2/verify 日志与 transcript。
3. 独立复现（本机实机执行）：用 Python 独立解析 4 个配置基线，对照交互方案 §7.3 与环境配置逐项核对（相机集合/地址/职责/枚举/ROI 引用/NVR 登记）。
4. 契约一致性对照：`to_string`/枚举取值 vs `INTERACTION_DESIGN` §7.2、`VISUAL_DESIGN` §3.6/§7.12。

**方法限制**：主机无编译器 → 不能本地构建/运行 CoreTests、不能独立执行 C++ 校验器；板端结论采用上游软件验证复跑证据。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | Python 独立解析 `app/config/cameras.yaml` | 6 路、id 唯一、`positions` 集 = {primary,left,right,roof,underfloor,cab}、`spectrum` 全 visible、`transport` 全 udp |
| R2 | 相机地址逐条对照环境配置 | CAM-01 .65 / CAM-02 .160 / CAM-03 .162 / CAM-04 .163 / CAM-05 .165 / CAM-06 .167 —— **全部一致** |
| R3 | `ai_enabled`↔`ai_profile` 职责对照交互 §7.3 | front / none / none / pantograph / wheel_rail / cab_door；一致性与设计职责相符 |
| R4 | `app.yaml` 契约值核对 | `mode=visible`、`render_path=cpu`、`recording_source=both`；NVR `192.168.1.4` 已登记且**无** protocol/channel_mapping（与 §0.4、§7.11“未配置”口径一致） |
| R5 | ROI 引用完整性 | 4 条 ROI 全部引用已存在相机 id，矩形均在 [0,1] 且 x+w≤1 |
| R6 | 板端测试日志核对 | `== 40 passed, 0 failed ==`、`TEST_EXIT=0`；`config-check` good `CFG_EXIT=0`、bad `CFG_EXIT=1` 且报 `thresholds.confirm_frames: required field missing` |
| R7 | 契约枚举命名对照 | `to_string(IntegrationState)` = probing/reachable/unreachable/unconfigured/unverified；`ImagingMode` visible/ir；`RecordingSource` local/nvr/both —— 与 `VISUAL_DESIGN` §3.6 与交互 §7.2 **逐字一致** |
| R8 | 必填缺失路径审阅 | `req_string/req_int/req_double/req_bool/req_enum` 先判空/类型再赋值；缺失记 `required field missing`，**不先赋默认值**；可选字段默认记入 `applied_defaults`（`cameras[1].tile_aspect` 已断言） |

---

## 4. 判据逐条核对（ITEM-004 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| CoreTests 对合法/非法配置逐项断言 | 40/40（含 16 合法项 + 10 期非法项）；上游验证另有 5 独立负例全部一致 | **满足** |
| 非法配置启动报出具体字段 | `railhmi-config-check` 与 `src/ui` 启动**同源**调用 `load_config_dir`；bad 报 `thresholds.confirm_frames: required field missing`，退出 1；Qt 入口非法 `return 4` 并逐条日志 | **满足**（板端实测在非 Qt 入口；Qt 入口同源） |
| 字段缺失/非法绝不静默用默认值 | 必填缺失即报错；可选默认记入 `applied_defaults`（可观察） | **满足** |
| 暴露 §7.2 静态契约字段（ai_enabled/ai_profile/integration_state/mode/recording_source） | 均在 `config.h` 暴露且取值与设计一致（R7）；NVR 初始 `unconfigured` 断言 | **满足**（运行时字段归 ITEM-005+，已如实标注） |
| 配置基线符合本期范围（6 路可见光、无红外、NVR 登记未配置） | R1–R5 独立核对一致 | **满足** |

---

## 5. 具体偏差与修复要求

**D-01（UI 契约的合法性缺口，建议按本期范围收紧）— 校验器接受 `mode: ir` / `spectrum: ir`，与“本期红外未接入”冲突**
- 观察：`ImagingMode` 与 `Spectrum` 枚举都含 `Ir`，`parse_imaging_mode`/`parse_spectrum` 接受 `"ir"`，`load_app`/`load_cameras` 用通用 `req_enum` 接受；而 `mode_ir_connected()` **硬编码 false**。即配置写 `mode: ir` 或某路 `spectrum: ir` 会**通过校验**，得到“本期不可能出现”的状态。
- 影响（UI 可见）：`VISUAL_DESIGN` §3.6 的模式位在本期固定显示“可见光 / 红外未接入”，且不提供切换控件。若配置能声明 `mode=ir`，UI 将收到自相矛盾的契约（模式为红外但无红外源），要么被迫特判、要么显示错误模式。
- 复现路径：`read app/src/core/include/railhmi/core/config.h`（`ImagingMode`/`Spectrum` 含 `Ir`）；`read app/src/core/src/config.cpp`（`parse_imaging_mode`/`parse_spectrum` 接受 `ir`；`mode_ir_connected(){return false;}`）；`test_config.cpp` 只测 `mode: thermal`（非法值），**未测 `mode: ir`**。
- 期望：增加**本期跨字段/范围校验**——当 `mode == Ir && !mode_ir_connected()` 时报 `app.mode: infrared not connected this phase`；相机 `spectrum == Ir` 同理报 `cameras[i].spectrum: ...`；并补 `mode: ir`、`spectrum: ir` 两个负例断言。IR 正式接入后再放开（与本 evidence 观察 O-1 同源，此处按 UI 契约提出修复要求）。

**D-02（语义澄清）— `registered=false` 的集成设备返回 `Unverified`，UI 标签可能误读**
- 观察：`initial_integration_state`：`!registered → Unverified`；`registered 且缺 protocol/channel_mapping → Unconfigured`。未登记的设备条目显示“未核验”，与 `VISUAL_DESIGN` §3.6 的“未配置（=未提供）”语义不完全贴合。
- 期望：明确 `registered=false` 的期望状态（“未配置”更贴合“未提供”，或明确保留“未核验”并写入契约说明），使 UI 的 integration_state → 徽标映射无歧义。

**D-03（契约展示层交接）— core 只提供机器枚举 token，检测职责/模式的中文展示标签需在 UI 侧统一定义**
- 观察：`to_string(AiProfile)` 返回 `front/pantograph/wheel_rail/cab_door/none`，`to_string(ImagingMode)` 返回 `visible/ir`；而 `VISUAL_DESIGN` §7.12 的 `DetectionBadge` 与 §3.6 模式位需要中文标签（“前方障碍/人员 / 受电弓三状态 / 轮轨 / 车门 / 广角·无检测”“可见光 / 红外未接入”）。
- 说明：core 提供 token 合理（i18n 归 UI），但须避免在各页面散落映射。期望：UI（主题/文案层，ITEM-004 之后的令牌/文案工作项）建立**单一映射表**（`ai_profile`→标签、`mode`→标签、`integration_state`→标签），与 §3.6/§7.12 逐字一致；`app.language`（zh-CN）已由本层装载，可作为 i18n 入口。

**D-04（UI 错误态交接）— 启动配置非法目前仅写 stdout/退出码，尚无屏上错误面**
- 观察：`src/ui/src/main.cpp` 在配置非法时经 `Logger` 写日志并 `return 4`，QML 未启动；`VISUAL_DESIGN` §7.9 要求失败态显示原因文字与[重试]且“不显示原始堆栈”。
- 说明：ITEM-004 仅要求“报出具体字段”，stdout 已满足；屏上错误态属后续 UI 工作项。期望：在 UI 工作项中补一个最小启动错误面（显示 `ConfigError.path + message` 的人话 + 重试/退出），避免现场只有终端日志可看。本项不因此判缺陷。

**D-05（范围缺口，非本项缺陷）— 无可评审的 UI**
- 观察：`src/ui` 仍为骨架；无页面/状态/交互、无截图/录屏。
- 期望：ITEM-004 之后产出可运行 UI 时，UI 实现评审按 `INTERACTION_DESIGN` §4–§11 与 `VISUAL_DESIGN` §7–§12 逐状态核对，并要求运行录屏/交互记录。

### 已确认无问题/正向项

- 必填字段缺失**绝不静默默认**（`req_*` 先报错再赋值），可选默认记入可观察的 `applied_defaults`。
- 配置基线与项目环境**逐项一致**（R1–R5）：6 路可见光、地址、检测职责、NVR 登记未配置。
- 契约枚举命名与视觉设计**逐字一致**（R7），避免 UI 侧二次转换歧义。
- 跨字段一致性（`ai_enabled`↔`ai_profile`）、重复 id、ROI 引用/越界均有字段级错误。
- core 仍为纯标准库，无 Qt/GStreamer/SQLite 依赖；UI 启动与 `config-check` 同源校验（单点真相）。
- 错误可复现且不伪造：40/40 + good/bad + 验证 5 负例（`VERIFY-ITEM-004.md`）。

---

## 6. 未覆盖 / 未执行（不得外推）

1. 目标板任何命令本轮**未执行**；板端结论采用上游软件验证复跑与夹具证据。
2. 任何界面视觉/交互还原（`src/ui` 仍为骨架，无 UI 产物）。
3. §7.2 **运行时**字段（`state/fps/bitrate/last_frame_ts/quality_flags/reconnect_count`、AI 运行态、报警事件、录像段、PTZ 实际值、NVR `management_reachable`/`last_probe_ts`）—— 本层未实现，归 ITEM-005/007/…。
4. `integration_state` 的 probing/reachable/unreachable 运行时转换（ITEM-017 NvrProbe）。
5. 主机编译/运行 CoreTests（主机无 C/C++ 工具链）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-004 配置与静态状态契约层）。
- 依据：任务内嵌用户可观察点——合法/非法配置逐项断言（40/40，上游独立复跑一致）、非法配置报**具体字段**并非零退出、必填缺失不静默默认、交互 §7.2 静态契约字段（`ai_enabled`/`ai_profile`/`integration_state`/`mode`/`recording_source`）按设计暴露、配置基线与本期 6 路可见光范围一致——经源码审阅、主机独立数据核对与归档证据核对逐条成立。
- 明确边界：本 PASS **不表示**任何 UI 可运行或视觉/交互通过，**不表示**§7.2 运行时字段已实现。
- 后续修复/交接要求见 §5：D-01（`mode: ir`/`spectrum: ir` 应在本期报错）为建议在本层收敛的 UI 契约项；D-02 语义澄清、D-03 展示标签单点映射、D-04 启动错误面为后续 UI 交接；D-05 待可运行 UI 后再评审。
