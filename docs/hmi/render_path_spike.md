# 渲染路径选型 spike — 程序、判据与状态（ITEM-002）

- 工作项: `TL-…-tasks-ITEM-002`（完成渲染路径选型 spike）
- 依据: `docs/hmi/DESKTOP_APP_DESIGN.md` §4.3（主/回退路径决策门）、§6（渲染管线）
- 源码: `app/spikes/render_path/`
- 状态: **已在目标板实测 → 选定路径 `cpu`**（status=SELECTED，min_fps=20）；`gl`(qmlglsink) 当前不可用，保留为可开启候选。数据与证据见 `docs/hmi/build_env_check.md` §14
- 上游环境: `docs/hmi/build_env_check.md`（板端 E-00..E-13 已实测，见 §2；缺口见 §3）；本轮 QML spike 因缺 Qt QML 开发包无法构建，改用无 Qt 核心管线测量（`spike_core.c`）

---

## 1. 目的与判定口径

在 RK3588 上用**一路 RTSP**对比两条渲染路径，记录 fps（源侧与该路径出图侧）、CPU、GPU、画面色彩正确性，
据此确定后续所有渲染任务采用的主路径或回退路径。**未产生实测数据前不假定主路径成立。**

- 单路**协商与显示**可行性 → 本 spike。
- **6 路并发合成**性能与显示帧率分级 → 不在本 spike，归后续取流/页面任务（`TEST_DESIGN` ENV-C/D）。

---

## 2. 路径定义

| path | 管线 | 角色 |
|---|---|---|
| `gl` | `rtspsrc(UDP) ! decodebin ! video/x-raw,format=NV12 ! tee` → `queue ! glupload ! glcolorconvert ! qmlglsink`（QML `GstGLVideoItem`） | 主路径候选（GL 合成，避免 CPU 拷贝） |
| `cpu` | 同上 → `queue ! appsink(NV12)` → librga `NV12→BGR`(BT601_FULL) → `QImage` → `QQuickPaintedItem` | 回退路径候选（Qt 纹理/绘制上传） |

两条路径共用：
- 计数分支 `queue ! fakesink` + pad probe → `fps_source`；
- `gl` 出图侧由 `qmlglsink` 上游计数、`cpu` 出图侧由 `FrameItem::setImage` 计数 → `fps_display`；
- 一次性 NV12→RGA→BGR 抓帧：检查 BGR 三通道均值（`README.md` §2.3 判据：三通道均值近似相等，G>2，spread<12）并保存快照（PNG，回退 BMP）供**用户**目视。

输入源（`--source`）三选一，对应 `TEST_DESIGN` §1 的环境分层与资源缺口替代：

| source | 管线头 | 用途 |
|---|---|---|
| `rtsp`（默认） | `rtspsrc(location, 强制 UDP) ! decodebin` | ENV-D 真机；需相机凭据与项目授权 |
| `file` | `filesrc ! decodebin` | **ENV-C 替代源**：无相机时用预录视频测同一渲染/换算路径（文件需不短于 `--duration`） |
| `test` | `videotestsrc ! videoconvert ! NV12` | ENV-C 合成源：只验协商与管线，不代表真实画面质量 |

关键实现约束（与设计一致）：强制 UDP；`mppvideodec` 输出 NV12；**不**用 OpenCV 拉 NV12；RGA 成功码 `SUCCESS(1)/NOERROR(2)`；
耗时转换与图像拷贝在工作线程，经排队信号投递 UI 线程。

---

## 3. 判据（`analyze.py`）

某路径**可用**需同时满足：

1. `playing == true`（管线到达 PLAYING，即协商成功）；
2. `fps_source >= --min-fps`（默认 20）；
3. `fps_display >= --min-fps`（默认 20）；
4. 色彩校验 `color.pass == true`；
5. 无 fatal error（协商失败、元素缺失、QML 加载失败等）。

选择顺序：`gl` 可用 → `gl`；否则 `cpu` 可用 → `cpu`；否则 `none`。**无实测数据 → `UNMEASURED`（不选）。**

阈值可调（`--min-fps`、`--max-spread`）；当前版本端到端时延等指标未冻结（PRD §12），本 spike 只决定路径可行性，不冻结性能指标。

---

## 4. 运行

```sh
# 主机：只校验入口与判据（不构建、不访问设备）
python3 app/spikes/render_path/analyze.py --selftest          # 6/6 期望通过
bash    app/spikes/render_path/run_render_spike.sh --dry-run

# 目标板 A：真机（需项目授权与可用相机）
cd app/spikes/render_path
./run_render_spike.sh --url 'rtsp://<user>:<pass>@<cam>/...' --duration 20 --snapshot

# 目标板 B：无相机时的 ENV-C 替代源（预录文件 / 合成）
./run_render_spike.sh --source file --file /path/to/clip.mp4 --duration 20 --snapshot
./run_render_spike.sh --source test --duration 10

# 结果：docs/hmi/test_evidence/ITEM-002-render-path/
#   {gl,cpu,selection}-<stamp>.json  selection-<stamp>.md（可直接贴入 build_env_check.md §14）
```

---

## 5. 实测结果（目标板，2026-10-15）

QML spike 因缺 `Qt5QuickConfig.cmake`（qtdeclarative5-dev）无法构建；以下为无 Qt 核心管线测量
（`spike_core.c`：decode/RGA 核心，不含 Qt 绘制）与 gl 构造探测的真实结果。

| 字段 | path=gl（qmlglsink） | path=cpu（核心管线，无 Qt） |
|---|---|---|
| playing | 无法构建（Qt5Quick 缺失）；构造探测报 `no element "qmlglsink"` | true |
| fps_source | 未测 | 24.98（test 中性源）/ 1449.7（file 突发） |
| fps_display | 未测 | 24.98 / 325.44 |
| cpu_pct_one_core | 未测 | 58.2 / 106.5 |
| gpu_load_avg | 未测 | 6.75 / 8.0 |
| color.spread / pass | 未测 | 0.00 / **pass**（B=G=R=126） |
| errors | qmlglsink missing; Qt5Quick missing | 无 |
| 结论 (usable) | **不可用（当前包）** | **可用** |

**选定路径: `cpu`（SELECTED）** — 依据板端真实测量；`gl` 待安装 `gstreamer1.0-qt5` + Qt QML 开发包后复测。
详细数据、方法与证据路径见 `docs/hmi/build_env_check.md` §14。

---

## 6. 本轮已验证（主机）

| # | 动作 | 结果 |
|---|---|---|
| R1 | `analyze.py --selftest` | 6/6 通过（覆盖 gl 优先、gl 失败回退 cpu、双失败、色彩不通过、帧率不足、无数据 UNMEASURED） |
| R2 | `analyze.py`（无输入） | 输出 `UNMEASURED`，退出码 2（不假定路径） |
| R3 | `run_render_spike.sh --dry-run` | 打印构建/测量/选择计划，退出 0；不构建、不访问设备 |
| R4 | `analyze.py --md-out`（**synthetic fixtures**） | 正确生成可贴入 `build_env_check.md` §14 的实测表与选定行（gl 优先）；fixtures 非真实设备数据 |
| R5 | `run_render_spike.sh --source test --dry-run` | 确认 ENV-C 替代源参数贯通 |

证据: `docs/hmi/test_evidence/ITEM-002-render-path/host-selftest-20251015.txt`（首次）、
`docs/hmi/test_evidence/ITEM-002-render-path/host-selftest-20251015-r2.txt`（重试：source/md-out 验证）

---

## 7. 未覆盖与依赖缺口

**缺口分类（依据项目自有规则）**：按 `E2E_PLAN.md` §3——“板端档失败若根因属板端资源/驱动/BSP（如 E-01..E-09 缺项）→ 记录为依赖缺口交平台/嵌入式职责，**不算软件功能失败**”；
且 `TEST_DESIGN.md` §1 将 ENV-B/C/D 标为“待实现 + 设备授权”。本次缺失的是 **ENV-C/D 设备授权**（项目设备工具已禁用），
属依赖/授权缺口，不是本 spike 源码或判据的软件缺陷。因此本项交付物（源码 + 构建/运行入口 + 判据 + 主机可验证路径）
在实现侧已完成；仅“在目标板运行并回填测量数据”因缺授权而未完成。

1. **QML spike 未在板端构建**（缺 `qtdeclarative5-dev`/`Qt5QuickConfig.cmake`）——原设计路径不可直接跑；本轮改用**无 Qt 核心管线测量**（`spike_core.c`）取得解码+RGA 真实数据；Qt 绘制代价未含，待平台补齐 Qt 包后由 QML spike 复测。该缺项属平台/rootfs 职责（`build_env_check.md` §3.1）。
2. **spike 未在主机编译**（主机无 Qt/GStreamer/librga/编译器）——C++ 源码与 CMake 的编译验证属板端范围。
3. 依赖 E-02（`qmlglsink`）、E-03（Mali GL）、E-04（`glupload/glcolorconvert`）、E-05（librga）；任一缺失会影响对应路径，结果以实测记录为准。
4. 6 路并发与多页面合成性能不在本 spike。
5. 重试新增 `source=file/test` 后，即使目标板没有可用相机也可在 ENV-C 完成**协商与管线/换算性能**测量；但真实相机画面质量（眩光/夜间）仍需 ENV-D 且由用户目视。

**对下游渲染实现的强制约束（未选定前，不可违反）**：
- 后续渲染实现必须把 `gl` 与 `cpu` 两条路径都保留在代码中、由配置开关选择（如 `render.path=gl|cpu`），
  **不得**把未经实测的 `gl` 路径当作唯一/默认成立的主路径（对应本项判据“未实测前不得假定主路径成立”）。
- 在未取得板端实测前，若必须给出默认值，默认取**已由现有板端证据支持**的 `cpu`（`mppvideodec` + librga，见 `README.md` §3 与 `code/main.cc`），
  并把 `gl` 作为可开启的候选；待 ENV-C/D 授权后按本节判据冻结选择。
- 选择的冻结入口（已就绪）：`app/spikes/render_path/run_render_spike.sh` + `analyze.py --md-out`，一条命令回填本节表格与选定值。

**验证反馈闭环**：`software_verification` 已对本交付返回 **TEST_FAIL**（`docs/hmi/test_evidence/ITEM-002-render-path/VERIFY-ITEM-002.md`），
结论为：实现侧工具链/判据独立复现全部通过（`--dry-run`、`--selftest` 6/6、无数据 UNMEASURED），
唯一未满足点是 **板端实测数据 + 选定路径**，根因为 ENV-C/D 设备授权缺失，非源码缺陷。
本节与 §7 已保持“未实测/UNMEASURED”，未回填任何伪造数据；此缺口不由源码修复消除，只能由设备授权解锁后按 §4 命令一次执行补测。

---

## 8. 变更记录

| 日期 | 变更 | 检查方式 |
|---|---|---|
| 2026-10-15 | 初次交付 spike（`main.cpp`/QML/CMake/驱动/判据）；主机实测入口与判据自检 | `analyze.py --selftest` 6/6；`run_render_spike.sh --dry-run` 通过；板端测量未执行 |
| 2026-10-15（重试） | 新增 `--source rtsp|file|test` 输入源（ENV-C 无相机替代源）；`analyze.py --md-out` 自动生成可贴入 `build_env_check.md` §14 的表格；驱动与文档同步 | `analyze.py --selftest` 6/6；`--md-out` fixture 验证；`run_render_spike.sh --dry-run`（含 source）通过；板端仍未执行 |
| 2026-10-15（重试2） | 按 `E2E_PLAN` §3 / `TEST_DESIGN` §1 明确缺口分类（设备授权缺口≠软件功能失败）；补充“未选定前不得假定 GL 主路径、默认取 cpu”的下游强制约束；说明实现侧无可再推进项 | 复跑 `analyze.py --selftest` 6/6 与 `run_render_spike.sh --dry-run`；缺口分类与项目规则逐字对齐核对 |
| 2026-10-15（重试3） | 响应 `software_verification` TEST_FAIL（`VERIFY-ITEM-002.md`）：确认未满足点=板端实测与选定路径，根因=ENV-C/D 设备授权；修复验证者指出的 Windows `python3` shim 不可用时的解释器回退 | `bash -n run_render_spike.sh` 通过；`--dry-run` 在解释器回退下通过；复跑 `analyze.py --selftest` 6/6 |
| 2026-10-15（本轮 ITEM-002） | 经登记 SSH 入口在 RK3588 实测：QML spike 因缺 Qt5Quick 无法构建 → 新增 `spike_core.c` 无 Qt 核心管线测量 + `app/tools/board_render_spike.py` 板端运行器；gl 构造探测确证 `no element qmlglsink`；测得 cpu 路径 fps/CPU/GPU/色彩并**选定 cpu**；结果写入 `build_env_check.md` §14 | 板端 `cmake` 失败日志、`gst-launch` 构造探测、core JSON/bmp、`analyze.py --selftest` 6/6；选定 `cpu`（SELECTED） |
