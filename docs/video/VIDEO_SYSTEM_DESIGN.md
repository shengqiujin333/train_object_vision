# 视频系统设计 — 六路可见光接入、帧分发、录像回放与恢复（VideoSystemDesignArtifact）

- Artifact: VideoSystemDesignArtifact（视频链路接口预算和恢复方案）
- Capability: `video_systems_engineer.video_system_design`
- 版本: v1（2026-10-17，基于本次接手 HEAD `f283363`；接口基线 `docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0）
- 上游输入:
  - `docs/interfaces/INTERFACE_CONTRACT.md`（InterfaceContract v1.0.0，**已批准输入**；本设计不新增/不更改 schema、C++ ABI、YAML 配置或 SQLite DDL）
  - `docs/hmi/DESKTOP_APP_DESIGN.md` v2、`docs/hmi/E2E_PLAN.md` v2
  - `app/config/cameras.yaml`、`app/config/app.yaml`、`app/config/thresholds.yaml`（唯一可执行配置来源）
  - 现有媒体层实现：`app/src/media/{include,src,tools}/**`、`app/src/core/include/railhmi/core/stream_state.h`、`app/src/core/include/railhmi/core/config.h`
  - 历史取流参考实现：`code/main.cc`（`MainGrabber`/`ThermalGrabber`、`nv12_to_bgr_rga`）
  - Runtime 统一环境与授权汇总（`runtime/environment.yaml` + `runtime/permissions.yaml`）
- 本次执行范围: **只做设计**。未连接相机、未取流、未编译、未部署、未改产品源码。仅执行了一次**板端只读** GStreamer 栈核对（见 §1.3），命令均有限等待、凭据仅从环境变量在进程内读取、不在命令行出现。

> 口径：本文所有事实分三类——**本轮实测**（本次只读探针原始输出）、**仓库既有证据**（既往证据文件，注明来源，不外推为本轮通过）、**待实测**（有明确测量方法但本次未执行）。`configured_unverified` 只表示配置与工具要求满足，不表示在线、依赖安装成功或功能通过。

---

## 0. 范围与硬约束

### 0.1 本期范围

- 接入 **6 路可见光**（CAM-01..CAM-06），全部 RTSP over **强制 UDP**；不含红外、不含历史 7/8 路地址、不含自动重启相机的默认行为。
- 视频系统职责：**取流协商、解码、格式转换（NV12→BGR）、帧分发、录像、回放、异常恢复**，以及相应的诊断、配置与版本。
- 显式排除（交对应责任能力）：模型算法与精度（模型职责）、Qt 纹理上屏/页面渲染（桌面 UI 职责）、BSP/驱动/rootfs/sysctl/governor（平台职责）、NVR 厂商协议接入（待环境登记与授权）、生产整机验收（测试职责）。

### 0.2 硬约束（来源：交接文档 + 接口契约 + 本轮实测）

1. **强制 UDP**：TCP 会在部分相机卡 SETUP 或 401（`README.md` §2.2/§2.3.1、`code/main.cc` 注释）。管线固定 `protocols=udp`，不提供自动 TCP 回退。
2. **板端 OpenCV 不可用于取流**：板端 OpenCV 4.5.4 的 NV12 协商有 bug（`cap.open` 挂死）。板端主链路必须使用**原生 GStreamer C API + appsink**；OpenCV 仅可用于既有诊断工具，不进入显示/推理主循环。
3. **格式转换走 librga im2d，不用 `rgaconvert`**：历史实测 `rgaconvert` 对 8-bit Main profile 出绿屏/在无 pinned caps 时协商成 1x1（`code/main.cc` 注释）。统一用 `imcvtcolor(..., IM_YUV_TO_RGB_BT601_FULL)`，且接受 librga 的两个成功码 `IM_STATUS_SUCCESS(1)` 与 `IM_STATUS_NOERROR(2)`。
4. **渲染路径默认 `cpu`**：`app.yaml render_path=cpu`，依据 ITEM-002 板端实测（`gl` 因 `qmlglsink`/Qt Quick 缺失不可用）。`gl` 保留为可开启候选，双路径代码保留，未实测不预设。
5. **连续录像不在本机**：31G 可用盘（本轮实测 `df -h /` = 57G/24G used/31G avail），6 路连续约 12GB/h（文档估算）。本地只保留**报警前后保护片段 + 快照 + 元数据**；连续录像由 NVR 承担，NVR 未配置时显式 `unavailable`。
6. **算力只够显示，不够 6 路全检**：推理只对 `ai_enabled` 相机运行；左右（CAM-02/03）`ai_profile=none` 永不进入推理队列。
7. **无凭据泄露**：密码只从登记环境变量读取；URL 不含凭据；含鉴权 URL、pipeline/bus 原文、原始凭据不得进入日志/DB/工件/进程命令行。`redact_url` 仅处理部分 `user:pass@` 形态，不足以替代整条错误消息脱敏。
8. **非 LIVE 不得把旧帧当实时**：渲染前必须经 `StreamStateMachine::may_present_as_live(now)` 判定。

---

## 1. 视频源与运行环境确认

### 1.1 视频源清单（唯一可执行来源 `app/config/cameras.yaml`，此处仅为引用）

| id | position | 主路径（不含凭据） | 凭据环境变量 | spectrum | ai_enabled / ai_profile | display_fps | 文档 codec / 分辨率 / fps | 状态 |
|---|---|---|---|---|---|---|---|---|
| CAM-01 | primary | `.65:554/cam/realmonitor?channel=1&subtype=0` | `TRAIN_FRONT_RTSP_CREDENTIAL` | visible | true / front | 25 | H.265 / 1280×720 / 25 | 文档基线（README 2026-10-13） |
| CAM-02 | left | `.160:554/h264/ch1/main/av_stream` | `TRAIN_SIDE_RTSP_CREDENTIAL` | visible | false / none | 15 | H.265 / 1280×720 / 25 | 文档基线 |
| CAM-03 | right | `.162:554/h264/ch1/main/av_stream` | 同上 | visible | false / none | 15 | 待实测（按 CAM-02 预期） | **待实测** |
| CAM-04 | roof | `.163:554/h264/ch1/main/av_stream` | 同上 | visible | true / pantograph | 15 | 待实测 | **待实测** |
| CAM-05 | underfloor | `.165:554/h264/ch1/main/av_stream` | 同上 | visible | true / wheel_rail | 15 | 待实测 | **待实测** |
| CAM-06 | cab | `.167:554/h264/ch1/main/av_stream` | 同上 | visible | true / cab_door | 15 | 待实测 | **待实测** |

- 全部 `transport: udp`、`username: admin`；左右历史流含 G.711u 音频轨，**只选视频 RTP pad、忽略音频**。
- CAM-01 路径与其余相机不同（大华壳 `cam/realmonitor`）；**不要**把大华路径套用到其余相机，也不要强制 TCP。
- 编码/分辨率/fps/实际 URL caps 一律**以协商 caps 为准**；上表是配置意图与文档预期，不是解码实测 FPS，也不是能力声明。
- `display_fps` 是**展示上限**，不是解码/推理帧率。

### 1.2 目标运行环境（仓库既有证据，注明来源）

| 项 | 值 | 来源 / 状态 |
|---|---|---|
| OS / 内核 / 架构 | Ubuntu 22.04.5 LTS / 6.1.141 / aarch64 | 本轮实测 `uname -smr` 一致 |
| GStreamer | 1.20.3 | 本轮实测一致 |
| OpenCV | 4.5.4（NV12 协商 bug，禁用于取流） | README 文档基线 |
| librga | `/usr/local/lib/librga.so` + `/lib/aarch64-linux-gnu/librga.so`(+.2)；`librga-dev 2.2.0-1` | 本轮实测（**注意：README 记载 1.10.1；实际 dev 包为 2.2.0，能力核验以 `im2d.h` 实际 API 为准**） |
| librknnrt | `/lib/librknnrt.so` | 本轮实测 |
| 显示 | 交互式 SSH 会话内 `DISPLAY` 未设置；release 探针曾记录 X11 活动（X0/X1001/active） | 本轮实测 + 既有证据；应用由 systemd 注入 `DISPLAY=:0` |
| CPU governor | `performance`，`cpu-performance.service active` | 本轮实测；**不得回退** |
| UDP 缓冲 | `rmem_max=16777216`、`rmem_default=4194304`，`/etc/sysctl.d/99-udp-buffers.conf` present | 本轮实测 |
| 磁盘 | `/dev/root 57G 24G used 31G avail 44%` | 本轮实测 |
| 温度 | thermal_zone0 = 40692（≈40.7°C，空闲） | 本轮实测 |
| sqlite3 CLI | 不存在（`libsqlite3.so.0` 存在） | 本轮实测；平台层已有 shim 方案，视频层不依赖 CLI |

### 1.3 GStreamer 视频栈只读核对（本轮实测，2026-10-17）

命令：`gst-inspect-1.0 <element>` 与 `gst-inspect-1.0 | grep`（一次只读 SSH 会话，10s 连接超时/60s 命令超时，主机指纹经受信任 known_hosts）。

| element | 结果 | 说明 |
|---|---|---|
| `rtspsrc` | **yes** | RTSP 源 |
| `rtph265depay` / `h265parse` | **yes** | H.265 解包/解析 |
| `rtph264depay` / `h264parse` | **yes** | H.264 解包/解析 |
| `decodebin` | **yes** | 自动协商解码（首选） |
| `mppvideodec` | **yes**（`gst-inspect-1.0 mppvideodec` 直查：Rockchip MPP 硬解，rank primary+1，plugin `rockchipmpp`） | 硬件解码 |
| `rgaconvert` | **yes**（rank **none**，plugin `rgaconvert`） | **不采用**（历史绿屏/误协商） |
| `videoconvert` | **yes** | 仅作文件/测试源兼容与诊断 |
| `glupload` / `glcolorconvert` | **yes** | GL 上传/色彩转换存在 |
| `qmlglsink` | **no** | **GL 上屏到 QML 不可用** → 佐证 `render_path=cpu` |
| `appsink` / `fakesink` / `queue` / `tee` | **yes** | 帧分发/记录/多分支基础 |
| ffmpeg / ffprobe | **yes**（板端） | 仅离线分析/诊断，不进主循环 |
| `gst-discoverer-1.0` / `gst-launch-1.0` | **yes** | 元数据探测/冒烟工具 |

> **诊断注意**：`gst-inspect-1.0 --exists <el>` 在本板返回了错误的 `mppvideodec=no`/`rgaconvert=no`，而直接 `gst-inspect-1.0 mppvideodec` 与插件列表均确认存在。**能力探测必须用直接 `gst-inspect-1.0 <el>`（解析 Factory Details/Long-name）判定，不得依赖 `--exists` 退出码**（该结论已作为诊断要求写入任务）。

### 1.4 环境缺口（不属于软件功能失败）

- 相机/ NVR 现场取流入口 `effective_status=configured_unverified`；`rk3588_build/deploy/verify` 当前 `unavailable_for_current_capability`；设备工具后端 `disabled`。本轮**未执行**真实取流，所有相机 codec/分辨率/fps 保持"待实测"。
- `qmlglsink` 缺失与 Qt Quick 包缺失属板端依赖缺口，须由平台按授权补装后才能评估 GL 路径；本轮不改系统。
- 主机 ffmpeg（gyan FFmpeg 9.0）只作离线媒体分析，**不**代表板端 GStreamer 与相机兼容性。

---

## 2. 帧结构、跨重连身份与缓冲区所有权

### 2.1 帧描述符（沿用接口契约 §5 的 `FrameDescriptor`，不新增字段）

`FrameDescriptor`：`camera_id / session_id / generation / seq / capture_mono_us / capture_wall_ms / pts_ns / width / height / format / colorimetry / planes[{offset,stride_bytes,size_bytes}] / buffer_bytes / buffer_ref / memory_kind`。

- `session_id`：应用启动生成的不透明字符串；`generation`：每次**真正重建**通道 +1；`seq`：同一 generation 内严格递增。
- 图像消费者（GUI/推理/快照）必须拒绝旧 session/generation、重复 seq、乱序结果；状态与时间比较也局限同一 session。
- v1 `memory_kind=cpu`；`format ∈ {NV12, BGR888, RGB888}`。GL/DMABUF 日后必须扩充能力协商、同步与线程生命周期，**不得**塞进 CPU 地址字段。
- NV12：宽高必须偶数，两平面 `stride≥width`，Y 区 `≥stride×height`，UV 区 `≥stride×height/2`；packed 单平面 `stride≥width×3`；`offset+size≤buffer_bytes`；平面不重叠。**尺寸/stride/colorimetry 来自 caps，不能固定为连续 `width×height`**（MPP 输出常见列/行 padding）。
- `buffer_ref` 是进程内租约的不透明查找键，**不得跨进程解引用**；schema 不传指针/fd/像素。

### 2.2 缓冲区所有权：不可变 FrameLease

- media 层提供 `latest_frame(camera_id) -> FrameLease`；空返回 `NOT_READY`。
- `FrameLease` 持有 `GstSample`/`GstBuffer` 的共享所有权（ref/unref）或必要时的深拷贝；释放回调在**最后一个消费者退出后**执行。
- **禁止**向 core 或任何消费者传已 `unmap` 的裸地址；core 仍只见 `FrameRef` 元数据，像素所有权留在 media。
- 真实模型适配器消费 `FrameLease`，不得从 `FrameRef` 推导像素。
- 显示、推理、录像三类消费者**各自独立取用**，互不借用可释放缓冲：任何消费者慢/退出都不得阻塞取流线程。

### 2.3 编码分支与原始帧分支的所有权分离

- 显示/AI/快照分支持有**解码后的原始帧**（NV12 或按需 BGR）。
- 录像分支从 `tee` 的**解码前压缩分支**（depay/parse 之后、`mppvideodec` 之前）取包，持有压缩 `GstBuffer`，**不复用显示丢帧策略，也不保存全部原始帧**。
- 两条分支的队列、预算与溢出计数分别统计（见 §4）。

---

## 3. 管线设计

### 3.1 每路 RTSP 管线（主用，强制 UDP + 硬件解码）

```
rtspsrc location=<url> latency=<latency_ms> timeout=<timeout_us>
        protocols=udp udp-buffer-size=<udp_buffer_bytes>
        ! decodebin                      # 自动协商 H.264/H.265；可配置为显式 depay/parse
        ! video/x-raw,format=NV12         # 统一进入 NV12
        ! tee name=t
            # --- 显示/AI 分支（解码后原始帧） ---
            t. ! queue leaky=downstream max-size-buffers=1 ! appsink name=frame_sink drop=1 max-buffers=1 sync=false
            # --- 计数/质量分支（可选，用于 fps/质量采样） ---
            t. ! queue leaky=downstream max-size-buffers=1 ! fakesink name=count_sink sync=false
            # --- 录像压缩分支（解码前，独立接入） ---
            <depay> ! <parse> ! tee_enc ! queue(max-bytes bounded) ! appsink name=enc_sink ...
```

设计要点：

1. **编码自适应**：默认 `decodebin`（H.264/H.265 均可协商）；配置可切换为显式 `rtph265depay ! h265parse` / `rtph264depay ! h264parse` + `mppvideodec`，用于固化已验证路径或排障。**不得硬编码单一编码**。
2. **强制 UDP**：`protocols=udp`；`timeout` 用 `rtspsrc` 源超时把"相机死流"变成可检测错误，而不是无限阻塞。`latency` 与 `udp_buffer_bytes` 来自配置。
3. **认证**：优先设置 `rtspsrc` 的认证属性（`user-id`/`user-pw`），避免拼接解析串；若适配旧入口必须正确编码 URI，且凭据只在内存中使用，日志脱敏。
4. **格式转换（cpu 路径）**：在 worker 内用 librga `imcvtcolor(NV12→BGR888, BT601_FULL)`；转换前按 §2.1 从 caps 计算 `ystride`/`uv_offset`（`hstride = uv_offset/ystride`）。接受两个成功码。
5. **GL 路径（保留，默认关闭）**：`... ! glupload ! glcolorconvert ! qmlglsink`。当前 `qmlglsink` 缺失（§1.3），只有补齐依赖且实测通过后才可成为主路径。
6. **无相机运行**：`test` 源（`videotestsrc`）与 `file` 源（`filesrc ! decodebin`）复用**同一条管线与同一状态机**，用于 ENV-A/C 验证；生产模式缺能力返回 `unavailable`，**不自动造数据**。`origin ∈ {live,file,simulation}` 由显式输入选择，不用帧到达/模型名/编译宏猜测。

### 3.2 媒体层数据结构与现有实现的差距（实现增量）

现状（`app/src/media/src/camera_stream.cpp`）：`appsink` 拉 NV12，算亮度后立即 `unref sample`；公开接口只有 `stats()`；**没有**向消费者提供像素（对应接口契约 D-01）。

本设计要求的增量（不影响 core/wire 契约）：

- 新增 `latest_frame()` 与 `FrameLease`；`FrameDescriptor` 由 media 填充。
- 新增 RGA NV12→BGR 转换与按需 BGR 视图（快照/推理预处理）。
- 新增录像压缩分支与有界滚动缓冲（§7）。
- 把"管线重建"从"仅 tick"升级为"退避到期真实重建"（§6）。

---

## 4. 线程、队列边界、背压与丢帧策略

### 4.1 线程边界

| 线程 | 职责 | 禁止 |
|---|---|---|
| 每路 Stream worker（每相机一个） | 管理该路 pipeline 生命周期、`appsink` 有限拉帧、坏帧/冻结检测、真实重连；向帧租约表发布最新帧；drain `GstBus`（ERROR/EOS/STATE_CHANGED） | 阻塞 GUI 线程；持全局锁做解码/转换 |
| GUI/Qt 主线程 | 事件处理与渲染；消费 `latest_frame()` 快照与状态模型 | 任何网络/磁盘/推理/`pump` 阻塞调用 |
| Qt 渲染线程 | 纹理创建/销毁/上传（消费不可变帧） | 直接访问 `GstSample` 内部地址（须经媒体验证过的 stable 视图或深拷贝） |
| AI worker（可插拔 Detector） | 有界队列消费、按 `plan_inference` 调度、温度守卫 | 持调度锁阻塞 submit/快照/stop |
| 存储写线程 | 单写者：SQLite 事务 + 片段落盘 | 被 GUI 直接调用 |

- `MultiStreamManager::pump_all` 当前是**顺序**调用每路 `pump`；**不得**在 GUI 线程调用，也不得把它描述为六个独立取流线程。实现增量应为每路独立 worker（或明确的有界工作池），并保证单路失败隔离。

### 4.2 有界队列与丢帧策略（逐消费者独立）

| 消费者 | 队列深度/策略 | 溢出行为 | 计数 |
|---|---|---|---|
| 显示 | latest-only，深 1，`drop=1 max-buffers=1`，`leaky=downstream` | 丢最旧，始终保留最新 | `display_dropped` |
| 推理 | 每相机有界（默认 2，配置 `app.ai.queue_depth`），按 `inference_interval` 降频 | **丢最旧帧**并计数；慢消费者不阻塞取流 | `ai_dropped`、`ai_queue_overflow` |
| 录像（压缩） | 有界（按字节/时长），独立分支 | 溢出记 `recording_gap` 并计数；**不**因 UI/AI 慢而丢录像 | `recording_overflow`、`gap_count` |
| GUI 状态更新 | 合并为 latest，`Qt::QueuedConnection` | 合并，不逐帧追加信号 | `ui_coalesced` |

- 硬要求：**UI/AI 慢不得阻塞取流**；所有跨线程队列有界；溢出分别计数且可观测。
- 每路采集在 worker 内有限 pull：产品单次 pull 默认 **≤200ms**；GUI 不等待。`start/重连/stop/infer` 必须有调用方 deadline 与完成回报；底层不可取消时保留资源所有权并上报超时，**不** detach/释放仍在用的内存。

### 4.3 页面切换与纹理复用

- P1/P4/P5 共享同一帧来源与状态真值；**切页不另拉流、不重置媒体服务、不重建会话**（避免 TEARDOWN 抖动）。
- 同一相机多视图复用同一纹理/管线；页面不可见时降载（暂停分析分支或降显示帧率），但会话保持。

---

## 5. 时间、身份与同步

### 5.1 三条时间线必须区分（不得混用）

| 时间线 | 时间字段 | 用途 | 关键约束 |
|---|---|---|---|
| **实时显示时间线** | `FrameRef.ts_ms` / `FrameDescriptor.capture_mono_us`（**接收时刻**） | 新鲜度判定、状态机、presentable | 是到达时间，**不是**相机采集时刻；不得当 UTC 显示 |
| **录像时间线** | 片段 `t0_ms/t1_ms`（UTC epoch 毫秒）；`pts_ns` 辅助 | 片段边界、回放定位、导出 | 以**关键帧边界**与媒体实际覆盖为准；不用期望窗口冒充实际覆盖 |
| **推理结果时间线** | `capture_mono_us` + `result_mono_us` + `latency_ms`（完成 − 接收，**含排队**） | 结果关联、延迟度量 | 必须与帧身份 `(session_id,generation,seq)` 关联；`DetectionResult.ts_ms` 现为结果时刻，真实适配器须明确改用完成时刻并另存 capture/result 两时刻 |

### 5.2 时钟与单位

- `ts_mono_us = TimeBase.steady_now_us()`；传 core 毫秒时**整除 1000**，不得再当微秒重复乘除。
- `ts_wall_ms`：启动必须 `TimeBase.calibrate(wall_ms, mono_us)` 后才能持久化带 UTC 的业务事件；公式 `ref_wall_ms + (mono_us-ref_mono_us)/1000`；重启重新校准。未校准的退化返回值**不得**冒充 UTC。
- `pts_ns`：相机/媒体 PTS，可无效且会重置，**不作**主机 UTC 或健康时钟。
- 片段/回放 `t0_ms,t1_ms` 为 UTC epoch 毫秒；UI 只在显示时按本机时区转换并标明。
- JSON 数值须有限（禁 NaN/Inf）；C++ id/seq 超 2^53−1 时以字符串透传。

### 5.3 多路同步：软件时间配对，**不冒充硬件同步**

- 无 GNSS/NTP 实测，**不得声称跨设备绝对同步**。
- 回放同步按各路**实际解码播放头**的统一 UTC 时间求 `max−min`；超过 `replay_sync_threshold_ms`（默认 300，配置）提示"同步偏差"；缺任一路有效时钟则 `available=false`。
- `setCameraOffsets` 是模型注入接口，**不得**以固定 offsets 冒充真实播放头；用户 seek 递增 request generation，丢弃旧完成回调；position 未到达不得提前显示"已定位"。
- 实时画面（P1/P4）不做逐帧硬件对齐；不同相机到达时间差不做补偿，只做状态与新鲜度判定。

---

## 6. 异常检测与恢复

### 6.1 状态机映射（沿用 core `StreamStateMachine`，不新增状态）

六态 `connecting/live/frozen/offline/reconnecting/degraded`；阈值来自 `thresholds.yaml`：首帧超时 5000ms、无新帧冻结 2000ms、重连退避 1000ms、重连阈值 3 次。

```
OFFLINE ──启动/重建──► CONNECTING ──首帧──► LIVE / DEGRADED
   ▲                        │失败                 │超时无新帧
   │                        ▼                     ▼
   └── 达阈值(3) ◄── RECONNECTING ◄──────── FROZEN / DEGRADED
```

- `source_error` 可直接触发重连；新鲜帧恢复清零计数。
- **`degraded` 有新鲜帧可展示**但须显式质量提示；其他非实时状态必须占位。唯一依据：`may_present_as_live(now)`，**不能只判断 `state==live`**。
- `presentable` 还须租约属于**当前 generation**。

### 6.2 真实重建（实现增量，对应接口契约 D-02）

- 现状 `pump/tick` **未实现** pipeline 重建，`offline` 自身不重试。
- 设计：每路媒体控制器在**退避到期**重建会话（置 `NULL` → 释放 → 重新建流），**每个实际尝试只计一次**；保留失败链计数；**不得**调用会清零计数的 `start()` 伪装恢复。
- `offline` 后仍由控制器有限速率重试或人工重连；**成功必须由真实新帧确认**，不能用 `PLAYING` 返回值确认。
- 单路失败**不得**重排槽位或重启其他五路；`stop`/重建须释放 MPP/RGA 资源。

### 6.3 相机坏状态与 ONVIF 看门狗（默认关闭，受控启用）

- 历史存在"废帧风暴/永不 preroll"坏状态（返厂前红外；可见光未确认）。看门狗（坏帧滑窗、preroll 失败阈值、冷却）作为**可选**能力保留，ONVIF `SystemReboot` 仅在**受控网段且经授权**时启用，事件入 `stream_events` + 维护日志。
- **自动重启相机不是本期默认恢复行为**。

### 6.4 断流/解码错误的分类与重试

| reason | 触发 | 重试策略 |
|---|---|---|
| `TIMEOUT` / `SOURCE_ERROR` | 网络/源超时、bus ERROR/EOS | 按逐路退避重试 |
| `AUTH_FAILED` / `CREDENTIAL_MISSING` | 鉴权失败/变量缺失 | **不自动反复重试**；等配置/凭据变更或显式操作 |
| `UNSUPPORTED` / `MODEL_MISMATCH` / `NOT_READY` | 格式/模型能力不匹配 | 仅输入或模型能力变更后重试 |
| `CANCELLED` / `STALE` / `CONFLICT` | 取消/旧 generation/重复参数不一致 | 按契约返回，不静默当成功 |

- 错误日志须**已脱敏**：不含原始凭据、含鉴权 URL、pipeline/bus 原文。

---

## 7. 录像与回放媒体链路

### 7.1 本机保护片段（唯一本机录像责任）

- **来源**：每路 `tee` 的**压缩分支**（解码前），有界滚动缓冲保存最近 `clip_pre_seconds`（默认 30s）压缩包。
- **触发**：`ClipRequest`（接口契约 §8）：`request_id` 为幂等键，`t0=trigger−pre×1000`、`t1=trigger+post×1000`（默认 −30s/+60s）；前方事件请求 left/primary/right 三路（各路独立报告缺口），其他事件请求来源路。
- **边界**：实际媒体区间 `[t0,t1)`；压缩流以**关键帧解码依赖**决定实际起点；回报 `actual_t0/actual_t1` 与请求窗口，**不得**用期望 90s 覆盖缺失前录/后录。
- **封装**：优先直通复制（避免转码耗算力）到 MPEG-TS 或 Matroska/MP4；写入顺序：**同目录临时文件 → 完成封装/校验 → flush+fsync 文件 → rename → fsync 目录 → 事务发布可用索引**。
- **可用性三态**：完整可用 / Incomplete（部分可播放）/ Unavailable（无片段）；现有 bool `record_available` 仅兼容完整 true，UI 用 `RecordAvailability` + 片段/gap 联合推导。**禁止**把 `.json` 清单、空文件、路径存在或估算 `size_bytes` 当录像。
- **缺口**：任何录像中断写 `recording_gaps(camera,t0,t1,reason)`；P3 时间轴斜纹留白。

### 7.2 回放与导出

- 设计扩展 `IRecordingProvider`：`querySegments/queryGaps/playSegment/exportSegment`，每次带 `request_id` 与 `deadline_mono_us`，返回 `status=ok|partial|unavailable|error` + `reason`（`UNCONFIGURED/UNVERIFIED/NOT_FOUND/AUTH_FAILED/TIMEOUT/UNSUPPORTED/STORAGE_ERROR`）。
- `ok` 空列表只表示"查询成功无覆盖"，**不能**声称可播放；`play/seek` 是异步请求，回报**实际解码 position**；请求接受 ≠ 播放成功。
- NVR：`state=unconfigured`，`query` 返回 `unavailable/UNCONFIGURED`；**不探测未知协议、不猜通道号、不修改 NVR**。`reachable` 仅网络可达，≠回放支持。NVR 不可用时仅当本地对应窗口有真实片段才回退。不生成模拟片段填补。
- 缩略图只用媒体生成的**可读引用**；`thumbSource` 缺基址返回空，不编造 URL。

### 7.3 存储预算与保留（磁盘有限）

- 连续录像**不在本机**：6 路 × ~4.5 Mbps ≈ 27 Mbps ≈ 3.4 MB/s ≈ **12 GB/h**（文档估算）；本地只留保护片段+快照+元数据。
- 单片段大小估算：`bytes ≈ bitrate_bps × clip_seconds / 8`。默认 90s、4.5 Mbps → **约 50.6 MB/片段**。前方事件涉及 3 路 → 约 **152 MB/事件**；最坏 6 路 → 约 **304 MB/事件**。
- 在当前 `max_gb=20`、31G 可用盘下，按 152 MB/事件约 130 次、按 304 MB/事件约 65 次即触顶。**结论：保留策略必须同时受容量与时间约束，且 `max_gb` 与实际 free bytes 分开换算；`max_gb` 不得当磁盘可用量。** 实际 bitrate 必须实测后回填（见 §8.4）。
- 保留：普通片段先淘汰，必要时最旧保护片段仍可淘汰（保护 ≠ 永不删除）；低空间阻止新写并报警；删除/失败同步更新片段索引与事件录像可用性，不残留播放成功标记。

---

## 8. 预算分析与测量方法

每个预算给出：**要求来源 → 计算/目标 → 测量点 → 判据 → 本次状态**。

### 8.1 吞吐（解码/转换/分发）

- 要求来源：6 路 × 1280×720、`display_fps` 前 25/其余 15。像素率 `= 25 + 5×15 = 100 fps × 0.9216 Mpx = 92.2 Mpx/s`；NV12 原始 ≈ 138 MB/s，BGR888 ≈ 276 MB/s。
- 既有证据：ITEM-002 板端 `cpu` 路径 test 源实测 `fps_source=24.98 / fps_display=24.98`、单核 CPU 58.2%、RSS 17.9MB、GPU avg 6.75%（`spike-...-r2b.core_...json`）——**单路**证据，不外推为六路容量。
- 测量点：每路 `count_sink` 的源帧计数、`frames_display`、`rss_mb`、`/proc/stat` CPU、`/sys/kernel/debug/rknpu/load`、thermal。
- 判据：六路并行、显示帧率不低于配置、无持续冻结、内存无持续增长。
- 本次状态：**待实测**（六路真机负载未执行）。

### 8.2 端到端延迟

- 要求来源：低延迟监控与"辅助制动提示"。
- 计算：玻璃到玻璃 ≈ `rtspsrc latency(默认 500ms)` + 解码(~10–30ms) + RGA 转换(5–15ms) + 队列 + Qt 呈现(~16ms) ≈ **0.55–0.65s**。`latency` 是主导项。
- 测量点：`capture_mono_us`（到达）→ `decode_done_mono_us` → `convert_done_mono_us` → `present_mono_us`；或显示器注入时钟/二维码端到端实测（需人工/现场）。
- 判据：稳态端到端 ≤ 目标（由需求/现场确认）；分位数（p50/p95）记录，不以均值掩盖抖动。
- 本次状态：**待实测**（软件时间戳埋点可在主机/板端先建，真机数值待授权）。

### 8.3 时间同步精度

- 要求来源：三联同步回放与事件时间追溯；`replay_sync_threshold_ms=300`。
- 目标：回放三路播放头 UTC 偏差 ≤ 300ms（可配）；实时多路不做硬件对齐。
- 测量点：各路实际解码播放头 UTC 快照，`max−min`；事件 `ts_wall_ms` 与片段边界一致性。
- 判据：偏差超阈值显式提示；缺任一有效时钟 `available=false`。**软件配对，不声称硬件同步。**
- 本次状态：**待实测**。

### 8.4 存储预算

- 见 §7.3。测量点：实测每路 bitrate（`gst-discoverer`/编码分支字节计数）、片段实际大小、free bytes。
- 判据：单事件片段总量与容量/时间保留一致；`max_gb` 与实际 free 分开；低空间告警。
- 本次状态：bitrate **待实测**；算式与阈值已固定。

### 8.5 断流恢复

- 要求来源：HLT-006/E2E-03；阈值 `first=5000ms, freeze=2000ms, backoff=1000ms, threshold=3`。
- 目标：真实重建后由**新帧**确认恢复；单路失败隔离；恢复时间可观测。
- 测量点：`StreamEvent(ReconnectAttempt/Escalated/Recovered)`、`reconnect_count`、重建到首帧耗时（受相机 GOP 影响）。
- 判据：断流→冻结→重连→达阈值升级；恢复后计数清零且画面为新帧；其余五路不中断、槽位不重排。
- 本次状态：**板端文件源可先验**（FI-01 注入），**真机断流待授权**。

---

## 9. 接口与职责边界

| 边界 | 视频系统提供 | 对端职责 | 契约 |
|---|---|---|---|
| 视频 ↔ 视觉推理 | `FrameLease` + `FrameDescriptor`（同 generation 身份、capture 时刻、stride/format）；有界丢旧队列 | 模型适配器消费像素、返回 `InferenceEnvelope`（同身份、result 时刻、latency、boxes/mask） | 接口契约 IF-04；模型能力与精度归模型职责 |
| 视频 ↔ 桌面渲染 | 不可变帧 + `presentable` 快照；渲染路径能力（cpu/gl） | Qt 渲染线程纹理创建/上传/销毁；页面布局与叠加 | 接口契约 IF-02/IF-08；生产 UI 归桌面职责 |
| 视频 ↔ 平台/驱动 | 对 MPP/RGA/GStreamer/sysctl/governor 的运行时依赖清单与能力探测结果 | rootfs/BSP/驱动/包安装/sysctl/governor/显示会话 | §1.2/§1.4；缺口交平台，不在视频层改系统 |
| 视频 ↔ 录像/回放 | 压缩分支、片段、缺口、provider、播放头 UTC | SQLite 事务、保留策略消费、P3 页面 | 接口契约 IF-06/IF-07；DDL 不改 |
| 视频 ↔ 配置 | 读取 `cameras.yaml/app.yaml/thresholds.yaml`（凭据经环境变量） | 配置校验与热更新服务（core） | 接口契约 IF-01；本设计不新增配置字段 |

---

## 10. 视频库与加速路径选型对比（以证据选择）

| 候选 | 能力 | 证据 | 结论 |
|---|---|---|---|
| **原生 GStreamer + appsink** | 精细控制管线、样本所有权、状态、多分支 | `README.md` §4（OpenCV NV12 挂死）、`code/main.cc` 原生取流、本轮元素核对 | **选用**（板端主链路） |
| OpenCV `VideoCapture` | 简单 | 板端 4.5.4 NV12 协商 bug（`cap.open` 挂死） | **否决**（板端取流） |
| **MPP 硬件解码 `mppvideodec`** | H.264/H265 硬解 | 本轮直查存在且 rank primary+1；release 探针 mppvideodec=yes | **选用** |
| 软解 `avdec_*` | 兜底 | 存在（libav） | 备用/排障，非默认 |
| **librga `imcvtcolor` BT601_FULL** | NV12→BGR 硬件转换 | `code/main.cc`；本轮 librga 库存在 | **选用**（配色正确性判据：BGR 三通道均值近似相等） |
| `rgaconvert` 插件 | GStreamer 内转换 | 本轮存在但 rank none；历史绿屏/1x1 误协商 | **否决**（主路径） |
| `glupload+glcolorconvert+qmlglsink` | 零拷贝 GL 上屏 | `qmlglsink=no`（本轮）；ITEM-002 gl 不可用 | **保留候选，默认关闭**，依赖补齐且实测后启用 |
| 板端 ffmpeg/ffprobe | 离线分析 | 本轮存在 | **仅诊断/离线**，不进主循环 |
| 主机 ffmpeg 子进程 | PC 预览 | `tools/cam_viewer.py`、环境 ffmpeg | **仅 PC 侧**，不用于板端产品 |

> 加速路径默认组合：`rtspsrc(UDP) → decodebin → mppvideodec(NV12) → tee → appsink`；显示/分析在 worker 内用 librga 转 BGR（cpu 路径），录像走解码前压缩分支。

---

## 11. 配置项与"设备未知"时的验证需求

- **必须配置化**（不得写死）：RTSP URL/凭据变量、`transport=udp`、`latency_ms`、`timeout_us`、`udp_buffer_bytes`、本地/录像源路径、`render_path`、`display_fps`、推理 `inference_interval/queue_depth/npu_budget/temp_limit_c`、片段 `clip_pre/post`、回放 `replay_sync_threshold_ms`。
- **设备未知时保留配置项 + 验证需求**（本轮未实测）：
  1. 每路实际 codec/分辨率/fps/colorimetry/stride → 由协商 caps 在首次接入时探测并记录，不套用文档预期。
  2. 相机 GOP/关键帧间隔 → 决定片段实际起点与首帧时间，须实测。
  3. 实际 bitrate → 决定存储预算（§7.3）。
  4. TCP 禁用、音频忽略、`/streaming/channels/101` 备选路径 → 保留为配置/文档项，主用当前路径。
  5. NVR 协议/账户/通道映射 → 未提供，保持 `unconfigured`，不猜。
- 四份 YAML 必须**整体加载**；缺文件/非法字段失败并报具体字段路径，缺省可选项保留 `applied_defaults`。不得静默默认。

---

## 12. 诊断、度量点与证据分类

- **每路结构化指标**：`fps`、`last_frame_mono_ms`、`last_frame_wall_ms`、`reconnect_count`、`state`、`quality_flags`、`display_dropped`、`ai_dropped`、`recording_overflow`、`gap_count`、实际 `attempt_id`、`last_error`（脱敏）。
- **日志要求**：结构化、按日滚动；URL/凭据脱敏；错误消息整条脱敏，不只 `redact_url`。
- **能力探针**：必须用直接 `gst-inspect-1.0 <el>` 判定元素能力（§1.3 已证 `--exists` 不可靠），并记录 GStreamer/librga/librknnrt 版本。
- **证据分类（三类源分别留证，不互相替代）**：
  - **真实视频**（ENV-D，真机 RTSP：需授权）→ 实际 caps、fps、延迟、色彩 spread、断流恢复；
  - **文件回放**（ENV-C，录制文件循环 → 解码+RGA 链路）→ 复用历史 ITEM-002 file 源证据的方法，独立留证；
  - **模拟输入**（`test` 源 / FI 注入）→ 状态机、队列、背压、缺口逻辑；
- 静态/主机结论**不得**当板端/真机结论；本设计不替代验证能力的独立验收。

---

## 13. 风险、限制与交接

| 风险 | 影响 | 缓解 |
|---|---|---|
| `mppvideodec` 能力探测被 `--exists` 误判 | 误判不可用/误报通过 | 强制直接 `gst-inspect`（§1.3，已写入任务） |
| 六路并发掉帧/带宽不足 | 显示卡顿 | 显示帧率分级、页面卸载、纹理复用、latency/队列调优 |
| 相机 codec/分辨率未知 | 管线协商失败 | `decodebin` 自适应 + 显式 depay/parse 配置 + 首次探测记录 |
| RGА 版本与 README 不符（dev 2.2.0 vs 1.10.1） | API/行为差异 | 以 `im2d.h` 实际 API 与实测为准；不按版本号假设 |
| 关键帧间隔大 | 片段前录/首帧延迟 | 关键帧对齐起点；实测 GOP |
| 磁盘耗尽 | 录像失败 | 容量+时间双约束、低空间告警、保护片段上限 |
| NVR 不可用 | P3 连续回放受限 | 本机保护片段保证报警闭环；显式 `unavailable` |
| 视频层越界改系统/模型/UI | 违反职责边界 | 缺口登记并交平台/模型/桌面职责 |

**交下游实现（`video` 实现能力）**：按 `docs/video/video_system_tasks.yaml` 有序实现；先交付帧租约/解码/格式转换与状态接入（可在 `file/test` 源验证），再交付录像/回放/恢复；不修改 core/wire 契约、SQLite DDL 与既有 Qt 桥接签名。

**交验证能力**：真实取值（caps/fps/延迟/存储/断流恢复）按环境分层独立取证；本设计只定义测量方法与判据，不宣称已通过。

---

## 附：本次执行事实与未执行声明

- **已执行**：一次受信任 known_hosts 的板端只读 GStreamer/librga/环境核对（§1.2/§1.3），命令有限等待，凭据不在命令行出现，未改系统、未装包、未启动服务、未取流。
- **未执行**：真实相机取流、六路并发、编译、部署、真实录像/回放、断电恢复、任何物理测试。
- 既有 TEST_PASS / RELEASE_READY 保留其原范围，不外推为本设计或视频功能已通过。
