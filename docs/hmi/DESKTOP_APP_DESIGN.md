# 轨道车辆智能视觉辅助监控系统 — 桌面应用架构与构建运行方案

- Artifact: DesktopAppDesignArtifact（桌面架构依赖及构建运行方案）
- Capability: `embedded_linux_engineer.desktop_app_design`
- 版本: v1（首版）
- 上游输入:
  - `docs/hmi/INTERACTION_DESIGN.md`（FrontendDesignArtifact，n001，verified）
  - `docs/hmi/VISUAL_DESIGN.md` + `docs/hmi/design_tokens.yaml`（UIVisualDesignArtifact，n002，verified）
  - `功能需求.docx`（PRD V0.1）、`一些说明和要求.txt`、`README.md`（交接现状，2026-10-13）、`PROJECT_GUIDE.md`、`troubleshooting_cameras_demo.md`
  - 现有实现：`code/`（板端双模型 demo）、`tools/`（PTZ/ONVIF/取流/SSH 工具）
- 面向下游: `embedded_linux_engineer.desktop_app_implementation`（按本方案与任务清单实现）、`software_tester`（测试计划输入）、`embedded_tester`（板上联调输入）

---

## 0. 交付边界与研究声明

### 0.1 本 capability 交付什么

- 交付：桌面应用的技术选型与理由、运行/构建环境分解、模块与进程/线程架构、视频接入与渲染管线、状态与数据契约、报警/录像/持久化/配置/健康设计、构建部署方案、验证策略、依赖与未知事实清单，以及有序实现工作项清单。
- 不交付：桌面运行代码（归 `desktop_app_implementation`）、视觉稿（归 `ui_visual_design`）、模型算法与精度（归模型职责）、BSP/驱动/rootfs 变更（归平台职责）、部署执行与整机验收。

### 0.2 研究/验证声明

- **本轮未连接设备、未执行任何物理测试、未在板端编译**。项目设备工具已禁用。文中所有“板端已有 Qt/Mali/GStreamer 插件”等判断均为**待核验假设**，§4.4 给出核验命令与所需证据。
- 现有代码状态来自 `README.md`/`code/` 静态阅读，不是本轮实测。
- 分辨率、触控、环境光、车速、门磁、NVR 回放接口等待确认项（PRD §14、交互方案 §12、视觉规范 §13）**不虚构数据**，界面按“未接入/不适用”呈现。

### 0.3 平台判定（沿用交互方案 §0.1，不重判）

本 HMI 是运行在列车 **RK3588 车机上的本地桌面应用**（X11，`DISPLAY=:0`，显示器/VNC），不是浏览器 Web 应用。当前 demo `code/main.cc` 即用 OpenCV highgui 在 X11 显示，证明图形会话存在。

---

## 1. 现状复用与职责边界

### 1.1 复用资产（不重做）

| 现有资产 | 位置 | 桌面应用如何复用 |
|---|---|---|
| 可见光主链路：原生 GStreamer `rtspsrc→rtph265depay→h265parse→mppvideodec→rgaconvert→BGR appsink` + 独立取流线程 | `code/main.cc`（`MainGrabber`） | 复用其“绕开 OpenCV、原生 GStreamer C API、坏帧计数重连”的取流语义 |
| 热成像链路：`rtspsrc→decodebin→NV12 appsink` + librga `imcvtcolor(NV12→BGR, BT601_FULL)` | `code/main.cc`（`ThermalGrabber`、`nv12_to_bgr_rga`） | 复用 NV12→BGR 的 RGA 转换与双成功码判定（`SUCCESS(1)`/`NOERROR(2)`） |
| 双模型推理（yolov8s-seg 轨道 ROI + yolov5s 人形）、后处理、in-Rail-mask 判定 | `code/main.cc`、`rknpu2_postprocess.cc`、`yolov5_*` | 抽为 `railhmi-ai` 推理服务；模型与后处理逻辑不重写 |
| ONVIF SystemReboot 看门狗（零依赖 SHA1/b64/socket）、30s 坏帧滑窗、preroll 失败阈值 | `code/main.cc` | 抽为设备管理模块的看门狗能力 |
| PTZ 变焦/聚焦 CGI、ONVIF 探测/重启 | `tools/ptz_control.py`、`tools/onvif_test.py` | 作为云台接口契约来源，C++ 侧重实现 |
| PC 观看器诊断字段（FPS 滑窗、空帧过滤、gap>800ms、重连） | `tools/cam_viewer.py` | 直接作为流状态模型字段定义 |
| 设计令牌 | `docs/hmi/design_tokens.yaml` | 构建期生成 `Tokens.qml`/`tokens.json`，QML 不硬编码 |
| 页面线框 | `docs/hmi/mockups/*.svg` | 作为 P1..P6 布局参考（非像素切图） |

### 1.2 职责边界（不得越界）

| 层 | 归属 | 桌面应用的关系 |
|---|---|---|
| 内核/BSP/驱动/rootfs（Mali、MPP、RGA、NPU 驱动、sysctl UDP 缓冲、CPU governor、systemd、网络/磁盘） | 平台职责 | 只消费；发现缺包/缺驱动/权限问题记为依赖项，由平台处理 |
| 视频接入（RTSP 会话、解码、NV12 色彩转换正确性、GStreamer 插件） | 视频模块职责 | 消费其产出的帧与状态；取流管线参数在应用侧配置，但解码/色彩正确性不由桌面应用定义 |
| 视觉模型（模型文件、推理运行时调用、后处理、精度/误报） | 模型职责 | 消费 `模型版本/耗时/框/掩码/置信度`；报警规则的多帧确认与级别映射由应用事件层负责 |
| HMI 交互与视觉 | 前端设计职责 | 严格按交互方案与 `design_tokens.yaml` 实现，不自行改需求/配色/级别映射 |
| 桌面应用 | 本 capability | UI、状态模型、报警/事件、录像编排、回放、持久化、配置、权限、健康呈现、打包与自启 |

---

## 2. 目标与约束

### 2.1 目标

在 RK3588 车机上交付一个可长期无人值守运行的 6 路监控桌面应用：司机主界面（左-前-右三联 + 关键状态 + 报警闭环 + 一键回放），维护/研发可见全摄像头、AI 调试、设备维护；报警事件可追溯（时间/来源/算法/快照/录像）；断流、冻结、AI 退出、录像异常、容量不足均显式提示并可恢复；断电重启后自动恢复。

### 2.2 硬约束（来自项目事实）

1. **磁盘有限**：板端 `/` 57G 已用 24G（约 33G 可用）。6 路按 ~4-5Mbps 估算连续录像约 12GB/小时，**本机不可能长期连续录 6 路**。PRD 明确连续录像由 NVR 承担（`README.md` §2.4：NVR 已录 .64/.65，多消费者并发 OK）。因此桌面应用的本地存储职责收敛为**报警前后保护片段 + 快照 + 事件/日志元数据**，连续录像通过 `IRecordingProvider` 消费 NVR（接口待确认）。
2. **算力有限**：`README.md` §1 明确“算力只够显示，不够 7 路全检”。推理按 `ai_enabled` 只对 CAM-01/04/05（可含 06）运行，其余仅显示；NPU 预算需守卫。
3. **取流约束**：全部相机**强制 UDP**（TCP 卡 SETUP/401）；密码≠网页密码；板端 OpenCV 4.5.4 **不能拉 NV12 流**，必须走原生 GStreamer（现有架构）。
4. **色彩约束**：热成像/红外 NV12→BGR 必须走 librga BT601_FULL，转换成功后 BGR 三通道均值近似相等；绿色伪影帧需过滤（`Y mean < 10.0` + 绿色三通道判据）。
5. **图形后端**：现有为 X11（`DISPLAY=:0`）；Mali GPU 提供 GLES。Qt 平台插件与 GL 共享方式是**待核验依赖**。
6. **CPU governor**：`performance` 常驻服务不得回退（FPS 双峰根因）。
7. **无交叉编译基座**：项目既定模式是**板端本机编译**（aarch64 本地 gcc 11.4 / cmake 3.22.1，见 `README.md` §3.1），无独立交叉工具链。桌面应用沿用该模式。

---

## 3. 运行环境分解（主机 / 目标构建 / 目标运行 分开记录）

### 3.1 主机开发环境（Windows PC，非目标）

- 用途：源码编辑、静态阅读、UI 布局与状态在 **x86_64 Qt 开发预览** 下的迭代、设计还原走查、代码评审。
- 预览构建：`cmake -DRAILHMI_SIMULATION=ON`，使用**模拟输入**（录制帧/合成帧 + 模拟检测结果 + 模拟状态事件），不链接 NPU/RGA/真实相机。
- **明确**：主机可运行 **不等于** 目标可运行。预览不验证 NPU、Mali、MPP、RGA、UDP 取流、磁盘与 systemd。

### 3.2 目标构建环境（RK3588 板端本机）

- 系统：Ubuntu 22.04.5 aarch64，内核 6.1.141；`aarch64-linux-gnu-gcc` 11.4、cmake 3.22.1、pkg-config。
- 既有库：GStreamer 1.20.3（含 `app`/`video`）、OpenCV 4.5.4（系统 deb）、librga 1.10.1、librknnrt 2.3.2、Python 3.10。
- 待核验新增依赖：Qt（5.15 优先，见 §4）、`gstreamer1.0-qt5`（GstGLVideoItem/qmlglsink）、SQLite3、可能的 `libqt5gui5` xcb/eglfs 插件。
- 构建目录：`~/rknn_model_zoo/` 之外**新建独立工程目录**（例如 `/home/admin/railhmi/src`），不改动 rknn_model_zoo 官方树；编译在板端本机执行。

### 3.3 目标运行环境（车机）

- 图形：X11 会话，`DISPLAY=:0`；显示器或 VNC；分辨率 1920×1080 主档 / 1280×720 最低档。
- 输入：触控 + 鼠标 + 键盘（触控是否存在为待核验 V-01）。
- 服务：systemd 常驻 + 开机自启 + `Restart=always`；已有 `cpu-performance.service` 不得回退。
- 网络：`eth1`（192.168.1.200/24 static）接相机网段；SSH 走 `wlan0`。交换机 A/B 千兆（7 路终态要求，`README.md` §2.4）。
- 运行目录（建议）：`/home/admin/railhmi/`（`bin/ config/ models/ resources/ data/ logs/`）。

### 3.4 环境事实核验清单（本轮未执行；由实现/平台按授权执行并留证）

| 编号 | 待核验事实 | 核验命令（板端） | 期望证据 |
|---|---|---|---|
| E-01 | Qt 版本与平台插件 | `qmake -v`、`dpkg -l \| grep -i qt5`、`ls /usr/lib/aarch64-linux-gnu/qt5/plugins/platforms/` | 版本号 + 存在 `libqxcb.so`（或 `libqeglfs.so`） |
| E-02 | GStreamer Qt sink | `gst-inspect-1.0 qmlglsink`、`dpkg -l \| grep gstreamer1.0-qt` | 插件存在及 caps |
| E-03 | GL/EGL 能力 | `eglinfo`（或 `glxinfo -B`）、`cat /proc/driver/mali/*` | GLES 版本、renderer |
| E-04 | MPP 解码 + GL 共享 | `gst-inspect-1.0 mppvideodec glupload glcolorconvert`；实测 `mppvideodec ! glupload ! glcolorconvert ! qmlglsink` 协商 | 协商成功/失败与替代 sink |
| E-05 | RGA | `gst-inspect-1.0 rgavideoconvert`；`ldd` 确认 librga | 版本 1.10.1、可加载 |
| E-06 | NPU 运行时 | `cat /sys/kernel/debug/rknpu/version`；运行 rknn demo | 2.3.2、Core 占用可读 |
| E-07 | SQLite | `sqlite3 --version`、`ls /usr/lib/aarch64-linux-gnu/libsqlite3*` | 版本 |
| E-08 | 显示与自启 | `echo $DISPLAY`、`systemctl --user`/`graphical.target` 挂载点 | 图形会话用户与启动时序 |
| E-09 | 磁盘 | `df -h /`、`du -sh /home/admin` | 可用容量与预留阈值 |
| E-10 | 相机可达与凭据 | 逐路 RTSP 探测（UDP） | 六路 URL/编码/密码（.162/.163/.165/.167 凭据未知） |
| E-11 | NVR 回放/导出接口 | ONVIF/HTTP 探测或厂商文档 | 回放能力或“不可用”结论 |

> E-01..E-09 属构建前置依赖核对；E-10/E-11 属现场/网络事实。未核验项在设计与任务中标注为“未核验”，不得写成已通过。

---

## 4. 技术选型与决策记录

### 4.1 决策：Qt 5.15 QML（Qt Quick）+ C++ 后端 + GStreamer + SQLite

| 维度 | 选择 | 理由 |
|---|---|---|
| UI 框架 | Qt 5.15 QML | Ubuntu 22.04 官方仓库自带，板端本机 `apt` 可装，无需交叉工具链；QML 声明式状态驱动契合交互方案的状态机/角色路由；触控+键盘成熟 |
| 视频 | GStreamer（沿用现有链路）+ `GstGLVideoItem` | 复用现有取流/解码/RGA；GL sink 可把解码帧直接作为 QML 纹理合成，避免 6 路 CPU 拷贝 |
| 推理 | librknnrt 2.3.2 复用现有双模型代码 | 与板上运行时严格匹配（勿换版本） |
| 存储 | SQLite（WAL）+ 文件片段/快照 | 单文件、断电相对安全（WAL）、易检索与审计 |
| 配置 | YAML/JSON 配置 + 运行时校验 | PRD 非功能“可维护性”：相机/阈值/ROI/预置位/策略配置化，不改代码 |
| 图形后端 | X11/xcb（首选），EGLFS（无桌面会话时的备选） | 与现有 `DISPLAY=:0` 一致；EGLFS 作为 E-01 失败时的回退 |

### 4.2 备选方案与否决理由

| 备选 | 否决/降级理由 |
|---|---|
| Electron/Web 前端 | 需额外 Chromium 运行时、磁盘与内存开销大、硬解集成复杂；板端磁盘紧张 |
| GTK4 | 可行但 GStreamer GL 合成与团队/项目现有栈无积累；Qt 资源更充足 |
| 纯 OpenCV highgui | 仅适合单窗口 demo，无法承载 6 页面、角色、列表、时间轴、多路合成 |
| Qt 6 | Ubuntu 22.04 为 Qt 6.2，`gstreamer1.0-qt6` 插件在 22.04 仓库不可靠（Qt5 有 `gstreamer1.0-qt5`）；选 Qt5.15 更稳，Qt6 列为未来升级项 |
| 多进程全拆（UI/媒体/AI/录像各一进程） | v1 复杂度/运维成本高；改用单进程 + 模块边界清晰 + systemd `Restart=always`，必要时按 §5.3 抽离录像进程 |

### 4.3 渲染路径决策（含回退）

- **主路径（首选）**：每路一条 GStreamer pipeline，显示分支 `... ! mppvideodec ! glupload ! glcolorconvert ! qmlglsink`（QML `GstGLVideoItem`/`VideoOutput`）；分析分支经 `tee` → `queue leaky=downstream max-size-buffers=2` → `rgaconvert`（或 `videoconvert`）→ `appsink` 供推理/快照。
- **回退路径（E-04 协商失败或 qmlglsink 不可用）**：分析/显示统一走 worker 内 RGA `NV12→BGR`（复用 `nv12_to_bgr_rga`），在 Qt 渲染线程用 `QSGTexture`/`QQuickFramebufferObject` 上传纹理；对非前方瓦片做显示帧率上限（如 12–15fps）以约束 CPU/内存带宽。
- 决策门：任务清单中的“渲染路径选型 spike”必须先以 E-04 实测决定主/回退，结果写入构建环境核对记录；未实测前不得把主路径当既成事实。

### 4.4 选型风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| Mali GL 与 GStreamer GL 上下文共享不成立 | 无法零拷贝合成，CPU/带宽升高 | 回退路径（§4.3）；瓦片降帧 |
| Qt 包体积/依赖与磁盘 | 安装失败或占满磁盘 | 安装前 `df` 核对；仅装 `qtbase5-dev qtdeclarative5-dev qml-module-qtquick*` 等最小集 |
| 6 路 720p@25 合成与叠加的 GPU/内存带宽 | 掉帧、UI 卡顿 | 显示帧率分级、非前方降帧、叠加层尽量在 QML 层做 |
| NPU 并发 4 路模型导致算力/温升 | 推理延迟、降频 | 每路推理降频（如每 5 帧一次，沿用 demo `inference_interval`）、NPU 预算守卫、温度联动降载 |
| 板端本机编译耗时长 | 迭代慢 | 增量构建；分离 `railhmi-ai` 仅重编改动模块 |
| 显示器/触控实际参数未知 | 布局/字号返工 | 令牌化 + 两档适配；待 V-01 确认 |

---

## 5. 逻辑架构

### 5.1 分层

```
┌──────────────────────────────────────────────────────────────────────┐
│ UI 层 (Qt5 QML)  P1实时监控 P2报警中心 P3历史回放 P4全摄像头 P5AI调试 P6设备维护
│  组件: Button/StatusChip/AlarmCard/AlarmBanner/CameraTile/StatusStrip/Timeline
│  Theme 单例 ← 构建期由 design_tokens.yaml 生成 Tokens.qml/tokens.json
│  仅消费 ViewModel/StatusModel（QObject 属性+信号），不做阻塞 IO
├──────────────────────────────────────────────────────────────────────┤
│ 应用层 (C++ ViewModel / 状态发布)
│  StatusModel(单例, 唯一真值)  PageRouter+RolePolicy  AlarmBannerModel
│  ConfigService  PermissionService  RecoveryController
├──────────────────────────────────────────────────────────────────────┤
│ 领域层
│  EventManager(报警/事件/确认/抑制/快照/持久化)
│  RuleEngine(ROI内目标/多帧确认/跟踪ID/距离/级别映射PRD§8.2)
│  RecordingCoordinator(IRecordingProvider + ClipRecorder + 缺口记录)
│  HealthMonitor(摄像头/AI/录像/网络/温度/磁盘)  MaintenanceLog
├──────────────────────────────────────────────────────────────────────┤
│ 媒体/设备层
│  CameraRegistry(配置)  StreamManager(每路 CameraStream + 状态机)
│  GstPipelineFactory  FrameBuffer(有界)  AIInferenceService(每路 worker)
│  PtzClient(HTTP CGI + ONVIF)  OnvifWatchdog(复用demo)  SnapshotEncoder
├──────────────────────────────────────────────────────────────────────┤
│ 基础设施层
│  SqliteStore(单写线程, WAL)  SegmentFileStore  LogService(结构化,按日滚动)
│  ClockService(单调时钟↔墙钟, DATA-001)  BoundedQueue/Backpressure
└──────────────────────────────────────────────────────────────────────┘
                     │ 消费，不拥有
   OS/BSP/驱动(Mali/MPP/RGA/NPU) | 视频模块(GStreamer/RGA) | 模型(双 rknn 模型)
```

### 5.2 模块与目录建议

```
app/
  CMakeLists.txt                 # 单一工程，多 target
  build.sh                       # 板端本机构建入口
  deploy.sh                      # 板内安装到 /home/admin/railhmi
  src/
    main.cpp
    core/      config/ status_model/ event_bus/ storage/ logging/ permissions/ clock/
    media/     camera_registry/ camera_stream/ pipeline_factory/ frame_buffer/ quality/
    ai/        inference_service/ roi/ tracker/ confirm/ distance/ budget/
    events/    event_manager/ rule_engine/ alarm_map/ suppression/ snapshot/
    recording/ provider_iface/ clip_recorder/ gaps/ storage_policy/
    device/    ptz/ onvif/ watchdog/ health/
    ui/        viewmodels/ theme/ router/
  qml/         pages/ components/ dialogs/
  config/      cameras.yaml  thresholds.yaml  roi.yaml  ptz_presets.yaml  app.yaml
  models/      yolov8s-seg.rknn  yolov5s.rknn  rail_labels.txt   (由 deploy 复制)
  resources/   icons/ (登记来源许可)  tokens.json(生成物)
  tools/       gen_tokens.py     # design_tokens.yaml → tokens.json/Tokens.qml
```

### 5.3 进程/线程模型（非阻塞 UI 为硬要求）

- **进程**：v1 为单进程 `railhmi`。`IClipStore`/`IRecordingProvider` 接口保留；若录像稳定性需要独立生命周期，可抽出 `railhmi-recorder` 进程（WAL 允许并发读），但 v1 不强制拆分。systemd `Restart=always` + 启动自恢复承担崩溃恢复。
- **线程**：
  1. **Qt 主线程**：渲染与事件处理；**禁止**任何网络/磁盘/推理阻塞调用。
  2. **每路 StreamManager 线程**：管理该路 pipeline 生命周期、appsink 拉帧、坏帧/冻结检测、重连状态机；向 `FrameBuffer` 发布最新帧（带 seq）。
  3. **AI 推理线程**：有界队列（深度 1–2，丢旧帧）；同一时刻按 `ai_enabled` 的相机集合轮转；NPU 预算与温度守卫。
  4. **存储写线程**：所有 SQLite 写与片段落盘串行化（单写者，避免锁竞争）。
  5. **健康/看门狗线程**：周期采样 NPU/CPU/温度/网络/磁盘 + ONVIF 复位判定。
  6. **GLib 主上下文线程**（或复用 GStreamer 内部）：集中 `GstBus` 消息（EOS/ERROR/STATE_CHANGED），转成内部事件。
- **有界与背压**：所有跨线程队列有界；分析帧队列 `leaky` 丢旧；事件队列溢出时优先保护高等级报警并记录丢事件计数（可观测）。
- **UI 更新**：worker 只更新模型数据并经 `Qt::QueuedConnection` 发信号，QML 绑定自动刷新。

### 5.4 消息与服务接口（进程内）

| 接口 | 方向 | 载荷（要点） |
|---|---|---|
| `FrameBuffer::latest(seq, timestamp)` | media → ai/snapshot | NV12/BGR 帧句柄或深拷贝 + PTS/墙钟 |
| `DetectionResult` | ai → rule_engine | cam_id, ts, boxes, masks(ROI), conf, cls, model_version, infer_ms |
| `AlarmEvent`（§7.3） | rule_engine → event_manager | 级别/类型/来源/时间/目标/元数据 |
| `StreamStateEvent` | media → status_model/health | cam_id, state, fps, last_frame_ts, reconnect_count, quality_flags |
| `HealthSample` | health → status_model | npu/cpu/temp/net_errors/disk |
| `ClipRequest` | event_manager → recording | cam_id, t0, t1, event_id |
| `ConfigChanged` | config → 全模块 | 增量变更 + 校验结果 |

两者之间不共享可变全局状态；`StatusModel` 是 UI 的唯一真值源。

---

## 6. 视频接入与渲染设计

### 6.1 每路 pipeline（分析 + 显示）

```
rtspsrc(latency=.., protocols=udp, timeout=5s)
  ! rtph265depay/rtph264depay ! h265parse/h264parse
  ! mppvideodec                      # 硬件解码 → NV12
  ! tee name=t
      t. ! queue(leaky, max 2) ! glupload ! glcolorconvert ! qmlglsink(videoItem)   # 显示
      t. ! queue(leaky, max 2) ! rgaconvert/videoconvert ! video/x-raw,format=BGR  # 分析
                                ! appsink(name=analysis_sink, max-buffers=2, drop=true)
```

- 编码自适应：可见光/球机为 H.265 Main，热成像返厂后为 H.264 Baseline；depay/parse 用 `decodebin`-可协商或按配置选择，避免硬编码单编码。
- 强制 UDP：`protocols=udp`；`rtspsrc` 超时参数化；重连采用“相机自然超时 + 置 NULL 重建会话”（现有实现如此，避免无 TEARDOWN 的 kill 诱发相机坏状态）。
- 分辨率/比例：显示按源比例 letterbox，不拉伸、不裁剪内容（视觉规范 §7.5）。三联几何校正/裁剪仅用于“视觉连续”，不做像素级拼接。
- 统一时间基准：为每帧记录 `arrival_monotonic`，经 `ClockService` 映射到墙钟 `wall_ms`；用于三路同步回放与事件时间（DATA-001）。

### 6.2 流状态机（与交互方案 §7.1 一致）

```
OFFLINE ──启动/重连──► CONNECTING ──首帧──► LIVE
   ▲                       │失败               │ 超时无新帧(冻结)
   │                       ▼                   ▼
   └──── 达重连阈值 ◄── RECONNECTING ◄── FROZEN / DEGRADED
```

- 判定参数（配置化，默认值参考现有实现）：首帧超时、无新帧冻结阈值（`cam_viewer.py` 的 gap>800ms 作为卡顿观测，冻结需更长窗口）、重连阈值与退避、达阈值升级设备报警（HLT-006）。
- 硬约束：**非 LIVE 禁止把旧帧当实时**；必须渲染状态占位 + 最近帧时间戳。
- 质量标志：过暗/过曝/遮挡/模糊/绿色伪影/坏帧率（HLT-003、热成像坏帧过滤判据）；`DEGRADED` 计为“降级可用”。
- 前方主摄像头离线/冻结 → 前方 AI 状态切 `NOT_READY`，产生二级设备报警（PRD §8.2）。

### 6.3 显示帧率分级与 6 路并发

- 前方（CAM-01）：目标全帧率（≤25）。
- 两侧（CAM-02/03）：目标 15–25，按实测带宽调整。
- P4 全摄像头页与 P1 若同屏，复用同一 `GstGLVideoItem` 纹理避免重复解码（同一 pipeline 可服务多视图，或按页面可见性暂停不可见分支）。
- 页面不可见时降载：暂停分析分支/降显示帧率；切换页面不重建会话（避免 TEARDOWN 抖动）。
- GL 内存/纹理池上限与异常回收；解码器 close 时释放 RGA/NPU 资源。

### 6.4 快照

- 报警快照（ALM-006）：从分析帧或显示帧截取，走 RGA/JPEG（注意 libgdal 冲突：板端 cv2 的 JPEG/PNG 不可用，用 BMP 或独立编码路径——`README.md` 坑 6）。
- 快照与事件关联存储，P2 可查看快照。

---

## 7. 状态模型与 UI 数据契约

### 7.1 状态发布

`StatusModel` 为 QML 唯一数据源，暴露：
- `cameras[]`: `id,name,position,ai_enabled,state,fps,bitrate,resolution,last_frame_ts,quality_flags,reconnect_count`
- `ai[]`: `status,model_version,fps,latency_ms,last_result_ts`
- `alarms`（当前，最高级优先）、`alarmHistory`
- `recording`: `provider_status,segments,critical_segments,gaps,storage_free,storage_total`
- `device`: `npu_load,cpu,temp_c,net_errors,disk`
- `ptz`（按当前选中/前方）: `pan,tilt,zoom,preset,homing_state,tolerance_ok`
- `session`: `role,mode(visible|ir|auto),mute,time,speed_available/speed`
- `recovery`: `in_progress,steps[]`

字段命名与交互方案 §7.2 一一对应；未接入项显式 `未接入/不适用`。

### 7.2 页面路由与角色

- 侧边/底部固定 6 项，按角色过滤渲染（权限不可见项**不渲染**，非禁用可见）。
- 司机默认 P1；受限页直接访问显示“无权限”说明 + 返回，不空白崩溃。
- 从 P1 报警卡进入 P3 时锁定来源摄像头与报警时刻；返回 P1 保留来源上下文。
- 报警横幅（z=400）跨页面常驻；自动弹窗（z=300）不遮挡前方中央区。

### 7.3 报警/事件数据结构

```
AlarmEvent {
  id            // 唯一，时间序
  level         // 1|2|3（映射见 PRD §8.2，不可被 UI 改写）
  type          // front_obstacle|person_on_rail|panto_abnormal|wheel_derail|door_ajar|device_*
  source_cam    // CAM-01..06
  ts_wall_ms, ts_mono_us
  track_id      // 同一目标抑制与更新（ALM-005）
  confidence, distance_m, algo, model_version
  description
  ack_state     // unacked|acked（确认不改事件存在性，ALM-004）
  record_available  // 录像不完整/不可用显式标记（PLY-008）
  snapshot_ref
}
```

抑制：同一 `track_id` 在配置窗口内不新增独立事件，只更新距离/状态/持续时间与最后时间。

---

## 8. 报警/事件层

- 输入：`DetectionResult`（多帧跟踪后）+ 设备/流状态异常 + 健康阈值。
- 规则链（FWD-001/005/007）：轨道 ROI（rail mask）判定 → 目标在/接近危险区 → 多帧确认（帧数/时间阈值配置）→ 跟踪 ID → 级别映射（PRD §8.2）→ `AlarmEvent`。
- 输出动作：更新 `StatusModel` 当前报警与横幅 → 触发快照（ALM-006）→ 触发保护片段（PLY-002 前 30s/后 60s，可配置）→ 入库（事件+目标+元数据）→ 声音策略（分级，一级不可静音为无视觉）。
- 确认：只改 `ack_state`，一级声音停止、横幅降级可关闭，事件保留可检索（ALM-004）。
- 声音：PRD Q-07 未决；抽象 `IAudioNotifier`，未接入时仅视觉并在 UI 标注声音通道未接入。

---

## 9. 录像与回放

### 9.1 抽象

```
IRecordingProvider
  querySegments(cam, t0, t1) -> segments[] | unavailable
  queryGaps(cam, t0, t1) -> gaps[]
  playSegment / exportSegment(...)   // 权限受控（PLY-007）
```

- 实现 A `NvrRecordingProvider`：消费 NVR（回放接口待 E-11 确认）；不可用时 `unavailable`，UI 显式标注（PLY-008），不静默。
- 实现 B `ClipRecorder`（本机保证）：每路维护**有界滚动缓冲**（内存环形 + 落盘暂存），报警时把 `[t-30s, t+60s]` 固化为保护片段（MP4/TS，编码优先直通复制，避免转码耗算力）；容量/保留策略配置化。
- 缺口：任何录像中断区间写 `recording_gap` 记录，P3 时间轴斜纹留白。

### 9.2 回放

- 时间轴：连续录像段、报警区段显著色标、缺口斜纹、游标、关键帧缩略图（视觉规范 §7.7）。
- 快速跳转：-30s / 报警时刻 / +30s / +60s（PLY-004）。
- 三联同步：前方报警默认左/前/右同时间轴同步播放（PLY-005），以统一墙钟时间轴驱动多路 seek；三路启动前做时钟对齐，漂移超阈值显示“同步偏差”提示。
- 导出：仅授权角色（PLY-007），二次确认 + 写维护日志（HLT-007）。

### 9.3 存储策略（磁盘有限）

- 报警保护片段优先保留，不被普通循环覆盖；保留周期/容量上限配置。
- 存储低于阈值提前告警（DATA-003），P6 显示剩余容量与预计可录时长。
- 写入采用“先写临时文件→原子重命名”，断电保护（DATA-002）。

---

## 10. 持久化设计

### 10.1 SQLite（WAL，单写线程）

| 表 | 关键列 |
|---|---|
| `events` | id, level, type, source_cam, ts_wall_ms, ts_mono_us, confidence, distance_m, algo, model_version, description, ack_state, record_available, snapshot_ref |
| `event_targets` | event_id, track_id, boxes_json, first_ts, last_ts, duration_ms |
| `maintenance_log` | ts, user, action, target, result, detail |
| `device_health` | ts, npu_load, cpu, temp_c, net_errors, disk_free, disk_total |
| `recording_segments` | id, cam, t0_ms, t1_ms, path, kind(continuous|critical), complete |
| `recording_gaps` | id, cam, t0_ms, t1_ms, reason |
| `stream_events` | ts, cam, event(connect|reconnect|freeze|offline|watchdog), detail |
| `config_kv` | key, value, updated_ts, updated_by |

- 时间：统一 UTC 毫秒 + 单调微秒；启动时记录单调↔墙钟偏移，重启后校准。
- 数据库置于运行目录 `data/`，定期 `VACUUM`/归档；异常时降级为只读并能告警。

### 10.2 文件布局（建议）

```
/home/admin/railhmi/
  bin/railhmi
  config/*.yaml
  models/*.rknn
  resources/
  data/railhmi.db  data/snapshots/  data/clips/  data/tmp/
  logs/railhmi-YYYYMMDD.log
```

---

## 11. 配置设计（不改代码）

| 文件 | 内容 |
|---|---|
| `config/cameras.yaml` | 六路注册表：`id,name,position,rtsp_url,transport(udp),password_ref,ai_enabled,ai_profile,display_fps,tile_aspect` |
| `config/thresholds.yaml` | 多帧确认帧数/时间、危险区接近阈值、置信度、抑制窗口、冻结/重连阈值、存储阈值 |
| `config/roi.yaml` | 各相机 ROI（受电弓固定 ROI、轮轨标定基准、前方危险区） |
| `config/ptz_presets.yaml` | 前方预置位、容差、归位策略、可见光/红外预置位（PTZ-004，若适用） |
| `config/app.yaml` | 角色与默认角色、权限开关、语言、分辨率档、录像保留、日志级别、模拟开关 |

- 启动时 schema 校验，缺失/非法字段给出明确错误并在 P6 显示，不用默认值静默掩盖。
- 相机 IP/URL 只来自配置；UI 不硬编码（交互方案 §7.3）。凭据与密码分离（口令文件权限 600）。
- 热加载：可安全热更新的项（阈值、显示帧率）即时生效；结构变更（增删相机）需重启并在 UI 提示。

---

## 12. 设备/云台/看门狗与健康

- **PTZ**：`PtzClient` 提供绝对/相对移动、变焦、预置位调用、位置读取；CGI 语义参考 `tools/ptz_control.py`。开机归位→调前方预置位→读实际值→容差校验（PTZ-001/002），超差产生设备异常并令前方 AI `NOT_READY`。P6 提供“一键归位/重调前方预置位”（PTZ-005），二次确认 + 日志。
- **ONVIF 看门狗**：复用 demo 的零依赖 ONVIF SystemReboot（30s 坏帧滑窗阈值、preroll 失败阈值、冷却），作用于支持该能力的相机；事件入 `stream_events` 与维护日志。注意 `.64` ONVIF 不校验鉴权、可被任意主机触发——仅在受控网段使用，记录安全提示。
- **健康采样**：NPU `/sys/kernel/debug/rknpu/load`、CPU `/proc/stat` + governor、温度 `/sys/class/thermal/*`、网络 `/proc/net/snmp`（UDP InErrors）、磁盘 `statvfs`。周期写入 `device_health` 并发布到 `StatusModel`（HLT-004/005、DATA-003）。
- **AI 健康**：每路推理上报 `status/model_version/fps/latency/last_result_ts`；AI 退出时相关检测类报警标注“AI 不可用”，不静默。

---

## 13. 外部接口清单与未知事实

| 接口 | 现状/所需事实 | 影响 |
|---|---|---|
| 六路相机 RTSP（UDP） | `.162/.163/.165/.167` 凭据与 URL 未确认（`.65=ipc12345`、`.160=123456`） | 注册表填充与联调（E-10） |
| NVR 回放/导出 | 接口未知（Q-06） | P3 连续回放能力（E-11） |
| 车速/里程/GPS（Q-04） | 是否可从总线读取未知 | 顶部状态条字段与风险分级 |
| 车门方案（Q-05） | 视觉/门磁/融合未定 | 车门状态来源与是否预留开关量接口 |
| 报警声（Q-07） | 内置/外置蜂鸣器未定 | 声音通道选择 |
| 前方光学通道（Q-01） | 双光一体或两套光学未知 | 模式切换与预置位 |
| 触控/屏幕（V-01） | 屏幕尺寸/分辨率/是否触控未知 | 字号档与触控目标 |
| 环境光/眩光（V-02） | 未现场走查 | 深色主题可读性 |
| GNSS/NTP | 统一时间基准来源 | 多路同步与事件追溯（DATA-001） |

未接入项在界面以中性“未接入/不适用”呈现，不虚构数据。

---

## 14. 构建与部署方案

### 14.1 板端本机构建（目标构建）

```bash
# 依赖（板端，需授权；安装前先 df 核对磁盘）
sudo apt-get install -y qtbase5-dev qtdeclarative5-dev qml-module-qtquick2 \
  qml-module-qtquick-controls2 qml-module-qtquick-layouts \
  qtbase5-dev-tools gstreamer1.0-qt5 libsqlite3-dev pkg-config cmake

# 工程构建（板端本机，aarch64 本地 gcc）
cd /home/admin/railhmi/src
./build.sh Release            # 内部: cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
                              #      && cmake --build build -j$(nproc)
```

- 链接：`Qt5::Quick Qt5::Qml Qt5::OpenGL Qt5::Svg`、`gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0`、`librga`、`librknnrt`、`opencv4`、`sqlite3`、`pthread`。
- 不使用交叉编译；不修改 `~/rknn_model_zoo/` 官方树（模型/库从该树或 `install/` 复制引用）。
- 预览构建（主机 x86_64）：`./build.sh Preview` → `-DRAILHMI_SIMULATION=ON`，仅用于 UI/状态开发。

### 14.2 部署

```bash
./deploy.sh    # 安装到 /home/admin/railhmi/{bin,config,models,resources,data,logs}
               # 复制 models/*.rknn、rail_labels.txt、资源与图标（并登记来源许可）
```

### 14.3 常驻与自启

- `railhmi.service`（systemd）：`After=graphical.target`（或依赖实际显示会话）、`Environment=DISPLAY=:0`、`Restart=always`、`RestartSec=3`、资源与工作目录指向运行目录、`StandardOutput` 接 LogService。
- 不自建/不改 `cpu-performance.service`（保持 `performance`）。
- 依赖 `graphical.target` 的时序以 E-08 实测为准；若无人登录图形会话，评估 xcb 不可用时的 EGLFS 回退。
- 升级：版本化运行目录或 symlink 切换 + 保留上一版本回滚；数据库迁移脚本向前兼容。

### 14.4 断电恢复（非功能“可恢复性”）

启动自恢复序列（UI 显示进度）：
1. 加载配置并校验 → 2. 打开数据库（WAL 恢复）→ 3. 启动录像/存储 → 4. 启动各相机取流并入状态机 → 5. 启动 AI（按 `ai_enabled`）→ 6. 云台归位 + 调前方预置位 → 7. 前方 AI `READY` → 8. 发布 `recovery.completed`。

任一步失败：记录 + 显式告警，不阻塞其余步骤（单路非关键异常不导致整体退出）。

---

## 15. 验证策略与范围（严格区分）

| 范围 | 手段 | 能证明 | 不能证明 |
|---|---|---|---|
| 静态/依赖核对 | 阅读、`gst-inspect`、`dpkg`、`ldd`、schema 校验 | 依赖存在与接口匹配（E-01..E-09） | 运行时行为 |
| 主机开发预览（x86_64 模拟输入） | 模拟帧/模拟检测/模拟状态 | UI 布局、状态机、页面路由、令牌一致性、角色隔离 | 目标平台可运行、性能、NPU/RGA/Mali |
| 目标构建（板端 aarch64 编译） | `build.sh` | 目标 ABI 可编译链接 | 运行正确性 |
| 目标运行（板端真实相机） | 由用户/授权执行 | 取流/渲染/推理/报警/回放端到端 | 需现场走查项（眩光、触控） |
| 软件测试 | `software_tester` 计划 | 功能与交互可检验点 | 板上硬件行为 |
| 嵌入式测试 | `embedded_tester` | MCU/固件/嵌入式 Linux/真机联调 | 电气/信号（硬件测试） |

**本轮未执行上表任何运行/编译项**；设备工具已禁用，不假定物理测试已执行。

## 16. 风险登记

| 风险 | 触发条件 | 影响 | 应对 |
|---|---|---|---|
| GL sink 不可用 | E-04 失败 | 需回退渲染，性能下降 | §4.3 回退路径 + 瓦片降帧 |
| 6 路合成性能不足 | 实测掉帧 | 司机视图卡顿 | 显示帧率分级、页面卸载、纹理复用 |
| NVR 接口不可得 | E-11 无结论 | P3 连续回放受限 | 本地保护片段保证报警闭环；UI 显式标注不可用 |
| 磁盘耗尽 | 保护片段堆积 | 录像失败 | 保留策略 + 阈值提前告警 + 保护片段上限 |
| NPU/温度过载 | 多路推理 | 延迟/降频 | 推理降频、预算守卫、温度联动 |
| 相机凭据/编码未知 | E-10 | 无法联调 | 配置化，待现场确认；缺项不静默 |
| 图形会话时序 | E-08 | 自启失败 | systemd 依赖实测调整；EGLFS 回退 |

## 17. 交接

- 给 `desktop_app_implementation`：按 §5 架构与 `docs/hmi/desktop_app_tasks.yaml` 有序实现；渲染路径先做 spike（§4.3/§4.4）；严格消费 `design_tokens.yaml` 与交互/视觉规范，不自行改需求。
- 给 `software_tester`：§15 验证范围与交互方案 §11、视觉规范 §10 为可检验点输入，不替代测试计划。
- 给 `embedded_tester`：目标运行与真机联调（取流稳定性、渲染、NPU、断电恢复）。
- 给平台职责：§3.4 依赖核验中的 rootfs/BSP 缺项（Qt 包、Mali/MPP/RGA、sysctl、governor、显示会话）由平台按授权处理。
- 待确认项：§13 与 PRD §14 / 交互 §12 / 视觉 §13，保持“未核验/未接入”标注，不虚构事实。
