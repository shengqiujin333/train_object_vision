# ITEM-014 证据 — 视频渲染与实时监控页三联布局

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-014`
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §6.1/§7.1；`docs/hmi/INTERACTION_DESIGN.md` v2 §7.1；`docs/hmi/VISUAL_DESIGN.md` v2 §8.1（三联 26/48/26、前方最大）；`docs/hmi/TEST_DESIGN.md`
- 源码: `app/src/core/include/railhmi/core/layout.h`、`app/src/core/src/layout.cpp`、`app/tests/core/test_layout.cpp`、`app/src/ui/qml/pages/P1Realtime.qml`、`app/src/ui/qml/components/VideoSurface.qml`、`app/src/ui/qml/components/CameraTile.qml`、`app/src/ui/src/main.cpp`

## 1. 交付内容

1. **`railhmi-core` 三联布局计算（单一来源）**：`compute_layout(width, height)` 返回
   - 顶部状态条（48）、三联行、关键状态区（64）、当前报警条（64）的像素 rect；
   - 三联比例 `left 0.26 / front 0.48 / right 0.26`，前方中央可视区（中央 60%×60%）。
   判据接口：`three_panel_ordered()`（左-前-右）、`front_is_largest()`（前方最大且 ≥0.34）、
   `p0_fits_without_scroll()`（P0 元素一屏内不滚动即可见）、`alarm_banner_obscures_front_center()`（一级横幅不遮挡前方中央）、`ratios_preserved()`（缩放比例保持）。
2. **接入 QML**：`main.cpp` 由 `compute_layout(1920,1080)` 计算比例/高度注入 `railhmiLayout`；渲染路径由配置（`app.render_path`，默认 `cpu`，`gl` 可开关）经 `railhmiRenderPath` 注入。
3. **`VideoSurface.qml`**：按所选渲染路径选择渲染实现（cpu：librga NV12→BGR 图元；gl：GstGLVideoItem），非 LIVE 显示占位层，右下角显示 `render:<path>`；**绝不把旧帧当实时画面**。
4. **`P1Realtime.qml`**：左-前-右三联（比例来自 `railhmiLayout`），底部关键状态区 + 当前报警条；`CameraTile` 内嵌 `VideoSurface`（消费 `live`/`state`）。

## 2. 实测（板端 RK3588 本机；TestRun：core 单测 + 令牌自检 + UI 扫描 + sim-inject）

构建：`board_build.py --mode TestRun` → `TESTRUN_EXIT=0`（编译系统 CMake 3.22.1，aarch64，gcc 11.4.0，无 Qt/GStreamer 依赖的 core 层）。

单测结果：**377 passed / 0 failed**（含 ITEM-014 新增布局用例）。关键断言（原文）：
```
[PASS] 1920x1080 three-panel order left-front-right
[PASS] 1920x1080 front largest (>=0.34 width)
[PASS] 1920x1080 P0 elements fit without scroll
[PASS] 1920x1080 L1 alarm banner does not cover front center
[PASS] 1280x720 three-panel order left-front-right
[PASS] 1280x720 front largest (>=0.34 width)
[PASS] 1280x720 P0 elements fit without scroll
[PASS] 1280x720 L1 alarm banner does not cover front center
[PASS] 16:9 1920x1080 vs 1280x720 ratios preserved
[PASS] 16:9 1920x1080 vs 960x540 ratios preserved
[PASS] resized layouts keep left-front-right order
[PASS] front area ratio >= 0.34
[PASS] side ratio within [0.22,0.30]
```

两档分辨率几何（由 `compute_layout` 计算，单测覆盖）：
| 分辨率 | row_w | left | front | right | front 占比 | row_h | P0 used ≤ h |
|---|---|---|---|---|---|---|---|
| 1920x1080 | 1872 | 487 | 898 | 487 | 0.480 | 856 | 1080 ≤ 1080 |
| 1280x720 | 1248 | 324 | 600 | 324 | 0.481 | 512 | 720 ≤ 720 |

判据核对：
- 两档下三联顺序左-前-右、前方最大且占比 ≥0.34 ✅
- 两档下 P0 元素（顶部状态条+三联+关键状态区+当前报警条）一屏内不滚动即可见 ✅
- 一级报警横幅（顶部 48..112）不进入前方中央可视区（y≥130）✅
- 窗口缩放（同宽高比 1920x1080→1280x720→960x540）三联顺序与比例保持 ✅
- 渲染路径由配置选定接入 QML（默认 cpu，gl 可开关）✅

## 3. 边界

- **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（E-01b），QML 无法编译运行。本项以 **core 布局几何单测（板端实跑）** + **QML 源码/静态扫描（`check_no_hardcoded_ui` PASS）** 为证据；视觉截图/交互体验需平台补齐 Qt 后补测。
- 视频纹理来源为 media 层（ITEM-013）；本项只接入渲染路径选择与布局，不重复实现取流链路。
- 一级报警横幅的浮层 z-order 与告警内容渲染由报警中心项实现；本项确保其几何不遮挡前方中央（顶部条带）。

## 4. 证据文件

- `build-TestRun-192_168_0_103-20261016-ITEM014-r1.transcript.json` / `.log`（构建 + 单测 + 取回）
- `...-ITEM014-r1.core-evidence_core-tests-20261016-ITEM014-r1.log`（逐项通过原文）
- `...-ITEM014-r1.core-evidence_core-tests-20261016-ITEM014-r1.xml`（JUnit）
- `...-ITEM014-r1.core-evidence_sim-inject-...`（模拟注入回归）

## 5. 复现

```sh
# 板端
bash app/build.sh CoreTests && ./build-core/tests/railhmi-core-tests
# 经登记 SSH 入口（构建 + 单测 + 令牌自检 + UI 扫描，取回证据）
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-014-layout --stamp <tag>
```
