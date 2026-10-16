# UI 实现评审证据 v2 — ITEM-024（D-01..D-04 修复轮复核）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（修复轮 commit `fd9dde4`，stamp `20261016-ITEM024-uifix-r1`）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/ui/qml/pages/P6Maintenance.qml`、`app/src/ui/qml/components/RecoveryOverlay.qml`、`app/src/ui/src/health_model.{h,cpp}`、`app/src/core/{include/railhmi/core,src}/health.*`·`recovery.*`、`app/src/ui/src/main.cpp`、`app/tools/check_health_wiring.py`
  - 构建证据: `docs/hmi/test_evidence/ITEM-024-health/README.md`
- 被评工作项: `ITEM-024`（健康监控/结构化日志/断电自恢复。用户可观察：重启后各相机自动重连、AI 恢复、云台回前方预置位且**界面显示恢复步骤与完成状态**；单路非关键相机异常不导致应用退出）
- 本能力历史: `REVIEW-ITEM-024.md` **TEST_FAIL**（D-01 P6 网格溢出裁切；D-02 恢复/日志未接 `changed()` 不刷新；D-03 来源/关键字查询 UI 不可达；D-04 恢复路径未演示）
- 参照设计: `docs/hmi/DESKTOP_APP_DESIGN.md` §15.3/§15.4/§7；`docs/hmi/INTERACTION_DESIGN.md` v2 §13
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`VERIFY-ITEM-024.md`，VERIFY-20261017-v1：core 663/663 逐行一致 + ext v2 103/103 + ext v3 60/60 + 扫描 v3 20 键 mismatch=[] + 板端负例对照）
- 评审日期/环境: 2026-10-16；Windows 主机；**本轮未连接 RK3588 目标板**
- 判定: **TEST_PASS**（D-01..D-04 均已闭环；无运行期视觉结论）

---

## 0. 结论与口径

1. 本 v2 复核 `REVIEW-ITEM-024.md` 四项缺陷，**逐项以源码为据独立核对，不采信验证报告自述**。
2. 四项**全部闭环**（§3），core 层（健康评估/日志/恢复序列/非关键相机降级）未被 UI 修复轮改动（663/663 逐行一致）。
3. 本 capability 授权方法为 `read/ui_evidence/design_assets`；主机无 Qt、板端缺 Qt5 Quick/Qml（E-01b），故**不做运行期视觉/交互手感结论**；真实采样/落库/重启自恢复属平台 handoff。
4. **未修改任何实现代码**；只写本评审证据并维护清单。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `fd9dde4`（uifix-r1） | P6 卡片区包 `Flickable`（`clip/contentWidth/contentHeight`）；`RecoveryOverlay` 改缓存 `st`+`reload()`+`Connections{onChanged}`+演示 `Timer`；P6 增 `healthSt/logRows`+`reloadHealth/reloadLogs`+health `Connections`、`logSourceInput/logKeywordInput`、`recoveryDemo` chip；桥接 `resetRecovery/advanceRecovery` + `demo_tracker_` |
| 上轮评审 | `53fcfa0` | D-01..D-04 |
| 软件验证 | `9b5c6f7` | TEST_PASS（含合成 pre-fix 变异树负例对照，证明 checker 咬合） |

---

## 2. 评审方法

1. 复核 D-01：读 P6 卡片区容器结构（`Flickable` 四要素 + `Grid` 是否仍锚定）。
2. 复核 D-02：读 `RecoveryOverlay` 与 P6 的 `Connections`/缓存属性/`reload*`。
3. 复核 D-03：读结构化日志卡输入控件与三参 `logs()` 贯通。
4. 复核 D-04：读 `resetRecovery/advanceRecovery` 与演示 chip/Timer。
5. 桥接核对：`health_model.h/cpp` 的演示态与真实态是否分离。
6. 归档证据核对：v1 报告与 ext/扫描口径。

**方法限制**：主机无 Qt → 不能运行 P6/浮层或截图。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | **D-01 可滚动** | P6 卡片区为 `Flickable { id: flick; anchors.fill; clip: true; contentWidth: width; contentHeight: grid.height + 2*Theme.spacing.s3 }`；`Grid { id: grid; x/y: s3; width: flick.width - 2*s3; columns: 2; spacing: s3 }` **不再 `anchors.fill: parent`**。→ 固定高卡片累计 > 视口时由滚动承载，不再裁切。**已修复** |
| R2 | **D-02 浮层反应性** | `RecoveryOverlay`：`property var st: ({completed:true,steps:[]})` + `function reload(){ st = R.recovery() }` + `Component.onCompleted: reload()` + `Connections{ target: R; function onChanged(){ reload() } }`；`visible: st.completed===false`。**已修复** |
| R3 | **D-02 P6 反应性** | P6：`property var healthSt/logRows` + `reloadHealth()/reloadLogs()` + `Component.onCompleted: reload()` + `Connections{ target: railhmiHealth; function onChanged(){ reloadHealth(); reloadLogs() } }`；恢复卡 `model/text: page.healthSt…`、日志 `model: page.logRows`。**已修复** |
| R4 | **D-03 来源/关键字** | 结构化日志卡新增 `logSourceInput`(110px) / `logKeywordInput`(130px)，均高 `touch_target_min`，`onEditingFinished → { logCard.fSource/fKeyword = text; page.reloadLogs() }`；`reloadLogs()` 调 `railhmiHealth.logs(fLevel, fSource, fKeyword)` 三参贯通。**已修复** |
| R5 | **D-04 演示路径** | 桥接新增 `Q_INVOKABLE resetRecovery()/advanceRecovery()` + 独立 `demo_tracker_/demo_active_/demo_clock_`；`recovery()` 在 `demo_active_` 时返回演示态；`RecoveryOverlay` `Timer{ interval:600; running: visible&&R; onTriggered: R.advanceRecovery() }`；P6 `recoveryDemo` chip → `resetRecovery()`。**已修复** |
| R6 | 演示/真实分离 | `advanceRecovery` 驱动 `demo_tracker_`（含 `StartStreaming` → `camera_result(false,false)` 降级），`recovery()` 按 `demo_active_` 切换；真实 `recovery_` 不被演示覆盖（seed 后 `demo_active_` 默认 false）。 |
| R7 | 令牌/尺寸 | `touch_target_min`（输入框高）、`overlay_scrim/surface_elev/border_strong/degraded/error/ok/text_muted`、`radius.modal/chip` 均存在 |

---

## 4. 判据逐条核对

| 上轮偏差 | 现状 | 判定 |
|---|---|---|
| D-01 P6 溢出裁切 | `Flickable`+`clip`+`contentHeight`，Grid 去锚定（R1） | **已修复** |
| D-02 恢复卡不刷新 | 缓存属性 + `Connections{onChanged}`（R2/R3） | **已修复** |
| D-02 浮层不隐藏 | `visible: st.completed===false` + reload（R2） | **已修复** |
| D-03 来源/关键字不可达 | 两个 `TextInput` + 三参 `logs()`（R4） | **已修复** |
| D-04 恢复路径未演示 | `resetRecovery/advanceRecovery` + Timer + chip（R5） | **已修复** |
| 用户可观察：界面显示恢复步骤与完成状态 | 恢复卡 + 浮层可刷新/可演示（R1..R5） | 满足（源码级） |
| 单路非关键相机异常不退出 | core `camera_result(false,false)` → 降级 + `alive=true`（未改动，663/663） | 满足 |

---

## 5. 观察项（非阻断；handoff 或运行期待验）

**O-1（观察）演示恢复与真实恢复共用 `recovery()` 出口**
- 观察：`recovery()` 在 `demo_active_` 时返回演示 `demo_tracker_`。`RecoveryOverlay.Timer` 的 `running` 条件为 `visible && R`，即只要浮层可见（恢复未完成）就会每 600ms `advanceRecovery()`，而 `advanceRecovery()` 在 `demo_active_==false` 时会**自动启动演示**。因此若将来接入真实启动恢复（completed=false 且浮层可见），浮层将转而显示演示进度而非真实 `recovery_`。
- 期望：真实恢复接入时，Timer/`advanceRecovery` 仅应在明确的演示模式（如 `app.simulation` 或显式 chip 触发）下运行，避免演示态遮蔽真实恢复进度。

**O-2（观察）`logJson(int)` 桥接未在 UI 使用**
- 结构化日志行已内联 `levelLabel [source] message fields`；`logJson` 的逐条 JSON 视图未暴露（§7 结构化可查询的 JSON 形态）。建议补"查看 JSON"或复制入口。

**O-3（handoff）真实采样与落库、真实重启自恢复**
- 采样源（rknpu/`/proc/stat`/thermal/`/proc/net/snmp`/`statvfs`）、`device_health`/结构化日志落库、systemd/断电流水线属平台/集成（当前桥接内存列表）。

**O-4（运行期待验）Flickable 手感/浮层动画/输入框焦点**
- 属 Qt 环境（E-01b）就绪后运行期核验；本轮结论限于源码接线。

### 已确认无问题/正向项

- 四项缺陷均在源码级闭环；上游验证另有**板端负例对照**（合成 pre-fix 变异树令 checker FAIL），证明断言真实咬合。
- core 层（健康评估/结构化日志查询与日滚动/8 步恢复/非关键相机降级）未因 UI 修复轮改动（663/663 逐行一致）。
- Flickable/Connections/输入框/演示链路的 `objectName`（`recoveryOverlay/recoveryText/recoveryStep-*/maintLogQueryCard/structuredLogRow/logSourceInput/logKeywordInput/recoveryDemo/maintRecoveryCard`）齐备。

---

## 6. 未覆盖 / 未执行

1. QML 运行期视觉/交互（滚动/浮层/输入焦点）——无 Qt 环境。
2. 真实采样/落库/重启自恢复（平台/集成）。
3. 真机断电重启端到端。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-024 交付项与静态接线 vs 用户可观察点与 `DESKTOP §15.4`）。
- 依据：上轮 D-01（P6 溢出→`Flickable` 滚动）、D-02（恢复/日志不刷新→`Connections{onChanged}`+缓存属性）、D-03（来源/关键字不可达→输入框+三参 `logs()`）、D-04（恢复路径未演示→`resetRecovery/advanceRecovery`+Timer+chip）均在源码级闭环（R1..R5）；用户可观察的"界面显示恢复步骤与完成状态"与"单路非关键不退出"成立。
- 边界：本 PASS **不表示**运行期视觉/交互手感通过（无 Qt 环境，§6），**不表示**真实采样/落库/重启自恢复可用（平台 handoff，O-3）。观察项 O-1（演示/真实恢复共用出口）、O-2（`logJson` 未上屏）供后续集成与运行期复评参考，不改变本次判定。
