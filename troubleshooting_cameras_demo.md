# RK3588 铁轨异物检测 — 相机接入与 Demo 故障排查手册

> 最后更新：2026-09-19（红外 .64 接入 + 双窗口调试 + RGA 重构后残留绿闪帧复盘 §2.10）
> 适用：RK3588 板端 `rknn_yolov8_seg_demo`（可见光主窗口 + 红外第二窗口）
> 本文记录**实际出现过**的故障：现象 → 原因 → 解决 → 预防。

---

## 0. 设备与凭据速查

| 设备 | 地址 | 用途 | 凭据 | 备注 |
|------|------|------|------|------|
| RK3588 板 | 192.168.0.103:22 | 推理 + 显示 | SSH admin/admin | 工具脚本 `_rk3588.py` |
| 可见光球机（大华 PTZ） | 192.168.1.65 | 主检测窗口 | RTSP admin:ipc12345 | H.264 1280×720 |
| 红外热成像 TC-PTZ-X36GBL | 192.168.1.64 | 红外第二窗口 | **RTSP admin:ipc12345**；网页/ISAPI/ONVIF admin:123456 | H.265 1280×1024，固件 V1.8.104，两套密码不同（见 §2.5） |
| 8 路阵列 | 192.168.0.160–167 | 未来扩展 | admin/123456 | |
| 云训练机 | 36.129.34.186:6001 | 模型训练 | root/mnxw6VY5Bn$NUrYR | |

**RTSP URL 模板（大华风格，两台相机通用）：**
```
rtsp://admin:ipc12345@<IP>:554/cam/realmonitor?channel=1&subtype=0
```
- `subtype=0` 主码流（红外 1280×1024 H.265），`subtype=1` 辅码流（640×512 H.264）
- ⚠️ **禁止加 `protocols=tcp`**（见 §2.6），统一 UDP + `appsink drop=1` 吸收乱序

---

## 1. 健康检查速查（怀疑卡死先跑这几条）

| 检查项 | 命令 | 正常值 |
|--------|------|--------|
| 检测循环是否活着 | `sudo cat /sys/kernel/debug/rknpu/load` | Core0 35–49%（双模型推理） |
| X 窗口是否存在 | `DISPLAY=:0 xwininfo -root -tree \| grep -E 'yolov8\|thermal'` | 两窗口各有 frame |
| RTSP 连接是否在 | `ss -tn \| grep -E '1\.64\|1\.65'` | 两条 ESTAB 到 :554 |
| CPU 占用 | `top -bn1 \| grep Cpu` | us+sy < 40%（8 核） |
| FPS 日志条数 | `grep -c FPS /tmp/demo_run.log` | 每 5 秒一条实时落盘（stdout 已改行缓冲）；断流时 FPS 停止是正常表现（采集线程在重连） |
| 红外灰度是否正确 | `grep 'thermal heartbeat' /tmp/demo_run.log \| tail -3` | BGR 三通道均值相等（~88–90）= 灰度正确；三通道 0/135.x/0 = 绿帧混入（见 §2.10） |
| 双窗口是否还同时冻 | `grep -c 'visible stream stalled' /tmp/demo_run.log` | 0 = 正常（主循环已改为每轮渲染，只有推理按新帧跳过，§2.2） |
| 红外相机停顿/黑帧 | `grep -cE 'stream gap\|skipped empty' /tmp/demo_run.log` | 少量属相机端周期性怪癖（§2.11），黑帧已被过滤不上屏；持续暴涨 → 重启红外相机（看门狗会自动做，见 §2.13） |
| 坏状态看门狗 | `grep -E 'SystemReboot\|skip storm\|preroll failures' /tmp/demo_run.log \| tail -5` | 无输出 = 正常；出现即相机已被/将被自动重启（§2.13），~90s 后应自动恢复 |
| UDP 套接字溢出 | `grep '^Udp:' /proc/net/snmp \| tail -1`（隔段时间两次差分） | InErrors/RcvbufErrors 增量应为 0；增长 → §2.12（rmem 已调 16MB，若增长说明配置丢失） |
| RGA 核状态 | `cat /sys/kernel/debug/rkrga/scheduler_status` | 3 核空闲（pd_ref=0 是正常的，RGA 核空闲即下电） |
| CPU 大核频率（**满载时读**） | `cat /sys/devices/system/cpu/cpu4/cpufreq/scaling_cur_freq` | performance 已持久化（§2.14）：A76=2400000；若回到 interactive 且满载卡 408000 → 大核簇不升频复发（FPS 双峰 17↔9） |

**判读逻辑**：NPU 掉 0 = 检测循环被堵死（采集阻塞，§2.2）；NPU 正常但窗口不动 = 渲染/相机单路问题。

---

## 2. 故障条目（现象 → 原因 → 解决）

### 2.1 红外画面整体纯绿
- **现象**：红外窗口显示一片均匀纯绿，无任何图像内容。
- **原因**：GStreamer `rgaconvert` 插件（rga_api 1.10.1）对解码后 1280×1024 NV12 的 **UV 平面偏移解析错误**，读到全 0 区域。YUV 中 U=V=0 经 BT.601 变换正好是纯绿（BGR 均值 0/135/0 是其数值指纹）。已排除两个假设：不是 10-bit 编码（SPS 解析确认 8-bit Main profile），不是分辨率不对齐（1280/1024 均 16 对齐）——是插件本身的 bug。
- **解决（2026-09-19 最终方案，已上线验证）**：红外抓帧**绕开 OpenCV**，改用原生 GStreamer C API 直收解码器 NV12 → librga `imcvtcolor`（RGA 硬核）→ BGR。实测 BGR 三通道均值完全相等（87.4/87.4/87.4）= 正确灰度；CPU 占用 us 22.7%→**11.0%**。中间踩了三个坑：
  1. **OpenCV 4.5.4 拿不到 NV12**：它给用户提供的 appsink 强制设置自己能消费的 caps（BGR 类），上游 NV12 capsfilter 与之不相交 → 永远等不到 preroll，`cap.open` 挂死。裸 gst-launch 验证 NV12 协商正常 → 定位为 OpenCV 行为。
  2. **librga 有两个成功码**：`IM_STATUS_NOERROR=2` 和 `IM_STATUS_SUCCESS=1`，`imcvtcolor` 返回 SUCCESS。只判 NOERROR 会把成功当失败（日志特征：`convert failed: Run successfully`）。
  3. **解码器空帧 → 上线后残留绿闪帧**：首版方案上线后仍偶发均匀绿，根因是解码器近零占位帧骗过 `== 0.0` 判空——详见 §2.10。
- **教训**：插件级 RGA（rgaconvert element）不可控；应用级 RGA（im2d，显式传 wstride/hstride，hstride 由 UV 平面实际偏移反推）才是 8 路阵列的正确架构。CPU 兜底 `cvtColorTwoPlane` 保留（UV 平面 Mat 必须是 `h/2 × w/2` 的 CV_8UC2，写成 w 宽会触发 OpenCV 断言直接 abort）。

### 2.2 窗口全部冻结（两个窗口同时卡死不动）
- **现象**：检测窗口和红外窗口同时冻结；进程还在，窗口还在。
- **原因**：**采集阻塞在主线程**。任一路相机 `cap.read()`/`cap.open()` 阻塞（典型触发：相机不发关键帧，§2.3），整个检测循环被堵死，推理停止、两个窗口都停止刷新。红外接入初期此问题出现两次（2026-09-19）。
- **诊断**：冻结时看 NPU 负载——掉 0 即循环被堵。
- **解决（已上线）**：**主摄像头和红外都改为独立采集线程**（Grabber 模式：线程内采集、互斥锁交换最新帧 + seq 计数，检测循环只取最新帧、永不阻塞）。主摄像头线程带**断流自动重连**（连续 20 次 read 失败 → release → 重建会话）；rtspsrc 加 `timeout=5000000`（5 秒无数据报错→触发重连）。管道断流时主循环跳过推理（FPS 日志停止 = 诚实的卡顿信号），恢复后自动续上。红外线程同样自动重连（8 秒无帧或 10 秒无 preroll → 重建管道，相机恢复后自动出图）。
- **预防**：新增任何相机一律走采集线程模式；8 路阵列天然要求此架构。
- **补充（2026-09-19 第二阶段）**：采集线程化后仍出现"两个窗口一起卡一下"——第二处卡点在**主循环**：可见光 seq 不变时旧代码直接 `continue`，把 `imshow`/`waitKey` 一起跳过，X11 事件泵停转、红外帧也不再上屏。修复：**窗口每轮都刷新**（draw + imshow + waitKey 常驻），只有推理按新帧跳过；断流时窗口保持最后画面但事件泵不死。配套日志：`main loop: visible stream stalled Xms`（可见光断流 >200ms 才记，上线后为 0 次）。

### 2.3 相机"假死"：RTP 在流但永远出不了图（相机端坏状态）
- **现象**：RTSP 连接正常（`ss` 有连接），gst 日志反复刷 `Redistribute latency`（RTP 包照收），但 `decodebin` 永远等不到 IDR 关键帧，preroll 永不完成，`cap.open`/`read` 永久阻塞。杀掉 demo 单独拉流也失败——与板子无关。
- **原因**：反复建立/中断 RTSP 会话（尤其无 TEARDOWN 的 kill -9）后，相机进入坏状态：**停止发送 H.265/H.264 关键帧**。两台相机（.64/.65 同族 shuncheng 固件）都有此问题。
- **解决**：**重启相机（断电再上电）即恢复**，软件侧无法解救。2026-09-21 起 demo 内置 ONVIF 看门狗自动重启，见 §2.13。
- **预防**：
  - 尽量减少粗暴 kill；demo 停止时最好让管道自然 TEARDOWN。
  - 采集线程化后 demo 具备自愈能力：相机恢复发帧后自动出图，无需重启 demo。
  - "短暂断流"（网络抖动）可通过断流自动重连解决；"相机坏状态"只能重启相机。

### 2.4 rgaconvert 协商出 1×1 分辨率 → 启动即卡死
- **现象**：demo 启动后 cap.open 永不返回，全部窗口冻结，进程活着。
- **原因**：`rgaconvert` 在下游 capsfilter **不写 width/height** 时，协商出 `width=1,height=1` 的 BGR 输出。
- **解决**：主码流管道 caps 已钉死 `width=1280,height=720`（不受影响）；红外管道不用 rgaconvert（见 §2.1）。
- **教训**：rgaconvert 后面的 capsfilter 必须写全宽高。

### 2.5 RTSP 一直 401 Unauthorized
- **现象**：网页/ISAPI/ONVIF 用 admin/123456 全部正常，唯独 RTSP 所有密码都 401（试了 11 个密码 × 2 用户 × 20 种 URL 路径）。
- **原因**：该相机**RTSP 密码与网页密码分账户存储**：网页/ISAPI/ONVIF = 123456，**RTSP = ipc12345**（与 .65 大华相同）。URL 是大华风格 `/cam/realmonitor`（ realm `shuncheng/`）。
- **解决**：`rtsp://admin:ipc12345@192.168.1.64:554/cam/realmonitor?channel=1&subtype=0`。
- **排查方法论**（下次遇到 401 按此顺序）：ISAPI 摘要认证探测 → ONVIF WS-UsernameToken 探测（确认账户本身没问题）→ 裸 socket RTSP Basic/Digest 矩阵 → 找供应商/用户确认 RTSP 专用密码。ONVIF `GetStreamUri` 的坑：要用 `<Transport><Protocol>UDP</Protocol></Transport>`（不是 `<TransportProtocol>`）。

### 2.6 RTSP over TCP 卡在 SETUP
- **现象**：强制 `protocols=tcp` 后部分相机（海康热成像等）卡在 SETUP 阶段无法出图。
- **解决**：**所有测试相机 UDP 都正常**，不要默认加 tcp；丢包/乱序交给 `appsink drop=1 max-buffers=1` 吸收。

### 2.7 demo 读不了 JPEG/PNG 图片
- **现象**：`imread` JPEG/PNG 返回空，BMP 正常。
- **原因**：系统 libgdal 与 OpenCV 的 JPEG 库符号冲突。
- **解决**：测试图用 BMP；RTSP 视频流不受影响。

### 2.8 日志"长时间不动"被误判为卡死
- **现象**：`/tmp/demo_run.log` 几分钟没有新行，以为程序死了。
- **原因**：nohup 重定向后 stdout 全缓冲（4KB），FPS 行约 8–15 分钟才落盘一批。
- **解决**：**已在 main() 里加 `setvbuf(stdout, NULL, _IOLBF, 4096)` 改为行缓冲（2026-09-19），日志实时落盘，此问题根治**；且 abort 前的日志不再丢失。历史遗留日志仍可能滞后，判断死活优先用 §1 的 NPU 负载/窗口/连接检查。

### 2.9 RgaCollorFill 警告与 D 态 ioctl 噪音
- **现象**：日志偶见 `RgaCollorFill` 警告；推理时大量 D 态 ioctl 快照。
- **原因**：前者是 imfill(RGB888 colorfill) 走了 CPU memset 路径（**已修复，勿改回**）；后者是双模型推理时 RKNPU/RGA 的正常现象。
- **解决**：均无需处理，属噪音。

### 2.10 RGA 重构上线后红外仍出现绿色画面（"修了还绿"）
- **现象**：§2.1 的 RGA 方案（原生 GStreamer + imcvtcolor）已上线、验证过灰度正确，但红外窗口仍间歇出现均匀绿色画面。日志特征：一轮运行中启动后**帧 1–5 全是绿色**（BGR 均值 0/135.x/0），暖机后心跳灰度正常；另一轮只绿第 1 帧——**绿帧数量不定**。且当时代码里已有 `Y 均值 == 0.0` 的空帧过滤，为什么没拦住？
- **原因**：mppvideodec 解码器在 **preroll 及断流/重置之后**会发出近零占位帧（filler）：Y 平面均值 0.0x–0.1 **带微弱噪声、不是精确 0**（实测 0.10），UV=0，转出来就是均匀绿。首版判据 `== 0.0` 被噪声骗过；占位帧还可能**连续 1–5 帧**，不是固定第一帧——所以"修改了还是绿"。
- **解决（2026-09-19 已上线验证）**：判空改用**阈值**：`cv::mean(yPlane)[0] < 2.0` → 丢弃并计数，日志留痕 `thermal: skipped empty frame #N (Y mean 0.xx)`。部署后验证：占位帧被正确拦下（`skipped empty frame #1 (Y mean 0.10)`），帧 1–5 起即正确灰度（88.9 三通道相等），心跳稳定，FPS 17–18。
- **常驻监控**：心跳日志每 300 帧打印 `thermal heartbeat: BGR means a/b/c (N skipped)`——三通道相等 = 灰度正确；skipped 计数偶发增长（重连/断流后）属正常，持续增长需关注。
- **教训**：① "空帧"≠全零帧，**判空一律用阈值不用等值**；② 解码器占位帧在任何流重置后都会再现（包括自动重连后），过滤必须**常驻**在取帧路径里，不是只在启动时；③ 数值指纹是判断转换正确性的最快手段：BGR 0/135.x/0 = 绿帧，三通道相等 = 正确灰度。
- **过滤升级（2026-09-21，看门狗频繁触发后仍偶见绿帧）**：阈值 2.0 → **10.0**（流重置期占位帧 Y 均值带噪声可到 ~10；AGC 爬坡顶 1.9、正常画面 ~83，阈值 10 不伤正常帧）+ 转换后新增**绿色伪影检查**——`BGR 均值 G>50 且 B、R < 0.2*G` 即丢弃（白热灰度场景 B≈G≈R 恒成立，B/R 塌向 0 必是 UV 垃圾的解码器重同步畸形帧：Y 能过判空、UV 是旧数据）。日志 `thermal: skipped green artifact frame #N`，计入看门狗 skip 计数。

### 2.11 红外周期性停顿 + 暗帧爬坡（相机内部 AGC/ISP 行为，夜间阵发）
- **现象**：红外窗口偶尔停顿 ~0.5s；黑帧成串且 **Y 均值单调平滑爬坡**（实测 0.60→1.87 连续 14 帧后回正常 ~83）；可见光无此问题。**夜间阵发（每 7–30 秒一次的窗口期），白天几乎干净**。
- **已排除（证据链）**：① **板端丢包**——UDP 溢出修复后（§2.12）InErrors 增量=0、NIC 硬件/驱动计数全 0、jitterbuffer 序列号**零跳号**（GST_DEBUG=rtpjitterbuffer:4，55 秒测试会话仅建连头 6 秒有突发，之后 49 秒干净；丢包=序列号跳号，停顿=无事件）；② **码率爆表**——夜间实测 eth1 全程 5 Mbps（两路合计）；③ **5 分钟 FFC**——用户在相机 UI 确认自动校准为 5 分钟一次，频率不符，最多贡献 1 次/5min；④ **板上处理路径**——纯解码 appsink 60 秒曲线（python gi，无 RGA/转换/推理）每 5 秒 94–113 帧，无灾难性空洞。
- **结论（2026-09-21 定案）**：相机**内部**周期性停发 ~0.5s + 暗帧爬坡（AGC 收敛过程），夜间 AGC 压力大时阵发。属 shuncheng 固件行为。
- **相机 UI 排查结果（全部试完，定案）**：该固件 UI 仅 图像调节（亮度/对比度/平场矫正/背景矫正/手动矫正）+ 图像增强（数字降噪/调色板/图像细节增强DDE）+ 编码 三块——**增益(AGC)、积分时间(曝光) 根本没有暴露**。唯一可调的 DDE（原"图像细节增强 普通模式 等级50"）已于 2026-09-20 关闭，**夜间实测无效**（gap ~1.8 次/分 vs 关闭前 ~1 次/分，同量级）。编码现状：VBR 质量高、上限 4096kbps、H.265、GOP=25（未改；UDP 溢出修复后 GOP 无紧迫性）。
- **实际影响评估**：每次停顿 ~0.5s 丢 ~10 帧后自恢复；暗帧已被 ymean<2.0 过滤不上屏（§2.10）；检测逐帧进行不受损。**建议接受现状**；根治只剩厂商固件升级或换相机。
- **教训**：三层递进排查法：`/proc/net/snmp` Udp InErrors（套接字溢出）→ jitterbuffer 序列号（网络丢包 vs 相机停发）→ 每秒出帧曲线（python gi appsink，验证处理路径）。证据都指向相机内部后才去动相机设置。

### 2.12 UDP 接收缓冲溢出（板端 rmem_max 208KB，昼夜卡顿的叠加因素）
- **现象**：`/proc/net/snmp` Udp 行 `InErrors == RcvbufErrors == 1,924,102`（2 天累计，约占收包 4.7%）；夜间红外每 2–5 秒 gap、黑帧 ~3 帧/s；白天明显好转。
- **原因**：板子 `net.core.rmem_max = net.core.rmem_default = 212992`（208KB）。rtspsrc 默认申请 512KB 接收缓冲被内核压到 208KB，H.265 **IDR 关键帧是连续大包突发**，每个 GOP 边界都塞爆套接字缓冲 → 丢包 → 解码器纠错黑帧 + 重新同步停顿。夜间 AGC 增益高 → 码率高 → IDR 更大 → 溢出更严重（昼夜规律的另一半解释）。gap 周期 ≈ GOP 周期。
- **解决（已上线验证）**：① `sysctl -w net.core.rmem_max=16777216 net.core.rmem_default=4194304 net.core.wmem_max=4194304`，持久化到 `/etc/sysctl.d/99-udp-buffers.conf`（重启保留）；② 两路 rtspsrc 管道加 `udp-buffer-size=4194304`（main.cc）。验证：修复后 30 秒 InErrors 增量 = **0**，gap/黑帧率约减半（剩余部分是 §2.11 FFC）。
- **教训**：高码率 RTSP/UDP 设备调试第一步看 `/proc/net/snmp` 的 Udp InErrors/RcvbufErrors——套接字缓冲溢出是"白天正常晚上卡"的经典原因。

### 2.13 相机坏状态看门狗（demo 内置 ONVIF SystemReboot 自动重启，2026-09-21 上线）
- **背景**：两类相机坏状态（§2.3 关键帧停发 / 2026-09-20 白天自发废帧风暴 398 跳帧/30s、rx 掉到 500kbps）都只能靠重启相机恢复。验证该相机 ONVIF SystemReboot 可用后，把"检测坏状态 → 自动重启相机 → 自动重连"做进了 demo：坏状态从人工断电变成 **~90s 无人值守自愈**。
- **ONVIF 事实（已验证）**：端点 `http://192.168.1.64/onvif/device_service`（**80 端口**，GetSystemDateAndTime 免认证即可探活）；WS-UsernameToken **digest** 认证（PasswordDigest = base64(sha1(nonce+created+password))），admin/**123456**——与 RTSP 密码 ipc12345 不同（§2.5）。SystemReboot 真重启（OEM 没有做成空操作）。
- **实现（main.cc，零依赖）**：内嵌 SHA-1 + base64 + 原生 socket HTTP POST（不引 libcurl/openssl）；ONVIF 账号密码为文件内宏，相机 IP 从 RTSP URL 的 `@host` 提取。两类触发条件（共用 **10 分钟冷却期**）：
  1. **跳帧风暴**：30 秒滑窗内 >150 次空帧（正常夜间 AGC 阵发 ≤~12 次/30s，风暴实测 ~398 次/30s）→ 日志 `thermal: skip storm detected (N skips in 30s) -> ONVIF SystemReboot ...`
  2. **preroll 楔死**：连续 20 次无 preroll（≈4 分钟；阈值必须覆盖相机断电重启时 ~7 次启动期重试，避免重启循环）→ 日志 `thermal: N preroll failures -> ONVIF SystemReboot ...`
  两种触发都有 `thermal: ONVIF SystemReboot -> HTTP xxx` 留痕；相机重启 ~90s，期间既有 8s 无帧重连循环自然兜住，恢复后自动出图。
- **端到端自测（已做，结论可靠）**：临时 `sed` 把 `PREROLL_FAIL_LIMIT` 20→2 + `iptables -I OUTPUT -d 192.168.1.64 -p tcp --dport 554 -j REJECT`（**只屏蔽 RTSP，保留 HTTP 80**——先重启相机会让 ONVIF 连不上、验证不了认证）→ 看门狗真实触发 → **HTTP 200** → 相机重启 → 自动重连 → 心跳 85.4 三通道相等。C++ 版 SHA1/digest/SOAP/socket 全链路验证通过，正式版已恢复阈值 20 并部署。
- **注意事项**：① 相机重启瞬间解码器吐畸形缓冲，出现一小簇 `thermal RGA convert failed`（全历史 55 条，自限不复发，无需处理）；② 看门狗**不针对**夜间 AGC 阵发停顿（§2.11）——那是每 0.5s 自恢复的行为，skip 阈值也够不到（12 vs 150）；③ 冷却期防重启循环（病态上限 144 次/天），靠日志监控发现。
- **工具**：`E:\work\rail_seg_train\onvif_test.py`（板上 `/tmp/onvif_test.py`，纯标准库）：`python3 onvif_test.py probe|info|reboot`。
- **实战记录（2026-09-21 ~11:00，首次自动处置）**：白天再次自发进入坏状态（3 天第 3 次，频率升级）。看门狗 30s 内检测到（172 skips/30s）→ ONVIF 重启 HTTP 200 → 相机重启后**再次进入坏状态** → 10 分钟冷却期满第二次触发（399/30s）→ 重启后彻底恢复。全程无人干预，故障窗口 ~15 分钟。**教训**：① 软重启不保证一次治愈（首次出现"重启救不回来"，第二次才好）——10 分钟冷却期拉长了故障窗口，可考虑缩到 3 分钟；② 风暴期间可见光流其实没断（stalled=0 全程为 0），只是 FPS 19→13 波动 + 两窗并排，用户观感"双窗同卡"——判读先看 stalled 计数再下结论；③ 坏状态频率升级 + 软重启偶发失效 → 相机硬件恶化嫌疑上升，换机/找厂商优先级提高；④ 当时 30 分钟内还有 8 次 RGA `failed to map job info`（dmesg，demo 进程），与风暴无因果（风暴窗口内 RGA 零新增），属另一独立噪声。
- **实战记录（2026-09-21 ~16:20–16:45，复发加速）**：CPU 调频器修复后双路 FPS 21–24 稳定，但坏状态当天第 4、5 次发作（~16:20 demo 启动后即风暴，看门狗自动重启 2 次，短暂恢复又复发）。16:42 实测：skips +845/分钟、rx 满 5Mbps、InErrors=0——编码器楔死仍在发满速废帧。手动补第三次 ONVIF 重启 → ~100s 后恢复（心跳 86.6，skips 增速 0）。**软重启仍有效但复发间隔缩短到小时级**；演示用途下"卡 2 分钟自愈"已成常态，正式部署必须换机/返修。
- **实战记录（2026-09-21 ~17:05–17:25，子码流验证：坏状态不分码流）**：为绕开坏状态把红外切到 H.264 子码流（640×512，subtype=1，decodebin 自动适配、demo 零改动）。连接时即遇废帧潮（900 跳，Y 均值 2.3–3.5——新阈值 10 全部拦住，无绿帧漏网）→ 看门狗重启 → 恢复干净。但 5 分钟监控窗口内**子码流又复发 1 次**（看门狗再重启）+ 31 次 ~500ms 停顿，平时废帧率 ~0.8/s 低烧不断。**结论：主/子码流一起废，整机编码子系统失效，且重启后复发间隔已缩到分钟级（当天演变：天级 → 小时级 → 分钟级）。切子码流方案作废**，只剩断电重启 + 厂商返修/换机。厂商说明见 `thermal_camera_vendor_report.md`。

### 2.14 FPS 双峰 17↔9 交替（RK BSP interactive 调频器不给 A76 大核簇升频，2026-09-21 定案修复）
- **现象**：demo FPS 双峰交替（~17 ↔ ~9，各持续 1–2 分钟，周期性反复）；启动后前 1–2 分钟 stall 密集（数百次 400–700ms 停顿）后自愈；用户观感"画面一阵一阵卡"。此前一天里"720 模式 9.4fps vs 1024 模式 16fps"的差异其实是测量时机落在双峰不同相位，**分辨率无关**。
- **排查链（三个假设依次排除，证据在案）**：
  1. **RGA 对齐/缩放假设**（用户提出）：管道级 A/B 探测 `rga_probe.py`（同 depay/解码链，只换转换环节）——`rga720`（缩放）与 `rga1024`（原生）都满速 ~25fps/5s=126 帧，dmesg RGA 错误零新增（281 纹丝不动）；demo stall 期间也无新增。RGA 全案排除。
  2. **OpenCV VideoCapture 包装假设**：主摄像头抓取器已换裸 GStreamer C API（MainGrabber，见 §3.1 附注），**双峰依旧** → OpenCV 排除（换架构保留，属净收益）。
  3. **热节流假设**：全部 thermal zone 34°C，排除。
- **抓现行（决定性）**：绑核反转实验——`taskset -apc 4-7`（A76）→ **9.6fps**；`taskset -apc 0-3`（A55）→ **15–17fps**。大核比小核慢一倍 = 只能是频率问题。随即读 `scaling_cur_freq`：**A76 四核满载全部卡死 408MHz（最低档，表上限 2.4GHz）**，A55 能升到 1008MHz。interactive 调频器对大核簇 load tracking 失效，从不升频。主循环线程被调度器迁到 A76 的时段 = 9fps 相位，迁回 A55 = 17fps 相位。双模型推理 + 采集线程共享的"D 态 ioctl"（§2.9）与双峰无关。
- **修复（已上线验证 + 持久化）**：全 8 核切 `performance`（A76@2.4GHz / A55@1.8GHz）→ FPS 锁定 **22–24.4**（相机满帧率 25 扣除开销），连续 9 分钟无回落、无双峰。持久化：`/etc/systemd/system/cpu-performance.service`（oneshot，已 enable，重启生效）。板温 34°C 无热风险。
- **工具**：`E:\work\rail_seg_train\rga_probe.py`（板上 `/tmp/rga_probe.py`）：`python3 rga_probe.py <rtsp_url> nv12|rga720|rga1024|cpu720|cpu1024 [秒]`——管道级 A/B 基建，今后任何"卡顿在管道还是应用"的问题先跑它。
- **教训**：① "FPS 双峰交替"优先查 `scaling_cur_freq` 而不是应用层；**必须在满载时读**（空闲时全核低频会误导）；② 绑核反转（大核更慢）是调频器问题的决定性指纹；③ RK BSP interactive 调频器对 A76 簇失效是板级坑，固定部署板直接 performance；④ 排查"分辨率/编码/相机"三连排除后，别忘了板子自身状态（CPU/GPU/NPU/DDR 时钟）这一层。

---

## 3. 架构要点（为什么这样设计）

1. **采集必须线程化**：任何阻塞调用（cap.open/read、网络 IO）绝不放检测主循环。模式：`Grabber{ 线程 + mutex + 最新帧 }`，检测循环每轮取最新帧（丢帧保新鲜）。**主摄像头抓取器已换裸 GStreamer C API（MainGrabber，2026-09-21）**：appsink 直收 rgaconvert 输出的 BGR（`gst_video_frame_map` + stride 深拷贝），与 ThermalGrabber 同架构；OpenCV VideoCapture 包装层退出帧路径（曾疑卡顿元凶，§2.14 洗清但新架构更统一）。连续 20 次空拉（≈2s，>正常 AGC 停顿 <1s）重建会话。
2. **断流自动重连**：采集线程内 read 连续失败 → release → 重新 open（可带重试间隔）。能自愈"短暂断流"；"相机坏状态"（§2.3）自愈不了，但也不拖垮检测循环——最后一环由坏状态看门狗补上（§2.13，ONVIF 自动重启相机）。
3. **RGA 使用原则（硬件优先）**：
   - 管道内 `rgaconvert` 插件有协商与 UV 平面 bug（§2.1/§2.4），只在 caps 全钉死且验证过的场景使用（主码流 720p 已验证可用）。
   - **新增相机/更高分辨率一律走应用内 im2d（已落地）**：原生 GStreamer appsink 直收 NV12 → `imcvtcolor`（显式 wstride/hstride）→ BGR。管道内零 CPU 转换，转换全部进 RGA 硬核。注意 §2.1 的三个坑（OpenCV 拿不到 NV12、双成功码、全零首帧）。
   - 红外主码流实测：CPU 占用 us 22.7% → 11.0%，转换零 CPU。
4. **红外用主码流**（用户决策）：辅码流 640×512 距离远时人形像素不足，主码流 1280×1024 保留细节。

---

## 4. 工具与操作备忘

### 4.1 `_rk3588.py`（SSH/SFTP 工具）
```bash
python _rk3588.py "远程命令"                        # 执行命令（⚠️ 没有 run 子命令，第一条参数就是命令）
MSYS_NO_PATHCONV=1 python _rk3588.py --put "E:/work/xxx" /tmp/xxx   # 上传（必须 Windows 盘符路径）
python _rk3588.py --get /tmp/xxx "E:/work/xxx"      # 下载
```
- Windows 控制台 GBK 报错时加 `PYTHONIOENCODING=utf-8`
- nohup 启动会占住 SSH 通道报 "failed"——是假象，另开命令验证即可
- `--put` 的远程路径**必须绝对路径**（`~/...` 不被 SFTP 展开报 ENOENT）；远程命令里的远程命令链若含 `pkill -f 模式`，模式匹配会打到同链 cmdline 里的程序名字符串（如 cp 路径里的 rknn_yolov8_seg_demo）把自己 bash 杀掉——kill 和 launch 必须分两条命令（§4.2）

### 4.2 重启 demo 标准流程（kill 与 launch 必须分开两条命令，否则 pkill 会自杀）
```bash
# 第 1 条：杀进程（[o] 防止匹配到 grep 自身）
pkill -f 'rknn_yolov8_seg_dem[o]'
# 第 2 条：启动
cd ~/rknn_model_zoo/install/rk3588_linux_aarch64/rknn_yolov8_seg_demo && \
nohup env DISPLAY=:0 ./rknn_yolov8_seg_demo \
  model/yolov8s-seg.rknn model/yolov5s.rknn \
  'rtsp://admin:ipc12345@192.168.1.65:554/cam/realmonitor?channel=1&subtype=0' \
  'rtsp://admin:ipc12345@192.168.1.64:554/cam/realmonitor?channel=1&subtype=0' \
  > /tmp/demo_run.log 2>&1 &
```
- 参数顺序：模型1 模型2 可见光URL [红外URL] [宽 高]
- 编译：`~/rknn_model_zoo/examples/yolov8_seg/cpp` 下 `bash build.sh`（输出到 install/.../）

### 4.3 编码格式探测（判 8-bit/10-bit、分辨率）
- `sdp_probe.py`（板端 /tmp/）：裸 socket DESCRIBE，打印 SDP 的 rtpmap/fmtp；H.265 `profile-id=1` Main(8-bit)、`=2` Main10(10-bit)。无 profile-id 时解析 sprop-sps 的 general_profile_idc。

### 4.4 诊断现场快照（冻结时抓证据）
```bash
sudo cat /sys/kernel/debug/rknpu/load          # NPU 是否归零
PID=$(pgrep -f 'rknn_yolov8_seg_dem[o]' | tail -1)
cat /proc/$PID/task/$PID/syscall               # 主线程卡在哪个系统调用
sudo cat /sys/kernel/debug/rkrga/scheduler_status
DISPLAY=:0 xwininfo -root -tree | grep -E 'yolov8|thermal'
```

### 4.5 网络/流层诊断工具（2026-09-20 卡顿排查沉淀，三板斧）
按顺序用，证据全部指向相机内部才去动相机设置（案例见 §2.11/§2.12）：

| 步骤 | 命令 | 判读 |
|------|------|------|
| ① 套接字溢出 | `grep '^Udp:' /proc/net/snmp \| tail -1`（隔 30s 两次差分） | 第 4 列 InErrors、第 6 列 RcvbufErrors 增长 = 板端缓冲溢出（§2.12）；冻结 = 链路无溢出 |
| ② 丢包 vs 停发 | `GST_DEBUG=rtpjitterbuffer:4 gst-launch-1.0 rtspsrc location='<红外URL>' latency=500 ! decodebin ! fakesink 2>/tmp/jb.log`，再 `grep -c 'Add Lost timer' /tmp/jb.log` | **丢包 = `Add Lost timer for #N` 序列号跳号；相机停发 = 无任何事件**（序列号连续只是安静）。建连头几秒的突发属正常，看其后的窗口是否干净 |
| ③ 每秒出帧曲线 | 上传 `E:\work\rail_seg_train\fps_probe.py` 到板上后 `python3 /tmp/fps_probe.py '<红外URL>' 60` | 每 5 秒帧数 ≈ 相机帧率×5 且平稳；灾难性低谷 = 解码/处理路径停顿（本例相机 ≈20fps → 每 5 秒 ≈100） |
| 链路码率 | `R1=$(cat /sys/class/net/eth1/statistics/rx_bytes); sleep 30; R2=$(cat ...); echo $(( (R2-R1)*8/30/1000000 ))` Mbps | 夜间 5 Mbps（两路合计）为正常基线；暴涨 → 相机端码率问题 |
| 网卡驱动层 | `ethtool -S eth1 \| grep -iE 'miss\|fifo\|overflow'`；`cat /sys/class/net/eth1/statistics/rx_dropped` | 全 0/不增长 = 硬件驱动不丢包；ring 默认 RX 512 |

- fps_probe.py 依赖 PyGObject（板上 python3 有 gi）；fpsdisplaysink 元件板上没有、identity:4 不吐帧日志——别在这两条路上浪费时间。
- 板上临时探针脚本放 /tmp（重启即失），源码备份在本仓库根目录。
