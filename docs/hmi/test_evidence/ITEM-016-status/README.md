# ITEM-016 证据 — 关键状态区与顶部模式信息条

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-016`
- 依据: `docs/hmi/VISUAL_DESIGN.md` v2 §3.4/§3.5/§3.6/§7.6；`docs/hmi/INTERACTION_DESIGN.md` v2 §9（颜色非唯一编码）、§4.1 顶部状态条/关键状态区、§7.1；`docs/hmi/TEST_DESIGN.md`
- 源码: `app/src/core/include/railhmi/core/status.h`、`app/src/core/src/status.cpp`、`app/src/core/tools/status_probe.cpp`、`app/tests/core/test_status.cpp`、`app/src/ui/qml/components/{StatusItem,KeyStatusStrip,TopStatusBar}.qml`、`app/src/ui/qml/pages/P1Realtime.qml`、`app/src/ui/qml/main.qml`、`app/src/ui/src/main.cpp`、`app/tools/board_build.py`

## 1. 交付内容

1. **`railhmi-core` 状态模型**：`StatusLevel{Ok,Info,Warning,Alarm,Unavailable}` 每级具备**互不相同的颜色令牌 + 形状字形 + 文字**（`status_color/status_shape/status_glyph/status_level_label`），`status_abnormal` 判异常。
2. **关键状态区七类**（固定顺序）：受电弓/轮轨/车门/摄像头/AI/录像/存储。`build_key_status(snapshot, role)` 由快照派生：正常简洁（`ok` 点字形、无底块）、异常突出（`*_bg` 底块 + 级别色 + 形状 + 文字）；异常项 `clickable`，详情目标受电弓/轮轨/车门→P4、摄像头/存储→P6、AI→P5、录像→P3。
3. **顶部模式信息条**：时间（QML 实时时钟）/ 模式（`可见光` · `红外未接入`，neutral，无切换控件）/ 录像 / AI / 系统健康 / 未接入项（车速、门磁 → “未接入”）。`build_top_bar(snapshot, role)`。
4. **点击权限**：`resolve_status_click`/`resolve_top_click` 按角色判定；正常项不可点；异常项角色不允许时界面显示“无权限 + 返回”（复用外壳 DeniedView）。
5. **QML**：`StatusItem.qml`（三重编码渲染）、`KeyStatusStrip.qml`（七项）、`TopStatusBar.qml`（顶部各项），`P1Realtime.qml` 接入关键状态区，`main.qml` 处理 `navigateRequested → 跳转/无权限`；`main.cpp` 注入 `railhmiKeyStatus`/`railhmiTopBar`（按当前角色预解析 `allowed`）。
6. **`railhmi-status-probe` CLI**（core，无 Qt）：按场景/角色打印每项三重编码与点击解析，供无 Qt 环境核对。

## 2. 实测（板端 RK3588 本机；TestRun）

`board_build.py --mode TestRun` → `TESTRUN_EXIT=0` `STATUS_EXIT=0`。核心单测 **443 passed / 0 failed**（含 ITEM-016 新增状态断言）：
```
[PASS] 5 levels have 5 distinct shapes / 5 distinct glyphs (B&W distinguishable) / labels / color tokens
[PASS] key status has 7 items / 7 items in fixed order / default snapshot -> all 未接入, not abnormal, not clickable
[PASS] all-ok strip: nothing abnormal/clickable / all-ok uses ok color + dot shape (简单)
[PASS] camera warning prominent (triangle/degraded) / ai alarm prominent (square/error) / pantograph stays simple
[PASS] pantograph/wheel_rail/door -> P4 / camera -> P6 / ai -> P5 / recording -> P3
[PASS] driver clicking AI alarm -> no permission (提示无权限) / maintainer clicking AI alarm -> allowed to P5
[PASS] top bar: fixed order / shows time / mode bit: visible + IR not connected, neutral, no alarm color
[PASS] vehicle speed / door sensor shown as 未接入 / driver clicking AI warning -> no permission
```

状态探针（`--scenario mixed --role maintainer`，板端原文节选）：
```
item key=camera label=摄像头 level=warning shape=triangle glyph=▲ color=degraded value=CAM-04 离线 abnormal=true clickable=true target=maintenance allowed=true
item key=ai label=AI level=alarm shape=square glyph=■ color=error value=前方 AI 不可用 abnormal=true clickable=true target=ai_debug allowed=true
item key=mode label=模式 ... color=neutral value=可见光 secondary=红外未接入 clickable=false
item key=vehicle_speed label=车速 ... glyph=⊖ color=neutral value=未接入 unavailable=true
type=triple-encoding distinct_shapes=5 distinct_glyphs=5 distinct_labels=5 distinct_colors=5
type=acceptance items=7 top_items=7 triple_encoding_distinct=true mode_bit_ok=true unconnected_shown=true result=PASS
```

状态探针（`--role driver`）：异常项 `clickable=true` 但 `allowed=false` → 界面提示无权限（原文：`item key=ai ... allowed=false`）。

判据核对（用户可观察点）：
- 七类关键状态固定顺序、每项**颜色+形状字形+文字**三重编码，五级别形状/字形/文字/颜色两两不同 → **黑白截图仍可凭形状与文字区分** ✅
- 正常简洁（`ok` 点、无底块）、异常突出（底块 + 级别色 + 形状 ▲/■ + 文字）✅
- 顶部显示时间、可见光模式位与红外未接入、录像、AI、系统健康；车速/门磁显示“未接入” ✅
- 点击异常项：维护角色跳转对应详情（`allowed=true`），司机角色提示无权限（`allowed=false`）✅

## 3. 边界

- **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（E-01b），无黑白截图；本项以 **core 状态/点击单测（板端实跑 443/0）** + **`railhmi-status-probe` 三重编码与权限判定（板端实跑 PASS）** + **QML 源码/静态扫描 PASS** 为证据。视觉（去色可辨）由**形状字形+文字编码的机器可判定性**支撑，人工视觉确认按项目约束由用户执行。
- 七类状态的**实时来源**（AI/录像/存储采样、系统健康）尚未接线：应用当前按“未接入”中性呈现，异常突出/点击链路由模型与探针覆盖；实时采样接线归健康监控/推理接入项，本项不虚构数据。
- 顶部“车速/门磁”无硬件输入，按交互 §10 显示“未接入”，不伪造数值。

## 4. 证据文件

- `build-TestRun-...-ITEM016-r2.transcript.json` / `.log`（构建 + core 单测 + 令牌自检 + UI 扫描 + sim-inject + 状态探针）
- `...-ITEM016-r2.core-evidence_core-tests-...-r2.log` / `.xml`（443 passed / 0 failed）
- `...-ITEM016-r2.core-evidence_status-probe-...-r2.log`（mixed / maintainer：三重编码 + 点击允许）
- `...-ITEM016-r2.core-evidence_status-probe-driver-...-r2.log`（mixed / driver：异常可点但无权限）
- r1 轮为初次交付留痕（含 `status-probe`）

## 5. 复现

```sh
# 板端
bash app/build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-status-probe --scenario mixed --role maintainer
./build-core/tools/railhmi-status-probe --scenario mixed --role driver
# 经登记 SSH 入口（core 单测 + 状态探针）
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-016-status --stamp <tag>
```
