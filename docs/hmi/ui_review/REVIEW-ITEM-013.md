# UI 实现评审证据 — ITEM-013（单路取流与流状态机接入）

- Capability: `web_frontend.ui_implementation_review`
- 评审对象: `n005 embedded_linux_engineer.desktop_app_implementation`（commit `d96b0d2`，2026-10-16 02:19:12 +0800）
  - 声明产物（DesktopAppImplementation）: `app/README.md`
  - 主体交付: `app/src/media/include/railhmi/media/camera_stream.h`、`app/src/media/src/camera_stream.cpp`、`app/src/media/tools/stream_probe.cpp`、`app/src/media/CMakeLists.txt`
  - 构建证据: `docs/hmi/test_evidence/ITEM-013-stream/README.md`
- 被评工作项: `ITEM-013`（原生 GStreamer 强制 UDP 逐帧发布、接入 railhmi-core 六态与质量标志、统计 fps/最近帧时间/重连次数、无相机时 file/test 源运行同一管线；用户可观察：断流后进入重连中并显示已重连次数与最近帧时间，非 LIVE 渲染占位层且旧帧不当实时）
- 参照设计: `docs/hmi/INTERACTION_DESIGN.md` v2 §7.1/§7.2（`ef47841`）、`docs/hmi/VISUAL_DESIGN.md` v2 §3.5/§7.5（`40ccf4e`）、`docs/hmi/DESKTOP_APP_DESIGN.md` §6.1/§6.2、`docs/hmi/TEST_DESIGN.md` TC-STREAM-01..04
- 关联上游验证: `n006 software_tester.software_verification` **TEST_PASS**（`docs/hmi/test_evidence/ITEM-013-stream/VERIFY-ITEM-013.md`）
- 关联前序评审: `docs/hmi/ui_review/REVIEW-ITEM-005.md` D-01（当前质量标志未类型化暴露）、`REVIEW-ITEM-012.md` D-02（连接状态未按 §3.5 三重编码/无占位层）
- 评审日期/环境: 2026-10-16；Windows 主机（MINGW64 x86_64，无 C/C++/Qt/GStreamer）；**本轮未连接 RK3588 目标板**
- 判定: **TEST_PASS（范围严格限定为 ITEM-013 媒体层取流/状态机数据侧；不构成 QML 运行期视觉/交互结论；UI 占位层与字段消费缺口见 §5 D-03）**

---

## 0. 结论与口径（防止跨层/跨范围外推）

1. 本次评审范围是工作项 **ITEM-013**（单路取流与状态机接入）。判定对象是任务内嵌用户可观察点：强制 UDP 管线、逐帧接入六态与质量标志、fps/最近帧时间/重连次数统计、无相机 test/file 源可用、断流→重连中+计数+最近帧、非 LIVE 旧帧不当实时。
2. **本次未做 QML 运行期视觉/交互评审**：板端缺 Qt5 Quick/Qml（E-01b），QML 无法运行；本文以**媒体层源码 + 数据契约 + 设计对照**为证据（§5 D-06）。用户可观察的“渲染占位层”属 UI 域，**本项未实现**（D-03）。
3. **本轮未重新连接目标板**：板端 MediaCheck 构建 + 探针 + 验证自有夹具 26/26 由上游软件验证复跑。本 capability 授权方法为 `read/ui_evidence/design_assets`，主机无工具链；本轮以源码审阅与契约对照为独立证据。
4. 依赖缺口（主机无 C/C++/Qt；板端无 Qt5 Quick/Qml E-01b；真机 RTSP 未测）按职责交平台/环境/后续取流档；本评审不判定其“不可修复”，不返回 BLOCKED。
5. 本轮**未修改任何实现代码**，只写本评审证据文件与清单登记；未使用图像生成工具。

---

## 1. 版本观察

| 对象 | 版本/提交 | 观察 |
|---|---|---|
| 被评实现 | `d96b0d2`（2026-10-16 02:19） | 新增 `camera_stream.{h,cpp}`（GStreamer 管线 + 六态接入 + 质量/统计/断流注入）、`stream_probe.cpp`、CMake 接线；ITME-013 证据归档；**未改动任何 QML**（占位层渲染不在本提交） |
| 交互设计 | `ef47841` | v2 §7.1 六态与非 LIVE 硬约束、§7.2 运行时字段（`state/fps/last_frame_ts/quality_flags/reconnect_count`） |
| 视觉设计 / 令牌 | `40ccf4e` | §3.5 状态三重编码、§7.5 CameraTile（占位层 + 最近帧时间 + FPS） |
| 软件验证 | `849bde3` | TEST_PASS；板端 MediaCheck + 探针逐字段复现 + 扩展夹具 26/26；真机 RTSP 与 QML 占位层按边界留待后续 |

实现自述与仓库证据一致：`ITEM-013-stream/README.md`、板端探针输出与核心契约相符；**明确声明 QML 占位层渲染归 UI 域、真机 RTSP 未测**，未外推。

---

## 2. 评审方法

1. 源码审阅：`camera_stream.{h,cpp}`（管线构造/UDP、appsink 拉帧、`classify_quality` 接线、六态推进、`StreamStats`、`simulate_cut`、`pump`/`stop`）、`stream_probe.cpp`（结构化输出/JSON/参数）、CMake。
2. 数据契约对照：`StreamStats` 字段与交互 §7.2 运行时字段、视觉 §7.5 显示项对照；`presentable` 与交互 §7.1 硬约束对照。
3. 归档证据核对：板端探针序列（cut→reconnecting count=1、last_frame 冻结、presentable=false）、`VERIFY-ITEM-013.md`（含 26 断言夹具）。
4. UI 接线核对：原生 Grep 确认 QML 是否消费 `presentable`/`last_frame_ms`/`reconnect_count`/质量标志。

**方法限制**：主机无 GStreamer/Qt → 不能运行管线或界面；结论以源码/契约与归档证据为准。

---

## 3. 独立复现记录（本评审实际执行）

| # | 动作 | 结果 |
|---|---|---|
| R1 | 管线构造对照 DESKTOP §6.1 | `rtspsrc ... protocols=udp udp-buffer-size=...`（强制 UDP）+ `tee` 双分支（fakesink 计数 / appsink 取帧），与设计一致 |
| R2 | `StreamStats` 字段对照交互 §7.2 | 具备 `state/fps/last_frame_ms/reconnect_count/presentable/last_error`；**缺 `quality_flags`**（current flags 在 `pump` 局部计算后丢弃）→ D-01；**`last_frame_ms` 为单调毫秒**，非可显示墙钟 → D-02 |
| R3 | `presentable` 语义对照交互 §7.1 | `may_present_as_live(now)`，仅 LIVE/DEGRADED 且帧新鲜为 true；板端 cut 后全程 `presentable=false`（R6） |
| R4 | 断流可观察点（归档） | cut 后 `state=reconnecting reconnect_count=1`，`last_frame_ms` 冻结在断流前最后帧，`presentable=false`；无 live 复活 |
| R5 | 无相机可用性（归档） | `--source test` 全程跑通；`--cut-after 0` 首节点即 reconnecting、`last_frame_ms=0`，管线干净收尾 |
| R6 | UI 接线核对（原生 Grep） | 产品 QML **无任何** `presentable`/`last_frame_ms`/`reconnect_count`/`quality` 消费点；`CameraTile` 仍仅显示 `state` 原始 token 文本 → D-03 |
| R7 | 重连实现核对 | `simulate_cut()` 将管线置 NULL 且置 `cut=true`；`pump()` 以 `!cut` 跳过取帧，**无重建/重 PLAY 管线的代码**；bus error 亦只调用 `on_source_error`，不重启管线 → D-04 |
| R8 | 凭据契约核对 | `CameraStreamConfig.url` 注释“不含凭据”，但 `build_pipeline` 原样内嵌 `c.url`，探针示例 `--url 'rtsp://<user>:<pass>@...'` 含凭据 → D-05 |

---

## 4. 判据逐条核对（ITEM-013 用户可观察点）

| 判据 | 实际观察 | 判定 |
|---|---|---|
| 原生 GStreamer 强制 UDP 逐帧发布 | `protocols=udp` + appsink 逐帧（R1） | **满足** |
| 接入六态与质量标志 | `on_frame` + `classify_quality`（mean/stddev）接线；状态由 core 六态机驱动 | **满足（质量标志未回读，见 D-01）** |
| 统计 fps/最近帧时间/重连次数 | `StreamStats.fps/last_frame_ms/reconnect_count`（R2） | **满足（显示口径见 D-02）** |
| 无相机时 file/test 源运行同一管线 | R5 | **满足** |
| 断流后进入重连中并显示重连次数与最近帧时间 | R4 | **满足（数据侧）** |
| 非 LIVE 渲染占位层且旧帧不当实时 | 数据侧 `presentable=false`（R3/R4）；**UI 占位层未实现**（R6） | **数据侧满足；渲染侧未实现（D-03）** |

---

## 5. 具体偏差与修复要求

**D-01（UI 数据契约缺口，延续 REVIEW-ITEM-005 D-01）— `StreamStats` 不回传当前质量标志**
- 观察：`CameraStream::pump()` 计算 `q = classify_quality(frame_stats(vf), cfg.quality)` 后仅传给 `machine->on_frame`，**未保存**；`StreamStats` 无 `quality_flags` 字段。`VISUAL_DESIGN` §7.5 要求 CameraTile 显示质量角标（暗/过曝/遮挡/模糊），交互 §7.2 将 `quality_flags` 列为运行时字段。
- 复现路径：`read app/src/media/include/railhmi/media/camera_stream.h`（`StreamStats` 无质量字段）；`read app/src/media/src/camera_stream.cpp`（`pump` 中 `q` 局部丢弃）。
- 期望：在 `StreamStats` 增加类型化 `core::QualityFlags quality`（并在 `pump` 保存当前标志、非流状态清空），使 UI 可直接渲染角标，无需解析事件文本或另存副本。

**D-02（UI 显示口径）— `last_frame_ms` 为单调毫秒，UI 无法显示“最近帧 HH:MM:SS”**
- 观察：媒体层 `last_frame_ms` 取自 `steady_clock`（单调毫秒），`0` 为无帧哨兵；`VISUAL_DESIGN` §7.5 要求右下显示“最近帧 HH:MM:SS”，交互 §7.2 为 `last_frame_ts`。单调毫秒不可直接渲染为时间。
- 期望：暴露可显示口径——墙钟 `last_frame_ts`（经 `core::TimeBase` 由事件/流层统一基准转换），或直接暴露 `last_frame_age_ms` 让 UI 显示“N 秒前 / HH:MM:SS”；并定义无帧时的显示（如“—”）。

**D-03（用户可观察点的 UI 半未实现）— 无占位层/最近帧/计数渲染，产品 QML 未消费 `presentable`**
- 观察：本提交**未改动 QML**；`CameraTile` 仍 `text: tile.state`（原始 token），无 §7.5 的非 LIVE 占位层（灰化最近帧 + 状态徽标 + 最近帧时间戳）与 FPS/重连次数显示；产品 UI 无任何 `presentable` 使用点（R6）。因此“非 LIVE 状态渲染占位层且旧帧不被当作实时画面”只在数据侧成立。
- 影响：司机/维护看到的仍是未本地化 token，且无法从界面判断“画面是否可实时呈现/冻结多久/重连几次”。
- 期望：把 `CameraTile`（或帧视图组件）接到流运行时字段：`presentable==false` 时叠加占位层 + 显示最近帧时间（D-02 口径）+ 重连次数；`presentable==true` 才呈现实时画面。此项同时闭合 REVIEW-ITEM-012 D-02（状态三重编码）。

**D-04（重连语义缺口）— 断流/错误后无实际重建管线，无法恢复**
- 观察：`simulate_cut()` 将管线设 NULL 并置 `cut=true`；`pump()` 以 `!cut` 跳过取帧，且无任何“重建管线 / 重新 PLAY”的代码；对真实 bus error 亦仅 `on_source_error`，不重启管线。状态机可退避至 CONNECTING，但管线始终不恢复，故永不再出帧（验证亦记录“无复活/未出帧”）。
- 影响：UI 只依赖六态与帧新鲜度；若媒体层不实际重连，该路将长期停留非 LIVE，无法体现“自动重连/恢复”的交互（交互 §10）。
- 期望：实现真实重连（在 RECONNECTING/退避后重建并 PLAY 管线，或对 bus error 触发重建），或明确将重连实现归后续取流项并记录依赖；否则“重连中→恢复”的界面语义无数据支撑。

**D-05（凭据/日志契约，交实现确认）— RTSP URL 凭据承载方式与脱敏**
- 观察：`camera_stream.h` 注释 URL “不含凭据”，但 `build_pipeline` 将 `c.url` 原样拼入管线描述，探针示例 `rtsp://<user>:<pass>@...` 含凭据；未见 `rtspsrc` 的 `user-id/user-pw` 用法或 URL 脱敏。若 `last_error` 或管线描述被打印，可能泄漏凭据。
- 期望：明确凭据注入方式（在内存中以 URL 构造或 `user-id/user-pw` 属性），并确保管线描述/`last_error` 永不包含明文凭据（沿用 `platform::redact_url`）；在文档记录。

**D-06（范围缺口，非本项缺陷）— QML 运行期与真机 RTSP 未验证**
- 观察：板端缺 Qt5 Quick/Qml（E-01b），QML 未运行；真机六路 RTSP（ENV-D）未执行（四路凭据未核验）。无运行素材。
- 期望：平台补齐 Qt 后运行并录制：断流→占位层+最近帧+重连次数、恢复；真机档由 `read_camera_stream` 授权后补测。

### 已确认无问题/正向项

- 管线按设计强制 UDP（`protocols=udp`）+ `tee` 双分支，符合 DESKTOP §6.1 与历史兼容约束（不阻塞取流）。
- `presentable` 直接复用核心 `may_present_as_live`，非 LIVE/旧帧判否，**单一真相**（R3），是 UI “旧帧不当实时”的可靠数据依据。
- 断流语义可复现：reconnecting + `reconnect_count` + 冻结 `last_frame_ms` + `presentable=false`（R4）。
- 无相机可运行：test 源跑通、`--cut-after 0` 空态、无效 file 不崩溃、`stop()` 清理（R5），满足“无相机时可用”。
- 结构化输出（`t= state= fps= last_frame_ms= reconnect_count= presentable= error=` + JSON）便于 UI/测试消费与归档。
- `classify_quality` 仅消费 NV12 Y 平面统计，不触图像，边界归 ITEM-005 已验证域。

---

## 6. 未覆盖 / 未执行（不得外推）

1. QML 占位层/最近帧/重连次数的渲染（板端缺 Qt5 Quick/Qml E-01b；产品 QML 未改动）。
2. 真机六路 RTSP（ENV-D；凭据未核验），本轮仅 test 源。
3. file 源端到端解码一致性与 6 路并发合成（后续档）。
4. 质量标志的模糊判定（需拉普拉斯，当前未实现，已声明）。
5. 目标板命令本轮未执行（由上游验证复跑覆盖）。

---

## 7. 判定

**TEST_PASS**（范围：ITEM-013 媒体层取流/状态机数据侧）。
- 依据：任务内嵌用户可观察点在**数据侧**成立——强制 UDP 逐帧发布、接入六态与质量判定、fps/最近帧/重连统计、无相机 test/file 源可用、断流进入重连中并保留重连次数与最近帧、非 LIVE `presentable=false`——经源码审阅、数据契约对照与归档证据核对（板端探针 + 验证 26 夹具）逐条成立。
- 明确边界：本 PASS **不表示** QML 已运行或“非 LIVE 渲染占位层”已实现（D-03）；**不表示**断流可自动恢复（D-04 无实际重连）；质量角标数据（D-01）与可显示时间（D-02）尚缺。
- 后续修复/交接要求见 §5：D-01/D-02 为 UI 数据契约项，D-03 为**用户可观察点的 UI 半实现**（与 REVIEW-ITEM-012 D-02 一并闭合），D-04 为恢复语义关键项，D-05 凭据/日志契约，D-06 待 Qt 与真机授权后补测。
