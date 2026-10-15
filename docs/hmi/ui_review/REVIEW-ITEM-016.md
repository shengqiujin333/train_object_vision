# UI 实现评审证据 — ITEM-016（关键状态区与顶部模式信息条）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `2aa1af1`，2026-10-16 03:48:27 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/status.h`、`app/src/core/src/status.cpp`、`app/src/core/tools/status_probe.cpp`、`app/src/ui/qml/components/{StatusItem,KeyStatusStrip,TopStatusBar}.qml`、`app/src/ui/qml/pages/P1Realtime.qml`、`app/src/ui/qml/main.qml`、`app/src/ui/src/main.cpp`、`app/tests/core/test_status.cpp`
  - 构建证据: `docs/hmi/test_evidence/ITEM-016-status/README.md`
- 被评工作项: `ITEM-016`（受电弓/轮轨/车门/摄像头/AI/录像/存储七类状态正常简洁、异常突出且每项颜色+图标形状+文字三重编码；顶部时间/可见光模式位与红外未接入/录像/AI/系统健康，未接入项显示“未接入”；用户可观察：黑白截图仍可凭形状与文字区分状态，点击异常项跳转详情或提示无权限）
- 参照设计: `docs/hmi/VISUAL_DESIGN.md` v2 §3.4/§3.5/§3.6/§7.6/§9/§10（`40ccf4e`）、`docs/hmi/INTERACTION_DESIGN.md` v2 §4.1/§7.1/§9/§10（`ef47841`）、`docs/hmi/TEST_DESIGN.md`
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-016-status/VERIFY-ITEM-016.md`）
- 关联前序评审: `REVIEW-ITEM-011.md` D-01/D-02（受限直达无入口、返回落到受限页）、`REVIEW-ITEM-012.md`/`REVIEW-ITEM-015.md` D-02（连接状态无三重编码）
- 评审日期/环境: 2026-10-16；Windows 主机（MINGW64 x86_64，无 C/C++/Qt）；**本轮未连接 RK3588 目标板**
- 判定: **TEST_PASS（范围严格限定为 ITEM-016 状态模型与 QML 静态接线；不构成 QML 运行期视觉结论；受限返回缺陷与图元一致性见 §5）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-016**。判定对象是任务内嵌用户可观察点：七类状态三重编码且去色可辨、正常简洁/异常突出、顶部条字段与未接入呈现、点击异常项跳转或提示无权限。
2. **本次未做 QML 运行期视觉评审**：板端缺 Qt5 Quick/Qml（E-01b），无黑白截图/运行素材；本文以**状态模型 + QML 渲染源码 + 装配链路 + 归档证据**为证据（§5 D-06）。
3. **本轮闭合了长期 D-02 的“三重编码”面**：状态模型为 5 级别各具颜色+形状+字形+文字，`StatusItem` 渲染“形状字形+名称+值”，正常无底块、异常底块+级别色+边框。
4. **本轮使 ITEM-011 D-02 变为可达缺陷**：`main.qml` 的 `DeniedView.onBack` 仍只 `denied=false`、不复位 `currentPageId`；ITEM-016 的“点击异常项→无权限”路径现在能真实触发它（详见 §5 D-01）。
5. **本轮未重新连接目标板**：板端 443/443 与状态探针由上游软件验证复跑。本 capability 授权方法为 `read/ui_evidence/design_assets`，主机无工具链；结论以源码/契约与归档证据为准。
6. 依赖缺口按职责交平台/环境；本评审不判定其“不可修复”，不返回 BLOCKED。本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `2aa1af1`（2026-10-16 03:48） | 新增 `status.{h,cpp}`/`status_probe.cpp` + `test_status.cpp`；`StatusItem`/`KeyStatusStrip`；`TopStatusBar` 改为 items 驱动；`P1Realtime` 接入关键状态区；`main.qml` 处理 `navigateRequested`；`main.cpp` 注入 `railhmiKeyStatus`/`railhmiTopBar`；证据归档 |
| 视觉设计 | `40ccf4e` | §3.4 报警级别形状、§3.5 流状态、§3.6 集成状态、§7.6 关键状态条、§9 颜色非唯一编码、§10 可访问性 |
| 交互设计 | `ef47841` | §4.1 顶部状态条/关键状态区、§9 可访问性、§10 异常恢复 |
| 软件验证 | `43380de` | TEST_PASS；443/443（新增状态断言）+ 状态探针 mixed×{maintainer,driver} PASS |

实现自述与仓库证据一致：`ITEM-016-status/README.md`、板端状态探针原文相符；**明确声明七类实时来源未接线（应用按“未接入”呈现）、QML 运行期未执行**，未外推。

---

## 2. 评审方法

1. 源码审阅：`status.{h,cpp}`（`StatusLevel`/形状/字形/颜色/标签、七类固定顺序、`build_key_status`/`build_top_bar`、`resolve_*_click`）、`test_status.cpp`、`StatusItem.qml`、`KeyStatusStrip.qml`、`TopStatusBar.qml`、`P1Realtime.qml`、`main.qml`、`main.cpp`。
2. 契约对照：5 级别的颜色/形状/字形/文字集合与 §3.4/§3.5/§3.6/§9；七类顺序与 §7.6/交互 §4.1；顶部条字段与 §4.1；未接入项与交互 §10。
3. 交互链路追踪：状态项点击→`navigateRequested`→`onStatusNavigate`→`denied`/`currentPageId`→`DeniedView.onBack`（原生 Grep/阅读）。
4. 归档证据核对：443/443 与状态探针（maintainer allowed=true / driver allowed=false）。

**方法限制**：主机无 Qt → 不能运行界面；不得据去色可辨的“机器可判定性”宣称人工视觉已确认（用户确认归现场）。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 5 级别三重编码集合核对 | 形状 {dot,spinner,triangle,square,minus-circle}、字形 {●,↻,▲,■,⊖}、文字 {正常,信息,警告,报警,未接入}、颜色令牌 {ok,info,degraded,error,neutral} 各 **5 个互不相同** → 去色可凭形状/文字区分 |
| R2 | 七类顺序/默认 | `pantograph→wheel_rail→door→camera→ai→recording→storage`；默认快照全 `Unavailable/未接入`、`abnormal=false`、`clickable=false` |
| R3 | 正常简洁/异常突出 | `StatusItem`：`color = abnormal ? bgColor : transparent`、`border.width = abnormal ? 1 : 0`；`all-ok` 用 `ok`+`dot`，`camera warning` = `triangle/degraded`，`ai alarm` = `square/error` |
| R4 | 点击目标 | 受电弓/轮轨/车门→`all_cameras`(P4)；摄像头/存储→`maintenance`(P6)；AI→`ai_debug`(P5)；录像→`replay`(P3)；正常项不可点 |
| R5 | 角色权限 | `resolve_status_click`/`resolve_top_click` 经 `role_can_access`：司机点 AI → `clickable && !allowed`；维护 → `allowed` |
| R6 | 顶部条 | 固定序 `time/mode/recording/ai/health/vehicle_speed/door_sensor`；`mode` = `可见光`+`红外未接入`+`neutral`+不可点；`vehicle_speed/door_sensor` = `未接入` |
| R7 | 装配链路逐键 | `main.cpp` 注入键（`key/label/shape/glyph/color/value/secondary/abnormal/clickable/allowed/target`）与 QML `modelData.*` 比对无未定义引用 |
| R8 | 受限返回链路 | `main.qml` `onStatusNavigate(pageId,allowed){ currentPageId=pageId; denied=!allowed }`；`DeniedView.onBack: denied=false`（**不复位 currentPageId**）→ 见 D-01 |
| R9 | 图元/资产核对 | 字形为 Unicode 文本字符（●↻▲■⊖）而非 §6 图标集；无 SVG 资源（见 D-04） |

---

## 4. 判据逐条核对（ITEM-016 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 七类状态固定顺序 | R2 | **满足** |
| 每项颜色+形状+文字三重编码、去色可辨 | R1/R3 集合两两不同 | **满足（模型/静态）** |
| 正常简洁、异常突出 | R3 | **满足（静态）** |
| 顶部时间/可见光+红外未接入/录像/AI/健康 | R6 | **满足（静态）** |
| 未接入项（车速/门磁）显示“未接入” | R6 | **满足** |
| 点击异常项跳详情或提示无权限 | R4/R5 模型正确；**返回目标错误**（R8/D-01） | **部分满足** |

---

## 5. 具体偏差与修复要求

**D-01（关键、现可达）— “无权限 + 返回”的返回落到受限页（继续 ITEM-011 D-02）**
- 观察：`main.qml` `onStatusNavigate` 在角色不允许时设 `currentPageId=受限页`、`denied=true`；`DeniedView.onBack: shell.denied = false` **未复位 `currentPageId`**，返回后 `Loader.visible: !denied` 为真且 `pageSource(受限页)` 加载，用户看到受限页占位（如司机点 AI 状态 → 显示“AI 调试”页）。ITEM-016 的状态点击使该路径**真实可触发**。
- 影响：与“提示无权限”的语义冲突；违反交互 §8“直接导航受限页显示无权限说明与返回”。
- 复现路径：`read app/src/ui/qml/main.qml`（`onStatusNavigate`/`DeniedView.onBack`）；`read app/src/core/src/status.cpp`（`resolve_status_click` driver→not allowed）。
- 期望：`onBack` 复位到允许页（至少 `railhmiDefaultPage`，更佳为记录“最近允许页”）；与 ITEM-011 D-01（提供 `--page`/URL 直达入口）一并闭合此链路。

**D-02（跨组件一致性）— 状态条已三重编码，但 CameraTile/VideoSurface 的连接状态仍为纯文本**
- 观察：状态区/顶部条经 `StatusItem` 输出“形状字形+名称+值”，而 `CameraTile`（P1/P4/P6）仍 `Text { text: tile.state }` 纯文本、无形状/色块（ITEM-015 D-02 未变）。同一“连接状态”概念在两处编码不一致。
- 期望：把 `StatusItem`/`StatusChip` 复用到 CameraTile 与 `VideoSurface` 占位层（流状态映射到 §3.5 形状/颜色/文字），统一三处状态编码。

**D-03（形状词汇表冲突，跨组件歧义）— 状态级别 `Warning=▲`/`Alarm=■` 与视觉 §3.4 报警级别 `L1=▲/L2=●/L3=◆` 不一致**
- 观察：`status_shape` 中 `Warning→triangle`、`Alarm→square`；而 `VISUAL_DESIGN` §3.4 定义一级报警为 **▲ 实心三角**、二级 ● 实心圆、三级 ◆ 实心菱形。去色截图下，状态区的 ▲（琥珀“警告”）与报警横幅的 ▲（红色“一级”）会指向不同语义；■ 不在既有形状词汇表中。
- 期望：在 `design_tokens.yaml` 建立**统一形状词汇表**（状态级 + 报警级可区分且不复用矛盾形状），或明确“状态条与报警横幅形状集合各自独立、互不混用”并写入视觉规范；避免 B/W 下的跨集误读。

**D-04（图元/资产）— 状态字形用 Unicode 文本字符，未使用视觉规范图标集**
- 观察：`status_glyph` 返回文本字符（●↻▲■⊖），`StatusItem` 以 `Text` 渲染；`VISUAL_DESIGN` §6 要求 24px 描边图标集（并登记授权来源）。部分字形（↻、⊖）在不同字体下形状/基线可能不一致。
- 期望：改用规范图标（或项目自绘 SVG）承载形状编码；如需轻量方案，至少验证目标板 CJK/等宽字体对所选字形的覆盖与对齐，并在资源清单登记。

**D-05（数据来源未接线）— 七类状态当前全为“未接入”，实时来源未接入**
- 观察：`main.cpp` 以 `KeyStatusSnapshot{}` 注入，七项与顶部录像/AI/健康均 `未接入`；实时采样（AI/录像/存储/系统健康/摄像头聚合）未接线，`abnormal`/点击链在应用内无数据可触发。
- 期望：健康监控/推理接入项把采样映射为 `CategoryStatus`（含 level/value），刷新 `railhmiKeyStatus`/`railhmiTopBar`；在此之前不宣称异常突出/点击跳转的端到端呈现。

**D-06（范围缺口，非本项缺陷）— QML 运行期与黑白去色人工确认未执行**
- 观察：板端缺 Qt5 Quick/Qml（E-01b）；去色可辨目前由“形状字形+文字互异”的机器判据支撑，尚无黑白截图/用户目视。
- 期望：平台补齐 Qt 后运行并录制 P1 状态区/顶部条（含异常态与点击无权限），由用户确认去色可辨；UI 实现评审再做还原审阅（含 D-01/D-02/D-03）。

### 已确认无问题/正向项

- 5 级别**颜色+形状+字形+文字四元组两两不同**，满足 §9“颜色非唯一编码”；正常无底块、异常底块+级别色+边框，符合 §7.6“正常简洁/异常突出”。
- 七类固定顺序、默认“未接入/不可点”，无虚构数据；异常项 `clickable` 与角色 `allowed` 分离（模型正确）。
- 顶部条固定序（时间/模式/录像/AI/健康/车速/门磁）；模式位 `可见光 · 红外未接入`、`neutral`、不可点，符合 §3.6/§0.4（无切换控件）；未接入项统一 `⊖ 未接入`。
- 点击目标映射合理（受电弓/轮轨/车门→P4、摄像头/存储→P6、AI→P5、录像→P3），与交互 §5.1/§6.4 一致。
- 装配链路逐键匹配，`StatusItem` 颜色全部经 `Theme.*` 解析（无硬编码色值）；`status_probe` 可在无 Qt 环境核对三重编码与权限。

---

## 6. 未覆盖 / 未执行（不得外推）

1. QML 运行期状态区/顶部条呈现与黑白去色人工确认（板端缺 Qt5 Quick/Qml E-01b）。
2. 七类实时来源接线（AI/录像/存储/系统健康/摄像头）与异常态端到端点击（D-05）。
3. 报警横幅浮层及其与状态条形状词汇的协同（D-03；横幅归报警项）。
4. CameraTile/VideoSurface 状态三重编码（D-02）。
5. 目标板命令本轮未执行（由上游验证复跑覆盖）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-016 状态模型与 QML 静态接线）。
- 依据：任务内嵌用户可观察点在**模型/静态**层成立——七类固定序、每级颜色+形状+字形+文字两两不同（去色可辨的机器判据）、正常简洁/异常突出、顶部条时间/模式/录像/AI/健康与未接入项、点击异常项按角色解析——经源码审阅、契约对照与归档证据（板端 443/443 + 状态探针 mixed×{maintainer,driver}）逐条成立；并闭合长期 D-02 的三重编码面。
- 明确边界：本 PASS **不表示** QML 已运行或黑白去色经人工确认，**不表示**异常突出/点击跳转在应用内可端到端演示（D-05 未接线）；且**“无权限返回”目标错误（D-01）为使能后可达的缺陷**，须修复后方可宣称该用户可观察点成立。
- 后续修复/交接要求见 §5：D-01（返回复位到允许页）为**必须修复**；D-02（状态编码统一到 CameraTile）与 D-03（形状词汇统一）为跨组件一致性项；D-04 图标资产、D-05 实时接线为后续交接；D-06 待 Qt 就绪后运行期与人工确认。
