# 轨道车辆智能视觉辅助监控系统 — 桌面应用架构与构建运行方案

- Artifact: DesktopAppDesignArtifact（桌面架构依赖及构建运行方案）
- Capability: `embedded_linux_engineer.desktop_app_design`
- 版本: v2（2026-10-15 需求更新后复核，替代 v1）
- 上游输入（本次解析的当前版本）:
  - `docs/hmi/INTERACTION_DESIGN.md`（FrontendDesignArtifact **v2**，n001，verified）
  - `docs/hmi/VISUAL_DESIGN.md` + `docs/hmi/design_tokens.yaml`（UIVisualDesignArtifact **v2**，n002，verified）
  - `功能需求.docx`（PRD V0.1）、`一些说明和要求.txt`（2026-10-15 更新）、`README.md`（交接现状，2026-10-13）、`PROJECT_GUIDE.md`、`troubleshooting_cameras_demo.md`
  - 现有实现与证据：`code/`（板端双模型 demo）、`tools/`（PTZ/ONVIF/取流/SSH）、`app/tools/env_probe.py`、`app/spikes/render_path/`、`docs/hmi/build_env_check.md`、`docs/hmi/render_path_spike.md`、`docs/hmi/test_evidence/`
- 面向下游: `embedded_linux_engineer.desktop_app_implementation`（按本方案与任务清单实现）、`software_tester`（测试设计输入）、`embedded_tester`（板上联调输入）

---

## 0. 交付边界、版本变更与本期范围

### 0.1 本 capability 交付什么

- 交付：桌面应用的技术选型与理由、运行/构建环境分解、模块与进程/线程架构、媒体渲染与推理接入、状态与数据契约、检测职责与报警层、录像/回放/持久化/配置/健康设计、构建部署方案、分阶段验证策略、依赖与未知事实清单，以及有序实现工作项清单（`docs/hmi/desktop_app_tasks.yaml`）。
- 不交付：桌面运行代码（归 `desktop_app_implementation`）、视觉稿（归 `ui_visual_design`）、模型算法与精度（归模型职责）、BSP/驱动/rootfs/系统调优（归平台职责）、部署执行与整机验收。

### 0.2 相对 v1 的变化（本次复核结论）

| # | 变化 | 原因 |
|---|---|---|
| 1 | **本期明确只接入 6 路可见光、不接入红外**。v1 中红外/热成像双光链路、`mode(visible\|ir\|auto)` 切换、可见光/红外预置位、绿色伪影过滤均降级为“本期不实现，保留配置位与接口位”。 | 交互方案 v2 §0.4、PRD §13、`一些说明和要求.txt` 2026-10-15 更新 |
| 2 | **各摄像头检测职责进入注册表并驱动 AI/规则层**（`ai_enabled` + `ai_profile`）：前方障碍/人员、车顶受电弓三态、车底轮轨、司机室车门；左右侧仅提供大视野、不检测。 | `一些说明和要求.txt` 第 10/11 条、交互方案 v2 §7.3 |
| 3 | **NVR（`192.168.1.4`）作为已登记设备进入设备维护与录像来源**，账户/协议/6 路通道映射未提供时按 `未配置/未核验` 中性呈现，不虚构通道号，界面不提供自动修改 NVR 配置入口。 | 交互方案 v2 §0.4/§5.6、视觉规范 v2 §3.6/§7.11 |
| 4 | 数据契约新增 `ai_profile`、`detection_badge`、`integration_state(probing/reachable/unreachable/unconfigured/unverified)`、`mode.visible/ir(未接入)`；新增 `DeviceCard`（NVR）与 `DetectionBadge` 组件。 | 交互方案 v2 §7.2、视觉规范 v2 §3.6/§7.11/§7.12 |
| 5 | **环境事实按实际核验修正**：ENV-A 主机预览在**当前主机不可执行**（无 C/C++ 工具链、无 GStreamer、Qt 仅 Anaconda x86_64 且无平台插件）；板端与相机/ NVR 授权缺口如实登记。v1 “ENV-A 本轮可执行”一句作废。 | `docs/hmi/build_env_check.md` §3/§12 主机实测、Runtime 环境与授权 |
| 6 | **架构新增 `railhmi-core` 无 GUI/无媒体依赖层**，把配置、状态机、规则/事件、级别映射、保留策略等业务逻辑与 Qt/GStreamer 解耦，使最大量业务行为可在**任意有 C++ 编译器的主机**上单元测试，不再被板端/相机授权卡死。 | 历史实现连续 WORK_FAILED 的根因是首批工作项只有板端可观察点；修复面向通用机制 |
| 7 | 渲染路径维持 v1 的**双路径 + 未实测不预设**，默认 `cpu`（已有板端证据），`gl` 为可开启候选；依据 `render_path_spike.md` §7 强制约束。 | ITEM-002 实测未执行（UNMEASURED）+ `VERIFY-ITEM-002.md` TEST_FAIL |

> 除以上 7 点外，v1 经 `software_verification`/`ui_implementation_review` 认可的架构（Qt5.15 QML + C++、GStreamer 原生链路、SQLite WAL、systemd 常驻、三阶段环境分层、验证不得跨层外推）保持不变。

### 0.3 研究/验证声明（严谨口径）

- 本 capability 本轮**未连接设备、未在板端编译、未执行物理测试**；设备工具后端已禁用，不得假定物理测试已发生。
- 环境结论只在 §3 核对表中按“已核实可用 / 未核验 / 有证据的缺口”三态登记。`configured_unverified` 只表示配置与工具要求满足，**不证明在线、依赖安装成功或测试通过**。
- 现有代码状态来自对 `README.md`/`code/` 的静态阅读，不是本轮实测。
- **主机可运行不等于目标可运行**；反之，主机缺少某工具也**不等于**目标板缺该工具。单个工具后端关闭不证明全部目标资源缺失。

### 0.4 平台判定（沿用交互方案 v2 §0.1，不重判）

本 HMI 是运行在列车 **RK3588 车机上的本地桌面应用**（X11，`DISPLAY=:0`，显示器/VNC），不是浏览器 Web 应用。现有 demo `code/main.cc` 用 OpenCV highgui 在 X11 显示，证明图形会话存在。本期只覆盖 6 路可见光，不为红外/模式切换设计可交互控件。

---

## 1. 现状复用与职责边界

### 1.1 复用资产（不重做）

| 现有资产 | 位置 | 桌面应用如何复用 |
|---|---|---|
| 可见光主链路：原生 GStreamer `rtspsrc→rtph265depay→h265parse→mppvideodec→rgaconvert→BGR appsink` + 独立取流线程、坏帧计数重连 | `code/main.cc`（`MainGrabber`） | 复用“绕开 OpenCV、原生 GStreamer C API、坏帧/冻结检测重连”的取流语义（本期唯一取流语义） |
| NV12→BGR 的 RGA 转换与双成功码判定（`SUCCESS(1)`/`NOERROR(2)`） | `code/main.cc`（`nv12_to_bgr_rga`） | 回退渲染路径与推理前处理的色彩转换；本期用于可见光 NV12，不再用于红外 |
| 双模型推理（yolov8s-seg 轨道 ROI + yolov5s 人形）、后处理、in-Rail-mask 判定 | `code/main.cc`、`rknpu2_postprocess.cc`、`yolov5_*` | 抽为 `railhmi-ai` 推理服务；模型与后处理逻辑不重写；按 `ai_profile` 扩展职责 |
| ONVIF SystemReboot 看门狗（零依赖 SHA1/b64/socket）、30s 坏帧滑窗、preroll 失败阈值 | `code/main.cc` | 抽为设备管理模块的看门狗能力 |
| PTZ 变焦/聚焦 CGI、ONVIF 探测/重启 | `tools/ptz_control.py`、`tools/onvif_test.py` | 云台接口契约来源，C++ 侧重实现 |
| PC 观看器诊断字段（FPS 滑窗、空帧过滤、gap>800ms、重连） | `tools/cam_viewer.py` | 流状态模型字段定义 |
| 依赖探针与证据流程 | `app/tools/env_probe.py`、`run_env_check.sh`、`build_env_check.md` | 直接作为 E-01..E-13 核验入口，不另造一套 |
| 渲染路径 spike 与判据 | `app/spikes/render_path/`、`render_path_spike.md` | 渲染选型决策门；未实测前保留双路径 |
| 设计令牌 | `docs/hmi/design_tokens.yaml` | 构建期生成 `Tokens.qml`/`tokens.json`，QML/C++ 不硬编码 |
| 页面线框 | `docs/hmi/mockups/*.svg` | P1..P6 布局参考（非像素切图） |

### 1.2 职责边界（不得越界）

| 层 | 归属 | 桌面应用的关系 |
|---|---|---|
| 内核/BSP/驱动/rootfs（Mali、MPP、RGA、NPU 驱动、sysctl UDP 缓冲、CPU governor、systemd、网络/磁盘） | 平台职责 | 只消费；缺包/缺驱动/权限问题记为依赖项 |
| 视频接入（RTSP 会话、解码、NV12 色彩转换正确性、GStreamer 插件） | 视频模块职责 | 消费其产出的帧与状态；取流参数在应用侧配置，解码/色彩正确性不由桌面应用定义 |
| 视觉模型（模型文件、运行时调用、后处理、精度/误报、各相机替代模型） | 模型职责 | 消费 `模型版本/耗时/框/掩码/置信度`；报警规则的确认与级别映射由应用事件层负责 |
| HMI 交互与视觉 | 前端设计职责 | 严格按交互方案 v2 与 `design_tokens.yaml` 实现，不自行改需求/配色/级别映射 |
| 桌面应用 | 本 capability | UI、状态模型、AI 结果消费与检测职责调度、报警/事件、录像编排、回放、持久化、配置、权限、健康呈现、打包与自启 |

---

## 2. 目标与约束

### 2.1 目标

在 RK3588 车机上交付一个可长期无人值守运行的 6 路可见光监控桌面应用：司机主界面（左-前-右三联 + 关键状态 + 报警闭环 + 一键回放），维护/研发可见全摄像头、AI 调试、设备维护；按各相机检测职责输出报警事件并可追溯（时间/来源/算法/快照/录像）；断流、冻结、AI 退出、录像异常、容量不足均显式提示并可恢复；断电重启后自动恢复。

### 2.2 硬约束（来自项目事实与本期授权）

1. **磁盘有限**：板端 `/` 57G 已用 24G（约 33G 可用，文档基线）。6 路按 ~4-5Mbps 估算连续录像约 12GB/小时，**本机不可能长期连续录 6 路**。连续录像由 NVR 承担（`README.md` §2.4）；桌面应用本地存储职责收敛为**报警前后保护片段 + 快照 + 事件/日志元数据**，连续录像通过 `IRecordingProvider` 消费 NVR（接口待确认）。
2. **算力有限**：`README.md` §1“算力只够显示，不够 7 路全检”。推理只对 `ai_enabled` 的相机运行（本期 CAM-01/04/05，CAM-06 视门磁方案可选）；左右侧（CAM-02/03）仅显示；NPU 预算需守卫。
3. **取流约束**：全部相机**强制 UDP**（TCP 卡 SETUP/401）；密码≠网页密码；板端 OpenCV 4.5.4 **不能拉 NV12 流**，必须走原生 GStreamer。
4. **色彩约束**：NV12→BGR 必须走 librga BT601_FULL，转换成功后 BGR 三通道均值近似相等（可见光同判据）。
5. **图形后端**：现有为 X11（`DISPLAY=:0`）；Mali GPU 提供 GLES。Qt 平台插件与 GL 共享方式是**待核验依赖**。
6. **CPU governor**：`performance` 常驻服务不得回退（FPS 双峰根因）。
7. **无交叉编译基座**：项目既定模式是**板端本机编译**（aarch64 本地 gcc 11.4 / cmake 3.22.1），无独立交叉工具链；桌面应用沿用该模式。
8. **本期无红外**：不实现双光/模式切换/红外预置位；`mode` 固定 `可见光`，`红外` 显示“未接入”。

---

## 3. 环境、授权与依赖现状核对（本次唯一环境口径）

> 本节是本次能力的环境与授权汇总。环境事实只来自 Runtime `environment.yaml`，授权只来自 Runtime `permissions.yaml`（合成为 Unified Project Environment And Authorization）；其他资料只用于提出补全建议。`configured_unverified` 表示配置与工具要求满足，不证明在线、依赖安装成功或测试通过。

### 3.1 三阶段环境（开发 / 平台 / 目标，分开记录）

| 阶段 | 定义 | 本轮状态 | 能证明 | 不能证明 |
|---|---|---|---|---|
| **ENV-A 主机逻辑与预览** | 主机编译 `railhmi-core` 单元测试；可选 Qt 预览（`-DRAILHMI_SIMULATION=ON`，模拟帧/检测/状态） | **有证据的缺口**：主机当前无 gcc/g++/cmake/pkg-config、无 GStreamer、无 Qt 平台插件（Qt 仅 Anaconda x86_64 5.15.2）。可用的仅为 Python/Node/sqlite3。 | 有工具链后：业务逻辑、配置校验、状态机、规则/事件、持久化、令牌一致性、UI 布局 | 目标平台可运行、NPU/Mali/MPP/RGA/UDP 取流、真实性能 |
| **ENV-B 目标本机构建** | RK3588 上 `./build.sh Release` 产物存在、链接通过 | **有证据的缺口**：`rk3588_build` 当前 `unavailable_for_current_capability`（缺 `remote_build` 授权） | 目标 ABI 可编译链接、二进制可启动 | 运行正确性 |
| **ENV-C 目标运行 + 文件/模拟源** | 板上真实图形会话，帧源用录制文件循环/合成，不接真机相机 | 未核验（待 ENV-B + 设备授权） | 板上渲染主/回退路径、6 路合成真实负载、状态机上板行为 | 真实相机行为、真实网络抖动 |
| **ENV-D 目标运行 + 真机相机/NVR** | 走 `eth1` 相机网段，真实 RTSP/NPU/PTZ | 未核验；`read_camera_stream` 当前不可用 | 取流端到端、色彩、推理指标、云台容差、真实断流恢复 | 现场走查项（眩光/触控） |
| **ENV-E 目标系统级** | systemd 自启、断电重启、真实磁盘写入 | 未核验（待 ENV-B/D + 授权） | 自恢复链路、DATA-002/003、崩溃退出行为 | 永久可靠性（只做抽样时间窗） |

### 3.2 当前入口与授权核对

| 入口 | 目标 | 声明状态 | 本次可用性判定 |
|---|---|---|---|
| `rk3588_inspect`（Paramiko SSH，有限超时） | compute_host `192.168.0.103` | `configured_unverified`（`remote_inspect` 已授权） | 配置满足；**未核验在线**，连接须有限等待，首次连接需用户核验主机指纹 |
| `rk3588_build` | compute_host | `unavailable_for_current_capability`（缺 `remote_build`） | 本轮不可执行目标构建 |
| `rk3588_deploy` / `rk3588_verify` | compute_host | `unavailable_for_current_capability` | 本轮不可部署/目标验证 |
| 六路 `*_camera_stream` / NVR 直连 | 相机 / NVR | `unavailable_for_current_capability`（缺 `read_camera_stream`） | 本轮不可读流；凭据经环境变量注入，不写死 |
| hardware_verification 设备后端 | — | `disabled`，工具列表空 | **仅该后端禁用**；不推断其他通道不可用 |

> **主机文件读写/编译**受原生工具授权约束：`read/bash/edit/write/grep/find/ls` 可用，但主机**当前无 C/C++ 工具链**（E-00 实测），故主机编译能力实际为零，属环境缺口而非授权问题。

### 3.3 依赖核验清单（E-01..E-13，**本轮未在板端执行**）

沿用 `build_env_check.md` 编号；结论状态必须照实登记“已核验/未核验/有证据的缺口”，未核验不得写成已通过。

| 编号 | 待核验事实 | 核验命令（板端，`rk3588_inspect`） | 期望证据 | 当前状态 |
|---|---|---|---|---|
| E-00 | 架构与工具链（gcc/g++/cmake/pkg-config） | `uname -a`、`gcc --version`、`cmake --version` | 版本号 | 主机有证据缺口；板端未核验 |
| E-01 | Qt5.15 + xcb/eglfs 平台插件 | `qmake -v`、`dpkg -l`、`ls …/qt5/plugins/platforms/` | `libqxcb.so` 或 `libqeglfs.so` | 未核验 |
| E-02 | GStreamer + qmlglsink(`gstreamer1.0-qt5`) | `gst-inspect-1.0 qmlglsink` | 元素存在及 caps | 未核验 |
| E-03 | Mali EGL/GLES | `eglinfo`/`glxinfo -B`、`/proc/driver/mali/*` | GLES 版本、renderer | 未核验 |
| E-04 | `mppvideodec`/`glupload`/`glcolorconvert`/`rgavideoconvert` | `gst-inspect-1.0 <el>` | 元素存在与协商 | 未核验 |
| E-05 | librga | `ldconfig -p \| grep -i rga` | 1.10.1 可加载 | 未核验 |
| E-06 | librknnrt / rknpu 驱动 | `cat /sys/kernel/debug/rknpu/version` | 2.3.2 / 2.3.0a | 未核验 |
| E-07 | SQLite3 | `sqlite3 --version`、`ls libsqlite3*` | 版本 | 未核验 |
| E-08 | 显示会话与自启 | `printenv DISPLAY`、`systemctl get-default`、`loginctl list-sessions` | 图形会话用户与启动时序 | 未核验 |
| E-09 | 磁盘 | `df -h /`、`du -sh /home/admin` | 可用容量与预留阈值 | 未核验 |
| E-10 | 相机可达/凭据（六路） | 逐路 RTSP 探测（UDP，有限超时） | URL/编码/凭据结论 | 未执行（需授权） |
| E-11 | NVR 回放/导出接口 | ONVIF/HTTP 探测或厂商文档 | 回放能力或“不可用”结论 | 未核验 |
| E-12 | CPU governor / UDP 缓冲基线 | `systemctl status cpu-performance`、`sysctl net.core.rmem_max` | 不得回退 | 未核验 |
| E-13 | UI 渲染前提（CJK/等宽字体、触控、分辨率、QML 模块） | `fc-list \| grep -i noto`、`xinput`、`xrandr` | 字体/输入/分辨率 | 未核验 |

> 说明：E-01..E-09 属构建/运行前置依赖，E-10/E-11 属现场/网络事实，E-12/E-13 属平台/UI 前提。未核验项在设计与任务中标注“未核验”，不得写成已通过。

### 3.4 资源状态三态登记

- **已核实可用（本轮主机）**：`read/bash/edit/write/grep/find/ls`；Python（含 sqlite3 模块）、Node；现有产品源码与交接文档；依赖探针与渲染 spike 源码/判据（主机可跑部分）。
- **未核验**：板端 Qt/GStreamer/Mali/RGA/NPU/sqlite/显示会话/磁盘；六路相机凭据与可达；NVR 接口。
- **有证据的缺口**：①主机无 C/C++ 工具链（E-00，`build_env_check.md` §3）；②`rk3588_build`/`deploy`/`verify` 与相机流入口本轮 `unavailable_for_current_capability`；③hardware_verification 后端 `disabled`（仅该后端）；④`.162/.163/.165/.167` 凭据未提供。

---

## 4. 技术选型与决策记录

### 4.1 决策：Qt 5.15 QML（Qt Quick）+ C++ 后端 + GStreamer + SQLite，业务逻辑下沉到无 GUI 的 `railhmi-core`

| 维度 | 选择 | 理由 |
|---|---|---|
| UI 框架 | Qt 5.15 QML | Ubuntu 22.04 官方仓库自带，板端本机 `apt` 可装，无需交叉工具链；QML 声明式状态驱动契合状态机/角色路由；触控+键盘成熟 |
| 视频 | GStreamer（沿用现有链路）+ `GstGLVideoItem` | 复用现有取流/解码/RGA；GL sink 可把解码帧作为 QML 纹理合成，避免 6 路 CPU 拷贝 |
| 推理 | librknnrt 2.3.2 复用现有双模型代码 | 与板上运行时严格匹配（勿换版本） |
| 存储 | SQLite（WAL）+ 文件片段/快照 | 单文件、断电相对安全（WAL）、易检索与审计 |
| 配置 | YAML/JSON 配置 + 运行时校验 | PRD 非功能“可维护性”：相机/职责/阈值/ROI/预置位/策略配置化，不改代码 |
| 图形后端 | X11/xcb（首选），EGLFS（无桌面会话时的备选） | 与现有 `DISPLAY=:0` 一致；EGLFS 作为 E-01 失败时的回退 |
| **业务层可测性** | **`railhmi-core` 静态库：不依赖 Qt/GStreamer/相机**，只依赖 C++ 标准库 + SQLite | 使配置校验、六态状态机、AI 结果→规则→事件、级别映射、抑制、保留策略等可在任意有 C++ 编译器的主机单元测试；直接消除“业务逻辑只在板端/相机授权下才能验”的死结 |

### 4.2 备选方案与否决理由

| 备选 | 否决/降级理由 |
|---|---|
| Electron/Web 前端 | 需额外 Chromium 运行时、磁盘与内存开销大、硬解集成复杂；板端磁盘紧张 |
| GTK4 | 可行但 GStreamer GL 合成与项目现有栈无积累；Qt 资源更充足 |
| 纯 OpenCV highgui | 仅适合单窗口 demo，无法承载 6 页面、角色、列表、时间轴、多路合成 |
| Qt 6 | Ubuntu 22.04 为 Qt 6.2，`gstreamer1.0-qt6` 在 22.04 仓库不可靠；选 Qt5.15 更稳，Qt6 列为未来升级项 |
| 多进程全拆（UI/媒体/AI/录像各一进程） | v1 复杂度/运维成本高；改用单进程 + 模块边界清晰 + systemd `Restart=always`，必要时按 §5.3 抽离录像进程 |
| 把业务逻辑与 Qt 强耦合在 QML/C++ 单体内 | 使全部行为只能在薄端/板上验证，历史已多次因设备授权缺失而无法推进；改为 `railhmi-core` 解耦 |

### 4.3 渲染路径决策（含回退，受 spike 约束）

- **主路径（候选）**：每路一条 GStreamer pipeline，显示分支 `... ! mppvideodec ! glupload ! glcolorconvert ! qmlglsink`（QML `GstGLVideoItem`）；分析分支经 `tee` → `queue leaky=downstream max-size-buffers=2` → `rgaconvert` → `appsink` 供推理/快照。
- **回退路径（候选）**：分析/显示统一走 worker 内 RGA `NV12→BGR`，在 Qt 渲染线程用 `QSGTexture`/`QQuickFramebufferObject` 上传纹理；对非前方瓦片显示帧率上限（如 12–15fps）约束 CPU/内存带宽。
- **决策门（必须遵守 `render_path_spike.md` §7 强制约束）**：
  1. 后续渲染实现必须把 `gl` 与 `cpu` 两条路径都保留在代码中、由配置开关选择（如 `render.path=gl|cpu`），**不得**把未经实测的 `gl` 当作唯一/默认主路径；
  2. 未取得板端实测前，默认取**已有板端证据支持的 `cpu`**（`mppvideodec` + librga，见 `README.md` §3 与 `code/main.cc`），`gl` 作为可开启候选；
  3. 冻结入口已就绪：`app/spikes/render_path/run_render_spike.sh` + `analyze.py --md-out`；当前选定值 **UNMEASURED**（板端未执行，`VERIFY-ITEM-002.md` TEST_FAIL，根因为 ENV-C/D 设备授权缺口）。

### 4.4 选型风险

| 风险 | 影响 | 缓解 |
|---|---|---|
| Mali GL 与 GStreamer GL 上下文共享不成立 | 无法零拷贝合成，CPU/带宽升高 | 回退 `cpu` 路径（§4.3）；瓦片降帧 |
| Qt 包体积/依赖与磁盘 | 安装失败或占满磁盘 | 安装前 `df` 核对；仅装 `qtbase5-dev qtdeclarative5-dev qml-module-qtquick*` 等最小集 |
| 6 路 720p@25 合成与叠加的 GPU/内存带宽 | 掉帧、UI 卡顿 | 显示帧率分级、非前方降帧、叠加层尽量在 QML 层做 |
| NPU 并发多路模型导致算力/温升 | 推理延迟、降频 | 每路推理降频（沿用 demo `inference_interval`）、NPU 预算守卫、温度联动降载 |
| 主机无工具链 / 板端无构建授权 | 无法编译任何增量 | §3.2/§3.4 登记为环境缺口；`railhmi-core` 用最小工具链即可测；先交付纯逻辑增量与 Python 可跑工具，不把板端测量当唯一通过面 |
| 板端本机编译耗时长 | 迭代慢 | 增量构建；分离 `railhmi-core` 仅重编改动模块 |

---

## 5. 逻辑架构

### 5.1 分层（新增 core 边界）

```
┌──────────────────────────────────────────────────────────────────────┐
│ UI 层 (Qt5 QML)  P1实时监控 P2报警中心 P3历史回放 P4全摄像头 P5AI调试 P6设备维护
│  组件: Button/StatusChip/AlarmCard/AlarmBanner/CameraTile/StatusStrip/Timeline
│        DeviceCard(NVR)/DetectionBadge
│  Theme 单例 ← 构建期由 design_tokens.yaml 生成 Tokens.qml/tokens.json
│  仅消费 ViewModel/StatusModel（QObject 属性+信号），不做阻塞 IO
├──────────────────────────────────────────────────────────────────────┤
│ 应用层 (C++ ViewModel / 状态发布)  —— 依赖 core，不含业务判据
│  StatusModel(单例, 唯一真值)  PageRouter+RolePolicy  AlarmBannerModel
│  ConfigService  PermissionService  RecoveryController
├──────────── railhmi-core (无 Qt / 无 GStreamer / 无相机) ────────────┤
│  EventManager(报警/事件/确认/抑制)  RuleEngine(ROI/多帧确认/跟踪/距离)
│  ProfileRules(ai_profile 驱动：前方/受电弓/轮轨/车门)  AlarmMapper(PRD §8.2)
│  StreamStateMachine(六态)  QualityFlags  RetentionPolicy  RecordingModel(缺口)
│  ConfigSchema(cameras/app/thresholds/roi)  Clock(单调↔墙钟)  BoundedQueue
├──────────────────────────────────────────────────────────────────────┤
│ 媒体/设备层 (target 专用)  —— 依赖 Qt/GStreamer/板端库
│  CameraRegistry(配置)  StreamManager(每路 CameraStream)  GstPipelineFactory
│  FrameBuffer(有界)  AIInferenceService(每路 worker)  PtzClient  OnvifWatchdog
│  SnapshotEncoder  NvrProbe(集成状态)
├──────────────────────────────────────────────────────────────────────┤
│ 基础设施层
│  SqliteStore(单写线程, WAL)  SegmentFileStore  LogService(结构化,按日滚动)
└──────────────────────────────────────────────────────────────────────┘
                     │ 消费，不拥有
   OS/BSP/驱动(Mali/MPP/RGA/NPU) | 视频模块(GStreamer/RGA) | 模型(双 rknn 模型)
```

- `railhmi-core` 是**纯业务层**，用标准库 + SQLite；`railhmi-ui`/`railhmi-media` 是 target 专用适配层。
- 这样，规则/事件/状态机/配置/持久化/保留策略可在主机 `railhmi-core-tests` 单元验证；Qt/QML/GStreamer 层在 ENV-A(有 Qt 工具链)/ENV-C/D 验证。

### 5.2 模块与目录建议

```
app/
  CMakeLists.txt                 # 单一工程，多 target
  build.sh                       # 板端本机构建入口（target / preview / core-tests）
  deploy.sh                      # 板内安装到 /home/admin/railhmi
  src/
    core/      config/ status_model(纯数据)/ stream_state/ events/ rules/ profiles/
               alarm_map/ recording_model/ retention/ clock/ logging/
    platform/  sqlite_store/ segment_store/ log_sink/
    media/     camera_registry/ camera_stream/ pipeline_factory/ frame_buffer/ quality/
    ai/        inference_service/ roi/ tracker/ confirm/ distance/ budget/
    device/    ptz/ onvif/ watchdog/ health/ nvr_probe/
    ui/        main.cpp viewmodels/ theme/ router/ notifier/
  qml/         pages/ components/ dialogs/
  config/      cameras.yaml  thresholds.yaml  roi.yaml  ptz_presets.yaml  app.yaml
  models/      yolov8s-seg.rknn  yolov5s.rknn  rail_labels.txt   (由 deploy 复制)
  resources/   icons/ (登记来源许可)  tokens.json(生成物)
  tools/       gen_tokens.py     # design_tokens.yaml → tokens.json/Tokens.qml
  tests/       core/ (railhmi-core 单元测试)
```

### 5.3 进程/线程模型（非阻塞 UI 为硬要求）

- **进程**：v1 为单进程 `railhmi`。`IClipStore`/`IRecordingProvider` 接口保留；若录像稳定性需要独立生命周期，可抽出 `railhmi-recorder` 进程（WAL 允许并发读），v1 不强制拆分。systemd `Restart=always` + 启动自恢复承担崩溃恢复。
- **线程**：
  1. **Qt 主线程**：渲染与事件处理；**禁止**任何网络/磁盘/推理阻塞调用。
  2. **每路 StreamManager 线程**：管理该路 pipeline 生命周期、appsink 拉帧、坏帧/冻结检测、重连状态机；向 `FrameBuffer` 发布最新帧（带 seq）；状态变更投递 `railhmi-core` 的 `StreamStateMachine`。
  3. **AI 推理线程**：有界队列（深度 1–2，丢旧帧）；按 `ai_enabled` 集合轮转；NPU 预算与温度守卫；只对配置了 `ai_profile` 的相机产出 `DetectionResult`。
  4. **存储写线程**：所有 SQLite 写与片段落盘串行化（单写者）。
  5. **健康/看门狗线程**：周期采样 NPU/CPU/温度/网络/磁盘 + ONVIF 复位判定 + NVR 探测。
  6. **GLib 主上下文线程**（或复用 GStreamer 内部）：集中 `GstBus` 消息（EOS/ERROR/STATE_CHANGED），转成内部事件。
- **有界与背压**：所有跨线程队列有界；分析帧队列 `leaky` 丢旧；事件队列溢出时优先保护高等级报警并记录丢事件计数（可观测）。
- **UI 更新**：worker 只更新模型数据并经 `Qt::QueuedConnection` 发信号，QML 绑定自动刷新。

### 5.4 消息与服务接口（进程内）

| 接口 | 方向 | 载荷（要点） |
|---|---|---|
| `FrameBuffer::latest(seq, timestamp)` | media → ai/snapshot | NV12/BGR 帧句柄或深拷贝 + PTS/墙钟 |
| `DetectionResult` | ai → core/RuleEngine | cam_id, ai_profile, ts, boxes, masks(ROI), conf, cls, model_version, infer_ms |
| `AlarmEvent`（§7.3） | RuleEngine → EventManager | 级别/类型/来源/时间/目标/元数据 |
| `StreamStateEvent` | media → core/StatusModel/health | cam_id, state, fps, last_frame_ts, reconnect_count, quality_flags |
| `DeviceIntegrationState` | nvr_probe/health → StatusModel | target, state(probing/reachable/unreachable/unconfigured/unverified), protocol, channels, last_probe_ts |
| `HealthSample` | health → StatusModel | npu/cpu/temp/net_errors/disk |
| `ClipRequest` | EventManager → recording | cam_id, t0, t1, event_id |
| `ConfigChanged` | config → 全模块 | 增量变更 + 校验结果 |

`StatusModel` 是 UI 的唯一真值源；模块间不共享可变全局状态。

---

## 6. 视频接入与渲染设计（本期只做可见光）

### 6.1 每路 pipeline（分析 + 显示）

```
rtspsrc(latency=.., protocols=udp, timeout=5s)
  ! decodebin (或按配置 rtph265depay/h265parse 等)
  ! mppvideodec                      # 硬件解码 → NV12
  ! tee name=t
      t. ! queue(leaky, max 2) ! [render.path=gl] glupload ! glcolorconvert ! qmlglsink
      t. ! queue(leaky, max 2) ! [render.path=cpu] rgaconvert ! video/x-raw,format=BGR
                                ! appsink(name=analysis_sink, max-buffers=2, drop=true)
```

- 编码自适应：本期六路均为可见光，文档基线 H.265 Main；用 `decodebin` 可协商或按配置选择 depay/parse，避免硬编码单编码。
- 强制 UDP：`protocols=udp`；`rtspsrc` 超时参数化；重连采用“相机自然超时 + 置 NULL 重建会话”（现有实现如此，避免无 TEARDOWN 的 kill 诱发相机坏状态）。
- 分辨率/比例：显示按源比例 letterbox，不拉伸、不裁剪内容。三联几何校正/裁剪仅用于“视觉连续”，不做像素级全景拼接。
- 统一时间基准：为每帧记录 `arrival_monotonic`，经 `ClockService` 映射到墙钟 `wall_ms`；用于三路同步回放与事件时间（DATA-001）。
- **本期不含红外**：不实现双光切换、红外 NV12 灰度/绿色伪影专用阈值；相关代码位保留（`spectrum=visible`），待红外接入后增补，不删除需求编号。

### 6.2 流状态机（与交互方案 v2 §7.1 一致）

```
OFFLINE ──启动/重连──► CONNECTING ──首帧──► LIVE
   ▲                       │失败               │ 超时无新帧(冻结)
   │                       ▼                   ▼
   └──── 达重连阈值 ◄── RECONNECTING ◄── FROZEN / DEGRADED
```

- 判定参数（配置化，默认值参考现有实现）：首帧超时、无新帧冻结阈值（`cam_viewer.py` 的 gap>800ms 作为卡顿观测，冻结需更长窗口）、重连阈值与退避、达阈值升级设备报警（HLT-006）。
- 硬约束：**非 LIVE 禁止把旧帧当实时**；必须渲染状态占位 + 最近帧时间戳。
- 质量标志：过暗/过曝/遮挡/模糊/坏帧率（HLT-003）；`DEGRADED` 计为“降级可用”。
- 前方主摄像头离线/冻结 → 前方 AI 状态切 `NOT_READY`，产生二级设备报警（PRD §8.2）。

### 6.3 显示帧率分级与 6 路并发

- 前方（CAM-01）：目标全帧率（≤25）。
- 两侧（CAM-02/03）：目标 15–25，按实测带宽调整。
- P4 全摄像头页与 P1 同屏时复用同一 `GstGLVideoItem` 纹理避免重复解码（同一 pipeline 服务多视图，或按页面可见性暂停不可见分支）。
- 页面不可见时降载：暂停分析分支/降显示帧率；切换页面不重建会话（避免 TEARDOWN 抖动）。
- GL 内存/纹理池上限与异常回收；解码器 close 时释放 RGA/NPU 资源。

### 6.4 快照与色彩

- 报警快照（ALM-006）：从分析帧或显示帧截取，注意 libgdal 冲突：板端 cv2 的 JPEG/PNG 不可用，走 BMP 或独立编码路径（`README.md` 坑 6）。
- 快照与事件关联存储，P2 可查看。
- 色彩校验：NV12→BGR 后 BGR 三通道均值近似相等（`README.md` §2.3 判据，可见光同用）。

---

## 7. 状态模型与 UI 数据契约（v2）

### 7.1 状态发布

`StatusModel` 为 QML 唯一数据源，暴露：
- `cameras[]`: `id,name,position,ai_enabled,ai_profile,state,fps,bitrate,resolution,last_frame_ts,quality_flags,reconnect_count`
- `ai[]`: `status,model_version,fps,latency_ms,last_result_ts`
- `alarms`（当前，最高级优先）、`alarmHistory`
- `recording`: `provider_status,recording_source(local|nvr|both),segments,critical_segments,gaps,storage_free,storage_total`
- `device`: `npu_load,cpu,temp_c,net_errors,disk`
- `integrations[]`（NVR 等）: `id,name,address,registered,state,protocol,channel_mapping,last_probe_ts`
- `ptz`（按当前选中/前方）: `pan,tilt,zoom,preset,homing_state,tolerance_ok`
- `session`: `role,mode.fixed(visible)+mode.ir(unavailable),mute,time,speed_available/speed`
- `recovery`: `in_progress,steps[]`

字段命名与交互方案 v2 §7.2 一一对应；未接入项显式 `未接入/不适用`（如红外、车速、门磁）。

### 7.2 页面路由与角色

- 侧边/底部固定 6 项，按角色过滤渲染（权限不可见项**不渲染**，非禁用可见）。
- 司机默认 P1；受限页直接访问显示“无权限”说明 + 返回，不空白崩溃。
- 从 P1 报警卡进入 P3 时锁定来源摄像头与报警时刻；返回 P1 保留来源上下文。
- 报警横幅（z=400）跨页面常驻；自动弹窗（z=300）不遮挡前方中央区；`DetectionBadge` 只出现在 P4/P6，不进司机 P1。
- 顶部模式位固定显示 `可见光`（neutral），`红外未接入`（灰显），**无模式切换控件**。

### 7.3 报警/事件数据结构

```
AlarmEvent {
  id            // 唯一，时间序
  level         // 1|2|3（映射见 PRD §8.2，不可被 UI 改写）
  type          // front_obstacle|person_on_rail|panto_abnormal|wheel_derail|door_ajar|device_*
  source_cam    // CAM-01..06
  ai_profile    // 产生该事件的检测职责（sides 无检测则不产生）
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

## 8. AI 与检测职责（`ai_profile` 驱动）

### 8.1 检测职责映射（来源：`一些说明和要求.txt` 第 10/11 条、交互方案 v2 §7.3）

| ID | 位置 | `ai_enabled` | `ai_profile` | 检测产物 | 级别映射 |
|---|---|---|---|---|---|
| CAM-01 | 车头前方长焦（车内） | 是（最高优先） | `front` | 前方轨道障碍物/人员/躺卧人员，辅助制动提示 | 一级 |
| CAM-02 | 车辆左侧广角 | 否 | `none` | 仅提供大视野，不检测 | — |
| CAM-03 | 车辆右侧广角 | 否 | `none` | 仅提供大视野，不检测 | — |
| CAM-04 | 车顶（观察正前方） | 是 | `pantograph` | 受电弓三态：未升弓 / 正常接触 / 升弓异常（不到位） | 异常二级 |
| CAM-05 | 车底 | 是 | `wheel_rail` | 轮轨相对关系，明显异常/疑似脱轨 | 一级 |
| CAM-06 | 司机室 | 可选（视门磁方案） | `cab_door` | 左右车门关闭状态；可预留门磁输入 | 未关严二级 |

- 推理层按 `ai_enabled` 与 `ai_profile` 调度；改配置不改界面代码。左右侧 `none` 不进入推理队列。
- 现实中各相机可先用同一模型（`一些说明和要求.txt` 第 10 条），将来按相机替换模型；应用只消费模型版本/结果，不定义精度。

### 8.2 推理预算与调度

- 每路推理降频（沿用 demo `inference_interval`，如每 5 帧一次），配置化。
- NPU 预算守卫：并发路数超预算时按优先级（CAM-01 > CAM-05 > CAM-04 > CAM-06）排队；温度联动降载，不静默停推理（HLT/温度告警）。
- 有界丢旧帧队列（深度 1–2）；AI 退出/加载失败 → 状态转异常，相关检测类报警标注“AI 不可用”。

### 8.3 报警级别映射（严格 PRD §8.2，UI 不得改写）

前方轨道人员/障碍物→一级；轮轨明显异常/疑似脱轨→一级；受电弓异常→二级；车门未关严→二级；前方主摄像头离线/冻结→二级（前方 AI 切不可用）；侧视摄像头离线→三级。

---

## 9. 报警/事件层

- 输入：`DetectionResult`（多帧跟踪后）+ 设备/流状态异常 + 健康阈值。
- 规则链（`railhmi-core::RuleEngine` + `ProfileRules`）：
  1. 按 `ai_profile` 选对应判定（前方：轨道 ROI/危险区；车顶：受电弓固定 ROI 三态持续确认；车底：轮轨标定基准；司机室：车门 ROI）；
  2. 目标在/接近危险区（或多帧持续异常）→ 多帧确认（帧数/时间阈值配置化）；
  3. 跟踪 ID → 抑制窗口；
  4. `AlarmMapper` 按 PRD §8.2 映射级别 → `AlarmEvent`。
- 输出动作：更新 `StatusModel` 当前报警与横幅 → 触发快照（ALM-006）→ 触发保护片段（PLY-002 前 30s/后 60s，可配置）→ 入库（事件+目标+元数据）→ 声音策略（分级，一级不可被静音为无视觉）。
- 确认：只改 `ack_state`，一级声音停止、横幅降级可关闭，事件保留可检索（ALM-004）。
- 声音：PRD Q-07 未决；抽象 `IAudioNotifier`，未接入时仅视觉并在 UI 标注声音通道未接入。

---

## 10. 录像与回放

### 10.1 抽象

```
IRecordingProvider
  querySegments(cam, t0, t1) -> segments[] | unavailable
  queryGaps(cam, t0, t1) -> gaps[]
  playSegment / exportSegment(...)   // 权限受控（PLY-007）
```

- 实现 A `NvrRecordingProvider`：消费 NVR（`192.168.1.4`，回放接口待 E-11 确认）；接口未知时返回 `unavailable`，UI 显式标注（PLY-008），不静默；`channel_mapping` 未提供时按“未配置”呈现，不虚构通道号。
- 实现 B `ClipRecorder`（本机保证）：每路维护**有界滚动缓冲**（内存环形 + 落盘暂存），报警时把 `[t-30s, t+60s]` 固化为保护片段（MP4/TS，编码优先直通复制，避免转码耗算力）；容量/保留策略配置化。
- 缺口：任何录像中断区间写 `recording_gap` 记录，P3 时间轴斜纹留白。

### 10.2 回放

- 时间轴：连续录像段、报警区段显著色标、缺口斜纹、游标、关键帧缩略图。
- 快速跳转：-30s / 报警时刻 / +30s / +60s（PLY-004）。
- 三联同步：前方报警默认左/前/右同时间轴同步播放（PLY-005），以统一墙钟时间轴驱动多路 seek；三路启动前做时钟对齐，漂移超阈值显示“同步偏差”提示。
- 导出：仅授权角色（PLY-007），二次确认 + 写维护日志（HLT-007）。

### 10.3 存储策略（磁盘有限）

- 报警保护片段优先保留，不被普通循环覆盖；保留周期/容量上限配置。
- 存储低于阈值提前告警（DATA-003），P6 显示剩余容量与预计可录时长。
- 写入采用“先写临时文件→原子重命名”，断电保护（DATA-002）。

---

## 11. 持久化设计

### 11.1 SQLite（WAL，单写线程）

| 表 | 关键列 |
|---|---|
| `events` | id, level, type, source_cam, ai_profile, ts_wall_ms, ts_mono_us, confidence, distance_m, algo, model_version, description, ack_state, record_available, snapshot_ref |
| `event_targets` | event_id, track_id, boxes_json, first_ts, last_ts, duration_ms |
| `maintenance_log` | ts, user, action, target, result, detail |
| `device_health` | ts, npu_load, cpu, temp_c, net_errors, disk_free, disk_total |
| `recording_segments` | id, cam, t0_ms, t1_ms, path, kind(continuous\|critical), complete |
| `recording_gaps` | id, cam, t0_ms, t1_ms, reason |
| `stream_events` | ts, cam, event(connect\|reconnect\|freeze\|offline\|watchdog), detail |
| `integrations` | id, name, address, registered, state, protocol, channels_json, last_probe_ts |
| `config_kv` | key, value, updated_ts, updated_by |

- 时间：统一 UTC 毫秒 + 单调微秒；启动时记录单调↔墙钟偏移，重启后校准。
- 数据库置于运行目录 `data/`，定期 `VACUUM`/归档；异常时降级为只读并能告警。

### 11.2 文件布局（建议）

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

## 12. 配置设计（不改代码）

| 文件 | 内容 |
|---|---|
| `config/cameras.yaml` | 六路注册表：`id,name,position,rtsp_url,transport(udp),username,credential_env,spectrum(visible),ai_enabled,ai_profile,display_fps,tile_aspect` |
| `config/thresholds.yaml` | 多帧确认帧数/时间、危险区接近阈值、置信度、抑制窗口、冻结/重连阈值、存储阈值、保护片段前后时长 |
| `config/roi.yaml` | 各相机 ROI（前方危险区、受电弓固定 ROI、轮轨标定基准、车门 ROI） |
| `config/ptz_presets.yaml` | 前方预置位、容差、归位策略（本期只做可见光预置位；红外预置位不适用） |
| `config/app.yaml` | 角色与默认角色、权限开关、语言、分辨率档、录像保留、日志级别、渲染路径开关、模拟开关、NVR 登记项 |

- 启动时 schema 校验，缺失/非法字段给出明确错误并在 P6 显示，不用默认值静默掩盖。
- 相机 IP/URL 只来自配置；UI 不硬编码（交互方案 v2 §7.3）。凭据经环境变量注入（`credential_env`），URL 不含凭据，日志脱敏；口令文件权限 600。
- NVR 仅在 `app.yaml` 登记 `id/name/address/registered`，账户/协议/通道映射缺失时保持“未配置/未核验”，不虚构。
- 热加载：可安全热更新的项（阈值、显示帧率）即时生效；结构变更（增删相机、NVR 通道）需重启并在 UI 提示。

---

## 13. 设备/云台/看门狗/NVR/健康

- **PTZ**：`PtzClient` 提供绝对/相对移动、变焦、预置位调用、位置读取；CGI 语义参考 `tools/ptz_control.py`。开机归位→调前方预置位→读实际值→容差校验（PTZ-001/002），超差产生设备异常并令前方 AI `NOT_READY`。P6 提供“一键归位/重调前方预置位”（PTZ-005），二次确认 + 日志。
- **ONVIF 看门狗**：复用 demo 的零依赖 ONVIF SystemReboot（30s 坏帧滑窗阈值、preroll 失败阈值、冷却），作用于支持该能力的相机；事件入 `stream_events` 与维护日志。安全提示：ONVIF SystemReboot 不校验鉴权时仅在受控网段使用（`README.md` §2.3）。
- **NVR 集成（`192.168.1.4`）**：`NvrProbe` 做有限探测（ONVIF/HTTP 或厂商接口，待 E-11），维护 `integrations.state`（probing/reachable/unreachable/unconfigured/unverified）。账户/协议/6 路通道映射未提供时：可达性未知显示“未核验”，通道列显示“未配置”，界面不提供自动修改 NVR 配置入口。NVR 本期不替代六路直连相机。
- **健康采样**：NPU `/sys/kernel/debug/rknpu/load`、CPU `/proc/stat` + governor、温度 `/sys/class/thermal/*`、网络 `/proc/net/snmp`（UDP InErrors）、磁盘 `statvfs`。周期写入 `device_health` 并发布到 `StatusModel`（HLT-004/005、DATA-003）。
- **AI 健康**：每路推理上报 `status/model_version/fps/latency/last_result_ts`；AI 退出时相关检测类报警标注“AI 不可用”，不静默。

---

## 14. 外部接口清单与未知事实

| 接口 | 现状/所需事实 | 影响 | 处理 |
|---|---|---|---|
| 六路相机 RTSP（UDP） | `.162/.163/.165/.167` 凭据与 URL 未确认（E-10；`.65`/`.160` 有历史基线） | 注册表填充与联调 | 配置化，缺项不静默；经 `credential_env` 注入 |
| NVR 回放/导出 | 接口未知（E-11、Q-NVR） | P3 连续回放能力 | `unavailable` 分支 + 本机保护片段；UI 显式标注 |
| NVR 通道映射/账户/协议 | 未提供 | 设备卡通道列 | 显示“未配置/未核验”，不虚构 |
| 车速/里程/GPS（Q-04） | 是否可读未知 | 顶部状态条字段与风险分级 | 未接入中性显示 |
| 车门方案（Q-05） | 视觉/门磁/融合未定 | 车门状态来源 | `cab_door` profile + 预留门磁输入位 |
| 报警声（Q-07） | 内置/外置蜂鸣器未定 | 声音通道选择 | `IAudioNotifier` 抽象 |
| 触控/屏幕（V-01） | 尺寸/分辨率/是否触控未知 | 字号档与触控目标 | 令牌化 + 两档适配 |
| 环境光/眩光（V-02） | 未现场走查 | 深色主题可读性 | 待现场验证 |
| GNSS/NTP | 统一时间基准来源 | 多路同步与事件追溯 | 本机单调↔墙钟映射（DATA-001） |

未接入项在界面以中性“未接入/不适用”呈现，不虚构数据。未知项在 §3.3 与本节登记，不猜测地址/凭据/协议。

---

## 15. 构建与部署方案

### 15.1 构建目标

- `railhmi-core-tests`：主机或板端均可编译（仅标准库 + SQLite），验证业务逻辑。**这是可在最小工具链环境验证的主增量。**
- `railhmi`（target）：RK3588 板端本机 aarch64 编译，链接 Qt/GStreamer/librga/librknnrt/sqlite。
- `railhmi`（preview）：x86_64 + Qt + 模拟输入（`-DRAILHMI_SIMULATION=ON`），验证 UI/交互。

```bash
# 板端本机构建（target；需 rk3588_build 授权）
cd /home/admin/railhmi/src/app
./build.sh Release            # cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j$(nproc)
# 主机逻辑测试（最小工具链）
./build.sh CoreTests && ./build/build-core/tests/railhmi-core-tests
# 主机 UI 预览（需宿主 Qt 工具链）
./build.sh Preview
# 依赖（板端，需授权；安装前先 df 核对磁盘）
sudo apt-get install -y qtbase5-dev qtdeclarative5-dev qml-module-qtquick2 \
  qml-module-qtquick-controls2 qml-module-qtquick-layouts \
  qtbase5-dev-tools gstreamer1.0-qt5 libsqlite3-dev pkg-config cmake
```

- 链接（target）：`Qt5::Quick Qt5::Qml Qt5::OpenGL Qt5::Svg`、`gstreamer-1.0 gstreamer-app-1.0 gstreamer-video-1.0`、`librga`、`librknnrt`、`opencv4`、`sqlite3`、`pthread`。
- 不使用交叉编译；不修改 `~/rknn_model_zoo/` 官方树（模型/库从该树或 `install/` 复制引用）。
- **环境缺口如实反映**：主机当前无 C/C++ 工具链（E-00），故 `CoreTests`/`Preview` 在当前主机不可执行；板端构建需 `remote_build` 授权（当前 `unavailable_for_current_capability`）。缺口登记在 §3，不作为软件通过证据。

### 15.2 部署

```bash
./deploy.sh    # 安装到 /home/admin/railhmi/{bin,config,models,resources,data,logs}
               # 复制 models/*.rknn、rail_labels.txt、资源与图标（并登记来源许可）
```

### 15.3 常驻与自启

- `railhmi.service`（systemd）：`After=graphical.target`、`Environment=DISPLAY=:0`、`Restart=always`、`RestartSec=3`、工作目录指向运行目录、`StandardOutput` 接 LogService。
- 不自建/不改 `cpu-performance.service`（保持 `performance`）。
- 依赖 `graphical.target` 的时序以 E-08 实测为准；若无人登录图形会话，评估 xcb 不可用时的 EGLFS 回退。
- 升级：版本化运行目录或 symlink 切换 + 保留上一版本回滚；数据库迁移脚本向前兼容。

### 15.4 断电恢复（非功能“可恢复性”）

启动自恢复序列（UI 显示进度）：1. 加载配置并校验 → 2. 打开数据库（WAL 恢复）→ 3. 启动录像/存储 → 4. 启动各相机取流并入状态机 → 5. 启动 AI（按 `ai_enabled`/`ai_profile`）→ 6. 云台归位 + 调前方预置位 → 7. 前方 AI `READY` → 8. 发布 `recovery.completed`。任一步失败：记录 + 显式告警，不阻塞其余步骤（单路非关键异常不导致整体退出）。

---

## 16. 验证策略与范围（严格分阶段，不跨层外推）

| 范围 | 手段 | 能证明 | 不能证明 | 当前可执行性 |
|---|---|---|---|---|
| 静态/依赖核对 | 阅读、`gst-inspect`、`dpkg`、`ldd`、schema 校验 | 依赖存在与接口匹配（E-01..E-13） | 运行时行为 | 主机部分可（Python）；板端需 `rk3588_inspect` |
| **主机逻辑测试** | `railhmi-core-tests`（配置/状态机/规则/事件/级别映射/持久化/保留策略） | 业务逻辑正确性 | UI、媒体、NPU/Mali/RGA | 需主机 C++ 工具链（当前缺口） |
| 主机 UI 预览（x86_64 模拟输入） | `-DRAILHMI_SIMULATION=ON` + 模拟帧/检测/状态 | UI 布局、状态机集成、页面路由、令牌一致性、角色隔离 | 目标平台可运行、性能、NPU/RGA/Mali | 需宿主 Qt 工具链（当前缺口） |
| 目标构建（板端 aarch64 编译） | `build.sh` | 目标 ABI 可编译链接 | 运行正确性 | 需 `remote_build` 授权（当前不可用） |
| 目标运行（板端文件源/真机） | ENV-C/ENV-D | 取流/渲染/推理/报警/回放端到端 | 需现场走查项（眩光、触控） | 需设备授权 |
| 软件测试 | `software_tester` 计划 | 功能与交互可检验点 | 板上硬件行为 | 计划层 |
| 嵌入式测试 | `embedded_tester` | 嵌入式 Linux/真机联调 | 电气/信号 | 计划层 |

- 结论按环境分层记录；**主机可运行不等于目标可运行**；**未核验不等于不可用**。
- 渲染路径在取得 ENV-C/D 实测前保持 UNMEASURED 与双路径。
- 板端档失败若根因属板端资源/驱动/BSP/设备授权 → 记录为依赖缺口，交平台/嵌入式职责，不算软件功能失败（沿用 `E2E_PLAN.md` §3）。

## 17. 风险登记

| 风险 | 触发条件 | 影响 | 应对 |
|---|---|---|---|
| GL sink 不可用 | E-04 失败 | 需回退渲染，性能下降 | §4.3 回退路径 + 瓦片降帧 |
| 6 路合成性能不足 | 实测掉帧 | 司机视图卡顿 | 显示帧率分级、页面卸载、纹理复用 |
| NVR 接口不可得 | E-11 无结论 | P3 连续回放受限 | 本地保护片段保证报警闭环；UI 显式标注不可用 |
| 磁盘耗尽 | 保护片段堆积 | 录像失败 | 保留策略 + 阈值提前告警 + 保护片段上限 |
| NPU/温度过载 | 多路推理 | 延迟/降频 | 推理降频、预算守卫、温度联动 |
| 相机凭据/编码未知 | E-10 | 无法联调 | 配置化，待现场确认；缺项不静默 |
| 图形会话时序 | E-08 | 自启失败 | systemd 依赖实测调整；EGLFS 回退 |
| 主机无工具链 / 板端无构建授权 | §3.4 | 无法编译任何增量 | 业务逻辑下沉 `railhmi-core`；缺口如实登记；不把板端测量当唯一通过面 |
| 检测职责扩展（各相机替代模型） | 模型未就绪 | 事件无法产生 | 应用按 `ai_profile` 驱动，模型替换不改代码；未就绪时该路标注不可用 |

## 18. 交接

- 给 `desktop_app_implementation`：按 §5 架构与 `docs/hmi/desktop_app_tasks.yaml` 有序实现；**先交付 `railhmi-core` 业务逻辑与单元测试**（最小工具链可验），再逐层接入 Qt/GStreamer；渲染路径先按 §4.3 保留双路径并默认 `cpu`（或按 spike 实测结果冻结）；严格消费 `design_tokens.yaml` 与交互/视觉规范 v2，不自行改需求。
- 给 `software_tester`：§16 验证范围与交互方案 v2 §11、视觉规范 v2 §10 为可检验点输入，不替代测试计划。
- 给 `embedded_tester`：目标运行与真机联调（取流稳定性、渲染、NPU、断电恢复、各相机检测职责）。
- 给平台职责：§3.3 依赖核验中的 rootfs/BSP 缺项（Qt 包、Mali/MPP/RGA、sysctl、governor、显示会话、sqlite3-dev）由平台按授权处理；主机 C/C++ 工具链缺口若需主机预览，交环境/平台。
- 待确认项：§14 与 PRD §14 / 交互 §12 / 视觉 §13，保持“未核验/未接入”标注，不虚构事实。
- 版本记录：v1 首版；v2 依交互/视觉 v2 与本次环境核对更新（见 §0.2 变更表），并修复“业务逻辑只能板端验证”的架构问题。
