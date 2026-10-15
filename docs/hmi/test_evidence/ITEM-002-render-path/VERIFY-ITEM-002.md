# 软件验证 TestEvidence — ITEM-002 渲染路径选型 spike 交付（software_verification）

- Capability: `software_tester.software_verification`
- 被检对象: `n005_embedded_linux_engineer.desktop_app_implementation.implementation`
  （artifact_implementation.yaml → `app/spikes/render_path/README.md`；对应仓库 commit `ca5373c` 之工作树）
- 历史尝试对比: 本验证 ITER1（commit `5494dde`，TEST_FAIL，根因=板端授权缺口，证据见 §0）→
  commit `06b6f9f`（板端授权解锁，ENV-B 探针实测）→ commit `ca5373c`（本轮被检对象：板端 spike 实测 + §14 回填）
- 被检工作项: `TL-…-tasks-ITEM-002`（完成渲染路径选型 spike：板端真机 RTSP/UDP 或 file/test 替代源实测双路径，
  fps/CPU/GPU/色彩记录，selection 写回 build_env_check.md §14；未实测保持 UNMEASURED、默认 cpu、gl 候选、源码双路径开关存在）
- 验证日期: 2026-10-15（验证人本机 Windows 10 / MINGW64 执行，paramiko 5.0.0，凭据经登记环境变量进程内读取）
- 结论: **TEST_PASS**（范围与限制见 §0；逐条核对见 §3；限制与方法依赖见 §4）
- 更正记录: 本文件 ITER1 曾交付 TEST_FAIL（当时 `build_env_check.md` §14 为空占位）。本轮按 ITER1 §5 交接命令
  （`board_render_spike.py`，替代源 test/file）独立复跑并核对回填，所有 ITER1 未满足点均已消除（§3）。ITER1 结论到此为止，
  **不构成对本轮的预判**——本轮每一行判定均以 §1–§2 的实际执行与新观察为准。

---

## 0. 结论口径（防止跨层外推）

1. **TEST_PASS 判定对象**：工作项声明的用户可观察点——
   a) 板端（真实 RK3588，经登记 SSH 入口）以替代源实测 "RGA NV12→BGR" 路径并记录 fps/CPU/GPU/色彩；
   b) `gl` 路径元素的不可用经实测探究（构造探测 + 开发包缺失证据），非预判；
   c) selection 结果（SELECTED cpu）已写回 `build_env_check.md` §14（§14.0–14.5）；
   d) 双路径配置开关在源码中存在（`main.cpp --path gl|cpu`，§2 DC-4）；
   e) 未实测处（`gl` 无测量、真机 RTSP、六路并发、Qt 出图代价）保持 UNMEASURED/标记，不预设（§4）。
2. **不跨越的边界**（本验证未判定、不得由本文件外推）：
   - 画面内容/视觉正确性由用户目视确认；数值判据只覆盖中性灰色彩与帧率/负载。
   - `cpu` 选定的测量是**无 Qt 核心管线**（`spike_core.c`：解码/源 → NV12 → appsink → librga → BGR 判据），
     **不含 Qt 纹理上传/QQuickPaintedItem 出图代价**；交付文档（§14.4.1、`render_path_spike.md` §5）已如实标注，
     Qt 就绪后需由原 QML spike 复测——该后续归实现/后续工作项，不属于本项验收。
   - 单路测量不覆盖六路并发（TEST_DESIGN ENV-C/D 范围）。
   - "板端可复现"结论仅对 **RK3588（192.168.0.103，Ubuntu 22.04，GStreamer 1.20）** 成立。
3. 实机（RTSP）与替代源（test/file）证据区分记录：SAT（真机）未执行，SUT 替代源两项独立复现——与 ITER1 §0.1 口径一致。

## 1. 独立板端复现（本轮实际执行，2026-10-15 21:47–21:48）

使用与实现者相同的登记入口（`access.rk3588_inspect` → `board_render_spike.py`，受信任 known_hosts、全超时），
独立传参复跑两组测量（`--only run --keep-remote`）：

| =编号 | 动作/参数 | 实际结果 | 归档 |
|---|---|---|---|
| SUT-1 | `--source test --duration 8 --stamp verify-20261015-v1`（核心管线，test 中性源 0x808080） | fps_source=24.98, fps_display=24.98, cpu 58.71%, GPU avg 6.94（16 样本）, BGR 均值 126.00/126.00/126.00, spread 0.00 **pass**；`playing=true`；errors=[] | `spike-…-verify-20261015-v1.core_….json`（JSON 根 ↓） |
| SUT-2 | `--source file --duration 8 --gen-clip --stamp verify-20261015-v2`（板上 mppvideodec 解码 + RGA） | 板上生成 clip.mp4（14KB, H.264）；**突发** fps_source=1484.85, fps_display=303.03, cpu 96.97%, spread 0 **pass** | `spike-…-verify-20261015-v2.core_….json` |
| SUT-3 | `gl` 构造探测（两次运行各一次） | 两次均复现 `WARNING: erroneous pipeline: no element "qmlglsink"` | `…-v1.gl_construct.log`、`…-v2.gl_construct.log` |
| SUT-4 | 快照二进制独立解析（v1 BMP） | 1280×720 24bpp；画面为单一像素值 (126,126,126)——与 JSON BGR 均值逐值一致 | `…-v1.core_verify-...bmp`（解析命令与结果见验证 transcript） |
| RET-1 | `analyze.py --selftest`（本机） | **6/6 通过**（gl 优先/gl 失败回退 cpu/双失败/色彩否决/帧率否决/无数据 UNMEASURED） | `verify-selftest-20261015-r2.txt` |
| RET-2 | `analyze.py --cpu <r2b JSON>` 独立重算选择 | SELECTED cpu、gl=no measurement（与归档 selection r2b 一致） | `selection-…-verify-20261015-v1.{json,md}` |
| RET-3 | `analyze.py --cpu <v1 JSON>` 以复现数据重算选择 | SELECTED cpu（用验证人自己的测量同样得出选定 cpu） | `selection-…-verify-20261015-v1.json` |
| CLN-1 | 板端 /tmp 工件清理（v1/v2 两轮新产物 + spike_core 二进制 + build 目录） | `ls /tmp/railhmi_render_spike` 仅剩上传源码，无遗留产物 | `verify-tmp-cleanup-20261015-r2.log` |

- SSH 主机指纹（全部 6 份 transcript）一致：`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`，
  与实现者 r1/r2b/r3 及 ENV-B r2 相同——确认是同一目标板，不是别的盒。
- 复现偏差（均为预期内随机量，不改变结论）：CPU 58.2→58.7% (test)；突发 fps 1449→1484、303/325 fps（file，无时钟同步源）、
  GPU avg 6.75→6.94。结构性量（frames=200@8s→25fps 实时、spread=0、qmlglsink 缺失、decoding+RGA 链路可用）**全部重现**。

## 2. 核对当前交付（不只看包装文件）

- DC-1 `docs/hmi/build_env_check.md` §14.0–14.5：状态=SELECTED cpu；实测表逐值与归档 JSON **一致**
  （r2b 行 = `…-r2b.core_….json`；r3 行 = `…-r3.core_….json`）；14.3 gl 不可用列的是实测证据（构造探测/Qt 包缺失）非推断。
- DC-2 `docs/hmi/render_path_spike.md` → 状态行、§5 实测表、§7 双路径保留约束与 §14 一致；变更记录含本轮事实——**未见 UNMEASURED 残留**。
- DC-3 `app/spikes/render_path/README.md` 状态段与 §14 一致；`spike_core.c`（判据函数 `check_color`、BGR spread 阈值 12、
  JSON 字段名与归档数据一一对应）与 README §判据声明一致。
- DC-4 **双路径开关存在性（独立复核源码）**：`main.cpp:319, 344, 354` `--path gl|cpu`（默认 gl，仅 spike 入口首测优先，
  非产品配置）；`CpuView.qml`/`GlView.qml` QML 场景齐备；产品侧语义（默认 cpu、gl 待实测冻结）由 `docs/hmi/render_path_spike.md` §7 与 §14.2 说明一致。
- DC-5 ITER1 判据逐条复核 → §3 全部翻绿；未回填伪造数据——被检文档只写实测数值，且把"不含 Qt 出图代价/单路/色彩判据局限"
  三条限制显式写入 §14.4。

## 3. 判据逐条核对（对照 ITER1 TEST_FAIL 未满足点）

| 工作项/ITER1 判据 | 本轮实际观察（§1 证据） | 判定 |
|---|---|---|
| 板端以 RTSP/UDP 或 file/test 替代源实测 RGA 路径 | SUT-1（test）、SUT-2（file，mppvideodec 解码）两源实测成功；**真机 RTSP 未测**（授权/凭据归 ITEM-004 观察），不据此外推 | **满足**（替代源口径；真机缺口如实标注） |
| 探测失败时实测回退路径 NV12→BGR + Qt 纹理上传 | 核心管线（appsink→librga→BGR）实测通过；**Qt 纹理上传未包含**（板端缺 Qt QML 开发包，§14.3），已被文档如实记为限制 | **就 RGA 链路满足**（不含 Qt 代价的限制已在交付文档标注，非本轮隐藏） |
| 记录 fps/CPU/GPU/色彩 | §14.1 表两行数值逐项与归档 JSON 一致；BMP 快照独立解析与色彩均值一致 | **满足** |
| selection 结果写回 build_env_check.md §14 | §14.1–14.2 含 SELECTED cpu 与判据依据；recursive compare 通过 | **满足** |
| 未实测不预设、gl 保持候选 | `gl` 全程 "no measurement"，未写入任何伪造数值；文档显式"不得把未实测的 gl 当主路径" | **满足** |
| 双路径配置开关在源码中存在 | `main.cpp --path gl|cpu` + 两个 QML 场景（DC-4） | **满足** |
| (ITER1 附带) 判据自检 6/6、无输入 UNMEASURED | RET-1/2/3 复现通过 | 满足 |

## 4. 方法限制与本验证的缺口（不改变结论；交后续能力）

1. **Qt 出图代价未测**——`cpu` 选定基于核心管线。`gl` 元素与 Qt QML 开发包在板端恢复后，
   全 QML spike（`run_render_spike.sh`）复测两路径 → 交实现/平台（属于 ITEM-002 的"gl 候选复测"条款，非本轮验收缺口）。
2. **真机 RTSP/UDP 未执行**——read_camera_stream 授权与相机凭据按当前调用未在此运行；ENV-D 档归后续取流工作项（TEST_DESIGN TC-STREAM-08/09）。
3. **r3 突发吞吐口径**——filesrc 无时钟同步，1449/303 fps 只证明解码+RGA 链路能力与余量，不是实时指标（交付文档 §14.4.3 已标注）。
4. **TEST_DESIGN §0.1/§1 有引用"ITEM-002 UNMEASURED"的句子**（行 31/57 等）现已过时——按能力边界不代改；
   建议测试设计师在下一轮按 §14 当前状态刷新（`gl` 已实测不可用、默认 cpu 已有板端证据支撑）。
5. 验证人未改动任何被检实现文件；本轮新增文件仅 §6 列出的验证产物。

## 6. 本验证生成的新文件（全部在本目录，未改产品源码）

- `VERIFY-ITEM-002.md`（本文件，ITER2 重写）
- `spike-192_168_0_103-verify-20261015-v1.*`（test 源复现：transcript/core.log/json/bmp/cmake.log/gl_construct.log）
- `spike-192_168_0_103-verify-20261015-v2.*`（file 源复现：transcript/core.log/json/bmp/cmake.log/gl_construct.log）
- `selection-spike-192_168_0_103-verify-20261015-v1.{json,md}`、`…-v2.{json,md}`（判据独立重算结果）
- `verify-selftest-20261015-r2.txt`（analyze.py --selftest 6/6）
- `verify-tmp-cleanup-20261015-r2.log`（板端 /tmp 工件清理 + 终态列表）

## 7. 结论

**TEST_PASS** — 工作项 ITEM-002（本轮 commit `ca5373c` 交付）在已登记授权与真实板上：
替代源实测 RGA 核心路径（fps/CPU/GPU/色彩全项），`gl` 不可用为实测结论、保持候选；
selection=cpu 已写回 `docs/hmi/build_env_check.md` §14 并与归档证据一致；
双路径开关在源码中存在；未实测处全部如实标注、无伪造。
ITER1 之 TEST_FAIL（板端授权缺口）由 Runtime 解锁 + 实现方板端实测整改后，本轮以独立复现消除全部未满足点。
限制（Qt 出图代价、真机 RTSP、六路并发、BMP 内容目视）已按 §0/§4 区分记录。
