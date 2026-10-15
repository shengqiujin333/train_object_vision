# ITEM-015 软件验证 TestEvidence — 六路并发与全摄像头页

- 判定轮: `VERIFY-20261016-v2`（本文件为该轮判定文档；v1 过程轮留痕见 §6）
- 被验证交付: commit `db1bae7`（embedded_linux_engineer.desktop_app_implementation returned IMPLEMENTED，ITEM-015）
- 工作项: `TL-778f77eb-1fde-496d-9677-7330e26c6476-tasks-ITEM-015`
- 依据: `DESKTOP_APP_DESIGN.md` §6.2/§7.1；`INTERACTION_DESIGN.md` v2 §7.3（六路注册表/断流格保留）；`VISUAL_DESIGN.md` v2 §7.5（CameraTile 最近帧/FPS、状态三重编码）；`TEST_DESIGN.md` TC-STREAM-07 / TC-SMOKE-06 / TC-SMOKE-12 / §G2
- 结论: **TEST_PASS**（判据逐条满足，证据见下；边界如实标注）

## 1. 判据总表（工作项描述逐句）

| # | 验收判据（工作项） | 结果 | 证据 |
|---|---|---|---|
| J1 | 六路同时取流并在 2x3/3x2 网格显示 | ✅ 板端 media 实测：`--count 6` 三组参数（默认 cut3 / 验证轮 cut1 / cut0）每轮 `cells=6 live=6 presentable=6`、逐路独立 fps→25.x；形状 `choose_grid_shape` 宽>=高→3x2/否则2x3（`railhmiGrid` 注入，QML 随窗口宽高切列数）；ext 夹具 g0 手算精确值 + g0f 2x3 | `VERIFY-FIXTURES...multi-*` 解析 + ext `[run-ext]` 81/81 |
| J2 | 每格显示摄像头名、连接状态角标、实时 fps、最近一帧时间、检测职责徽标 | ✅ ext 夹具 c0/c0i：六态+未启动七种中文标签互不重复（三重编码文字部分）；uc0a-d：真实 cameras.yaml 基线名/徽标/ai_enabled 逐字段核对；m0/m1：格式边界（fps ε、`<1 秒前`、`分 秒前`）；QML 接线 34/34（p2b/p2c/p3a-d/p4b 消费链） | ext `run-ext` + `VERIFY-QML-WIRING15...` |
| J3 | 断流或冻结格子保留位置不重排 | ✅ media：断流后 `cells=6` 恒定、`idx=N` 的 id 不变（三组变体均 order_unchanged=true）；ext r0/r0b：部分缺流不丢格、id/slot 不动；core 411/411 含 `cut: slots do not reorder (grid does not jump)` | multi-parse 三组 + ext + core-test 行 |
| J4 | 用户可观察：断开其中一路后该格显示占位与最近帧时间、其余五路继续刷新 | ✅ media 独立判据解析（不信交付 acceptance 行）：断流格 `presentable=false` + `last_frame_ms` 冻结在断流前最后帧、其余路 last_frame_ms 持续推进/presentable=true（cut1→CAM-02、cut0→CAM-01、cut3 默认→CAM-04 全一致）；ext c1a-g：CAM-04 断流占位/`重连 3 次`/`7 秒前`+stale，五路 live；c2 冻结格保留最近帧；c3 降级格仍 presentable | multi-parse v1/v2 + ext c1/c2/c3 |
| J5 | 网格不跳位 | ✅ ext g0c 对齐恒等式（余量 631≤632、177+175=352）、s0/s1 判据鉴别力（伪造重叠/乱序槽位必须判 false）、`grid_rects_no_overlap` 全 6 格实跑；QML 槽位仅由 `railhmiChannels` 模型序决定（p1e 无手工位置源） | ext g0/s0/s1 + wiring p1e |
| J6 | 配置驱动的格子集合（TC-SMOKE-06 延伸） | ✅ ext uc1：真实 cameras.yaml 删除 CAM-02 → 5 路，registry/视图顺序跟随配置、不重排不丢格；uc2 改名 CAM-06 → 视图名跟随；uc0 六路名/徽标/顺序全部来自配置 | ext uc0/uc1/uc2 |

## 2. 板端独立复现（验证人独立 SSH 轮，stamp VERIFY-20261016-v2）

- 指纹: `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`（与实现者 transcript 一致；见 `VERIFY-FIXTURES-...v2.log`/`transcript.json`）。
- **交付入口复跑**: `bash tests/run_core_tests.sh --outdir core-evidence --stamp VERIFY-20261016-v2`（ENTRY_EXIT=0），**411/411**，RESULT PASS。
- **逐行比对**（程序化）: 验证轮归档 `[PASS]` 行 **411** == 实现者 `build-TestRun-...-ITEM015-r1...log` 411 行，`identical=True`（impl_pass_lines=411 verify_pass_lines=411）。
- **JUnit XML 独立解析**: `tests=411 failures=0`；GRID 域用例≥12（实测 hint=30）。
- 回归: tokens selftest `TOKENS_EXIT=0`；UI 无硬编码扫描 `SCAN_EXIT=0`；sim-inject `SIM_EXIT=0`（core 全套回归在本轮独立复跑）。
- `board_build.py` 媒体层独立触发: `bash build.sh MediaCheck`（MBUILD_EXIT=0）→ `railhmi_stream-probe`/`railhmi-multi-probe` 归档在 `_board` 序列（见 transcript）。

## 3. 交付 `railhmi-multi-probe` 验证轮独立参数复跑（media 层板端实跑）

| 变体 | 参数 | 独立解析判据 | 结果 |
|---|---|---|---|
| v2-cut1/view_count6 | `--count 6 --source test --duration 8 --cut-index 1 --cut-after 4` | all_live_before / cut_not_presentable / cut_frozen / others_presentable / order_unchanged | **全 true**，rounds=16，cut_cell_id=CAM-02 |
| v2-cut0Count6 | `--count 6 --source test --duration 8 --cut-index 0 --cut-after 4` | 同上 | **全 true**，rounds=16，cut_cell_id=CAM-01 |
| v2-count2Cut1 | `--count 2 --source test --duration 6 --cut-index 1 --cut-after 3` | 同上 | **全 true**，rounds=12，final_live=1 |
| 负例 | `--count 0` | 启动失败需干净退出码 3，不得崩溃/挂起 | `NEG_EXIT=3`（stderr 明确 all channels failed to start） |

- 验证轮解析**不采信**交付自身的 acceptance 行，仅当补充参考（记录于 parse 输出，result=PASS）。
- 三组断流参数（默认 CAM-04 + 验证轮 CAM-02/CAM-01）互证实现通用性——断流语义不绑定第 4 路特判。

## 4. 验证自有扩展夹具（81/81，超出实现者 test_grid.cpp 范围）

编译: 验证夹具 `verify_grid15_ext.cpp` 链接交付构建出的 `librailhmi_core.a`（g++ -std=c++17 -Wall -Wextra，GXX_EXIT=0 零警告）。
关键断言组:

- **g0/gb（13 断言）**: 3x2 单位格 6 格手算精确值（632x352 gap2 → cw209/ch175，x=0/211/422、y=0/177）逐字段一致 + 末格余量恒等式（422+209=631≤632 与 177+175=352 收边）+ gap=0 连续格 + 2x3 idx3 显式值 {164,162,156,154} + 0 尺寸钳位不崩
- **s0/s1/v0（14 断言）**: 判据鉴别力（重叠/完全相同矩形判 false、共享边不算重叠、乱序槽位判 false）；宽==高→3x2 边界；slot(5)/slot(7) 行列映射显式核对
- **c0/c0i（9 断言）**: 六态+未启动七种中文标签逐 token 核对 + 两两互异（三重编码文字部分无歧义）
- **m0/m1/st0/st1/st2（14 断言）**: format_fps（ε=0.05 严格、两位小数进位、负值）；format_last_frame 全区间（0/999/1000±、59999/60000、61 分 1 秒）；陈旧阈值严格大于（==2000 不陈旧、2501→"2 秒前"+stale）；负 age（时钟回拨）→"—"不崩溃；无帧→"—"
- **r0/r1/r2（6 断言）**: 部分运行时（2 路 live 4 路缺流）——6 格不丢、id/slot 不动、live 路正常、缺流格占位；runtime 长于注册表被忽略；同输入两次构建逐字段确定性
- **c1/c2/c3（13 断言）**: 断流 CAM-04 身份保留/占位/`重连 3 次`/`7 秒前`+stale/fps"—" + 五路 live；冻结格保留最近帧且占位；DEGRADED 降级可用（presentable）
- **uc0/uc1/uc2（15 断言）**: 真实 cameras.yaml 基线 → 视图名/徽标/ai_enabled/slot 全核对；删 CAM-02 后 5 路视图顺序=配置序不丢格；改名 CAM-06 → 视图名跟随其余不变

## 5. QML 接线静态核对（34/34，主机执行）

QML 无法板端执行（E-01b Qt5 Quick/Qml 缺失，ITEM-010..014 同边界），按既定口径静态结构核对：

- p1 固定槽位: `Repeater model: railhmiChannels` 唯一位置源（p1e 无任何按状态重排的代码路径）；cell 尺寸 = `(grid.{width,height} - (cols-1)*gap) / {cols,rows}` 与 core `grid_cell_rect` 同一公式
- p2/p3: 五字段（name/stateLabel/badge/aiEnabled/fpsText/lastFrameText/reconnectText）+ presentable→live 全部从 modelData 消费；P4 `showBadge: true`（P4/P6 契约）
- p4: CameraTile 底部 fps/最近帧列、顶部重连计数、VideoSurface 占位层与实时层精确互补（`visible: surface.live` / `!`）
- p5/p6: `main.cpp` 注入 `railhmiChannelViews` ← `build_channel_views(registry, {}, 0)`（未启动全占位不丢格 fallback）与 `railhmiGrid` ← core `grid_columns/rows`（无字面量双源）；页面形状 fallback 等值 3x2/2x3 且仅限 typeof 守卫兜底
- p8: P4/CameraTile 无摄像头名/IP/ID 字面量（TC-SMOKE-06 配置即视图代码前提）

## 6. 边界（如实）与过程轮留痕

- **真机六路 RTSP（ENV-D）未执行**: `.162/.163/.165/.167` 凭据未核验，多数相机读取入口 `configured_unverified`；本轮以**板端 test 源**六路并发实测并发/断流/不重排语义。`--source file/rtsp` 入口已就绪，TC-STREAM-07 的 ENV-D/ENV-C 档归后续素材/触达补测；未尝试的连接不宣称离线。
- **QML 运行期未执行**: 同 §5 边界；逐帧呈现/视觉体验归用户确认，不在自动验证判据内。
- **媒体层到 QML 的运行时接线**: 应用进程尚未把 `MultiStreamManager` 接入 QML 运行时（媒体层 target 专用注入属后续集成）；`main.cpp` 以未启动占位驱动 `railhmiChannels`，网格结构/字段消费已就绪——J2/J4 的"格内容"级证据来自 core（ext）与 media（探针），运行期画面呈现留到集成项与用户核验。
- **六路全模型推理不在本项**（归推理接入项）。
- **过程轮 v1**: ext 首跑 79/81（g0f 手算误写 col0/x=0、st0b 误把 2501ms 取整成 3 秒），**均为验证侧期望构造错误**（自核对三遍后修正，见 v1 transcript 与 cpp 编辑历史），v2 权威轮 81/81；本地 `__pycache__` 过程残留已清理。

## 7. 文件清单（本轮验证产出）

- `VERIFY-ITEM-015.md`（本文件）
- `verify_grid15_ext.cpp` / `verify_grid15_fixtures.py` / `verify_qml_wiring15.py` / `run_verify_round.sh`
- v2: `VERIFY-FIXTURES-VERIFY-20261016-v2.log` / `.transcript.json`（全流程逐命令/退出码/指纹/清理）
- v2: `...v2.core-evidence_core-tests-VERIFY-20261016-v2.log` / `.xml`（411/0 逐行一致）+ `...v2.core-evidence_sim-inject-...` 归档
- v2: `...v2.multi-cut1_count6.log` / `...v2.multi-cut0_count6.log` / `...v2.multi-count2_cut1.log`（三组独立参数板端实跑原文）
- v1 留痕: `VERIFY-FIXTURES-VERIFY-20261016-v1.*`（ext 首跑 79/81 与核心复跑证据，见 §6）
- 主机静态: `VERIFY-QML-WIRING15-VERIFY-20261016-v2.log`

## 8. 复现

```sh
# 主机静态接线核对
python docs/hmi/test_evidence/ITEM-015-grid/verify_qml_wiring15.py
# 板端验证轮（需 TRAIN_RK3588_SSH_CREDENTIAL 已在 Windows User env）
VERIFY_STAMP=<stamp> bash docs/hmi/test_evidence/ITEM-015-grid/run_verify_round.sh
# 板端交付入口（最小直跑）
bash app/tests/run_core_tests.sh
bash app/build.sh MediaCheck && ./build-media/tools/railhmi-multi-probe \
  --count 6 --source test --duration 8 --cut-index 3 --cut-after 4 --out m.log --json m.json
```
