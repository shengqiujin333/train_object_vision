# ITEM-015 证据 — 六路并发与全摄像头页

- Work item: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-015`
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §6.2/§7.1；`docs/hmi/INTERACTION_DESIGN.md` v2 §7.3；`docs/hmi/VISUAL_DESIGN.md` v2 §7.5（CameraTile 最近帧/FPS、状态三重编码）；`docs/hmi/TEST_DESIGN.md`
- 源码: `app/src/core/include/railhmi/core/grid.h`、`app/src/core/src/grid.cpp`、`app/src/core/include/railhmi/core/channel_view.h`、`app/src/core/src/channel_view.cpp`、`app/tests/core/test_grid.cpp`、`app/src/media/include/railhmi/media/multi_stream.h`、`app/src/media/src/multi_stream.cpp`、`app/src/media/tools/multi_probe.cpp`、`app/src/media/CMakeLists.txt`、`app/src/ui/qml/pages/P4AllCameras.qml`、`app/src/ui/qml/components/CameraTile.qml`、`app/src/ui/src/main.cpp`、`app/tools/board_build.py`

## 1. 交付内容

1. **`railhmi-core` 网格模型**：`choose_grid_shape`（宽>=高→3x2，否则 2x3）、`grid_columns/rows`、`grid_slots`（槽位 = `row=i/cols, col=i%cols`，**仅依赖形状与序号，与运行时状态无关**）、`grid_cell_rect`、`grid_rects_no_overlap`。
2. **`railhmi-core` 每格视图**：`build_channel_views(registry, runtime, now_ms)` 逐槽位输出：摄像头名、状态中文标签（`channel_state_label`）、实时 fps（`format_fps`）、最近一帧时间（`format_last_frame`，以单调毫秒差换算“N 秒前 / —”，闭合评审 D-02 口径）、重连次数、检测职责徽标、`presentable`/`placeholder`。**输出长度与顺序严格等于注册表顺序**——断流/冻结只改该格内容，不重排、不丢格。
3. **`railhmi-media` 多路并发管理**：`MultiStreamManager` 同时持有并推进 N 路 `CameraStream`（各自独立六态机），`start_all`/`pump_all`/`simulate_cut(index)`/`stats()`/`stop_all`；单路启动失败不影响其余路。
4. **`railhmi-multi-probe` CLI**：同时启动多路（默认 6）test/file/rtsp 源，逐轮输出每格 `state/fps/last_frame_ms/reconnect/presentable` 与网格汇总，可注入单路断流并程序化核对。
5. **QML 接入**：`main.cpp` 注入 `railhmiChannels`（每格视图，缺省未启动占位）与 `railhmiGrid`（3x2/2x3 形状）；`P4AllCameras.qml` 用 `Grid` 渲染固定槽位网格，随窗口宽高切换 3x2/2x3；`CameraTile.qml` 增加 fps / 最近帧 / 重连显示与非 LIVE 占位层。

## 2. 实测（板端 RK3588 本机；TestRun + MediaCheck）

### 2.1 核心单测（`board_build.py --mode TestRun`，`TESTRUN_EXIT=0`）

**411 passed / 0 failed**（含 ITEM-015 新增 12 组布局/网格/视图断言）。关键断言（原文）：
```
[PASS] landsacpe -> 3x2 / 1280x720 -> 3x2 / portrait -> 2x3 / 3x2 dims / 2x3 dims
[PASS] 6 slots / slots identical across calls (state-independent)
[PASS] 3x2 slot mapping (row = i/3, col = i%3) / 2x3 slot mapping (row = i/2, col = i%2)
[PASS] 3x2 cells do not overlap / 3x2 cells within area
[PASS] unstarted: still 6 cells (no cell dropped) / unstarted: order equals registry order
[PASS] cut: grid size unchanged / cut: slots do not reorder (grid does not jump)
[PASS] cut: channel 4 reconnecting / cut: channel 4 placeholder / cut: reconnect count text
[PASS] cut: frozen last-frame time shown / cut: the other five cells keep refreshing
```

### 2.2 六路并发取流（`board_build.py --mode MediaCheck`，`BUILD_EXIT=0` `MULTI_EXIT=0`）

`railhmi-multi-probe --count 6 --source test --duration 8 --cut-index 3 --cut-after 4` 摘要（原文）：
```
type=multi source=test channels=6 started=6 cut_index=3 cut_after_s=4
t=0    type=grid cells=6 live=6 presentable=6
...
t=3533 type=grid cells=6 live=6 presentable=6      # 断流前六路全部实时
t=4013 type=injection cut=FI-01 index=3
t=4013 type=cell idx=3 id=CAM-04 state=reconnecting fps=25.0 last_frame_ms=220110067 reconnect=1 presentable=false
t=4013 type=cell idx=0..2,4,5 state=live ... presentable=true
t=4533 type=cell idx=3 id=CAM-04 state=reconnecting fps=18.5 last_frame_ms=220110067 reconnect=1 presentable=false
t=4533 type=cell idx=0..2,4,5 state=live (last_frame_ms 继续推进) presentable=true
type=summary channels=6 final_live=5 final_presentable=5
type=acceptance all_presentable_before_cut=true cut_not_presentable=true others_still_presentable=true cut_last_frame_frozen=true result=PASS
```

判据核对（用户可观察点）：
- 六路**同时取流**：断流前 `cells=6 live=6 presentable=6`（每路独立 fps≈25）✅
- **断开一路**（索引 3，CAM-04）：该格 `reconnecting`、`presentable=false`、`reconnect=1`，其余五路仍 `live`/`presentable=true` 且 `last_frame_ms` 继续推进 ✅
- 该格**保留位置不重排**：网格始终 `cells=6`、`idx=3` 仍为 CAM-04 ✅
- 该格显示**最近帧时间**（`last_frame_ms=220110067` 冻结于断流前最后帧），不被当作实时 ✅

## 3. 边界

- **真机六路 RTSP（ENV-D）未执行**：相机读取入口对多数相机为 `configured_unverified`，`.162/.163/.165/.167` 凭据未核验；本轮以 **test 源**六路并发跑通并实测断流语义与不重排/不丢格。`--source file/rtsp` 入口已就绪，待授权/素材补测。
- **QML 运行期未执行**：板端缺 Qt5 Quick/Qml 开发包（E-01b）；本项以 **core 网格/视图单测（板端实跑）** + **media 六路并发探针（板端实跑）** + **QML 源码/静态扫描 PASS** 为证据。
- **UI 取流接线**：应用进程尚未把 `MultiStreamManager` 接入 QML 运行时（媒体层为 target 专用，注入运行时的接线属后续集成）；`main.cpp` 当前以“未启动”占位驱动 `railhmiChannels`，网格结构与字段消费已就绪。
- 六路**全模型推理**不在本项（归推理接入项）；本项只验证取流并发与显示网格。

## 4. 证据文件

- `build-TestRun-...-ITEM015-r1.transcript.json` / `.log`（构建 + core 单测 + 令牌自检 + UI 扫描 + sim-inject）
- `...-ITEM015-r1.core-evidence_core-tests-...-r1.log` / `.xml`（411 passed / 0 failed）
- `build-MediaCheck-...-ITEM015-media-r1.transcript.json` / `.log`（media 构建 + 单路探针 + 六路探针 + 取回）
- `...-ITEM015-media-r1.core-evidence_multi-probe-...-r1.log` / `.json`（六路逐格状态原文 + 验收判定 PASS）
- `...-ITEM015-media-r1.core-evidence_stream-probe-...-r1.log` / `.json`（单路回归）

## 5. 复现

```sh
# 板端
bash app/build.sh CoreTests && ./build-core/tests/railhmi-core-tests
bash app/build.sh MediaCheck && ./build-media/tools/railhmi-multi-probe \
  --count 6 --source test --duration 8 --cut-index 3 --cut-after 4 --out m.log --json m.json
# 经登记 SSH 入口（core 单测）
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode TestRun \
  --outdir docs/hmi/test_evidence/ITEM-015-grid --stamp <tag>
# 经登记 SSH 入口（六路并发探针）
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode MediaCheck \
  --outdir docs/hmi/test_evidence/ITEM-015-grid --stamp <tag>
```
