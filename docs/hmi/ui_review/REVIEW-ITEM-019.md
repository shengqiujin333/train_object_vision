# UI 实现评审证据 — ITEM-019（本地报警保护片段与缺口记录）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `97aacc8`，2026-10-16 05:31:14 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/core/include/railhmi/core/clip.h`、`app/src/core/src/clip.cpp`、`app/src/core/tools/clip_probe.cpp`、`app/tests/core/test_clip.cpp`
  - 依赖复用: `apply_retention`/`add_gap`（ITEM-007）、`railhmi-platform::FileStore::write_atomic`
  - 构建证据: `docs/hmi/test_evidence/ITEM-019-clips/README.md`
- 被评工作项: `ITEM-019`（每路有界滚动缓冲；报警时固化前 30s/后 60s（可配置）为保护片段；先写临时文件再原子重命名；容量上限与保留策略来自配置并优先保留保护片段；任何录像中断写入缺口记录；用户可观察：触发报警后保护片段存在且时长符合配置，中断录像后事件与时间轴出现录像不可用标记而非静默）
- 参照设计: `docs/hmi/DESKTOP_APP_DESIGN.md` §10/§7（持久化 `recording_segments`/`recording_gaps`）、`docs/hmi/INTERACTION_DESIGN.md` v2 §5.2/§5.3/§10（PLY-002/PLY-008）、`docs/hmi/VISUAL_DESIGN.md` v2 §7.7/§3.4（时间轴缺口/录像不可用黄条）
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-019-clips/VERIFY-ITEM-019.md`）
- 关联前序评审: `REVIEW-ITEM-007.md`（保留策略/缺口模型/录像三态）、`REVIEW-ITEM-018.md`（P2 行内录像不可用标注）
- 评审日期/环境: 2026-10-16；Windows 主机（MINGW64 x86_64，无 C/C++/Qt/GStreamer）；**本轮未连接 RK3588 目标板**
- 判定: **TEST_PASS（范围严格限定为 ITEM-019 保护片段/缺口核心模型与文件落盘；不构成 P3 回放/时间轴的界面结论；无 QML 改动，见 §5 D-04）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-019**。判定对象是任务内嵌用户可观察点：保护片段存在且时长=配置 (pre+post)、原子写盘、配置容量/期限保留且保护优先、录像中断写缺口（不静默）。
2. **本项为 core/平台模型交付，无 QML 改动**：`git show --stat 97aacc8` 仅含 core 源码/测试/探针与 README、board_build；P3 回放页与时间轴缺口呈现尚未实现，故“时间轴出现录像不可用标记”的**界面半未落地**（§5 D-02/D-04）。
3. **保护片段是“逻辑片段 + JSON 清单”，不是可播放视频**：`ProtectedClip.path` 指向清单文件；真实视频编码/封包复用媒体层（媒体/视频职责），应用只固化保护标记与时间范围（§5 D-01）。
4. **本轮未重新连接目标板**：板端 537/537 与 clip-probe 由上游软件验证复跑。本 capability 授权方法为 `read/ui_evidence/design_assets`，主机无工具链；结论以源码/契约与归档证据为准。
5. 依赖缺口按职责交平台/媒体/集成；本评审不判定其“不可修复”，不返回 BLOCKED。本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `97aacc8`（2026-10-16 05:31） | 新增 `clip.{h,cpp}`/`clip_probe.cpp` + `test_clip.cpp`；复用 `apply_retention`/`add_gap`/`FileStore::write_atomic`；**未改动任何 QML**；r1/r2 留痕（r1 探针 config 行容量打印 0，r2 修正） |
| 交互设计 | `ef47841` | v2 §5.2 录像不可用标注、§5.3 回放时间轴/缺口、§10 异常恢复（PLY-002/PLY-008） |
| 视觉设计 | `40ccf4e` | §7.7 时间轴缺口斜纹、§3.4 级别编码、§8.3 P3 |
| 软件验证 | `d2df981` | TEST_PASS；537/537 + clip-probe PASS（时长/原子写盘/保护优先/缺口合并） |

实现自述与仓库证据一致：`ITEM-019-clips/README.md`、板端 clip-probe 原文相符；**明确声明视频编码归媒体层、段行入库未接线、探针写 /tmp**，未外推。

---

## 2. 评审方法

1. 源码审阅：`clip.{h,cpp}`（`RollingBuffer`/`ClipConfig`/`ProtectedClip`/`ClipRecorder`/`clip_manifest_json`/`estimate_clip_size`）、`clip_probe.cpp`、`test_clip.cpp`。
2. 契约对照：片段时长=pre+post（PLY-002）、原子写盘（DESKTOP §10）、保护优先保留（§10/DATA）、缺口不静默（PLY-008）、时间轴缺口呈现（§5.3/§7.7）。
3. 复用核对：`apply_retention` 的 Critical 优先、`add_gap` 合并语义、`FileStore::write_atomic` 原子性。
4. UI 影响核对：`main.qml` 页面路由与 P3 状态；`EventRow.record_available`/`RecordAvailability` 与缺口的关系。
5. 归档证据核对：537/537、clip-probe（duration=90000ms、wrote/exists、evicted continuous=1 critical=0、gap merged）。

**方法限制**：主机无 Qt/媒体链 → 不能运行回放界面或真实编码；不得据清单宣称可播放视频。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 有界滚动缓冲 | `push` 满丢最旧；`max_frames_per_channel = (pre+post)*fps + 25`（默认 30/60/25fps → 2275）；`drop_before(ts-(pre+post))` |
| R2 | 片段时间范围/时长 | `on_alarm` → `t0=trigger-30s`、`t1=trigger+60s`；`duration_ms()=90000`（探针原文一致） |
| R3 | finalize 完整性 | `now>=t1` 时 `state=Finalized`、`complete=true`、`frame_count=count_in(t0,t1)`；探针 `before=0 at=1`。**frame_count 依赖当次缓冲现场**：若 `now >> t1`（缓冲已滚过窗口尾部）才 finalize，帧数偏小（VERIFY-ITEM-019 O-1）——不影响 `complete` 语义，但 UI 若展示“帧数/秒数”需以固定页 [t0,t1] 或约定 finalize 时机为准（D-03 口径） |
| R4 | 原子写盘 | 复用 `FileStore::write_atomic`（先 `<path>.tmp.<pid>` 再 rename）；探针 `wrote=true/exists=true` 且回读一致 |
| R5 | 保留策略（保护优先） | `segments()` 中保护片段为 `Critical`；`apply_retention` → 普通先淘汰、保护仅仍超容量时最旧淘汰；探针 `evicted=1 continuous=1 critical=0`；淘汰后从 `clips_`/`normal_` 移除 |
| R6 | 缺口记录 | `on_recording_interrupt` → `add_gap`（重叠/相邻合并、reason 保留）；探针 `gap count=1 t0..t1 reason=recording_interrupted` |
| R7 | JSON 清单字段 | `clip_manifest_json` 输出 id/cam/level/trigger/t0/t1/duration/state/complete/frame_count；**未输出 `path`/`size_bytes`**（D-01） |
| R8 | UI 影响核对 | `main.qml pageSource("replay")` 仍为 `pages/PageView.qml`（占位）；无 P3 时间轴/缺口渲染代码（D-02/D-04） |

---

## 4. 判据逐条核对（ITEM-019 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 每路有界滚动缓冲 | `RollingBuffer`（R1） | **满足（模型）** |
| 报警固化前30s/后60s、时长=配置 | `on_alarm`/`duration_ms`（R2） | **满足（模型/清单）** |
| 先临时文件再原子重命名 | `FileStore::write_atomic`（R4） | **满足（探针实证）** |
| 容量/期限来自配置、保护优先保留 | `apply_retention` + `Critical`（R5） | **满足** |
| 中断写缺口记录（不静默） | `add_gap`（R6） | **满足（数据侧）** |
| 触发报警后“保护片段存在” | 存在**JSON 清单**，无视频文件（R3/R7/D-01） | **部分满足（清单/逻辑片段）** |
| 中断后事件与时间轴出现录像不可用标记 | 事件侧经 `RecordAvailability`（数据）；**时间轴 UI 未实现**（R8/D-02） | **部分满足（数据侧）** |

---

## 5. 具体偏差与修复要求

**D-01（“保护片段”为清单而非可播放视频；清单未含路径/大小）**
- 观察：`ProtectedClip.path`/`size_bytes` 存在，但探针把 `path` 置为 `/tmp/railhmi_clips/clip-1.json`——**落盘的是 JSON 清单**（`clip_manifest_json`），实际视频编码/封包复用媒体层（未在本项）。清单 JSON 亦**不含 `path`/`size_bytes`**（R7），仅时间范围与帧数。
- 影响（UI）：P3 回放需要可播放的文件与索引；当前“保护片段存在”仅为逻辑片段+清单，回放取流无实际文件。
- 期望：媒体/集成项接入真实编码后，把视频文件路径/大小写入片段行与清单；`clip_manifest_json` 增补 `path`/`size_bytes`，并保证 `path` 为最终（重命名后）文件名。UI 在无文件时应显式“录像不可用”，不假设存在。

**D-02（时间轴/回放界面未实现，用户可观察点的 UI 半未落地）**
- 观察：`main.qml` `pageSource("replay")` 仍返回占位 `PageView.qml`；无 P3 时间轴、缺口斜纹留白、报警区段色标、`-30s/报警时刻/+30s/+60s` 跳转等实现。缺口记录/录像三态目前只在数据侧（ITEM-018 P2 行内标注）。
- 期望：P3 回放项实现时按 `VISUAL_DESIGN` §7.7/§8.3 与交互 §5.3 呈现：连续录像段、保护片段（level 色标）、**缺口斜纹/留白 + “录像缺失”提示**、录像不可用事件的黄条标注；并消费本项的 `recording_segments`/`recording_gaps`。

**D-03（入库与运行时接线）— 片段/缺口行与录像缓冲未接入 SQLite/媒体运行时；`frame_count` 口径**
- 观察：`RecordingSegmentRow`/`RecordingGapRow` 的 SQLite 写入复用 ITEM-007，但应用未打开数据库；`ClipRecorder` 也未接入 `CameraStream`/媒体帧（缓冲为模型时间戳）。真实“报警→片段→库/文件”链路归后续集成。另：`finalize` 的 `frame_count` 依当次缓冲现场（VERIFY-ITEM-019 O-1），若晚于 post 窗调用会偏小；UI 若呈现帧数/时长需先钉口径（建议 finalize 于 post 窗结束时即调，或以固定页 [t0,t1] 独立保留段统计）。
- 期望：集成项把媒体帧时间戳喂 `on_frame`、报警喂 `on_alarm`、编码完成喂 `finalize`/`set_clip_size`，并将段/缺口行写入 `recording_segments`/`recording_gaps`；在此之前不宣称端到端。

**D-04（范围缺口，非本项缺陷）— 本项无界面产物**
- 观察：提交未含 QML/组件改动，无法进行布局/组件/状态视觉评审；用户可观察点的“时间轴标记”无运行素材。
- 期望：待 P3 回放页与媒体接线落地后，UI 实现评审按 `VISUAL_DESIGN` §7.7/§8.3 与交互 §5.3 逐状态核对（段落/缺口/保护片段/不可用标注/跳转）。

### 已确认无问题/正向项

- 保护片段时长严格 `(pre+post)`（探针 90000ms=30+60），与 PLY-002/配置一致（R2）。
- **原子写盘**复用 `FileStore::write_atomic`（临时文件+rename），并有板端写入/回读实证（R4）。
- **保护片段优先保留**：保护片段作为 `Critical` 复用 ITEM-007 `apply_retention`，普通片段先淘汰、保护仅在仍超容量时最旧淘汰（R5）。
- **缺口不静默**：`add_gap` 写入并合并重叠/相邻缺口，reason 保留，供 P2/P3 “录像不可用/缺失”数据来源（R6）。
- 有界缓冲容量按 fps×(pre+post)+裕量推导，避免无界内存；`finalize` 幂等（已 finalize 跳过）。
- 纯 stdlib core，可在最小工具链单测（537/537）；文件 IO 抽到平台层，符合分层。

---

## 6. 未覆盖 / 未执行（不得外推）

1. 真实视频编码/封包/可播放片段（媒体层职责）。
2. P3 回放页、时间轴缺口/保护片段/跳转、录像不可用黄条（D-02/D-04）。
3. `recording_segments`/`recording_gaps` 的 SQLite 入库与运行时接线（D-03）。
4. 真机报警→片段→库/文件端到端（ENV-D）。
5. QML 运行期（无界面产物）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-019 保护片段/缺口核心模型与文件落盘）。
- 依据：任务内嵌用户可观察点在**模型/数据侧**成立——有界滚动缓冲、报警固化 (pre+post) 时长、原子写盘、配置化保留且保护优先、录像中断写缺口合并——经源码审阅、复用契约对照与归档证据（板端 537/537 + clip-probe PASS）逐条成立。
- 明确边界：本 PASS **不表示**存在可播放的保护视频（当前为 JSON 清单，D-01），**不表示**时间轴/事件“录像不可用”标记已实现（P3 未实现，D-02），**不表示**段/缺口已入库或端到端接线（D-03）。
- 后续修复/交接要求见 §5：D-01（清单补路径/大小 + 真实视频接入）、D-02（P3 时间轴/缺口/不可用标注）为**用户可观察点闭环**项；D-03 入库/运行时接线为后续集成；D-04 待界面落地后评审。
