# UI 实现评审证据 — ITEM-009（模拟输入与故障注入系统）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `2a0215b`，2026-10-16 00:40:28 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/simulation.h`、`app/src/core/src/simulation.cpp`、`app/src/core/tools/sim_inject.cpp`、`app/tests/core/test_simulation.cpp`
  - 构建证据: `docs/hmi/test_evidence/ITEM-009-simulation/README.md`
- 被评工作项: `ITEM-009`（模拟帧源、可重置的按 `ai_profile` 模拟检测序列、FI-01..FI-05 与 FI-10 软件开关/CLI 参数；无需相机即可驱动全部页面与状态；注入参数与观测写入结构化日志）
- 参照设计: `docs/hmi/TEST_DESIGN.md` §3（FI 目录/设计要求）、TC-SIM-01、TC-STREAM-03/04（`e7d7484` 版基线）、`docs/hmi/INTERACTION_DESIGN.md` v2 §7.1/§7.2/§10（`ef47841`）、`docs/hmi/VISUAL_DESIGN.md` v2 §3.5/§7.5/§7.9（`40ccf4e`）、`docs/hmi/DESKTOP_APP_DESIGN.md` §5.3/§16
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-009-simulation/VERIFY-ITEM-009.md`）
- 评审日期/环境: 2026-10-16；Windows 主机（MINGW64 x86_64，无 C/C++ 工具链）；**本轮未连接 RK3588 目标板**（板端事实采用上游软件验证的独立板端复跑）
- 判定: **TEST_PASS（范围严格限定为 ITEM-009 模型层模拟/注入交付物；不构成任何界面视觉/交互或 UI 可运行结论；且对“驱动全部页面与状态”声明记录未覆盖状态，见 §5 D-01）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-009**（模拟输入与故障注入）。判定对象是任务内嵌用户可观察点：按脚本注入任一故障后进入对应状态且可恢复、注入参数与观测写入结构化日志、按 `ai_profile` 注入可重置检测序列、无需相机。
2. **本次未对任何界面做视觉/交互还原评审**：本项交付的是**模型层确定性运行器与 CLI**，无 UI 产物。板端缺 Qt5 Quick/Qml（E-01b），QML 页面无法构建；本证据不宣称六页面已可用（与实现 README §3、验证件边界一致）。
3. **本轮未重新连接目标板**：板端 274/274、sim-inject 63 行复现与验证自有 49 条夹具已由上游软件验证独立复跑。本 capability 授权方法为 `read/ui_evidence/design_assets`，主机无 C/C++ 工具链；本轮以**源码审阅 + 状态/观测契约对照（交互 §7.1/§7.2、视觉 §3.5）+ 归档证据核对**为独立证据。
4. 依赖缺口（主机无 C/C++ 工具链 E-00；板端无 Qt5 Quick/Qml E-01b、无 `libsqlite3-dev`）按职责交平台/环境；本评审不判定其“不可修复”，不返回 BLOCKED。
5. 本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `2a0215b`（2026-10-16 00:40） | 新增 `simulation.{h,cpp}`、`sim_inject.cpp`、`test_simulation.cpp`；CMake/main.cpp/board_build TestRun 接线；ITME-009 证据归档；无范围外改动 |
| 交互设计 | `ef47841` | v2 §7.1 六态与非 LIVE 硬约束、§7.2 运行时字段、§10 异常恢复 —— 本项须能驱动这些状态 |
| 视觉设计 / 令牌 | `40ccf4e` | v2 §3.5 六状态、§7.5 质量角标、§7.9 恢复反馈 |
| 测试设计 | `e7d7484` | §3 FI 目录与“FI-01..05/FI-10 须软件开关/CLI”要求 |
| 软件验证 | `d01fe7d` | TEST_PASS；274/274 + sim-inject 逐行一致 + 夹具 49/49 + CLI 负例/确定性 |

实现自述与仓库证据一致：`ITEM-009-simulation/README.md`、板端 r2 归档（274 PASS、63 行注入日志、JSON）与验证件相互印证；**未宣称页面已联调，未把模型层结果外推为板端渲染/相机行为**。

---

## 2. 评审方法

1. 源码审阅：`simulation.{h,cpp}`（`FaultKind`/`parse_fault`/`fault_active` 窗口/`run()` 逐 tick 逻辑/`format_observation`）、`sim_inject.cpp`（CLI 解析/`--list`/退出码/JSON）、`test_simulation.cpp`。
2. 状态/观测契约对照：观测 `stream_state/ai/storage/ptz/gap/alarm/summary` 与交互 §7.1/§7.2、视觉 §3.5/§7.5/§7.9 对照；FI-01..05/FI-10 与 TEST_DESIGN §3 逐项对照。
3. 归档证据核对：板端 r2 归档 log/XML/sim-inject log+JSON、`VERIFY-ITEM-009.md`（含 49 断言夹具的状态时间线）。
4. 独立推演：FI 时间窗口 `[onset, recovery)` 与恢复语义、六态可达性、PTZ/存储观测自洽性。

**方法限制**：主机无 C/C++ 工具链 → 不能本地运行；板端结论采用上游软件验证复跑证据。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | FI 项与 TEST_DESIGN §3 对照 | `FaultKind` 六项 = FI-01/02/03/04/05/10，`fault_id`/`to_string`/`parse_fault` 齐备；与“FI-01..05 与 FI-10 须软件开关/CLI”要求一致 |
| R2 | 六态可达性推演（对照验证夹具时间线） | FI-01→RECONNECTING/OFFLINE、FI-02→FROZEN→RECONNECTING→**CONNECTING**→OFFLINE→LIVE；LIVE/ CONNECTING/FROZEN/OFFLINE/RECONNECTING 可达；**DEGRADED 不可达**（`on_frame` 恒用无标志 `qok`）→ 见 D-01 |
| R3 | 观测覆盖对照交互 §7.2 | `stream_state`（含 `presentable`）、`ai`（ok/unavailable）、`storage`（low/record_available）、`ptz`（tolerance_ok）、`gap`、`alarm`（type/level/cam/track_id）、`summary` —— 覆盖 UI 主要运行时字段 |
| R4 | PTZ 观测自洽性核对 | 归档日志 `t=1500 type=ptz tolerance_ok=false target_zoom=10.000000 actual_zoom=10.000000` —— **target==actual 却判超差，且未注入 Pan/Tilt、未记录 tolerance** → 见 D-02 |
| R5 | 归档证据核对 | 板端 `274 passed, 0 failed`、`RESULT: PASS`；sim-inject `SIM_EXIT=0`、63 行含各类观测与 `summary final_state=live … gaps=1` |
| R6 | CLI 参数校验核对 | `--fi` 非法 → exit 2；`--profile` 非法 → **忽略返回值静默落到默认 `front`**（`sim_inject.cpp`）→ 见 D-03 |
| R7 | 确定性/可重置核对 | `run() const` + 重新实例化重置；验证夹具双实例 byte-identical（R5 佐证） |

---

## 4. 判据逐条核对（ITEM-009 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 模拟帧源节拍 + 故障注入进入对应状态 | `step_ms`/`onset`/`recovery` 窗口；FI-01..05/FI-10 逐 tick 观测（R1/R2/R5） | **满足（模型层）** |
| 故障后可恢复 | `fault_active` 在 recovery 后解除，各状态回 live/low=false/tolerance_ok=true | **满足** |
| FI-01..FI-05、FI-10 软件开关/CLI 参数 | `FaultKind` 六项 + `parse_fault` 多别名 + `--fi`/`--list` | **满足** |
| 无需相机即可驱动状态 | 无相机、无 Qt（CoreTests 档），CLI 可独立运行 | **满足** |
| 注入参数与观测写入结构化日志 | `injection` 行含 camera/profile/faults/时间参数；63 行 8 类；`.log`+`.json` | **满足** |
| 按 `ai_profile` 注入可重置检测序列 | front→person、pantograph→Abnormal、wheel_rail→wheel_abnormal、cab_door→Ajar、none→无；重置确定 | **满足** |
| “无需相机即可驱动**全部**页面与状态” | DEGRADED（质量降级）无注入路径（R2） | **部分满足**（见 D-01） |

---

## 5. 具体偏差与修复要求

**D-01（UI 状态覆盖缺口）— `DEGRADED`（质量降级）无法由本模拟系统驱动**
- 观察：`simulation.cpp` 在非故障 tick 恒以默认无标志 `const QualityFlags qok; machine.on_frame(t, qok)` 出帧，且无任何 FI/配置可注入质量标志（过暗/过曝/遮挡/模糊）。因此模型层**不可能进入 `DEGRADED`**，`VISUAL_DESIGN` §3.5“画面降级”与 §7.5 质量角标、交互 §7.1“降级可用”均无法被本项驱动；与工作项“无需相机即可驱动全部页面与状态”的表述不符。
- 说明（范围口径）：TEST_DESIGN §3 把 `DEGRADED` 的复现归到 **FI-06/FI-08（预录媒体）**，因此严格按媒体层归属可不落在 ITEM-009 模型层；但 ITEM-009 的三档验证（未含 DEGRADED）留下一个 UI 可见状态无可跑驱动源。
- 复现路径：`read app/src/core/src/simulation.cpp`（`on_frame(t, qok)`）；`read app/src/core/include/railhmi/core/simulation.h`（`SimulationConfig` 无 quality 字段）；TEST_DESIGN §3 FI-06/08、TC-STREAM-03。
- 期望：二选一并写明——①在模型层增加轻量质量注入（如 `--quality too_dark,blurred` 或 `QualityFlags` 配置项），使 DEGRADED 与质量角标可在**无相机/无媒体**下驱动；或②在 README/证据中明确“DEGRADED 归 FI-06/08 媒体层，ITEM-009 不驱动”，避免“全部状态”的过度声明。两者择一即成闭环。

**D-02（注入数据自洽性，UI 可见）— FI-05 PTZ 注入的 target/actual 与应用结果矛盾，且缺 Pan/Tilt 与 tolerance 字段**
- 观察：FI-05 激活时观测为 `tolerance_ok=false target_zoom=10.000000 actual_zoom=10.000000`（归档日志原文）；`ptz_target_zoom==ptz_actual_zoom`、`ptz_tolerance=0.5`，差值为 0 却判“超差”，且观测**未输出 tolerance**、也**未注入 Pan/Tilt**（TEST_DESIGN FI-05 描述“返回注入的 Pan/Tilt/Zoom”）。此外 TEST_DESIGN FI-05 期望“容差失败显示 + 前方 AI NOT_READY”，本模拟在 FI-05 下仍输出 `ai status=ok`。
- 影响（UI 可见）：`VISUAL_DESIGN` §8.6 P6 云台卡显示“目标值 vs 实际值与容差校验结果”；若目标=实际却显示“超差”，司/维护人员会看到自相矛盾的界面；且无法驱动 Pan/Tilt 或“前方 AI 未就绪”状态。
- 复现路径：`read app/src/core/src/simulation.cpp`（FI-05 分支硬编码 `tolerance_ok=false`，不改变 actual）；归档 `...core-evidence_sim-inject-...-r2.log`（`target_zoom=10.000000 actual_zoom=10.000000`）；TEST_DESIGN FI-05 行。
- 期望：FI-05 激活时把 `actual_zoom`（或 pan/tilt）设为超出 `ptz_tolerance` 的值，并在观测中输出 `tolerance` 与 pan/tilt；按 TEST_DESIGN 联动前方 AI `NOT_READY`（或明确该联动归上层并记录）。

**D-03（不静默默认，CLI）— `--profile` 非法值被静默忽略并落到默认 `front`**
- 观察：`sim_inject.cpp` 对 `--profile` 调用 `parse_ai_profile(p, &cfg.profile)` 但**未检查返回值**；非法 profile 不报错、不退出，静默使用默认 `front`（而 `--fi` 非法则明确 `invalid --fi list` 并 exit 2）。与项目“字段缺失/非法绝不静默默认”的既有口径不一致。
- 复现路径：`read app/src/core/tools/sim_inject.cpp`（`--profile` 分支忽略返回值）。
- 期望：校验 `parse_ai_profile` 返回值，非法时打印 `invalid --profile` 并 `exit 2`，与 `--fi` 对称。

**D-04（测试精度，轻量）— `no fault summary alarms=1` 为子串弱断言**
- 观察：`test_simulation.cpp` 用 `summary.find("alarms=1")`，该子串同样命中 `alarms=10..19`（验证 O-1 同源），无法证明“恰好 1 条”。
- 期望：改用数值解析后精确比较（`alarms == 1`），避免弱断言在报警数量增长后仍假通过。

**D-05（范围/交接）— FI-06..FI-09 未实现，属 TEST_DESIGN 其他层次**
- 观察：TEST_DESIGN §3 定义 FI-01..10；本项按设计仅要求 FI-01..05、FI-10（FI-06/08 预录媒体、FI-07 断电/systemd 归 ITEM-025、FI-09 报警风暴级别竞争）。
- 期望：保持本项范围；但 **FI-09（一级优先不被覆盖、横幅最久驻留）** 与 **FI-06/08（DEGRADED）** 是 UI 可见状态，需在后续 UI/媒体/系统项提供等价注入（或与 D-01 合并处理），以免 P1 横幅优先级与降级角标无可跑用例。

**D-06（范围缺口，非本项缺陷）— 无可评审的 UI**
- 观察：本项为模型层，QML 页面因缺 Qt5 Quick/Qml 无法构建；无可运行界面、无截图/录屏。
- 期望：待平台补齐 Qt、UI 接入后，UI 实现评审按 `INTERACTION_DESIGN` §4–§11 与 `VISUAL_DESIGN` §7–§12 逐状态核对（六态 + 质量角标 + 恢复反馈 + 报警横幅优先级），并要求运行录屏/交互记录。

### 已确认无问题/正向项

- FI-01..05/FI-10 与 TEST_DESIGN §3 软件开关要求**逐项对应**，`parse_fault` 支持 `01`/`fi-01`/别名多形式（R1）。
- 确定性：模拟时钟、无 wall 依赖；重新实例化即重置；验证夹具双实例 byte-identical（R7）。
- 恢复语义清晰：`fault_active` 窗口 `[onset, recovery)`，`recovery<=onset` 不解除；每类故障恢复后回正常（R2/R5）。
- 观测覆盖 UI 主要运行时字段（`stream_state` 含 `presentable`、`ai`、`storage`、`ptz`、`gap`、`alarm`、`summary`），`format_observation` 单行 `t= type= key=value` 便于 CI/日志消费（R3）。
- FI-04 的 `low=true` 与 `record_available=false` 联动符合 PLY-008；FI-10 复用 ITEM-007 已验证的缺口合并（R5）。
- 无相机、无 Qt 即可运行；CLI 退出码 0/2、`--list` 可用；JSON 双路输出可独立解析。

---

## 6. 未覆盖 / 未执行（不得外推）

1. 目标板任何命令本轮**未执行**；板端结论采用上游软件验证复跑与夹具证据。
2. 任何界面视觉/交互还原（无 UI 产物；QML 因 E-01b 无法构建）。
3. 真实像素级模拟（预录/合成视频）——归 media 层与 ITEM-002 spike。
4. FI-06..FI-09（预录质量/断电/报警风暴）——归媒体/系统/后续 UI 项。
5. 注入观测写入 SQLite 的运行时接线——归后续录像/健康任务。
6. 主机运行（无 C/C++ 工具链 E-00）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-009 模型层模拟/注入交付物）。
- 依据：任务内嵌用户可观察点——FI-01..05/FI-10 软件开关/CLI、故障进入对应状态且可恢复、注入参数与观测写入结构化日志、按 `ai_profile` 注入可重置检测序列、无需相机即可运行——经源码审阅、状态/观测契约对照与归档证据核对逐条成立；板端 274/274 与验证 49 夹具一致。
- 明确边界：本 PASS **不表示**任何界面可运行或视觉/交互通过，**不表示**“全部页面与状态”已被覆盖（DEGRADED 未覆盖，见 D-01）。
- 后续修复/交接要求见 §5：D-01（DEGRADED 驱动）为 UI 状态覆盖关键项；D-02（PTZ 注入自洽/补 Pan·Tilt·AI NOT_READY）为 UI 可见数据正确性项；D-03（`--profile` 静默默认）为项目口径项；D-04/D-05 测试精度与后续注入面交接；D-06 待可运行 UI 后再评审。
