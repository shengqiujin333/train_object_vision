# UI 实现评审证据 — ITEM-012（配置驱动摄像头注册表与 UI 接入）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `0747577`，2026-10-16 01:56:25 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/camera_registry.h`、`app/src/core/src/camera_registry.cpp`、`app/src/ui/qml/components/{CameraTile,DetectionBadge,DeviceCard}.qml`、`app/src/ui/qml/pages/{P1Realtime,P4AllCameras,P6Maintenance}.qml`、`app/src/ui/src/main.cpp`、`app/tests/core/test_registry.cpp`
  - 构建证据: `docs/hmi/test_evidence/ITEM-012-registry/README.md`
- 被评工作项: `ITEM-012`（从 `cameras.yaml` 读取六路注册表并渲染槽位与排序；位置名、连接状态、检测职责徽标 DetectionBadge（仅 P4/P6，司机 P1 不显示）；登记 NVR 集成项（地址/已登记/状态/通道映射=未配置）；用户可观察：仅改配置增删/改名一路相机即改变视图槽位与徽标，删除必填字段启动报具体缺失项）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §4.2/§7.2/§7.3（`ef47841`）、`docs/hmi/VISUAL_DESIGN.md` v2 §3.5/§3.6/§7.5/§7.11/§7.12 + `design_tokens.yaml`（`40ccf4e`）、`docs/hmi/TEST_DESIGN.md` TC-SMOKE-06/TC-BADGE-01/TC-INTG-01
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-012-registry/VERIFY-ITEM-012.md`）
- 评审日期/环境: 2026-10-16；Windows 主机（MINGW64 x86_64，无 C/C++/Qt 工具链）；**本轮未连接 RK3588 目标板**
- 判定: **TEST_PASS（范围严格限定为 ITEM-012 注册表模型与 UI 源码接入；不构成 QML 运行期视觉/交互结论；状态徽标三重编码等还原项见 §5）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-012**（配置驱动注册表与 UI 接入）。判定对象是任务内嵌用户可观察点：配置顺序派生槽位、位置名、连接状态、检测职责徽标仅在 P4/P6、NVR 登记项（含通道映射未配置）、改配置改变槽位/徽标、删必填字段报具体缺失项。
2. **本次未做 QML 运行期视觉/交互评审**：板端缺 Qt5 Quick/Qml（E-01b），QML 无法运行；本文以**核心注册表模型 + QML 组件/页面源码 + 数据装配链路 + 设计契约对照**为证据（§5 D-06）。
3. **本轮未重新连接目标板**：板端 348/348 与测试/扫描由上游软件验证复跑。本 capability 授权方法为 `read/ui_evidence/design_assets`，主机无工具链；本轮以源码审阅与契约对照为独立证据，并标注引用关系。
4. 依赖缺口（主机无 C/C++/Qt；板端无 Qt5 Quick/Qml）按职责交平台/环境；本评审不判定其“不可修复”，不返回 BLOCKED。
5. 本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `0747577`（2026-10-16 01:56） | 新增 `camera_registry.{h,cpp}`、`CameraTile/DetectionBadge/DeviceCard`、`P1Realtime/P4AllCameras/P6Maintenance`、`main.cpp` 注入 `railhmiCameras/railhmiIntegrations`；`test_registry.cpp`；证据归档；无范围外改动 |
| 交互设计 | `ef47841` | v2 §4.2 三联、§7.2 字段、§7.3 检测职责 |
| 视觉设计 / 令牌 | `40ccf4e` | §3.5 流状态、§3.6 集成状态、§7.5 CameraTile、§7.11 DeviceCard、§7.12 DetectionBadge |
| 软件验证 | `419fd4c` | TEST_PASS；348/348（注册表域 25）+ ui-scan + sim-inject 一致 |

实现自述与仓库证据一致：`ITEM-012-registry/README.md`、板端日志与核心断言相符；**明确声明 QML 未在板端运行，未把模型层结论外推为界面已可用**。

---

## 2. 评审方法

1. 源码审阅：`camera_registry.{h,cpp}`（`build_registry`/`detection_badge_for`/`detection_badge_visible`/`build_integrations`）、`test_registry.cpp`、QML 组件与页面、`main.cpp` 注入映射。
2. 装配链路核对：C++ 注入 map 键（`id/name/position/badge/aiEnabled/state`；`id/name/address/registered/state/protocol/channelMapping`）与 QML `modelData.*` 引用**逐键比对**。
3. 契约对照：徽标文本/可见范围与视觉 §7.12；NVR 字段与 §7.11、§3.6；三联比例与交互 §4.2；状态呈现与视觉 §3.5。
4. 归档证据核对：板端 r2 关键断言（348/348、配置驱动增删改序、NVR 未配置、徽标可见性）。

**方法限制**：主机无 Qt/C++ 工具链 → 不能运行 QML；视图行为以源码/装配链路分析为准，不作运行期结论。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 徽标文本对照视觉 §7.12 | `前方障碍/人员`、`受电弓三状态`、`轮轨`、`车门`、`广角·无检测` —— **逐字一致** |
| R2 | 徽标可见范围对照 §7.12 | `detection_badge_visible` 仅 `AllCameras/Maintenance` 为真；`P1Realtime` 三联 `showBadge:false`；P4/P6 `showBadge:true` —— 满足“司机 P1 不显示” |
| R3 | 装配链路逐键比对 | C++ 键与 QML `modelData.*` **全部匹配**（cameras 与 integrations 均无未定义键） |
| R4 | NVR 对照 §7.11/§3.6 | `id=NVR`、`address=192.168.1.4`、`registered=true`、`state=unconfigured`、`protocol=—`、`channel_mapping=未配置`，不虚构通道号 |
| R5 | 三联比例对照交互 §4.2 | P1 左 0.26 / 前 0.48 / 右 0.26，顺序左-前-右；前方面积最大 |
| R6 | 配置驱动（归档断言） | rename/remove(→5)/add(→7)/order-follows-config；缺 `cameras[0].rtsp_url` 报具体字段 |
| R7 | P4/P6 网格尺寸核对 | `P4AllCameras`：`columns: 3`、每格 `height = (parent.height - spacing)/2` → **硬编码按 6 路（2 行×3 列）**；7 路时第 3 行溢出/裁切（见 D-01） |

---

## 4. 判据逐条核对（ITEM-012 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 从 `cameras.yaml` 派生六路槽位与顺序 | `build_registry` 保持配置顺序（R6） | **满足** |
| 显示位置名 | `CameraTile.title` = `modelData.name`（配置 name） | **满足** |
| 显示连接状态 | `CameraTile.state` = `modelData.state`（当前为原始 token `connecting`，见 D-02） | **部分满足（呈现形态）** |
| DetectionBadge 仅 P4/P6、司机 P1 不显示 | R2 | **满足** |
| NVR 登记项（地址/已登记/状态/通道映射=未配置） | R4 | **满足** |
| 仅改配置增删/改名/改序改变视图槽位与徽标 | 模型层满足；P4/P6 用 Repeater 跟随；P1 为固定三联（§4.2 要求）；**P4/P6 网格尺寸按 6 路固定**（R7，见 D-01） | **模型满足；UI 网格计数不自适应** |
| 删除必填字段启动报具体缺失项 | `cameras[0].rtsp_url` 断言 + 启动校验（ITEM-004） | **满足** |

---

## 5. 具体偏差与修复要求

**D-01（UI 自适应缺口）— P4/P6 摄像头网格按“恰好 6 路”硬编码尺寸，配置增删超过 6 路会溢出/裁切**
- 观察：`P4AllCameras.qml` 用 `Grid { columns: 3 }` 且每格 `height: (parent.height - Theme.spacing.s3) / 2`（固定 2 行）；`P6Maintenance.qml` 亦 `columns: 3` 且固定 `height: 120`。工作项明确要求“仅改配置增删一路相机即改变视图槽位”，且 `test_registry.cpp` 有 `add camera -> 7 slots` 用例；但视图在 7 路（或删至 4/5 路）时不会重排行数，第 3 行会溢出容器被裁切。
- 影响（UI 可见）：配置驱动在模型层成立，但渲染层对非 6 路配置不自适应，违背“改配置即改变视图槽位”的端到端预期。
- 复现路径：`read app/src/ui/qml/pages/P4AllCameras.qml`（`columns: 3` 与 `height/(2)`）；`read app/src/ui/qml/pages/P6Maintenance.qml`（固定 120 高）；`read app/tests/core/test_registry.cpp`（`add camera -> 7 slots`）。
- 期望：按 `railhmiCameras.length` 计算列/行（或使用可滚动容器/Flow），使 1..N 路均不裁切；并在文档写明 P4 支持的路数上限。

**D-02（视觉还原/编码规则）— 连接状态与 NVR 集成状态以原始英文 token 纯文本呈现，未按 §3.5/§3.6 做“颜色+形状+文字”状态徽标**
- 观察：`CameraTile` 右上角直接 `text: tile.state`（当前值 `connecting` 等原始 token），无状态色/形状/中文标签，也无“最近一帧时间戳/非 LIVE 占位层”；`DeviceCard` 亦 `"状态：" + integrationState`（原始 `unconfigured`）。而视觉规范 §3.5 要求摄像头/流状态用“文字+颜色+形状”三重编码（连接中/在线/画面冻结/离线/重连中/降级），§3.6 要求集成状态（未配置/未核验/可达/不可达/探测中）；`VISUAL_DESIGN` §7.5 还要求右下 FPS+最近帧时间、非在线叠加占位层。
- 影响（UI 可见）：状态可读性弱、含未本地化 token，且颜色/形状语义缺失——与“颜色非唯一编码、告警/异常需可辨”的视觉原则不符。本项工作项本身即含“显示连接状态”，故属该交付物的还原缺口。
- 复现路径：`read app/src/ui/qml/components/CameraTile.qml`；`read app/src/ui/qml/components/DeviceCard.qml`；对照 `VISUAL_DESIGN` §3.5/§3.6/§7.5。
- 期望：引入 `StatusChip`/状态→{label,color,shape} 的单点映射（消费 `Theme.stream_state`/`Theme.integration_state`），CameraTile 右上用徽标、非 LIVE 显示占位层与最近帧时间；DeviceCard 状态用 §3.6 徽标而非原始 token。

**D-03（组件细节缺口）— DetectionBadge 缺 §7.12 图标；DeviceCard 缺 §7.11 集成徽标与“最近探测时间”；`channel_mapping` 已配置时丢失通道列表**
- 观察：§7.12 徽标结构为“图标(16px)+文本”，当前 `DetectionBadge.qml` 只有文本；§7.11 DeviceCard 预期含集成状态徽标与“最近探测时间”字段，当前仅文本行；`build_integrations` 在 `has_channel_mapping` 时返回固定 `"已配置"`，未携带通道列表（§7.11 “已知列表 monospace / 未配置”）。当前 NVR 未配置，故仅“已配置”分支为潜在缺口。
- 期望：补徽标图标（按 §6 图标授权规则）；DeviceCard 增状态徽标与 `last_probe_ts`（运行时字段，未接入时显示"—/未核验"）；`channel_mapping` 保留已知通道列表文本。

**D-04（单一真相）— 徽标可见范围规则在 core 与 QML 各写一遍**
- 观察：`detection_badge_visible(page)` 仅被单测使用（原生 Grep 无 UI 调用点）；页面直接硬编码 `showBadge: true/false`。两处规则未来可能漂移（如新页面）。
- 期望：页面从 core 提供的“当前页是否显示徽标”派生（或 C++ 注入该布尔），使 §7.12 规则只有一处实现。

**D-05（观察，设计一致性）— P1 为固定三联，不随配置相机数增删**
- 观察：`P1Realtime` 固定渲染 left/primary/right 三格，取自配置；若配置缺某位置则显示名称回退（“左侧/前方/右侧”）与 `连接中` 占位。交互 §4.2 要求三联始终存在，故该固定行为符合设计；仅记录：P1 不受配置增减影响（与 P4/P6 不同），确保评审不误判。
- 期望：维持；文档注明三联为固定司机视野，配置增删主要影响 P4/P6。

**D-06（范围缺口，非本项缺陷）— QML 运行期未验证**
- 观察：板端缺 Qt5 Quick/Qml（E-01b），QML 未编译/运行；无截图/录屏。“司机 P1 无徽标、P4/P6 有徽标”的运行期呈现无运行素材。
- 期望：平台补齐 Qt 后运行并录制 P1/P4/P6（含改配置前后槽位/徽标变化、NVR 未配置卡），UI 实现评审再做还原审阅（含 D-02/D-03）。

### 已确认无问题/正向项

- 徽标文本与视觉 §7.12 **逐字一致**（R1），且“广角·无检测”使用更低对比度（`text_muted`）符合规范。
- 徽标可见范围严格限 P4/P6，司机 P1 `showBadge:false`（R2），契合“不得进入司机主画面”。
- C++→QML 数据装配**逐键匹配**（R3），无字段名错配导致的空绑定。
- NVR 字段与 §7.11/§3.6 一致（地址/已登记/状态/协议占位/通道映射未配置），不虚构通道号（R4）。
- P1 三联比例 0.26/0.48/0.26 与交互 §4.2 一致，前方最大（R5）。
- 配置驱动在模型层完整（rename/remove/add/order/缺字段报错，R6），与 `railhmi-config-check` 启动校验同源。
- 槽位保持 `cameras.yaml` 顺序、带 `order` 字段，排序由配置决定而非硬编码。

---

## 6. 未覆盖 / 未执行（不得外推）

1. QML 编译/运行与运行期渲染（板端缺 Qt5 Quick/Qml E-01b；主机无 Qt）。
2. 目标板命令本轮未执行（由上游验证复跑覆盖）。
3. 运行时连接状态/帧率/最近帧时间接入（取流状态机 ITEM-005/后续）。
4. NVR 通道映射“已配置”分支与 `last_probe_ts`（当前未提供，未实测）。
5. 非 6 路配置下的 P4/P6 布局（D-01，源码分析，未运行）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-012 注册表模型与 UI 源码接入）。
- 依据：任务内嵌用户可观察点——配置顺序派生槽位、位置名、检测职责徽标仅 P4/P6（司机不显示）、NVR 登记项（通道映射未配置）、改配置改槽位/徽标、缺必填字段报具体项——经核心模型审阅、装配链路逐键核对、设计契约对照与归档证据核对成立（板端 348/348）。
- 明确边界：本 PASS **不表示** QML 已运行或界面还原通过；**状态徽标三重编码/网格自适应/组件细节（D-01..D-03）尚需实现补齐**后方可宣称视觉得分。
- 后续修复/交接要求见 §5：D-01（网格自适应）、D-02（状态徽标三重编码与占位层）为**还原关键项**；D-03 组件细节、D-04 单一真相为工程改进；D-05 记录设计一致性；D-06 待 Qt 就绪后做运行期与还原审阅。
