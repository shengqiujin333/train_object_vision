# UI 实现评审证据 — ITEM-022（设备维护页 P6）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（修复轮 commit `f6acafb`，stamp `20261016-ITEM022-fix-r1`）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/maintenance.h`、`app/src/core/src/maintenance.cpp`、`app/src/ui/src/maintenance_model.{h,cpp}`、`app/src/ui/qml/pages/P6Maintenance.qml`、`app/src/ui/qml/components/DeviceCard.qml`、`app/src/ui/src/main.cpp`、`app/tools/check_maintenance_wiring.py`
  - 构建证据: `docs/hmi/test_evidence/ITEM-022-maintenance/README.md`
- 被评工作项: `ITEM-022`（设备维护页：摄像头在线/码流/帧率/最近帧/质量、AI 状态与模型版本、录像进程与剩余容量、网络错误计数与温度、云台 Pan/Tilt/Zoom 实际值与目标值及容差校验、重连入口与按日滚动维护日志、NVR 设备卡〔登记/可达/未配置 + 通道映射，无自动修改入口〕、一键归位/重调前方预置位 + 二次确认。用户可观察：调用后显示目标值与实际值及容差结果；超差显示失败并令前方 AI 未就绪；操作写入维护日志）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §5.6、§3.1（P6 行）、§7.4（云台字段）、异常表（PTZ-002）；`docs/hmi/VISUAL_DESIGN.md` v2 §8.6、§7.11（DeviceCard）；`docs/hmi/design_tokens.yaml`
- 关联上游验证: `n006 software_tester.software_verification` — v3 **TEST_FAIL**（缺手动重连/按日滚动日志）→ 修复轮 **TEST_PASS**（`VERIFY-ITEM-022.md`，core 617/617 + 维护探针 + 扫描）
- 关联前序评审: `REVIEW-ITEM-012.md`（DeviceCard/NVR 卡）、`REVIEW-ITEM-018.md`（维护日志落库）、`REVIEW-ITEM-021.md`（P5 AI 字段）
- 评审日期/环境: 2026-10-16；Windows 主机；**本轮未连接 RK3588 目标板**
- 判定: **TEST_PASS**（范围：ITEM-022 交付项与 `INTERACTION §5.6` / `VISUAL §8.6`·`§7.11` 逐项满足；无运行期视觉结论）

---

## 0. 结论与口径

1. 判定针对 ITEM-022 交付项与用户可观察点（目标 vs 实际 + 容差结果、超差 → 前方 AI 未就绪、操作写日志、重连、按日滚动日志、NVR 未配置呈现、二次确认）。
2. 所有交付项均可在**源码级**确证在位。**运行期视觉/交互（卡片观感、模态手感、滚动）本轮不做结论**（主机无 Qt、板端缺 Qt5 Quick/Qml，E-01b）。
3. 真实云台/重连/录像/温度采样属平台/现场；维护日志持久化归后续集成（handoff，§5）。
4. 本轮**未连接目标板**；core 617/617、维护探针、扫描取上游 `software_verification`。本 capability 授权方法为 `read/ui_evidence/design_assets`。
5. **未修改任何实现代码**；只写本评审证据并维护清单。残存均为观察项，不构成失败判据（§5）。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `f6acafb` | core 维护模型（PTZ 容差/动作/日志/前方 AI/维护行/录像/健康 + `build_maintenance_log`/`log_day_key`/`roll_logs_by_day`）；桥接 `RailhmiMaintenanceModel`（`ptzState/ptzAction/recording/health/reconnect/logsByDay/recentLogs`）；P6 分区卡片 + 二次确认模态；`check_maintenance_wiring.py` |
| 交互设计 | `ef47841` | v2 §5.6（字段/云台/日志/重连/NVR）、§7.4 |
| 视觉设计 | `40ccf4e` | §8.6（分区卡片/云台目标 vs 实际/模态确认写日志）、§7.11（DeviceCard 未配置态不显红、无自动改 NVR 入口） |
| 软件验证 | `b26c7cb` | TEST_PASS；617/0 + 维护探针 + 扫描（D-01/D-02 修复轮闭环） |

---

## 2. 评审方法

1. 源码审阅：`P6Maintenance.qml`（卡片/字段/云台/日志/NVR/模态）、`maintenance_model.{h,cpp}`（桥接键）、`maintenance.h`（core 字段/语义）、`DeviceCard.qml`、`main.cpp`（注入键）。
2. 契约对照：§5.6 字段清单、§8.6 分区卡片、§7.11 NVR 未配置态、§7.4 云台字段。
3. 令牌核对：P6/模态使用的 `Theme.color.ok/info/overlay_scrim/surface_elev/border_strong`、`Theme.radius.modal` 是否存在。
4. 隔离核对：NVR 卡无自动修改入口（DeviceCard 无按钮）。
5. 归档证据核对：v3/v4 验证报告与探针口径。

**方法限制**：主机无 Qt → 不能运行 P6 或截图。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 摄像头卡 | `maintCameraCard`：每行 `name (id)`、`在线/离线`（`ok`/`error`）、`stateLabel · fpsText · lastFrameText · 质量 qualityLabel · reconnectText`，含 `reconnect-<id>` 重连 chip 与 `reconnectNotice` |
| R2 | AI 卡 | `maintAiCard`：每路 `id · profileLabel · modelVersion · FPS fpsText · 延迟 latencyMs · lastResultText`（不可用时附 `unavailableReason`） |
| R3 | 录像/存储卡 | `maintRecordingCard`：`recording().stateText · 剩余 freeText · 已用 usedPct%` |
| R4 | 网络/温度卡 | `maintHealthCard`：`health().text`（NPU/CPU/温度/网络错误计数） |
| R5 | 云台卡（关键） | `ptzValues`：`Pan 实际 X / 目标 Y …Tilt…Zoom…`；`ptzCheckText`（容差内 `ok` 绿 / 超差 `error` 红）；`ptzFrontAi`：`前方 AI：READY/NOT_READY`；`ptzHome`/`ptzPreset` chip |
| R6 | 二次确认 | `askPtz` 设 `pendingKind` → `ptzConfirmModal`（`overlay_scrim` 全屏遮罩 + `确认/取消` chip）；`confirmPtz` 调 `M.ptzAction(kind)` 后 `reload()` |
| R7 | 日志卡 | `maintLogCard`：`M.logsByDay(7)` 按日分组（`maintLogDay` 日键+计数 + 内层 `maintLogRow` `action · user · result · detail`）；标题「维护日志（按日滚动）」 |
| R8 | NVR 卡 | `Repeater(railhmiIntegrations)` → `DeviceCard`：名称/地址/状态/协议/通道映射；**无任何修改按钮**（`DeviceCard.qml` 仅展示） |
| R9 | 桥接键对齐 | `main.cpp` 注入 `railhmiMaintenance`（`ptzState/ptzAction/recording/health/reconnect/logsByDay`）+ `railhmiMaintenanceCameras`（`id/name/online/stateLabel/fpsText/lastFrameText/qualityLabel/reconnectText`）+ `railhmiAiDebug` + `railhmiIntegrations`；与 P6 读取键一一对应 |
| R10 | 令牌可用性 | `ok/info/overlay_scrim/surface_elev/border_strong`、`radius.modal` 均在 `Theme.qml` 存在；无绑定缺失 |
| R11 | 操作→日志→AI | core `ptzAction` 执行后 `logs_.push_back(build_ptz_log(...))`；`checkMap` 经 `front_ai_ready(c.ok, front_ai_available)` → 超差 `NOT_READY`；`reconnect()` 写 `build_maintenance_log("reconnect",…)` |

---

## 4. 判据逐条核对（ITEM-022 交付项）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 摄像头在线/帧率/最近帧/质量 | `online`/`fpsText`/`lastFrameText`/`qualityLabel`（R1） | 满足 |
| 摄像头「码流」 | 以 `stateLabel`（实时/画面冻结/…）与在线态呈现（见 D-01） | 部分/待确认 |
| AI 状态与模型版本 | `maintAiCard` `modelVersion`+可用性+FPS+延迟+最近结果（R2） | 满足 |
| 录像进程与剩余容量 | `stateText`+`freeText`+`usedPct`（R3） | 满足 |
| 网络错误计数与温度 | `health().text`（R4） | 满足 |
| 云台 T/P/Z 实际值与目标值 | `ptzValues`（R5） | 满足 |
| 容差校验 | `ptzCheckText` + `pan/tilt/zoomDelta`（R5） | 满足 |
| 超差 → 前方 AI 未就绪 | `ptzFrontAi` NOT_READY（R5/R11） | 满足 |
| 一键归位/重调预置位 + 二次确认 | `ptzHome`/`ptzPreset` + `ptzConfirmModal`（R6） | 满足 |
| 操作写入维护日志 | `build_ptz_log`/`build_maintenance_log`（R11） | 满足 |
| 重连入口 | `reconnect-<id>` + `reconnectNotice`（R1） | 满足 |
| 按日滚动维护日志 | `logsByDay(7)` + `roll_logs_by_day`（R7） | 满足 |
| NVR 登记/可达/未配置 + 通道映射，无自动修改入口 | `DeviceCard`（R8） | 满足 |

---

## 5. 偏差/观察项（非阻断）

**D-01（观察，待确认）「码流」字段的呈现口径**
- 观察：§5.6/§8.6 摄像头卡列出「在线/**码流**/帧率/最近帧/质量」；实现以 `stateLabel`（实时/画面冻结/…）+ 在线态承载流状态，**未单列码率/编码/分辨率等 bitstream 数值**。若「码流」指实际码率/编码参数，则缺失；若指流状态/在线，则已覆盖。
- 期望：与设计确认「码流」口径；若需数值，补 `codec/分辨率/码率`（可由配置或取流层提供）或明确以流状态代替。

**D-02（观察）固定卡片高度 + 无滚动，长内容/空状态未处理**
- 观察：`Grid` 2 列、各卡固定高（日志卡 200、摄像头卡 260），无 `Flickable`/`ScrollView`；按日滚动拥多日/多条时日志与摄像头行会**超出裁切**；`logsByDay` 为空（非模拟）时日志卡仅剩标题，无「暂无日志」空态。
- 期望：日志/摄像头卡加滚动或自适应高，并补空状态文案（评审清单的「长文本/空状态」）。

**D-03（观察）重连计数双源**
- 观察：重连后 `reconnect()` 自增桥接内 `reconnect_counts_` 并显示于 `reconnectNotice`；但摄像头行的 `reconnectText` 来自静态注入的 `railhmiMaintenanceCameras`，**不随重连更新**；同一计数存在两处来源，可能不一致。
- 期望：重连后统一刷新摄像头行（由桥接输出行数据），单一口径。

**D-04（观察）AI 卡范围**
- 观察：P6 AI 卡复用 `railhmiAiDebug`（仅 `ai_enabled` 相机，样例 4 路），非全部 6 路；§5.6「AI：状态…」为总体视图。
- 期望：如设计需要总览，补非 AI 相机的「不适用/未启用」行，避免读者误以 4 路为全部。

**D-05（观察）`frontAiText()` 取用陈旧 `ptzResult`**
- 观察：一旦执行过动作，`frontAiText()` 优先返回 `ptzResult.check.frontAiReady`；其后 `reload()`（如 `onChanged`）不会清除该值，AI 就绪读数可能滞后于当前态，直到下次动作。
- 期望：以 `ptzState().check` 为主（动作结果仅作瞬时反馈/toast）。

**D-06（观察，非本项）录像「写入异常/文件完整性」**
- 观察：§5.6 录像含「写入异常、文件完整性（HLT-005）」；P6 录像卡仅 `运行状态/剩余/已用%`。属录像/存储能力范围。
- 期望：后续存储/录像项补写入异常与完整性呈现（如缺口/损坏计数）。

**D-07（handoff）真实操作与持久化**
- 真实云台 CGI/ONVIF、手动重连执行、录像进程、温度/网络采样归平台/现场；维护日志入库（`maintenance_log`）归后续集成（当前桥接内存 cap，按日滚动）。

### 已确认无问题/正向项

- 用户可观察三点齐备：归位/预置位后**目标 vs 实际 + 容差结果**；**超差 → 前方 AI NOT_READY**（`front_ai_ready`）；**操作写维护日志**（`build_ptz_log`）。
- 二次确认真模态（遮罩 + 确认/取消），按钮为 44px 可键盘 `FilterChip`。
- 摄像头（在线/状态/帧率/最近帧/质量）、AI（状态/模型版本）、录像存储、网络温度、云台、日志、NVR 七区卡片齐备；NVR 未配置态经 `DeviceCard` 呈现且**无自动修改入口**（`VISUAL §7.11`）。
- 重连入口与**按日滚动日志**（v3 D-01/D-02）已在源码级闭环。

---

## 6. 未覆盖 / 未执行

1. QML 运行期视觉/交互（卡片观感、模态、滚动）——无 Qt 环境。
2. 真实云台/重连/录像/传感器操作（平台/现场）。
3. 维护日志 DB 持久化与真机写入（集成项）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-022 交付项与静态接线 vs `INTERACTION §5.6` / `VISUAL §8.6`·`§7.11`）。
- 依据：摄像头/AI/录像存储/网络温度/云台/日志/NVR 七区卡片与字段齐备；云台 `目标 vs 实际 + 容差校验 + 前方 AI 就绪`、`一键归位/重调预置位 + 二次确认`、`操作写维护日志`、`重连入口`、`按日滚动日志`、`NVR 未配置态且无自动修改入口` 均在源码级确证（R1..R11）；用户可观察三点成立。
- 边界：本 PASS **不表示**运行期视觉/交互手感通过（无 Qt 环境，§6），**不表示**真实云台/重连/传感器操作可用（平台 handoff，D-07）。观察项 D-01（「码流」口径）、D-02（无滚动/空态）、D-03（重连计数双源）、D-04（AI 卡范围）、D-05（陈旧 AI 读数）、D-06（录像写入异常/完整性）供后续实现与运行期复评参考，不改变本次判定。
