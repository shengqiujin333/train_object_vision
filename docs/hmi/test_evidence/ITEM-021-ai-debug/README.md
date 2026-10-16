# ITEM-021 证据 — AI 调试页与司机视图隔离

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-021`
- 依据: `docs/hmi/INTERACTION_DESIGN.md` v2 §5.5（显示检测框/ROI/置信度/距离/帧率/端到端延迟/模型版本/最近结果时间；逐项叠加层开关；司机默认不含本页且任一叠加层默认关闭；权限研发/调试/维护）、§7.4（AI 运行时字段）；`docs/hmi/VISUAL_DESIGN.md` v2 §8.5（主画面叠加检测框/ROI/置信度/距离；右侧指标面板 FPS/延迟/模型版本/最近结果时间；**调试叠加高对比描边：框 `#00E5FF`、ROI 虚线 `#FFD400`，避免与报警红混淆**）、§7.5（不叠加调试框除 P5）
- 源码: `app/src/ui/qml/pages/P5AiDebug.qml`、`app/src/core/include/railhmi/core/inference.h`、`app/src/core/src/inference.cpp`、`app/src/media/src/ai_worker.cpp`、`app/src/ui/src/main.cpp`、`docs/hmi/design_tokens.yaml`（+ 生成 `Theme.qml`/`tokens.json`）、`app/tools/check_ai_debug_wiring.py`、`app/tools/board_build.py`

## 1. 交付内容

1. **展示字段**：P5 每路叠加/面板显示 检测框、ROI、置信度、**目标距离**、帧率(FPS)、**端到端延迟**、模型版本、最近结果时间。
   - core `DetectionBox` 新增 `distance_m`；`AiDebugChannel` 新增 `distance_text`（由最近结果框取最小有效距离，`%.1f m` 或 `—`）。
2. **调试叠加层逐项开关，默认全部关闭**：`overlayBoxes`/`overlayRoi`/`overlayLabels`/`overlayMasks` + “全部关闭”，四个开关默认 `false`；ROI 用 `Canvas` 画虚线，掩码用半透明填充。
3. **独立高对比描边色**：新增主题令牌 `color.debug_box=#00E5FF`（框）与 `color.debug_roi=#FFD400`（ROI 虚线），来自 `VISUAL §8.5`，经 `design_tokens.yaml` 生成 `Theme.color.debug_box/debug_roi`；**UI 不硬编码色值**，且与报警红（`error_fg #FF6B6B`）明确区分。
4. **司机视图隔离**：导航按角色过滤（司机无 P5 入口）；受限直达由外壳 `DeniedView` 显示“无权限 + 返回”；P5 再加防御性司机无权限占位（`aiDebugDenied`），且司机主界面不渲染任何检测框/叠加层（P1 无调试框）。core 提供 `ai_debug_visible(role)`（= `role_can_access(role, AiDebug)`：司机 false，维护/研发/管理员 true）。
5. **静态取证工具** `app/tools/check_ai_debug_wiring.py`（板端可跑）：核对四开关默认关闭、调试令牌存在且被消费、距离/其余字段在位、司机守卫与路由/DeniedView、无硬编码色值。

## 2. 实测（板端 RK3588 本机；TestRun）

`board_build.py --mode TestRun`（stamp `20261016-ITEM021-r2`）→ `TESTRUN_EXIT=0` `SCAN_EXIT=0` `REPLAY_WIRE_EXIT=0` `AIDBG_WIRE_EXIT=0`。核心单测 **586 passed / 0 failed**（含 ITEM-021 新增断言）：
```
[PASS] debug distance from box (ITEM-021)
[PASS] driver cannot see AI debug/overlay (ITEM-021)
[PASS] maintainer/developer/admin can see AI debug
[check_ai_debug_wiring] PASS (ITEM-021 AI debug page + driver isolation wired)
```

`railhmi-ai-probe`（板端原文节选）：
```
type=debug camera=CAM-01 profile=front available=true model=rknn-2.3.2/yolov5s latency=33 boxes=1 conf=80 distance=9.0 m last=<1 秒前
type=debug camera=CAM-04 ... distance=11.0 m ...
type=debug camera=CAM-05 ... distance=10.0 m ...
type=debug camera=CAM-06 ... distance=12.0 m ...
type=acceptance worker_down_status=AI 不可用 alarm_annotation=AI 不可用 debug_all_unavailable=true temp_guard_degrades=true driver_debug_denied=true wide_angle_excluded=true
```

判据核对（用户可观察点）：
- 维护/研发角色可**逐项**开关叠加层（框/ROI/文字/掩码），默认全关 ✅
- 调试描边用独立高对比色（青框 `#00E5FF` / 黄 ROI 虚线 `#FFD400`），不与报警红混淆 ✅
- 司机登录后主界面无检测框与参数；`ai_debug_visible(Driver)=false`、P5 有司机无权限占位、直达由 DeniedView 显示无权限 ✅
- 展示字段齐全（含 **距离** 与 **端到端延迟**）✅

## 3. 边界

- **真实检测框/掩码/缩略图图像**由媒体/模型层生成；本项消费 `DetectionResult`（框/ROI/置信度/距离/耗时/模型版本）并做展示与叠加。
- **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（E-01b）；本项以 core 板端实跑（586/0）+ `railhmi-ai-probe` 距离/不可用/司机隔离判定 + QML 源码/静态扫描（硬编码 + AI 调试接线）为证据。
- 应用尚未把 `AiWorker` 接入 QML 运行时；非模拟时调试页按无数据/无结果呈现（如实）。

## 4. 证据文件

- `build-TestRun-...-ITEM021-r2.*`（权威轮：586/0 + ai-probe 距离/司机隔离 + 扫描/接线 PASS）
- `...-ITEM021-r1.*`（留痕：检查器令牌路径在板端未命中导致 `AIDBG_WIRE_EXIT=1`，r2 传 `--tokens design_tokens.yaml` 修正）

## 5. 复现

```sh
# 板端
bash app/build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-ai-probe --config ./config
python3 tools/check_ai_debug_wiring.py --root src/ui --tokens design_tokens.yaml
# 经登记 SSH 入口
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-021-ai-debug --stamp <tag>
```
