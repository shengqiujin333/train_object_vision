# railhmi — RK3588 车载监控 HMI 桌面应用

本目录是桌面监控应用的源码工程。当前实现进度对应实现任务清单
`docs/hmi/desktop_app_tasks.yaml` 的工作项，按序增量交付；**尚未交付完整应用**。

- 设计: `docs/hmi/DESKTOP_APP_DESIGN.md`（DesktopAppDesignArtifact）
- 交互/视觉: `docs/hmi/INTERACTION_DESIGN.md`、`docs/hmi/VISUAL_DESIGN.md`、`docs/hmi/design_tokens.yaml`
- 测试: `docs/hmi/TEST_DESIGN.md`、`docs/hmi/E2E_PLAN.md`

---

## 当前状态（增量）

| 工作项 | 内容 | 状态 |
|---|---|---|
| ITEM-001 | 目标构建/运行依赖基线与授权缺口登记 | **源码 + 板端实测已交付**：`tools/env_probe.py`、`tools/board_env_probe.py`；板端 E-00..E-13 已实际采集（六路相机/NVR 端口可达），三态缺口与替代结论见 `docs/hmi/build_env_check.md` |
| ITEM-002 | 渲染路径选型 spike（qmlglsink 主 / RGA+纹理回退） | **已在板端实测并选定路径**：`cpu`（SELECTED）；`gl` 因缺 `qmlglsink`/Qt QML 包不可用（保留可开启候选）；数据与证据见 `docs/hmi/build_env_check.md` §14 与 `spikes/render_path/spike_core.c` |
| ITEM-003 | 分层 CMake 工程骨架与三档构建入口（`build.sh`） | **已交付**：`railhmi_core`（纯 stdlib）/`railhmi_platform`（日志/文件存储+可选 SQLite）/`railhmi_media`（target GStreamer+librga）/`railhmi`（Qt/QML）；板端 `CoreTests` 构建成功且 **9/9 测试通过**、`MediaCheck` 媒体层编译成功；`Release`/`Preview` 缺 Qt5 Quick/Qml 开发包时**明确报错**。证据：`docs/hmi/test_evidence/ITEM-003-build/README.md` |
| ITEM-004 | `railhmi-core` 配置与状态契约层（cameras/app/thresholds/roi） | **已交付**：无依赖 YAML 解析 + 配置模型/枚举 + 校验器（报具体字段，不静默默认）；`app/config/*.yaml` 基线；启动校验入口 `railhmi-config-check`；板端 CoreTests **40/40 通过**，坏配置报 `thresholds.confirm_frames` 并非零退出。证据：`docs/hmi/test_evidence/ITEM-004-config/README.md` |
| ITEM-005 | `railhmi-core` 流状态机与质量判定 | **已交付**：六态（CONNECTING/LIVE/FROZEN/OFFLINE/RECONNECTING/DEGRADED）+ 首帧/冻结/重连退避/阈值升级设备事件；质量标志（过暗/过曝/遮挡/模糊）与 `classify_quality`；非 LIVE 禁止旧帧接口 `may_present_as_live`。板端 CoreTests **79/79 通过**。证据：`docs/hmi/test_evidence/ITEM-005-stream/README.md` |
| ITEM-006 | `railhmi-core` 检测职责与报警规则/事件层 | **已交付**：按 ai_profile 分派（front 危险区 / pantograph 三态 / wheel_rail / cab_door / none）；多帧确认、跟踪 ID 与抑制窗口；级别严格按 PRD §8.2；ack 只改状态不删除事件。板端 CoreTests **124/124 通过**。证据：`docs/hmi/test_evidence/ITEM-006-rules/README.md` |
| ITEM-007 | `railhmi-core` 持久化与保留策略模型 | **已交付**：9 表 schema + 数据模型；UTC 毫秒 + 单调微秒时间基准；保护片段优先的保留淘汰；缺口模型；`SqliteStore`/`SqliteRepository`（WAL）。板端以运行时库 + 项目 shim 链接 SQLite（不改系统），CoreTests **230/230 通过**（含 WAL 重放/写入-重启-读取/淘汰顺序/缺口查询）。证据：`docs/hmi/test_evidence/ITEM-007-persistence/README.md` |
| ITEM-008 | `railhmi-core` 单元测试与可复现验证入口 | **已交付**：`app/tests/run_core_tests.sh` 单命令（构建+运行+归档日志+JUnit XML+退出码）；覆盖配置/六态状态机/规则级别映射/抑制/持久化/保留策略；最小工具链（C++ 编译器+SQLite，无 Qt/GStreamer/相机）。板端 **243/243 通过**，`RESULT: PASS`，JUnit 243/0。证据：`docs/hmi/test_evidence/ITEM-008-coretests/README.md` |
| ITEM-009 | 模拟输入与故障注入系统 | **已交付**：`SimulationRunner`（模拟时钟确定性；按 ai_profile 注入可重置检测序列；FI-01..05/FI-10 软件开关；故障后自动恢复）+ `railhmi-sim-inject` CLI（结构化日志/JSON）；无相机即可驱动模型层全部状态。板端 **274/274 通过**，sim-inject `SIM_EXIT=0`。证据：`docs/hmi/test_evidence/ITEM-009-simulation/README.md` |
| ITEM-010 | 设计令牌主题层（生成 + Theme 单例 + 无硬编码） | **已交付**：`gen_tokens.py` 由 `design_tokens.yaml` 生成 `tokens.json`/`Theme.qml`/`Tokens.qml`/`qmldir`（含 color/alarm L1-L3/stream_state/mode/integration_state/detection_badge/typography/spacing/radius/z_index）；`main.qml` 改用 `Theme.*`；`check_no_hardcoded_ui.py` 静态扫描。主机与板端 selftest 8/8、扫描 PASS，CoreTests 274/274。证据：`docs/hmi/test_evidence/ITEM-010-tokens/README.md` |
| ITEM-011 | 应用外壳与角色化导航 | **已交付**：`railhmi-core` 导航/权限模型（六页固定顺序、司机仅见实时/报警/回放、维护/研发/管理员全部、受限直达 denied、模式位固定可见光/红外未接入无切换）+ QML 外壳（main.qml/TopStatusBar/NavBar/PageView/DeniedView，消费 Theme，无硬编码）。板端 **319/319 通过**、扫描 PASS。证据：`docs/hmi/test_evidence/ITEM-011-shell/README.md` |
| ITEM-012 | 配置驱动摄像头注册表与 UI 接入 | **已交付**：`railhmi-core` 注册表模型（六路按 cameras.yaml 顺序、位置/职责徽标/`DetectionBadge` 仅 P4/P6、NVR 集成项通道映射=未配置）+ QML 组件（DetectionBadge/CameraTile/DeviceCard）与 P1/P4/P6 页面接入。板端 **348/348 通过**，扫描 PASS。证据：`docs/hmi/test_evidence/ITEM-012-registry/README.md` |
| ITEM-013 | 单路取流与流状态机接入 | **已交付**：`CameraStream`（原生 GStreamer 强制 UDP，test/file/rtsp 源；接入 railhmi-core 六态 + 质量标志；fps/最近帧/重连次数/presentable；`simulate_cut` 断流注入）+ `railhmi-stream-probe` CLI。板端 MediaCheck 构建成功；test 源实测 live 25fps → 断流 → reconnecting(count=1, presentable=false) → connecting。证据：`docs/hmi/test_evidence/ITEM-013-stream/README.md` |
| ITEM-014 | 视频渲染与实时监控页三联布局 | **已交付**：`railhmi-core` `compute_layout`（三联 0.26/0.48/0.26、前方最大 ≥0.34、P0 一屏内可见、一级横幅不遮挡前方中央、缩放比例保持）+ QML 接入：`main.cpp` 注入 `railhmiLayout` 与配置选定渲染路径 `railhmiRenderPath`（默认 cpu，gl 可开关）；`VideoSurface.qml`（按路径选渲染实现 + 非 LIVE 占位层）、`P1Realtime.qml`（左-前-右三联 + 关键状态区 + 当前报警条）。板端 TestRun **377/377 通过**（两档分辨率用例齐全），扫描 PASS。证据：`docs/hmi/test_evidence/ITEM-014-layout/README.md` |
| ITEM-015 | 六路并发与全摄像头页 | **已交付**：`railhmi-core` 网格模型（3x2/2x3 形状、稳定槽位、单元格不重叠）+ 每格视图（名/状态中文标签/fps/最近帧/重连/徽标；顺序=注册表，断流不重排不丢格）；`railhmi-media` `MultiStreamManager`（多路并发 + 单路断流注入，不互相影响）+ `railhmi-multi-probe`；`P4AllCameras.qml` 3x2/2x3 网格消费 `railhmiChannels`。板端 MediaCheck 实测：6 路 test 源同时 live，断开第 4 路后该格 presentable=false、最近帧冻结、重连计数，其余五路继续 presentable；core 单测网格稳定性。 |
| ITEM-016 | 关键状态区与顶部模式信息条 | **已交付**：`railhmi-core` 状态模型（`StatusLevel` 五级，颜色+形状字形+文字三重编码互不相同）+ 七类关键状态（受电弓/轮轨/车门/摄像头/AI/录像/存储，正常简洁/异常突出、异常可点击按角色）+ 顶部条（时间/模式·红外未接入/录像/AI/系统健康/车速·门磁未接入）+ 点击权限解析 + `railhmi-status-probe`；QML `StatusItem`/`KeyStatusStrip`/`TopStatusBar` 接入。板端 TestRun **443/443 通过**；探针三重编码 distinct=5/5/5/5，维护角色 allowed=true、司机 allowed=false，扫描 PASS。证据：`docs/hmi/test_evidence/ITEM-016-status/README.md` |
| ITEM-017 | 推理服务接入与双模型复用 | **已交付**：`railhmi-core` 推理模型（有界丢旧帧/帧间隔降频/优先级 front>wheel_rail>pantograph>cab_door/NPU 预算/温度守卫降载不停/两侧广角不入队/AI 不可用语义与报警标注/AI 调试视图）+ `railhmi-media` `AiWorker` 工作线程（可插拔 `Detector`；`SimDetector` 替代输入）+ 配置 `app.yaml ai:`（帧间隔/队列深度/预算/温度/模型版本）+ QML `P5AiDebug.qml`（框/ROI/置信度/耗时/模型版本，叠加层默认关）+ `railhmi-ai-probe`/`railhmi-ai-runner`。板端 TestRun **480/480 通过** + ai-probe PASS；MediaCheck ai-runner `eligible=4 with_result=4`、停止后 `AI 不可用`。证据：`docs/hmi/test_evidence/ITEM-017-inference/README.md` |
| ITEM-018 | 报警中心页面 | **已交付**：`railhmi-core` 报警中心模型（字段派生/默认排序级别降序→时间降序/六维筛选与空态/确认与批量确认只改 ack 不删事件/维护日志条目/录像三态标注/UTC 时间工具）+ UI 桥接 `RailhmiAlarmModel`（过滤排序确认委托 core）+ QML `P2AlarmCenter.qml`（筛选栏/列表/空态/行内录像标注/确认·查看录像·查看快照）+ `railhmi-alarm-probe`。板端 TestRun **512/512 通过**，探针 PASS（批量确认 changed=3、事件保留、空态、维护日志）。证据：`docs/hmi/test_evidence/ITEM-018-alarm-center/README.md` |
| ITEM-019 | 本地报警保护片段与缺口记录 | **已交付**：`railhmi-core` 有界滚动缓冲 + 保护片段（报警固化 [pre,post]、post 后 finalize）+ 原子写盘（`FileStore.write_atomic`）+ 保留策略保护优先 + 缺口记录合并 + `railhmi-clip-probe`。板端 TestRun **537/537 通过**，探针 PASS（时长 90000ms、写入存在、保护保留、缺口合并）。证据：`docs/hmi/test_evidence/ITEM-019-clips/README.md` |
| ITEM-020 | 历史回放页 | **已交付**：`railhmi-core` 回放模型（时间轴连续/报警/缺口/关键帧、四跳转 -30s/报警/+30s/+60s、游标 seek、三联同步偏差阈值提示、来源回退本机片段、事件详情）+ UI 桥接 `RailhmiReplayModel` + QML `P3Replay.qml` + 配置 `replay_sync_threshold_ms` + `railhmi-replay-probe`。板端 TestRun **566/566 通过**，探针 PASS（报警段/缺口/同步偏差提示/本机片段回退）。证据：`docs/hmi/test_evidence/ITEM-020-replay/README.md` |
| ITEM-021 | AI 调试页与司机视图隔离 | **已交付**：P5 展示检测框/ROI/置信度/**距离**/FPS/**端到端延迟**/模型版本/最近结果时间；调试叠加层逐项开关（框/ROI/文字/掩码）**默认全关**；新增令牌 `debug_box=#00E5FF`（框）/`debug_roi=#FFD400`（ROI 虚线，避免与报警红混淆）；司机隔离（导航过滤 + DeniedView + P5 司机无权限占位；`ai_debug_visible` core 判定）。板端 TestRun **586/586 通过**，`check_ai_debug_wiring` PASS，ai-probe 距离/司机隔离 PASS。证据：`docs/hmi/test_evidence/ITEM-021-ai-debug/README.md` |
| ITEM-022 | 设备维护页 | **已交付**：`railhmi-core` 维护模型（PTZ 目标 vs 实际 + 容差校验/一键归位·重调前方预置位二次确认/维护日志/超差→前方 AI NOT_READY/维护行含质量/录像存储/健康）+ UI 桥接 `RailhmiMaintenanceModel` + QML `P6Maintenance.qml`（分区卡片 + 二次确认模态 + NVR 未配置卡无自动修改）+ `railhmi-maintenance-probe`。板端 TestRun **612/612 通过**，维护探针/接线扫描 PASS。证据：`docs/hmi/test_evidence/ITEM-022-maintenance/README.md` |
| ITEM-023.. | 权限、恢复、打包 | 未实现 |

> 本文件是工程构建/运行入口说明；三档构建见 `build.sh`（`Release`/`Preview`/`CoreTests`，另 `MediaCheck` 媒体层检查）。UI/取流/页面在后续工作项接入。

---

## 目录

```
app/
  README.md               # 本文件：构建/运行入口说明
  CMakeLists.txt          # 分层工程（core/platform/media/ui 子目录）
  build.sh                # 构建入口: Release | Preview | CoreTests | MediaCheck | check
  src/
    core/                 # railhmi_core: 纯业务逻辑（无 Qt/GStreamer/相机/SQLite）
      include/railhmi/core/{version,yaml,config}.h
      tools/config_check.cpp   # 启动配置校验入口（railhmi-config-check）
    platform/             # railhmi_platform: 日志/文件存储 + 可选 SQLite(SqliteStore)
    media/                # railhmi_media: target 专用 GStreamer + librga
    ui/                   # railhmi: Qt/QML 应用（qml/main.qml；启动即校验配置）
  config/                 # 运行配置: cameras.yaml / app.yaml / thresholds.yaml / roi.yaml
  tests/core/             # railhmi-core-tests（最小工具链可跑；含配置域 TC-CORE-01）
  tools/
    env_probe.py          # 依赖/环境只读探针（标准库；目标板与主机均可运行）
    run_env_check.sh      # 探针运行入口（输出 md/raw/json 到 test_evidence）
    board_env_probe.py    # 经登记 SSH 入口的板端只读探针运行器（受信任 known_hosts + 全超时）
    board_build.py        # 经登记 SSH 入口的板端构建运行器（上传 app/ + build.sh 三档）
  spikes/render_path/     # ITEM-002 渲染路径选型 spike
    main.cpp GlView.qml CpuView.qml qml.qrc
    CMakeLists.txt        # 板端本机构建（Qt5 + GStreamer + librga）
    spike_core.c          # 无 Qt 核心管线测量
    analyze.py            # 选择判据（--selftest 主机可跑）
    run_render_spike.sh   # 驱动（--dry-run 可在无板环境校验入口）
```

---

## 运行：依赖/环境核验（当前唯一可执行入口）

目标板（RK3588，Ubuntu 22.04 aarch64，待授权后执行）：

```sh
python3 app/tools/env_probe.py \
  --out  docs/hmi/test_evidence/ENV-B-dependency/env-probe-<board>.md \
  --raw-out docs/hmi/test_evidence/ENV-B-dependency/env-probe-<board>.raw.txt \
  --json-out docs/hmi/test_evidence/ENV-B-dependency/env-probe-<board>.json
```

或使用入口脚本（默认输出目录 `docs/hmi/test_evidence/ENV-B-dependency`）：

```sh
sh app/tools/run_env_check.sh
# 相机/NVR 网络探测需授权，显式开启：
sh app/tools/run_env_check.sh docs/hmi/test_evidence/ENV-B-dependency --probe-cameras
```

- 只读探测：不安装、不修改系统、不写产品目录以外位置；无第三方依赖。
- 相机/NVR 端口探测**默认关闭**，需项目授权。
- 结果口径见 `docs/hmi/build_env_check.md` §0/§12：**主机结果不等于目标板结论**。

### 板端依赖基线（ITEM-001，经登记 SSH 入口）

```sh
python app/tools/board_env_probe.py \
  --host 192.168.0.103 --user admin \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL \
  --out-dir docs/hmi/test_evidence/ENV-B-dependency \
  --stamp <stamp> --connect-timeout 10 --command-timeout 90 --probe-cameras
```
- 使用受信任 `known_hosts`（默认 RejectPolicy），连接/命令/SFTP 全超时；凭据仅从环境变量读取，不落日志。
- 只读探测；写入仅限目标 `/tmp`。产出 `.md/.raw.txt/.json/.commands.json`（含主机指纹与逐条命令）。
- 结果与三态缺口见 `docs/hmi/build_env_check.md` §2/§3。

---

## 运行：渲染路径选型 spike（ITEM-002）

```sh
# 主机：只校验入口与判据（不构建、不访问设备）
python3 app/spikes/render_path/analyze.py --selftest
bash    app/spikes/render_path/run_render_spike.sh --dry-run

# 目标板：真实测量（需项目授权与可用相机）
cd app/spikes/render_path
./run_render_spike.sh --url 'rtsp://<user>:<pass>@<cam>/...' --duration 20 --snapshot
# 无相机时用 ENV-C 替代源（预录文件/合成）完成协商与换算测量
./run_render_spike.sh --source file --file /path/to/clip.mp4 --duration 20 --snapshot
./run_render_spike.sh --source test --duration 10
```

结果与判据说明: `docs/hmi/render_path_spike.md`、`docs/hmi/build_env_check.md` §14。
未实测前**不选定主路径**。

---

## 构建（ITEM-003 已落地）

目标构建沿用项目既定模式：**板端本机编译，无交叉工具链**。分层：`railhmi_core`（纯 stdlib）/`railhmi_platform`（日志/文件存储 + 可选 SQLite）/`railhmi_media`（target GStreamer+librga）/`railhmi`（Qt/QML）。

```sh
# 目标板（aarch64 本机构建）——需 Qt5 Quick/Qml + GStreamer + librga
cd <app>
./build.sh Release                # RAILHMI_BUILD_TARGET
./build.sh MediaCheck             # 仅 railhmi_media（GStreamer+librga，无 Qt）编译检查

# 主机开发预览（x86_64 模拟输入）——需宿主 Qt5 Quick/Qml
./build.sh Preview                # RAILHMI_BUILD_PREVIEW + RAILHMI_SIMULATION

# 最小工具链单元测试（C++ 编译器 + CMake；不依赖 Qt/GStreamer/相机）
./build.sh CoreTests && ./build-core/tests/railhmi-core-tests

# 只做依赖自检
./build.sh check
```

经登记 SSH 入口的板端构建（代传 `app/` 并执行）：

```sh
python app/tools/board_build.py --host <addr> --user <user> \
  --credential-env TRAIN_RK3588_SSH_CREDENTIAL --mode CoreTests \
  --outdir docs/hmi/test_evidence/ITEM-003-build --stamp <tag> --run-tests
# 其它: --mode MediaCheck | Release | Preview | check
```

- 无对应工具链时 `build.sh` 逐项打印缺失依赖并退出 1，**不静默失败**。
- 依赖与版本以 `docs/hmi/build_env_check.md` 的板端核验结果为准；构建结果见 `docs/hmi/test_evidence/ITEM-003-build/README.md`。
- 主机编译不代表目标板运行。
