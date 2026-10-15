# railhmi 渲染路径选型 spike (ITEM-002)

在 RK3588 目标板上用一路 RTSP 实测两条候选渲染路径，按固定判据选定主/回退路径。
**本 spike 的目标是产生实测数据，因此必须在目标板上运行；未实测前不假定任何路径成立。**

## 两条路径

| path | 管线 | 角色 |
|---|---|---|
| `gl` | `rtspsrc(UDP) ! decodebin ! NV12 ! tee` → `glupload ! glcolorconvert ! qmlglsink`（QML `GstGLVideoItem`） | 主路径候选（GL 合成/近零拷贝） |
| `cpu` | 同上 → `appsink(NV12)` → librga `NV12→BGR`(BT601_FULL) → `QImage` → `QQuickPaintedItem` | 回退路径候选 |

输入源 `--source rtsp|file|test`：`rtsp`=真机（ENV-D）；`file`=预录文件（ENV-C 替代源）；`test`=合成源（只验协商）。

两条路径共用计数分支 `fakesink + pad probe`（源帧率）与 `/proc`、`/sys/class/devfreq/*gpu/load` 采样；
色彩正确性用 librga 转换后的 BGR 三通道均值判据（`README.md` §2.3），并保存一帧快照供人工目视（由用户执行）。

## 文件

```
app/spikes/render_path/
  main.cpp            # spike 主程序（路径 gl|cpu，测量 fps/CPU/GPU/色彩，写 JSON）
  GlView.qml          # GL 路径 QML（GstGLVideoItem）
  CpuView.qml         # CPU 回退路径 QML（FrameItem）
  qml.qrc             # QML 资源
  CMakeLists.txt      # 板端本机构建（Qt5 + GStreamer + librga）
  analyze.py          # 选择判据（含 --selftest；无实测输入返回 UNMEASURED）
  run_render_spike.sh # 驱动: 构建 + 跑两条路径 + 选择 + 输出
  spike_core.c        # 无 Qt 核心管线测量（GStreamer + librga；Qt 开发包缺失时仍可测解码+RGA）
```

## 运行（目标板）

```sh
# 需要项目授权的相机流；凭据来自项目配置，勿写死在仓库
./run_render_spike.sh --url 'rtsp://<user>:<pass>@<cam>/...' --duration 20 --snapshot
# 无相机时用 ENV-C 替代源（预录文件 / 合成）
./run_render_spike.sh --source file --file /path/to/clip.mp4 --duration 20 --snapshot
./run_render_spike.sh --source test --duration 10
# 产物: docs/hmi/test_evidence/ITEM-002-render-path/{gl,cpu,selection}-<stamp>.json (+ .png)
#       selection-<stamp>.md 可直接贴入 docs/hmi/build_env_check.md §14 实测表
```

无板时只校验入口（不构建、不访问设备）：

```sh
./run_render_spike.sh --dry-run
python3 analyze.py --selftest
```

## 判据（analyze.py）

某路径可用 = `playing` 且 `fps_source >= --min-fps` 且 `fps_display >= --min-fps`
且色彩校验通过且无 fatal error。选择顺序 `gl` → `cpu` → `none`；无实测数据为 `UNMEASURED`。
判据与阈值见 `docs/hmi/render_path_spike.md`。

## 状态

**已在目标板（RK3588）实测 → 选定路径 `cpu`**（status=SELECTED）。板端缺 `qtdeclarative5-dev`
（`Qt5QuickConfig.cmake`）与 `gstreamer1.0-qt5`（`qmlglsink`），故原 QML spike 本轮无法构建；
改用 `spike_core.c`（无 Qt 核心管线测量，仅 GStreamer + librga）取得解码+RGA 的真实
fps/CPU/GPU/色彩数据；`gl` 路径经 `gst-launch` 构造探测确证 `no element "qmlglsink"`，保留为可开启候选。
详细数据、方法与证据见 `docs/hmi/build_env_check.md` §14 与 `docs/hmi/render_path_spike.md` §5。
盘端运行器：`app/tools/board_render_spike.py`（经登记 SSH 入口构建/测量）。
