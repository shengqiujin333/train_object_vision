# UI 实现评审证据 — ITEM-011（应用外壳与角色化导航）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `00d60ce`，2026-10-16 01:35:05 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/navigation.h`、`app/src/core/src/navigation.cpp`、`app/src/ui/qml/main.qml`、`app/src/ui/qml/components/{TopStatusBar,NavBar}.qml`、`app/src/ui/qml/pages/{PageView,DeniedView}.qml`、`app/src/ui/src/main.cpp`、`app/tests/core/test_navigation.cpp`
  - 构建证据: `docs/hmi/test_evidence/ITEM-011-shell/README.md`
- 被评工作项: `ITEM-011`（单窗口、顶部全局状态条、六页固定导航与返回路径、按角色过滤渲染入口；顶部模式位固定可见光/红外未接入且无切换控件；用户可观察：司机启动不渲染 AI 调试/维护入口、直达受限页显示无权限说明与返回、维护角色全部入口出现）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §2/§3.1/§3.2/§4.1/§8/§11#10、§11#14（`ef47841`）、`docs/hmi/VISUAL_DESIGN.md` v2 §3.6/§8.1 + `design_tokens.yaml`（`40ccf4e`）、`docs/hmi/TEST_DESIGN.md` TC-SMOKE-07/TC-ROLE-*
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-011-shell/VERIFY-ITEM-011.md`）
- 评审日期/环境: 2026-10-16；Windows 主机（MINGW64 x86_64，无 C/C++/Qt 工具链）；**本轮未连接 RK3588 目标板**（板端事实采用上游软件验证复跑）
- 判定: **TEST_PASS（范围严格限定为 ITEM-011 导航/权限模型与外壳交付物；不构成 QML 运行期视觉/交互结论；并对“直达受限页→返回”链路记录源级缺陷，见 §5 D-01/D-02）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-011**（应用外壳与角色化导航）。判定对象是任务内嵌用户可观察点：六页固定导航与返回路径、按角色过滤入口、司机启动不渲染 AI 调试/维护入口、直达受限页显示无权限说明与返回、维护角色全部入口出现、模式位固定且无切换控件。
2. **本次未做 QML 运行期视觉/交互评审**：板端缺 Qt5 Quick/Qml（E-01b），QML 外壳无法构建运行，无截图/录屏；本文以**核心导航模型 + 外壳/组件源码 + 权限契约对照**为证据（§5 D-06）。
3. **本轮未重新连接目标板**：板端 319/319 与测试/扫描由上游软件验证复跑。本 capability 授权方法为 `read/ui_evidence/design_assets`，主机无工具链；本轮以源码审阅与契约对照为独立证据，并标注引用关系。
4. 依赖缺口（主机无 C/C++/Qt 工具链 E-00；板端无 Qt5 Quick/Qml E-01b）按职责交平台/环境；本评审不判定其“不可修复”，不返回 BLOCKED。
5. 本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `00d60ce`（2026-10-16 01:35） | 新增 `navigation.{h,cpp}`（六页/权限/默认页/模式位）、QML 外壳与组件/页面骨架、`main.cpp` 角色化注入；`test_navigation.cpp`；ITME-011 证据归档；无范围外改动 |
| 交互设计 | `ef47841` | v2 §3.1 页面集合、§3.2 导航/不可见项不渲染/返回路径、§8 权限矩阵、§4.1 顶栏字段、§11#10/#14 |
| 视觉设计 / 令牌 | `40ccf4e` | §3.6 模式位、§8.1 外壳结构；令牌经 ITEM-010 生成 |
| 软件验证 | `5096cb1` | TEST_PASS；319/319（新增导航域 45）+ ui-scan + sim-inject 一致 |

实现自述与仓库证据一致：`ITEM-011-shell/README.md`、板端日志与核心断言相符；**明确声明 QML 未在板端运行，未把模型层结论外推为界面已可用**。

---

## 2. 评审方法

1. 源码审阅：`navigation.{h,cpp}`（`Page`/`to_string`/`page_title`/`role_can_access`/`visible_pages`/`default_page`/`resolve_navigation`/`mode_display`）、`test_navigation.cpp`、`main.qml`、`NavBar.qml`、`TopStatusBar.qml`、`PageView.qml`、`DeniedView.qml`、`src/main.cpp`。
2. 契约对照：六页 id/标题/顺序、权限矩阵、默认落地页、模式位与交互 §3.1/§3.2/§8/§4.1、视觉 §3.6/§8.1 逐项对照。
3. 导航链路追踪：谁设置 `currentPageId`、`denied` 的触发与复位、`resolve_navigation` 的调用点（原生 Grep）。
4. 归档证据核对：`VERIFY-ITEM-011.md`、板端 r1 日志与关键断言。

**方法限制**：主机无 Qt/C++ 工具链 → 不能运行外壳；QML 行为以源码级链路追踪为准，不作运行期结论。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 六页 id/标题/顺序对照交互 §3.1/§3.2 | `realtime/alarm_center/replay/all_cameras/ai_debug/maintenance`，标题 实时监控/报警中心/历史录像/全摄像头/AI 调试/设备维护；顺序与 P1..P6 一致 |
| R2 | 权限矩阵对照交互 §8 | 司机=实时/报警/回放；维护/研发/管理员=全部；`visible_pages` 保持固定顺序、隐藏项不出现 |
| R3 | 默认落地页对照交互 §2 | 司机→realtime、维护→all_cameras、研发→ai_debug、管理员→maintenance |
| R4 | 模式位对照视觉 §3.6/交互 §4.1 | `mode_display()` = 可见光 / 红外未接入（`text_disabled` 灰显）/ `interactive=false`；`TopStatusBar` 无任何切换控件 |
| R5 | 导航链路追踪（原生 Grep `currentPageId/denied/resolve_navigation`） | `currentPageId` 仅由 `NavBar.onNavigate` 修改，而 NavBar 只渲染**已过滤的可见项**；`resolve_navigation` **无任何 UI 调用点**；`denied` 因此无触发路径（见 D-01） |
| R6 | 返回路径核对 | `DeniedView.onBack` 仅 `shell.denied = false`，未复位 `currentPageId`（仍为受限页）→ 返回后 `PageView` 会以受限页标题显示（见 D-02） |
| R7 | 归档证据核对 | 核心断言含 `driver sees 3 nav entries`、`maintainer sees all 6 nav entries`、`driver direct AI debug -> denied`、`mode has no interactive toggle`；板端 319/319 |

---

## 4. 判据逐条核对（ITEM-011 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 单窗口 + 顶部全局状态条 + 六页固定导航 | `main.qml` 单窗口：TopStatusBar + NavBar + 页面宿主；六页固定（R1） | **满足** |
| 按角色过滤渲染入口（司机不渲染 AI 调试/维护/全摄） | `visible_pages` + NavBar 仅渲染过滤项（R2/R5） | **满足** |
| 维护角色全部入口出现 | `visible_pages(Maintainer).size()==6`（R2/R7） | **满足** |
| 直达受限页显示无权限说明与返回 | `DeniedView` 存在且文案正确，但**无直达入口**且返回行为不正确（R5/R6） | **源级部分满足**（见 D-01/D-02） |
| 模式位固定可见光、红外未接入、无切换控件 | `mode_display()` + `TopStatusBar`（R4） | **满足** |
| 顶部状态条字段（时间/车速/模式/录像/AI/健康） | 时钟/模式/录像/AI/健康齐备；车速可选未接入（交互 §4.1 “若可接入”） | **满足** |

---

## 5. 具体偏差与修复要求

**D-01（导航通路的可观察缺口）— “直达受限页”无入口，DeniedView 与 `resolve_navigation` 的拒绝分支在运行外壳中不可达**
- 观察：`main.qml` 的 `currentPageId` **仅**由 `NavBar.onNavigate` 修改，而 `NavBar.entries = railhmiNav` 已按角色过滤，司机看不到 `ai_debug/maintenance`，因此 `shell.denied` 在运行中无法变为 true。核心函数 `resolve_navigation(role, page)`（含 refused 分支）**没有任何 UI/C++ 调用点**，仅在单测中使用（R5）。外壳另用 `shell.allowed()` 从注入的过滤列表自行判定权限。
- 影响：工作项目标“直达受限页显示无权限说明与返回”在运行外壳中**无法被触发/验证**；`DeniedView` 成为事实上的死路径。
- 复现路径：`read app/src/ui/qml/main.qml`（`onNavigate`/`allowed`/`currentPageId`）；原生 Grep `resolve_navigation` 仅命中 `navigation.cpp`/`test_navigation.cpp`。
- 期望：提供一条**显式直达入口**并经 `resolve_navigation` 路由——例如命令行 `--page <id>`、`railhmi://` URL scheme 或调试用 `gotoPage(id)`，使司机可请求受限页 → 命中拒绝 → 显示无权限；同时外壳改用 `resolve_navigation` 的返回值驱动 `denied`，使其可被端到端验证。

**D-02（返回路径逻辑缺陷）— `DeniedView` “返回”只清 `denied`，不复位 `currentPageId`，返回后落到受限页**
- 观察：`main.qml` `DeniedView { onBack: shell.denied = false }`；`currentPageId` 仍为被拒页（如 `ai_debug`）。返回后 `PageView.visible: !denied` 为真且 `pageTitle: titleFor(currentPageId)` 显示“AI 调试”占位页。即“返回”把用户带到了**本应无权限的页面**，而非允许页。
- 影响：与工作项“返回路径”及交互 §3.2 返回语义直接冲突；即便 D-01 修补后，该返回也会误导司机。
- 复现路径：`read app/src/ui/qml/main.qml`（`onBack`）；`read app/src/ui/qml/pages/DeniedView.qml`（`back()`）。
- 期望：`onBack` 复位到允许页（至少 `railhmiDefaultPage`，更佳为记录“最近允许页”并返回）；或直接以 `default_page(role)` 作为受限请求的回退页。

**D-03（单一真相/可维护性）— 外壳重复实现权限判定，未复用 `resolve_navigation`**
- 观察：核心 `resolve_navigation` 提供允许/拒绝结果，但外壳用 `shell.allowed(id)`（在注入的 `railhmiNav` 里查 id）自行判定；两条判定路径未来可能漂移（如权限矩阵扩展时）。
- 期望：外壳导航统一经 `resolve_navigation`（把结果暴露给 QML，或 C++ 侧提供 `requestPage(id)`），使权限矩阵只有一处实现；`resolve_navigation` 也应被 UI 覆盖（否则该函数仅测试用）。

**D-04（观察，令牌/文案/死属性）— 观感与语义小项**
- `TopStatusBar.modeInteractive` 被声明并由外壳传入，但组件内**未使用**（无论真假都不渲染切换控件）；建议要么用它显式断言“无控件”，要么移除以避免暗示可交互。
- `NavBar` 用 `height: Theme.size.nav_width`（横向宽度令牌）作为**导航项高度**；建议新增 `size.nav_item_height`，避免宽度调整时高度被动变化。
- `PageView` 显示 `roleName` 为机器 token（`driver/maintainer/...`）而非中文；`page_title(AiDebug)` 为“AI 调试”（设计为“AI调试”，空格差异）。均为占位骨架可接受，但需在文案/令牌层统一（与 REVIEW-ITEM-006 D-02、REVIEW-ITEM-004 D-03 同源的“显示标签单点映射”）。

**D-05（范围/交接）— 上下文相关返回路径未实现**
- 交互 §3.2 要求：P2/P3 从 P1 进入时保留来源（“返回实时监控”）；从报警卡片进入 P3 时锁定该报警相关摄像头与时间。本项只交付固定导航 + 受限返回，未实现上下文返回。期望：由后续页面/路由项（报警中心/回放）实现，并在此外壳预留路由上下文。

**D-06（范围缺口，非本项缺陷）— QML 运行期未验证**
- 观察：板端缺 Qt5 Quick/Qml（E-01b），外壳未编译/运行；无截图/录屏。司机/维护角色的实际渲染结果、无权限页呈现均无运行素材。
- 期望：平台补齐 Qt 后运行外壳并录制：司机启动（无 AI 调试/维护项）、直达受限页（无权限+返回）、维护角色（6 项）、模式位（可见光+红外未接入、无控件）；UI 实现评审再做还原审阅。

### 已确认无问题/正向项

- 六页 id/标题/顺序与交互 §3.1/§3.2 **逐项一致**（R1）；权限矩阵与 §8 **逐项一致**（R2）。
- 默认落地页按角色（R3）；模式位 `可见光 / 红外未接入 / interactive=false` 与交互 §4.1、视觉 §3.6 一致，`TopStatusBar` 无任何切换控件（R4）。
- 隐藏项**不渲染**（`NavBar.entries` 为过滤后列表），符合交互 §3.2“权限不可见项不显示”。
- `DeniedView` 文案含“无权限”+被拒页标题+返回，符合交互 §8 的说明形态。
- 外壳/组件全部经 `Theme.*` 消费令牌，`check_no_hardcoded_ui.py` 扫描 PASS（无硬编码色值/字号），延续 ITEM-010 约束。
- 核心导航域 45 条断言（板端 319/319）覆盖权限矩阵、过滤、默认页、拒绝与模式位。

---

## 6. 未覆盖 / 未执行（不得外推）

1. QML 外壳编译/运行与运行期渲染（板端缺 Qt5 Quick/Qml E-01b；主机无 Qt）。
2. 目标板命令本轮未执行（由上游验证复跑覆盖）。
3. 页面内容（六页具体实现）—— 本层仅骨架与导航。
4. 上下文相关返回路径（交互 §3.2）与录像导出权限在页面内的落实。
5. `--page`/URL 直达入口（当前不存在，见 D-01）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-011 导航/权限模型与外壳交付物）。
- 依据：任务内嵌用户可观察点中可核验的部分——六页固定导航与顺序、按角色过滤渲染入口、维护角色全部入口、模式位固定且无切换控件、受限页文案与返回控件存在——经核心模型审阅、契约逐项对照与归档证据核对成立（板端 319/319）。
- 明确边界：本 PASS **不表示** QML 外壳已运行或角色化渲染通过；并且 **“直达受限页→无权限→返回”链路在源级存在缺陷**（D-01 无入口、D-02 返回落到受限页），须由实现修复后方可宣称该用户可观察点成立。
- 后续修复/交接要求见 §5：D-01（直达入口 + 走 `resolve_navigation`）、D-02（返回复位到允许页）为**必须修复**项；D-03（单一真相）、D-04（死属性/令牌/文案）为工程改进；D-05 上下文返回交后续页面项；D-06 待 Qt 就绪后做运行期与还原审阅。
