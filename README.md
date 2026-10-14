# RK3588 铁轨异物检测 —— 交接文档（2026-10-13）

> 给后续开发监控系统的 agent:本文档 + `code/` + `models/` + `tools/` 是完整现状快照。
> 请通读后再动手。视觉验证（看图像/视频内容）必须由用户完成，agent 只写代码和文字。

---

## 1. 当前例子目标

铁路沿线（轨道区域）的**异物/人形入侵检测**：
- **可见光相机** (.65)：yolov8s-seg 分割轨道区域 + yolov5s 检测人形，判"人在轨道区"便报警
- **红外相机** (.64)：热成像灰度图，夜间/大雾可见光失效时的备份链路（当前 demo 只显示不推理）
- 终态部署：NVR 录 7 路 + RK3588 显示 7 路（算力只够显示，不够 7 路全检）；报警的判定在板端
- 四份历史文档在 `E:\work\rail_seg_train\`（PROJECT_GUIDE.md 已复制到本目录，含故障排查手册 troubleshooting_cameras_demo.md）

## 2. 硬件现状

### 2.1 RK3588 板（192.168.0.103 wlan / 192.168.1.200 eth1）
- SSH: `admin / admin`（连接工具见 §6）
- OS: Ubuntu 22.04.5 LTS, 内核 6.1.141 aarch64
- 内存 7.7Gi（已用 1Gi），磁盘 / 57G 已用 24G
- **CPU governor 已切 performance**（systemd 服务 `cpu-performance.service` 常驻——interactive governor 不升 A76，FPS 17↔9 双峰的根因，勿回退）
- **NPU**: rknpu 驱动包 2.3.0a，运行时库 librknnrt **2.3.2**，推理时 NPU Core0 ~35-49%
- GStreamer 1.20.3（板端取流全靠它）；OpenCV 4.5.4（板端，有 NV12 协商 bug，详见 §4）
- Python 3.10.12 + cv2 4.5.4
- **网络**：eth1 有线接相机网段 192.168.1.x（**已打通**，2026-10-13）；wlan0 接 192.168.0.x（调试网段），SSH 走 wlan
- **UDP 缓冲已扩**：`/etc/sysctl.d/99-udp-buffers.conf` rmem_max=16MB/default=4MB（H.265 IDR 突发溢出曾是 InErrors 来源，勿删）
- 板→PC 传输工具：`tools/_rk3588.py`（§6）

### 2.2 可见光相机 .65（Dahua PTZ）
- RTSP: `rtsp://admin:ipc12345@192.168.1.65:554/cam/realmonitor?channel=1&subtype=0`（H.265 1280x720 main）
- 网页/ONVIF/onvif-onvif123（ContinuousMove 坏等 quirks 见 memory dahua-ptz-onvif-quirks）；serial CA-HZ4025GBCS2-A（变焦倍数未确认）
- 很稳定，Prototype 上一直 ~25fps

### 2.3 红外相机 .64（TC-PTZ-X36GBL 热成像）
- **2026-10-10 左右返厂重新烧录固件**，变化：
  - 主码流从 H.265 改为 **H.264 Baseline 1280x1024**（yuvj420p）
  - 当前取流地址（PC/板端均已验证）: **`rtsp://admin:ipc12345@192.168.1.64/cam/realmonitor?channel=1&subtype=0`（不带端口）**
  - 海康风格 `:554/streaming/channels/101` 同样可用
- RTSP 密码 ipc12345（网页=123456；ONVIF 不校验鉴权——任何密码都过，ONVIF SystemReboot 可被任意主机触发）
- 强制 UDP，**禁止 protocols=tcp**（卡 SETUP）
- 历史坏状态（废帧风暴、preroll 不出）在返厂前存在；板端已有 watch dog 自动 ONVIF 重启兜底；返厂后 PC 端通宵 15.3h 零坏帧
- JPEG 快照通道独立：`http://192.168.1.64/onvif-http/snapshot?Profile_1`（admin/123456），编码崩溃时的降级通道
- 板端红外转换：**必须走 librga (im2d) NV12→BGR (BT601_FULL)**，BGR 三通道均值相等=转换正确

### 2.3.1 新普通球机 .160（2026-10-13 打通，试接入第 3 路）
- **RTSP:`rtsp://admin:123456@192.168.1.160:554/h264/ch1/main/av_stream`**（老海康风格）。密码是 **123456**（口述的 12345 会 401）；新海康风格 `/streaming/channels/101` 同样可用
- 编码 **H.265 Main 1280x720 25fps** + G.711u 音频轨（ffmpeg 解析时选视频流或 -an）
- **传输强制 UDP**：`-rtsp_transport tcp` 反而 401（与 .64 同脾气 —— 已验证）
- 大华风格 `cam/realmonitor` 404 不可用；厂商指纹（vue+h5player、nginx、双端口 8000/554，CGI 全 404）非大华非海康标准壳，疑安卓 IPC 方案，不影响取流
- 用户中途改过一次网段才上线；此后该机在 192.168.1.x。PC 已用 gyan ffmpeg 9.0 拉流成功；OpenCV 内置 Lavf61 是否兼容**未验证**（PC 端接入 viewer 请直接用 FfmpegCapture 路数，别赌 OpenCV）

### 2.4 疑似 NVR（192.168.1.4）
- 正在录 .64/.65；**多消费者并发已验证 OK**（NVR录 + PC viewer + 板端 demo 三方同时拉流无冲突）
- 网络：单段调换机 100Mbps（PC↔调换机口被协商成 100M）。**7 路终态必须千兆**（每路 ~4-5Mbps，H.265 主码流）——列为开工前事项
- 两网段（192.168.1.x vs 192.168.0.x 板端 wlan）虽已通过 eth1 打通，但板端 eth1 属 192.168.1.200/24 static by NetworkManager（noprefixroute）

## 3. 程序现状（板端 demo）

- 源码：`code/`（板端拉回，与板端 `/home/admin/rknn_model_zoo/examples/yolov8_seg/cpp/` 一致）
  - `main.cc` (45KB, 核心入口)
  - `yolov5_person.cc/.h`, `yolov5_postprocess.cc/.h`, `person_measure.cc` （人形检测+测量）
  - `postprocess.h`, `yolov8_seg.h`, `rknpu2_postprocess.cc`（yolov8-seg 后处理）
  - 编译产物也可以直接作备份: `code/rknn_yolov8_seg_demo` (aarch64 ELF)
  - 其它依赖头（image_utils/c pointers/drawing 等）在仓库 `examples/yolov8_seg/cpp/` 与 `utils/` 下，需要重建构建树请参考仓库根 rknn_model_zoo 的构建脚本
- 二进制：板端 `/home/admin/rknn_model_zoo/install/rk3588_linux_aarch64/rknn_yolov8_seg_demo/`
  - lib/librga.so, lib/librknnrt.so
  - model/yolov8s-seg.rknn (13.3MB), yolov5s.rknn (8.5MB), rail_labels.txt, coco_80_labels_list.txt
- 双模型：yolov8s-seg（轨道分割）+ yolov5s（人形，COCO person），输入均 640x640 NHWC INT8
- **红外窗口架构**（main.cc 内）：
  - **可见光 & 主推理管线**：原生 GStreamer（rtspsrc→rtph265depay→h265parse→mppvideodec→rgaconvert→BGR appsink），独立 CameraGrabber 线程
  - **热成像管线**：`rtspsrc→decodebin→video/x-raw,format=NV12→appsink` 主码流每帧走 librga `imcvtcolor(NV12→BGR, BT601_FULL)`，独立 ThermalGrabber 线程。**decodebin 自动协商 H264/H265 → 返厂后 .64 改 H.264 天然兼容，无需改板端代码（2026-10-13 验证）**
  - **为什么绕开 OpenCV**：OpenCV 4.5.4 给 appsink 强制 BGR 类 caps，上游 NV12 capsfilter 永远协商不上→ cap.open 挂死（当时决策；勿改回）
  - 常驻 draw+imshow+waitKey 每轮执行，仅推理按新帧跳过（避免 X11 事件泵停转→双窗口同冻）
- **看门狗**（main.cc 内嵌，零依赖 SHA1/b64/socket）：
  - 触发条件：30s 滑窗 >150 坏帧（skip storm）或连续 20 次 preroll 失败（≈4min）
  - 动作：ONVIF SystemReboot 到 .64（10 分钟冷却），相机重启 ~90s，自动重连
  - 2026-09-21 上线，端到端自测通过
- PC 端 viewer（`tools/cam_viewer.py`）：独立 FfmpegCapture 子进程（gyan ffmpeg 9.0）取流，Tk 窗口，详细统计/坏帧过滤/标记卡顿按钮。**PC 端 OpenCV 自带 FFmpeg(Lavf61) 与返厂后 .64 协商不上——这是 2026-10-12 的关键教训**，详见 memory 与 troubleshooting 手册
- 板端 demo 当前状态（2026-10-13 14:32 为止）：nohup 正在运行 `/tmp/demo_run.log`，两路各 ~22fps，0 坏帧

### 运行命令（板端）
```bash
cd ~/rknn_model_zoo/install/rk3588_linux_aarch64/rknn_yolov8_seg_demo
nohup env DISPLAY=:0 ./rknn_yolov8_seg_demo \
  model/yolov8s-seg.rknn model/yolov5s.rknn \
  'rtsp://admin:ipc12345@192.168.1.65:554/cam/realmonitor?channel=1&subtype=0' \
  'rtsp://admin:ipc12345@192.168.1.64/cam/realmonitor?channel=1&subtype=0' \
  > /tmp/demo_run.log 2>&1 < /dev/null &
```

## 4. 已知的坑（监控系统开发必读）

1. **板端 OpenCV 4.5.4 不能拉 NV12 的相机流**——用原生 GStreamer（现有架构）或独立 ffmpeg 子进程（PC 路数）
2. **.64 强制 UDP**；TCP 传输会卡 SETUP 反复重试（NVR 添加 .64 时也须选 UDP）
3. **RGA 坑**：imcvtcolor 返回 SUCCESS(1) 也是成功（NOERROR=2 也是成功），别只判 NOERROR；绿色伪影帧 = 解码器近零占位帧，过滤判据 `Y mean < 10.0` + 绿色三通道判定（G>50 且 B/R<0.2G）
4. **CPU governor 勿回退** interactive（FPS 双峰根因）
5. **日志缓冲**：板上 stdout 若改小缓冲会出假卡顿误判（现 setvbuf 行缓冲，勿动）
6. libgdal 冲突：板端 cv2 读写 JPEG/PNG 不可用（用 BMP），RTSP 取流不受影响
7. **多消费者并发 OK**，但反复 kill 会话（无 TEARDOWN）历史上曾诱发 .64 坏状态——重连用相机自然超时+TEARDOWN 语义（现有 grabber 已如此）
8. 板端 SSH 偶发连接重置，等 ~20s 重试；长操作用板端 nohup + 轮询

## 5. 给监控系统 agent 的建议方向（现状清单，方案待你设计）

现有监控点（散在 demo 日志/PC viewer 日志）——集中/结构化是当前缺口：
- 帧统计：FPS 5s 滑窗（板端 demo "FPS:" 行）、帧计数
- 红外质量：坏帧过滤计数（seq skip 计数 + Y 均值 + 绿色三通道）、BGR heartbeat
- 流健康：gap (>800ms 才记)、断流重连、preroll 失败、看门狗触发/Reboot 事件
- 硬件：NPU load `/sys/kernel/debug/rknpu/load`、CPU governor、UDP InErrors `/proc/net/snmp`
- PC 端已有 `cam_viewer_log.txt`（文本日志）和 Tk 界面（ peppers "标记卡顿"按钮）
- 待补：帧 ts/detection 事件的持久化（sq/SQLite）、告警（阈值超则通知）、多路扩展（7 路终态）
- **.camera 清单现状（3 路在线，全部 UDP!!）**：
  | ID | IP | 类型 | RTSP URL | 编码 | 密码 | 备注 |
  |---|---|---|---|---|---|---|
  | visible | 192.168.1.65 | Dahua PTZ | `rtsp://admin:ipc12345@192.168.1.65:554/cam/realmonitor?channel=1&subtype=0` | H.265 720p 25fps | ipc12345 | 主推理链路 |
  | thermal | 192.168.1.64 | TC-PTZ-X36GBL | `rtsp://admin:ipc12345@192.168.1.64/cam/realmonitor?channel=1&subtype=0` (无端口) **H.264 Baseline 1280x1024**（返厂后） || ipc12345 | 板端 decodebin 天然兼容，看门狗 ONVIF 重启兜底 |
  | dome3 | 192.168.1.160 | 新球机(非大华非海康) | `rtsp://admin:123456@192.168.1.160:554/h264/ch1/main/av_stream`(或 101) | H.265 720p 25fps + G.711u | 123456 | 2026-10-13 打通，PC ffmpeg 拉流常见 |
- **三路统一注意事项**：全部强制 UDP（TCP 会卡/401）；RTSP 密码 ≠ 网页密码；PC 取流统一走 gyan ffmpeg 子进程路数（OpenCV 内置 Lavf61 对 .64 不兼容）

## 6. 板端连接方式（PC 侧）

`tools/_rk3588.py` 用法：
```bash
PYTHONIOENCODING=utf-8 python E:/work/rail_issue/rk3588_handover/tools/_rk3588.py "ls /"
# 上传/下载：MSYS_NO_PATHCONV=1 (git-bash 需要) python _rk3588.py --put/--get
```
paramiko SSH 到 192.168.0.103 admin/admin。长任务板端 nohup + 轮询；pkill 单独发。

## 7. 本目录清单

```
rk3588_handover/
├── README               ← 本文档
├── PROJECT_GUIDE.md             （项目总纲，历史）
├── troubleshooting_cameras_demo.md （相机故障排查手册，历史，返厂前状态）
├── code/                板端核心源码 + aarch64 编译产物
├── models/              yolov8s-seg.rknn / yolov5s.rknn / rail_labels.txt
└── tools/               PC 端 cam_viewer.py / _rk3588.py / onvif_test.py / ptz_control.py
```

板上还有 NPU 推理运行时库 `librknnrt.so 2.3.2` 与 `librga.so 1.10.1`（库文件未复制，板上 lib/ 均有，体积可从板端随时取）。
