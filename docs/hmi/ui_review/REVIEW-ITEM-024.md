# UI 实现评审证据 — ITEM-024（健康监控、结构化日志与断电自恢复）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `93e625a`，stamp `20261016-ITEM024-r2`）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/health.h`、`app/src/core/src/health.cpp`、`app/src/core/include/railhmi/core/recovery.h`、`app/src/core/src/recovery.cpp`、`app/src/ui/src/health_model.{h,cpp}`、`app/src/ui/qml/components/RecoveryOverlay.qml`、`app/src/ui/qml/pages/P6Maintenance.qml`、`app/src/ui/qml/main.qml`、`app/src/ui/src/main.cpp`、`app/tools/check_health_wiring.py`
  - 构建证据: `docs/hmi/test_evidence/ITEM-024-health/README.md`
- 被评工作项: `ITEM-024`（周期采样 NPU/CPU/温度/网络错误/磁盘并落库与上屏；日志按日滚动且可结构化查询；启动按顺序恢复配置→数据库→录像→取流→AI→云台归位→前方预置位并显示恢复进度。用户可观察：重启后各相机自动重连、AI 恢复、云台回前方预置位且**界面显示恢复步骤与完成状态**；单路非关键相机异常不导致应用退出）
- 参照设计: `docs/hmi/DESKTOP_APP_DESIGN.md` §15.3/§15.4/§7；`docs/hmi/INTERACTION_DESIGN.md` v2 §13；`docs/hmi/VISUAL_DESIGN.md` v2
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`VERIFY-ITEM-024.md`，core 663/663 + health-probe + 接线扫描）
- 关联前序评审: `REVIEW-ITEM-022.md`（P6 固定卡高/无滚动 D-02）、`REVIEW-ITEM-023.md`（P6 追加矩阵卡）
- 评审日期/环境: 2026-10-16；Windows 主机；**本轮未连接 RK3588 目标板**
- 判定: **TEST_FAIL**（core 健康/日志/恢复模型与 `STATUS` 判据成立，且"单路非关键不退出"模型成立；但**界面侧两处源码可确证缺陷**：P6 网格溢出裁切本项新增的"结构化日志/恢复进度"卡；恢复/日志 UI 未接线 `changed()`，进度不刷新、浮层不消失）

---

## 0. 结论与口径

1. 判定针对 ITEM-024 交付项与用户可观察点。core 层（健康评估/结构化日志查询与按日滚动/8 步恢复序列/非关键相机降级不退出）在源码级成立。
2. **失败项为源码可确证的界面缺陷**（不是"仅运行期视觉不可判定"）：
   - **D-01**：P6 `Grid` 现有 5 行固定高卡片累计约 1160px（+间距/边距 ≈ 1232px），超出可用高 ≈1008px（1920×1080 − 顶栏 48 − 边距），**无滚动** → 本项新增的"结构化日志卡""恢复进度卡"（末行，各 260px）约 3/4 被裁切到窗口外。
   - **D-02**：`RecoveryOverlay` 与 P6 恢复/日志卡对 `railhmiHealth` 仅做**方法调用绑定**，未接 `changed()`；QML 不追踪方法调用依赖 → 进度/步骤/日志**不会随模型更新刷新**，浮层 `visible` 亦不会在恢复完成时变为 false（会一直停留在"启动恢复 0%"）。
3. 运行期视觉/交互手感本轮仍不做结论（主机无 Qt、板端缺 Qt5 Quick/Qml，E-01b）。
4. **未修改任何实现代码**；只写本评审证据并维护清单。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `93e625a` | core `health.{h,cpp}`（`HealthSample`/`evaluate_health`/`log_record_json`/`query_logs`/`roll_health_logs_by_day`）、`recovery.{h,cpp}`（`RecoveryTracker` 8 步/`camera_result` 降级）；桥接 `RailhmiHealthModel`（`health/recovery/logs/logJson`）；`RecoveryOverlay.qml` + `main.qml` 浮层；P6 结构化日志卡 + 恢复进度卡；`check_health_wiring.py` |
| 设计 | `ef47841`/`40ccf4e` | §15.4 启动自恢复+UI 进度；§15.3 健康采样；§7 日志按日滚动 |
| 软件验证 | `ddb1005` | TEST_PASS；core 663/0 + health-probe + 接线扫描（r1 `log_day_key` 重复定义链接失败已修） |

---

## 2. 评审方法

1. 源码审阅：`health.h/cpp`、`recovery.h/cpp`、`health_model.{h,cpp}`、`RecoveryOverlay.qml`、`P6Maintenance.qml`、`main.qml`、`main.cpp`。
2. **布局几何独立复算**：P6 `Grid` 各卡固定高累计 vs 窗口可用高（1920×1080）。
3. **QML 反应性核对**：恢复/日志绑定是否引用可通知属性或连接 `railhmiHealth.changed`。
4. 契约对照：§15.4（UI 显示进度）+ 用户可观察点。
5. 归档证据核对：health-probe 原文与 v2 报告。

**方法限制**：主机无 Qt → 不能运行 P6/浮层或截图。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | core 健康 | `HealthSample`(npu/cpu/temp/net_errors/disk) + `HealthThresholds` + `evaluate_health` → Ok/Warn/Error + alerts（探针 `level=warn alerts=1 first="温度 80.0°C > 75.0°C"`） |
| R2 | core 日志 | `LogRecord` + `log_record_json`（单行 JSON）+ `query_logs`（severity/source/keyword/时间窗）+ `roll_health_logs_by_day`（按日滚动） |
| R3 | core 恢复 | `RecoveryTracker` 8 步 `load_config…front_ai_ready`；`camera_result(ok=false,critical=false)` → StartStreaming `Degraded` 且继续；关键步失败 → `alive=false`；`progress_pct` 12→100 |
| R4 | **布局几何（关键）** | P6 `Grid` 2 列固定卡高：260(camera)/260(AI) + 120/120 + 200(ptz)/200(log) + 140(NVR)/320(permMatrix) + **260(logQuery)/260(recovery)** = 1160px；+4×spacing(12)=48 → 1208；+边距 24 → **≈1232**。可用高 = 1080 − 顶栏 48 − 边距 24 = **1008** → **溢出 ≈224px**，无 `Flickable/ScrollView`。末行（本项新增卡）大部分在窗口外 |
| R5 | **反应性（关键）** | `RecoveryOverlay`：`property var st: R ? R.recovery() : …`（一次性方法调用绑定，无 `Connections`/`Timer`）；P6 恢复卡 `text/model: railhmiHealth.recovery()…`、结构化日志 `model: railhmiHealth.logs(fLevel,fSource,fKeyword)`——均只捕获 `railhmiHealth` 对象，无 `onChanged` 连接。除 `logCard.fLevel`（可通知，切筛选时会重算）外，其余**不随 `changed()` 刷新**；浮层 `visible: st.completed===false` 不会在完成时变 false |
| R6 | 桥接键 | `main.cpp` 注入 `railhmiHealth`（`health/recovery/logs/logJson`），样例 seed 一份**已完成**的 recovery（`rec.finish`）与 3 条日志（含跨 2 天 Warn） |
| R7 | 日志查询 UI 暴露面 | P6 结构化日志卡仅 3 个级别 chip（全部/警告+/错误）；有 `fSource/fKeyword` 属性但**无输入控件**，来源/关键字查询不可达 |
| R8 | 令牌 | `overlay_scrim/surface_elev/border_strong/ok/degraded/error/text_muted`、`radius.modal` 均存在 |

---

## 4. 判据逐条核对（ITEM-024 交付项）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 周期采样 NPU/CPU/温度/网络错误/磁盘 | core `HealthSample` + `evaluate_health`（R1） | 满足（模型） |
| 落库与上屏 | 上屏字段齐备；`device_health` 落库归集成 | 部分（handoff） |
| 日志按日滚动 | `roll_health_logs_by_day`（R2） | 满足 |
| 日志可结构化查询 | core `query_logs` 支持（R2）；UI 仅暴露级别，**来源/关键字控件缺失**（R7） | 部分 |
| 启动 8 步顺序恢复 + 显示进度 | core 序列（R3）；**UI 进度不刷新（R5）+ 恢复卡被裁切（R4）** | **不满足（界面侧）** |
| 单路非关键相机异常不退出 | `Degraded`+继续+`alive=true`（R3；探针 `degraded_cameras=1`） | 满足 |
| 界面显示恢复步骤与完成状态 | P6 恢复卡静态显示；浮层不更新/不消失（R4/R5） | **不满足** |

---

## 5. 偏差与修复要求（交桌面实现）

**D-01（严重）P6 网格溢出裁切本项新增的"结构化日志/恢复进度"卡**
- 观察：P6 `Grid` 无滚动，5 行固定高卡片累计 ≈1232px > 可用 ≈1008px（1920×1080）；末行 `maintLogQueryCard`(260) / `maintRecoveryCard`(260) 约 3/4 落在窗口外（R4）。这正是 ITEM-024"结构化日志上屏 + 恢复进度上屏"的载体。
- 复现路径：读 `app/src/ui/qml/pages/P6Maintenance.qml` 各卡 `height:` 与 `Grid{ anchors.fill: parent; columns:2 }`；与 `main.qml` `width:1920 height:1080` 及 `Theme.size.top_bar_height=48` 复算。
- 期望：为 P6 引入 `Flickable/ScrollView`（或分区选项卡/自适应高），确保含 ITEM-022/023/024 全部卡片可滚动可达；不得让依赖信息在默认窗口下不可见。

**D-02（严重）恢复/日志 UI 未接线 `changed()`，进度不刷新、浮层不消失**
- 观察：`RecoveryOverlay` 的 `st` 为一次性方法调用绑定（无 `Connections`/`Timer`）；P6 恢复卡与结构化日志 `model/text` 亦未连接 `railhmiHealth.changed()`。QML 不追踪方法调用依赖 → 恢复步骤/进度/日志行不会随模型更新刷新；`visible: st.completed===false` 在恢复完成时不会转为 false，浮层将停留在"启动恢复 0%"覆盖 UI（R5）。
- 复现路径：读 `RecoveryOverlay.qml`（无 `Connections`）与 `P6Maintenance.qml` 恢复/日志绑定；对比 `P2AlarmCenter.qml`/`P3Replay.qml`/P6 维护卡均使用 `Connections{ target: …; function onChanged() { … } }`。
- 期望：模型 expose 可通知属性或 QML 增 `Connections { target: railhmiHealth; function onChanged(){ …reload… } }`；恢复浮层据进度/完成切换可见性；结构化日志在新增记录时刷新。

**D-03（中）结构化查询的来源/关键字在 UI 不可达**
- 观察：结构化日志卡只有级别 chip；`fSource`/`fKeyword` 无输入控件（R7）。`DESKTOP §7`/item 要求"可结构化查询"。
- 期望：补来源/关键字输入（或按 `source` 下拉），使 `query_logs` 的 source/keyword 维度可从界面使用。

**D-04（观察）样例恒为"已完成恢复"，浮层路径未演示**
- 观察：`main.cpp` 样例 `rec.finish(...)` 后再 seed → `completed=true`；`RecoveryOverlay` 恒隐藏，实时进度路径无从观察（R6）。
- 期望：仿真下以定时推进若干步（配合 D-02 的反应性修复）演示进度与"降级"步骤。

**D-05（handoff）真实采样与落库**
- 真实采样源（rknpu/`/proc/stat`/thermal/`/proc/net/snmp`/`statvfs`）、真实重启自恢复（systemd/断电流水线）、`device_health`/结构化日志落库属平台/集成（当前桥接内存）。

### 已确认无问题/正向项

- core 健康评估（Ok/Warn/Error + 多阈值告警）、结构化日志 JSON/查询/按日滚动、8 步恢复序列与 `RecoveryTracker` 进度模型均在源码级成立。
- **"单路非关键相机异常不导致应用退出"**：`camera_result(ok=false,critical=false)`→`Degraded` 且继续，`alive=true`（探针 `degraded_cameras=1`）；关键步（配置/数据库）失败 → `alive=false` 并有显式提示文案。
- `RecoveryOverlay`/P6 卡片的 `objectName`（`recoveryOverlay/recoveryText/recoveryAlive/recoveryStep-*/maintLogQueryCard/structuredLogRow/maintRecoveryCard/recoveryStepRow-*`）齐备，利于自动化定位。

---

## 6. 未覆盖 / 未执行

1. QML 运行期视觉/交互（浮层呈现/隐藏、进度刷新、滚动）——无 Qt 环境。
2. 真实采样/落库/重启自恢复（平台/集成）。
3. 真机断电重启端到端。

---

## 7. 判定

**TEST_FAIL**（范围：ITEM-024 交付项与静态接线 vs 用户可观察点与 `DESKTOP §15.4`）。
- **失败依据（源码可确证，非运行期视觉不确定性）**：
  1. D-01：P6 无滚动网格溢出，本项"结构化日志/恢复进度"卡被裁切（≈224px 溢出），界面可观察的"显示恢复步骤"载体不可达；
  2. D-02：恢复/日志 UI 未接 `changed()`，进度/步骤/日志不刷新，恢复浮层不会在完成时消失（会覆盖 UI）。
- **边界**：本 FAIL **不否定** core 健康/日志/恢复模型与非关键相机降级语义（663/663、探针 PASS 成立）。修复要求见 §5；界面修复交桌面实现能力。
- 运行期视觉/交互手感仍留待 Qt 环境（§6），但本 FAIL 不依赖运行期——所列为源码级事实。
