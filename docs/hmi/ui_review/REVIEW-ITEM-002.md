# UI 实现评审证据 — ITEM-002（渲染路径选型 spike 交付）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `ca5373c`，2026-10-15 21:40:36 +0800）
  - 声明产物（DesktopAppImplementation）: `app/spikes/render_path/README.md`
  - 主体交付: `docs/hmi/build_env_check.md` §14、`docs/hmi/render_path_spike.md`、`app/spikes/render_path/{main.cpp,spike_core.c,analyze.py,run_render_spike.sh,...,CMakeLists.txt,*.qml}`
  - 板端运行器: `app/tools/board_render_spike.py`
- 被评工作项: `ITEM-002`（完成渲染路径选型 spike：板端真机 RTSP/UDP 或 file/test 替代源实测双路径，记录 fps/CPU/GPU/色彩；selection 写回 `build_env_check.md` §14；未实测保持 UNMEASURED、默认 cpu、gl 候选、源码双路径开关存在）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2（`ef47841`）、`docs/hmi/VISUAL_DESIGN.md` v2 + `docs/hmi/design_tokens.yaml` v2（`40ccf4e`）、`docs/hmi/DESKTOP_APP_DESIGN.md` v2 §4.3/§6（`e691118`）
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（commit `3c3fa4f`；`docs/hmi/test_evidence/ITEM-002-render-path/VERIFY-ITEM-002.md`）
- 评审日期/环境: 2026-10-15；Windows 主机（MINGW64 x86_64），Python 3.13.9，paramiko 5.0.0；**本轮未连接 RK3588 目标板**（本 capability 授权方法为只读源码/证据审阅；板端事实采用上游软件验证的独立板端复跑，见 §0.3）
- 判定: **TEST_PASS（范围严格限定为 ITEM-002 交付物；不构成任何 UI 视觉/交互或真实画面色彩还原结论）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-002**（渲染路径选型 spike）。判定对象是工作项声明的用户可观察点：板端替代源实测 cpu（RGA NV12→BGR）核心管线并记录 fps/CPU/GPU/色彩；`gl` 不可用为实测探究而非预判；selection 写回 §14；源码存在双路径开关；未实测处保持 UNMEASURED。
2. **本次未对任何界面做视觉/交互还原评审**：本项交付的是渲染管线 spike 与运行器，**不是 UI**；`app/README.md` 自述“尚未交付完整应用”，无可运行界面、无交互记录。因此本文不对布局、组件、字体、焦点、空/错/恢复状态做任何 PASS 结论（§5 D-05）。
3. **本轮未重新连接目标板**：板端测量（r1/r2b/r3）与软件验证的独立板端复跑（`…-verify-20261015-v1/v2`）已由上游完成并归档；本 capability 的授权方法为 `read/ui_evidence/design_assets`。为避免重复连接与在目标 `/tmp` 产生新残留，本轮以**主机独立复现（判据自检 + 用归档 JSON 重算选择）+ 归档证据/源码审阅**为独立证据，并明确标注引用关系，不冒充自跑。
4. 待处理的依赖缺口（Qt QML 开发/控件包缺失、`qmlglsink` 缺失、真机 RTSP 未测、六路并发未测）按职责交平台/rootfs、现场与后续工作项；本评审不判定其“不可修复”，不返回 BLOCKED。
5. 本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `ca5373c`（2026-10-15 21:40） | 新增 `spike_core.c`（无 Qt 核心管线测量）、`board_render_spike.py`（板端运行器）、板端 r1/r2b/r3 证据全量归档；回填 `build_env_check.md` §14；`render_path_spike.md`/`README.md` 状态更新；无范围外改动 |
| 交互设计 | `ef47841` | v2；本项只消费“渲染性能与状态呈现前提”，不核验界面 |
| 视觉设计 / 令牌 | `40ccf4e` | v2；本项不核验界面还原 |
| 桌面应用设计 | `e691118` | §4.3 主/回退路径决策门、§6 渲染管线为本项判据来源 |
| 软件验证 | `3c3fa4f` | ITER2 TEST_PASS；板端独立复跑 + selection 独立重算成立 |
| 历史 | `5494dde`→`eb459ec` | ITER1 曾 TEST_FAIL（板端授权缺口）；本轮板端授权解锁后实测 |

实现自述与仓库证据一致：状态段、§5、§14 均标 `SELECTED cpu`、`gl` 不可用保留候选；**未见把未实测的 `gl` 写成默认主路径，未见回填伪造数据**。

---

## 2. 评审方法

1. 源码审阅：`spike_core.c`（采集/色彩判据）、`main.cpp`（`--path gl|cpu` 与 QML 场景、色彩判据）、`analyze.py`（可用判据与 selection）、`board_render_spike.py`（连接/超时/known_hosts/清理）、`run_render_spike.sh`。
2. 文档审阅：`docs/hmi/build_env_check.md` §14.0–14.5、`docs/hmi/render_path_spike.md`、`app/spikes/render_path/README.md`。
3. 归档证据核对：板端 r1/r2b/r3 core JSON 与 selection JSON、软件验证复跑件。
4. 独立复现（本评审实机执行）：`analyze.py --selftest`；用归档 core JSON 独立重算 selection（r3 与 r1）。
5. 判据逐条对账。

**方法限制**：无板端连接、无图形会话、无 UI 产物 → 不能独立确认板端测量、不能验证渲染/交互行为。板端现状采用上游软件验证 ITER2 的独立复跑证据。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | `python app/spikes/render_path/analyze.py --selftest` | **6/6 通过**（gl 优先 / gl 失败回退 cpu / 双失败 / 色彩否决 / 帧率否决 / 无数据 UNMEASURED），与上游验证件一致 |
| R2 | `analyze.py --cpu <r3 core JSON> --out --md-out` | 独立重算得 `status=SELECTED, selected=cpu`，与归档 `selection-…-board-r3.json` 一致；cpu `color_spread=0.0`、fps_source=1449.7、fps_display=325.44 |
| R3 | `analyze.py --cpu <r1 core JSON>`（r1=**SMPTE 彩色源**） | 独立重算得 `status=NONE_USABLE`，理由 `color check failed` + `color spread 15.84 > 12.0`（playing=true、fps≈25 正常）→ **证实彩色内容无法通过该色彩判据** |
| R4 | 源码核对 `main.cpp` | `--path gl|cpu` 开关存在（`main.cpp:319,344`；`GlView.qml`/`CpuView.qml` 齐备）；`check_color_bgr`（`:107–125`）与 `spike_core.c:check_color` 同判据（三通道均值 + spread<12 + g>2） |
| R5 | 源码核对 `board_render_spike.py` | `load_system_host_keys()` + 默认 RejectPolicy（无 AutoAddPolicy）；连接/命令/SFTP 全超时；凭据仅取环境变量；**含 `cleanup`（`rm -rf $remote`，非 `--keep-remote` 时执行）**——未在本项目重演 ITEM-001 的“无清理步”问题 |
| R6 | 板端证据比对 | r2b（test 中性灰）B/G/R=126/126/126、spread=0、pass；r3（file 解码）同值；软验复跑 transcript 指纹与实现者一致（同一目标板） |

---

## 4. 判据逐条核对（ITEM-002 用户可观察点）

| 判据（工作项原文） | 实际观察 | 判定 |
|---|---|---|
| 板端以 RTSP/UDP 或 file/test 替代源实测双路径 | `test` 与 `file`（mppvideodec 解码）两源实测记录（r2b/r3）；**真机 RTSP 未测**（授权/凭据归后续），已如实标注 | 满足（替代源口径） |
| 记录 fps/CPU/GPU/色彩（BGR 三通道均值近似相等） | §14.1 两行数值与归档 JSON 逐项一致；BMP 快照归档 | 满足（色彩判据本身的局限见 §5 D-01） |
| selection 写回 `build_env_check.md` §14 | §14.0–14.5 含 `SELECTED cpu`、判据依据、gl 不可用实测证据、方法与限制 | 满足 |
| 未实测不预设、gl 保持候选、默认 cpu | `gl` 全程 “no measurement”，未写入伪造数值；`gl` 不可用有构造探测 + cmake 失败证据 | 满足 |
| 双路径配置开关在源码中存在 | `main.cpp --path gl|cpu` + 两 QML 场景；`render_path_spike.md` §7 明确产品侧配置开关约束 | 满足（产品级开关归后续，见 §5 D-04） |
| （未取得板端授权时）维持 UNMEASURED | 本轮已获授权并实测，故不再要求 UNMEASURED；`analyze.py` 无输入仍返回 UNMEASURED | 满足（条件已满足） |

---

## 5. 具体偏差与修复要求

**D-01（UI 面向的正确性证据缺口）— 色彩判据对“通道顺序”无鉴别力，当前色彩 pass 不能证明 BGR 正确**
- 观察：`spike_core.c:check_color` 与 `main.cpp:check_color_bgr` 只比较**整帧三通道均值**（`spread<12` 且 `g>2`）。通道置换（B↔R）不改变三通道均值的集合，故 `spread` 与 `pass` 完全不变——**该判据在数学上对通道顺序完全不敏感**。
- 证据：r1（SMPTE 彩色源）B/G/R=138.45/122.61/124.01、spread=15.84、`pass=false`（R3 复算 `NONE_USABLE`）；r2b/r3（中性灰 0x808080）三通道=126/126/126、spread=0、`pass=true`。即“pass”只在近似单色帧成立，且快照 `core_*.bmp` 像素为均匀 126（人眼也无法据此判断通道顺序）。
- 影响（UI 可见）：视频/图像的 BGR↔RGB 通道颠倒会把肤色、报警红、状态色整体错色，是典型用户可见缺陷；而当前全部自动化“色彩通过”证据（含供人工目视的快照）均为中性灰，**对通道顺序零覆盖**。§14.4.2 已声明“只对中性灰场景成立”，但未点明“无法检测通道颠倒”，下游易把 `pass=true` 误读为“色彩正确”。
- 复现路径：
  - `python app/spikes/render_path/analyze.py --cpu docs/hmi/test_evidence/ITEM-002-render-path/spike-192_168_0_103-20261015-board-r1.core_20261015-board-r1.json` → `NONE_USABLE`（color check failed），而 playing/fps 均正常；
  - 阅读 `app/spikes/render_path/spike_core.c` 的 `check_color` 与 `main.cpp` 的 `check_color_bgr`。
- 期望（交 `desktop_app_implementation`，本轮不代改实现）：
  1. 增加**确定性的通道顺序校验**：用已知分区颜色的源（如固定四象限红/绿/蓝的合成 NV12 帧，或 `videotestsrc pattern=...` 的已知色区），对每个区域断言“期望通道的最大值出现在正确通道”（如红区 `r>g` 且 `r>b`），而不是只比全帧均值；
  2. 保留中性帧检查用于“绿帧/全黑/全白/单通道塌陷”等粗差；
  3. 在 §14 与 `render_path_spike.md` 明确标注：现有色彩证据仅覆盖单色帧、**不覆盖通道顺序**；在 ENV-D 真机彩色帧 + 用户目视确认前，不得以 `color.pass` 宣称色彩还原正确；
  4. 快照建议改存**彩色**帧（真机或 SMPTE）供用户目视，否则目视也无法发现通道颠倒。

**D-02（证据完整性，低成本）— 突发运行的 GPU 样本数未标注**
- 观察：r3 `gpu_samples=1`（0.17s 突发，仅 1 个 500ms 采样），而 §14.1 表列出 `8.0 / 8.0` 未注明 n=1；r2b 为 16 样本。
- 期望：在 §14.1 / `render_path_spike.md` §5 标注每行 GPU 采样次数（或对 n<3 标注“样本不足，仅供参考”），避免把单点值当稳定 GPU 负载。

**D-03（选择口径一致性）— `SELECTED cpu` 的适用范围应显式标为“核心管线已定，整 UI 渲染代价待复测”**
- 观察：cpu 的实测是**无 Qt 核心管线**（`spike_core.c`：解码/RGA 转换），不含 `QImage` 拷贝、纹理上传与 `QQuickPaintedItem` 绘制代价；而 §14 标题/§14.2、`render_path_spike.md` §5、`README.md` 状态段均以“选定路径 `cpu`（SELECTED，主路径）”呈现。§14.4.1 记了限制，但状态口径读起来像最终冻结。
- 影响：`cpu` 路径在 6 路 720p 下的实际 UI 出图代价（每帧 BGR→QImage 拷贝 + 纹理上传 + 绘制）可能成为瓶颈；把它当作已冻结的主路径会影响后续页面性能预期。
- 期望：把选择状态统一表述为“**核心管线已实测可用并暂定主路径 `cpu`；QML 全路径与六路并发待 Qt 包就绪后复测再冻结**”，与 `render_path_spike.md` §7 的“待复测再冻结”一致；`gl` 保持可开启候选。

**D-04（交接，非缺陷）— 产品级双路径配置开关尚未存在**
- 观察：双路径开关由 spike CLI `--path gl|cpu` 提供；产品级 `render.path=gl|cpu` 配置属 ITEM-003+，当前无产品代码。
- 期望：在 ITEM-003/渲染实现任务中落实由配置驱动的 `render.path`（默认 `cpu`，`gl` 可开启），并保留两条路径；属后续工作项，不在本 spike 判为缺陷。

**D-05（范围缺口，非本项缺陷）— 无可评审的 UI**
- 观察：无 UI 源码、无可运行界面、无交互记录。
- 期望：ITEM-003 起产出可运行 UI 后，UI 实现评审按 `INTERACTION_DESIGN` §4–§11 与 `VISUAL_DESIGN` §7–§12 逐状态核对，并要求运行录屏/交互记录；静态图不足以证明交互行为。

### 已确认无问题/正向项（供后续复用）

- `board_render_spike.py` 含 `cleanup` 步骤且 `--keep-remote` 可控，未重复 ITEM-001 的清理声明问题。
- `gl` 不可用是**实测探究**（`gst-launch` 构造探测 `no element "qmlglsink"` + `cmake` 报 `Qt5QuickConfig.cmake` 缺失），非预判。
- 判据 `analyze.py` 的无数据 `UNMEASURED`、6/6 自检、`--md-out` 回填路径均成立。

---

## 6. 未覆盖 / 未执行（不得外推）

1. 目标板任何命令本轮**未执行**；板端现状采用上游软件验证 ITER2 复跑证据。
2. 任何界面视觉/交互还原（无 UI 产物）。
3. 真机 RTSP/UDP 取流（`read_camera_stream` 与相机凭据按本轮调用未执行）— 归后续取流工作项 ENV-D。
4. Qt 全路径（QML 出图）与六路并发合成性能 — 待 Qt 包就绪与页面任务。
5. 真实彩色画面的色彩还原与通道顺序 — 见 D-01，须补测并由用户目视。
6. 快照画面内容的人工目视确认 — 归用户（数值不代替人工确认）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-002 交付物）。
- 依据：工作项用户可观察点——板端替代源（test/file）实测 cpu 核心管线并记录 fps/CPU/GPU/色彩、`gl` 不可用为实测探究、selection（SELECTED cpu）写回 `build_env_check.md` §14、源码双路径开关存在、未实测处无伪造——经源码审阅、归档证据核对与主机独立复现（判据自检 6/6 + 用归档 JSON 重算选择）逐条成立。
- 明确边界：本 PASS **不表示** Qt 全路径渲染可用或性能达标，**不表示**真实画面色彩/通道顺序正确（D-01），**不表示**六路并发可行，**不表示**任何 UI 视觉/交互通过。
- 后续修复要求见 §5：D-01（通道顺序校验）为 UI 正确性关键项，D-02/D-03 为文档口径与证据补齐，D-04 为后续实现交接，D-05 待 ITEM-003 产生可运行 UI 后再评审。
