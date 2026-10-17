# 视频系统软件测试设计（TestDesignArtifact）

- Artifact: TestDesignArtifact（视频系统测试设计）
- Capability: `software_tester.software_test_design`
- 日期: 2026-10-17
- 主案: `n011_software_tester_software_test_design`（CALL-0dc49465-951f-4206-8db3-94dacb8e6826）
- 数据源: `docs/video/VIDEO_SYSTEM_DESIGN.md`、`docs/video/video_system_tasks.yaml`（ITEM-001..017）、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0、`docs/hmi/TEST_DESIGN.md` v2、`docs/hmi/E2E_PLAN.md` v2、`app/config/{cameras,app,thresholds}.yaml`、`app/src/media/**`、`app/tests/core/**`、`docs/hmi/test_evidence/ENV-B-dependency/*`
- 接口: 视频层适配 InterfaceContract `contract_version=1.0.0`；视频测试设计不新增 core schema、C++ ABI、YAML 字段或 SQLite DDL。

---
## 1 范围

### 1.1 输入与覆盖域

- 上游视频系统设计（已核对 DESIGN_READY 版本）。17 个任务项 ITEM-001..017。
- 测试分层：ENV-A0（core/media 主机单元测试，GStreamer/media 依赖桩）、ENV-A（主机+媒体桩模拟）、ENV-B（板端构建 GStreamer/librga 真实依赖 + 离线媒体如 filesrc/gallery）、ENV-C（板端 file/test 源）、ENV-D（板端真实 RTSP 六路）。
- 依赖能力: `video_systems_engineer.video_system_design`（上游设计）、`embedded_linux_engineer.video_implementation`（待执行实现）、`software_tester.video_software_verification`（执行验证）。

### 1.2 范围排除

不覆盖：模型精度/误报漏报（属模型能力）、电气上电、系统级断电重启（归 ENV-E 桌面回归已登记项）、NVR 厂商协议（未配置）。不做性能容量定论（ITEM-017 定义仪器，测量值属执行轮）。

---
## 2 测试层与环境

| 层 | 载体 | 测试对象 | 覆盖任务 | 说明 |
|---|---|---|---|---|
| ENV-A0 | 主机 + GStreamer stub（无真实 rtspsrc） | pipeline_factory 配置 → GStreamer 元素映射（不真正 spawn）、camera_stream 状态机单元（模拟 frame 回调）、帧身份/租约逻辑、RollingBuffer 容量/淘汰、ClipRequest 幂等逻辑、录像 provider 状态机 | ITEM-001..005, 007..009, 011..016 逻辑 | 用 `pipeline_factory` 的 per-source builder 单测；注入 FakeFrameSink。避开真实 rtspsrc（本机无相机） |
| ENV-B | 板端（GStreamer 1.20.3 真库） | gst-inspect 元素可用性（ITEM-001）、librga imcvtcolor 真实转换（ITEM-005）、真实 decodebin/mppvideodec 能力探测 | ITEM-001, 005 | 绿幕/红蓝测试图 → mp4 → filesrc，验证 NV12 承载、BT601_FULL 转换正确、BGR 通道分离（蓝屏“蓝”白“白”色差） |
| ENV-C | 板端 testcase 源 + `filesrc` 模拟流（录制文件循环） | 六路 worker 真实线程模型、状态机 FI 注入、buffer/lease 泄漏 budgeting（/proc/self/fd & ps RSS 稳态）、recording/rolling buffer 行为 | ITEM-006..012 | 单机本地化即可，产生真实 GStreamer 事件流。断流注入 = 停止文件源 feeder |
| ENV-D | 板端真实 RTSP | 六路真实协商 caps、真实断流恢复（拔线时序）、真实时延分量、真实 bitrate | ITEM-001, 006, 010, 014..017 | 需要 ENV_D_CONNECT；每路独立用 case 标注"真实协商结果为准"，不预设 1280x720 |

分层规则：低层用例失败时不把失败外推到高层（单测失败 ≠ 板端必然失败）；ENV-D 不预设成功，用实际协商 caps 判定。

---
## 3 故障注入目录（video 专用 FI-V）

| ID | 注入 | 实现 | 验证观察 |
|---|---|---|---|
| FI-V-01 | 源帧停止（模拟相机冻结） | media worker 数据源接口注册可注入 feeder，stop_emission | `last_frame_mono_ms` 停止推进，frozen 计数按 thresholds.yaml 冻结阈值 |
| FI-V-02 | 源 EOF（真实断流等价） | file 源 feeder 发 EOS / 停止供给（ENV-D 为实际断相机） | source_error → reconnect 计数 → offline，退避间隔 ENV-B/C 用缩短退避参数加速观察 |
| FI-V-03 | 重连风暴（来回切流） | feeder 反复 stop/start 交替（间隔 < reconnect 阈值） | generation 每次真实重建 +1；seq 重置；stale 帧不进入消费 |
| FI-V-04 | 凭据缺失/错误 | env `TRAIN_*_CREDENTIAL` 未设置或错误值启动 | CREDENTIAL_MISSING/AUTH_FAILED；不反复重试；日志含脱敏错误不含明文 |
| FI-V-05 | 背压（慢消费） | AI 队列 depth=min(2)，消费 stub sleep 目标帧间隔×4 | ai_dropped 计数递增且取流 fps 不塌 |
| FI-V-06 | RGA 转换失败 | 注入非法 stride/奇数宽 NV12 descriptor | 显式错误不静默帧丢失；quality_flags 有相应标志 |
| FI-V-07 | 录像写中断 | ENV-C 可用 tmpfs 满 / 直接 kill 存储 writer | recording_overflow、gap 记录；滚动缓冲不崩 |
| FI-V-08 | 播放头漂移 | ENV-C 注入 hit 间隔不一致的播头时钟 | replay_sync 超阈值提示，`available=false` 条件正确 |
| FI-V-09 | 盘满 | ENV-C 用 loop 镜像或限定配额；ENV-D 谨慎 | 提前告警 + 新片段拒写 + `record_available=false`，重启后媒体可恢复 |
| FI-V-10 | Session 混同 | 注入构造携带旧 (session_id,generation,seq) 的帧 | 消费者侧拒绝：stale/重复/乱序 各自可观测 |

---
## 4 场景用例集

### G1 源能力与配置（ITEM-001）— 主 env: B / D

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-V-CAP-01 | GStreamer 元素探测 | 板端执行直接 `gst-inspect-1.0 <element>` 各元素 | 全部 `rtspsrc/decodebin/mppvideudec/rtph264depay/rtph265depay/h264parse/h265parse/appsink/tee/queue` 及版本号入结构化报告 | 不使用 `--exists` 退出码；每元素具体"是/否"结论明确 |
| TC-V-CAP-02 | 缺失元素显式报缺 | 移除某插件路径后（模拟：环境变量 GST_PLUGIN_PATH 设为空目录）启动 | 启动报错指明`<element>`缺失，仅该路 offline，不整机崩 | 错误可定位到具体元素 |
| TC-V-CAP-03 | 缺凭据 | 清空 `TRAIN_FRONT_RTSP_CREDENTIAL` 后启动 | 该路 `CREDENTIAL_MISSING`，其余凭据正常 | 日志无明文凭据，不含 `${...}` 变量名当密码 |
| TC-V-CAP-04 | 协商 caps 记录 | ENV-D 每路 bounded capture（20s probe + 30s capture） | 每路 codec/res/fps/colorimetry 从实际实测返回，值在 cameras.yaml 无须可复现 | 不以 README 文档值预设通过；.65 用大华路径、还用 `streaming/channels` 备选不晋级主 |
| TC-V-CAP-05 | 协商 caps 与 camera yaml 不符 | 强制 cameras.yaml 声称 H.264 而实际 H.265（ENV-D 才有意义） | 按 CONFIG 预设错误报警或按实际协商（设计选择）——判据是显式报告不隐穿 | 不因文档预期失败即 fail |

### G2 管线工厂/帧身份/租约（ITEM-002..004）— 主 env: A0 / C

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-V-PIPE-01 | rtsp/file/test 同管线 | factory 三类源同一 build 函数 | 三类源生成 pipeline 参数完整；rtsp 有 `protocols=udp` 且无 `protocols=tcp`；无自动 TCP 回退代码路径 | rtsp factory 参数来源 config；file/test 复用同一状态机 |
| TC-V-PIPE-02 | 显式 depay/parse 固化路径 | config 选 explicit-h265 路径 | factory 产出 `rtph265depay ! h265parse` 直连 mppvideudec；不插 decodebin | 与 ITEM-002 承诺一致 |
| TC-V-SEQ-01 | 帧身份生成 | 模拟源 100 帧 → 消费 lease 顺序 | seq 严格递增 per (session,generation)；capture_mono_us 单调递增/相等；wall_ms 一致单次转换 | 单位符合 InterfaceContract §4 |
| TC-V-SEQ-02 | 新建会话 generation+1 | FI-V-03 | 两次真实重建之间 generation 差 =1（每次真实重建恰好 1）；旧 generation 的帧在消费者侧拒绝（stale） | 无重复 seq 近似代际；重复帧可标记 |
| TC-V-LEASE-01 | lease 生命周期 | 消费者 hold lease >2 帧 → 释放 | 析构顺序：最后一个持有者释放后 GstSample/Bufer unref 被调用（可用计数 injected pointer 验证） | 无 use-after-free；uv/san 观察无 invalid read |
| TC-V-LEASE-02 | NOT_READY | 无帧时调用 latest_frame | 返回 NOT_READY；不 segfault；不返回悬垂 buffer | 明确错误语义 |

### G3 NV12→BGR 转换（ITEM-005）— 主 env: B / C

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-V-RGA-01 | 板端真实 imcvtcolor | solid blue/red/green NV12 输入 | 输出 BGR 三通道赋值，`spread = max−min` 每通道均值；蓝屏应为 B↑ / R≈0 且三通道非均值时判绿帧 | 纯色图三通道差显性可判；接受 IM_STATUS_SUCCESS/NOERROR 两码 |
| TC-V-RGA-02 | 带 padding NV12 | stride > width 输入 | 输出宽=width；右缘无 padding 垃圾数据泄漏 | ystride/uv_offset/hstride 按 caps 计算，非连续假设 |
| TC-V-RGA-03 | 奇数宽高拒绝/偶数化 | 1281×720 NV12 输入 | 显式拒绝或记录处理（按 caps/设计），不盲目写越界 | 无越界 |
| TC-V-RGA-04 | 绿帧检测判据 | 历史 rgaconvert 绿帧序列 | 保 RGA 主路径不产生绿帧；若发现绿帧直接失败（历史问题回归） | 见 VIDEO_SYSTEM_DESIGN §10 |

### G4 状态机/worker/多路（ITEM-006..008, 014）— 主 env: A0 / C / D

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-V-STATE-01 | 六态全序 | FI-V-01/02 序列驱动 | connecting→live→frozen→reconnecting→offline 按实际阈值（5000/2000/2000/1000/3）逐一点位 | 状态转换只由真实帧/真实 bus 事件驱动 |
| TC-V-STATE-02 | AUTH_FAILED 不重试 | FI-V-04 | 重试次数不随时间增长；状态 frozen offline；无 AUTH 退避循环 | 区别于 TIMEOUT 重试 |
| TC-V-STATE-03 | 恢复由新帧确认 | FI-V-02 后 feeder 复活 | reconnect_count 在新帧到达后停止增长；恢复判定时刻=首帧到达时刻（从 stream_events 提取） | 不以 PLAYING 返回值判恢复 |
| TC-V-WORKER-01 | 单路失败隔离 | 六路跑单 file 源 + FI-V-02 停一路 | 其余五路 frame 计数持续增长、P4 网格 slot 不位移；异常路 index 保持 | 单路 stop 不触发其他 worker 重启 |
| TC-V-WORKER-02 | 六路并发非阻塞 | 六路真实源 ENV-D / 文件源 ENV-C | 六路 fps ≥ 分档目标（-10% 容差）5s 滑窗 | 每路独立 worker；pump 不在 GUI 线程 |
| TC-V-REC-01 | 背压三路独立 | 注入 FI-V-05 慢 AI 消费 | ai_dropped↑ 但 display/recording 不 dropped；取流 fps 塌陷量 <5% | 独立计数 display_dropped/ai_dropped/recording_overflow |

### G5 录像回放（ITEM-011..013）— 主 env: A0 / C

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-V-RECBUF-01 | 滚动缓冲上限 | 按 clip_pre_seconds 注入时长 > 上限 数据 | 缓冲字节数 ≤ 上限、最旧帧淘汰、不 OOM | 字节+时长双约束 |
| TC-V-RECBUF-02 | 滚动缓冲与显示互不干扰 | 慢 display 时观察录像掉帧 | 录像 buffer 帧率不因 display 慢丢帧 | 独立分支 |
| TC-V-CLIP-01 | 幂等 request_id | 同 request_id 提交两次、不同参数一次 | 相同→同一作业；不同→CONFLICT；中断实际片段以 actual_t0/actual_t1 回报 | 接口契约 IF-07 口径 |
| TC-V-CLIP-02 | 事务发布顺序 | kill -9 于 rename 之前/之后重复触发 | 无悬空 partial 发布（无 .tmp 算正式片段）；缺口有记录 | 无 `.json` 冒充片段 |
| TC-V-PLY-01 | provider 状态 | querySegments/queryGaps/playSegment/exportSegment 全返回路径 | 每返回都有 status + reason + 段或缺口列表；NVR 未配填充 UNCONFIGURED | "ok 空列表"≠播放能力 |
| TC-V-PLY-02 | 回放 max−min 同步偏差 | ENV-C file 源三路注入运动时间差 200ms/400ms | ≤300ms 不提示，>300ms 显式提示（UI/日志可见） | 按实际 decod 播头不按 offsets |

### G6 配置热加载/失败分类（ITEM-016）— 主 env: A0 / C

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-V-LOG-01 | 脱敏全面性 | 一次 AUTH_FAILED + 一次 SOURCE_ERROR + 正常运行产生 pipeline 描述串 | 采样日志全文 grep 用户密码值与 `.65/.160/.162/.163/.165/.167` 鉴权 URL，含 password/token 关键字日志 | 无明文凭据；错误消息含 pipeline/bus 原文则失败 |
| TC-V-LOG-02 | 结构化指标每路齐备 | 六路跑 60s | 每路导出 state/fps/last_frame_*/reconnect_count/quality_flags/display_dropped/ai_dropped/recording_overflow/gap_count/attempt_id/last_error | 字段 exist；last_error 脱敏 |
| TC-V-LOG-03 | target vs 主机分层证据 | 同参数分别在 ENV-A0 (stub) 与 ENV-B（真库）跑 | ENV-B 输出注明"主机可运行/目标已复现/未复现"分区 | 主机结论不外推为板端结论 |
| TC-V-CFG-01 | YAML 三份整体加载 | 缺 cameras.yaml / 缺 app.yaml / 缺 thresholds.yaml 三次残缺启动 | 每次具体报缺哪个文件/字段路径，不做静默认值 | applied_defaults 显式列出 |
| TC-V-CFG-02 | applied_defaults 显式 | 删除可选字段 | 日志/P6 展示默认值来源；默认值合法 | 必填缺失→报错路径 |

### G7 预算仪器（ITEM-017）— 主 env: B / C / D

| TC | 描述 | 刺激 | 观测 | 判据 |
|---|---|---|---|---|
| TC-V-INSTR-01 | 仪器可归档 | 六路 ENV-C 跑 60s → 输出 JSON | 每路 fps/CPU/RSS/NPU%/temp/bitrate/延迟分位/恢复时长齐全 | 数值有限（无 NaN）；数据落 `docs/video/test_evidence/<TC>/*.json` |
| TC-V-INSTR-02 | 三类源分开留证 | 同型场景在 file 源、test 源、真实源各跑一轮 | 证据文件标注 origin；不互相替代 | 命名/字段含 origin |
| TC-V-LAT-01 | 端到端延迟分布 | ENV-C 内嵌注入帧标记（overlay 序号）→ 摄录输出比对 | capture→present p50/p95 ms 可供审阅；不做绝对 pass/fail（PRD 指标未冻结） | 记录分布；credibly report |

---
## 5 冒烟集（P0）

每轮实现后最先执行，失败即挡住后续轮次：

| TC | 描述 | env |
|---|---|---|
| TC-V-SMOKE-01 | core/media 单测全绿（host） | A0 |
| TC-V-SMOKE-02 | 元素探测 + 库版本归档 | B |
| TC-V-SMOKE-03 | file/test 源单路跑 30s 无崩溃、lease/丢帧计数稳定 | C |
| TC-V-SMOKE-04 | 六路 file/test 源帧率分层可达，RSS 稳态（3min 无持续增长） | C |
| TC-V-SMOKE-05 | 保护片段触发一次产生真实媒体件（ffprobe 可读） | C |

---
## 6 需求覆盖矩阵

| VIDEO 任务 | 覆盖用例 |
|---|---|
| ITEM-001 能力探测 | TC-V-CAP-01..05 |
| ITEM-002 管线工厂 | TC-V-PIPE-01..02 |
| ITEM-003 帧身份 | TC-V-SEQ-01..02 |
| ITEM-004 帧租约 | TC-V-LEASE-01..02 |
| ITEM-005 RGA 转换 | TC-V-RGA-01..04 |
| ITEM-006 worker/状态机 | TC-V-STATE-01..03, TC-V-LAT-01 |
| ITEM-007 多路隔离 | TC-V-WORKER-01..02 |
| ITEM-008 三消费者背压 | TC-V-REC-01 |
| ITEM-009 显示/纹理交接 | hmi 集已有 TC-STREAM-*；此处验证 media 侧 presentable/帧快照接口 via TC-V-STATE-01 + TC-V-WORKER-02 |
| ITEM-010 AI 派发 | TC-V-REC-01；ai_profile=none 永不派发 via FI-V-03/05 观测 |
| ITEM-011 滚动缓冲 | TC-V-RECBUF-01..02 |
| ITEM-012 保护片段 | TC-V-CLIP-01..02 |
| ITEM-013 provider | TC-V-PLY-01..02 |
| ITEM-014 恢复控制器 | TC-V-STATE-01..03 |
| ITEM-015 时间基准 | TC-V-PLY-02 + InterfaceContract 时钟单调/UTC 判据 via TC-V-SEQ-01 |
| ITEM-016 诊断日志 | TC-V-LOG-01..03 |
| ITEM-017 预算仪器 | TC-V-INSTR-01..02 |

隔离声明：`degraded` 转为有质量提示的可展示状态（非 LIVE 占位）→ 判定归 InterfaceContract INV-02（旧帧不当实时）；测试判据走 `may_present_as_live(now)`。

---
## 7 时间与证据管理

| 项 | 说明 |
|---|---|
| 测试证据目录 | `docs/video/test_evidence/<TC-ID>/` — 原始命令+退出码+输出。每个 TC 每个 env 单独文件不合并 |
| 命名 | `<TC-ID>-<env>-<date>-r<k>.{md,json,raw.txt}` ；Verifier 独立复跑 `verify-` 前缀 |
| 附加产物 | 板端 gst-inspect 输出、imcvtcolor 灰度 CSV、mp4/TS 件 sha256、`stream_events` DB 导出 json |
| 失败记录 | pass/fail/not_run + blocked 原因 + 复现命令 + 已尝试修复（按 permissions environment_repair 范围） |
| 不要混淆 | file 源 pass ≠ 真机 pass；模拟质量警报≠真实质量警报；无 NVR != NVR 失败 |

---
## 8 移交/边界

1. 依赖实现能力：常驻 worker 线程模型（ITEM-006 承诺）、FI-V-01..03/05/07 软件注入钩子（若实现能力交付不可行则如实记录缺口，不替代为假通过）、`recording_source` 选择处理、buffer 计数注入器（测试观察指针 wrap）。
2. 现场实测（ENV-D 现场准备）：六路真实断流时序、真实 E2E 延迟，验收判据分布不判 pass/fail（PRD §12 指标未冻结），需现场执行。
3. NVR 协议缺失→ 保持 unconfigured 分支测试；`truth` 补完后补真回调用例（同 E2E_PLAN 处理模式）。
4. 模型精度与检测能力（ITEM-010 前后真实推理）属模型能力边界，本计划只测到"派发接口"（带身份的帧投递）为界。
5. 环境缺口：板端真实相机 `effective_status=configured_unverified`、`rk3588_build/deploy/verify` 对测试能力当前 `unavailable_for_current_capability`。当前仅设计工作，不执行板端操作；执行轮须按环境授权。

---
## 9 修订

- v1: 初版，对齐 VIDEO_SYSTEM_DESIGN.md v1 + 17 项视频任务 + InterfaceContract v1.0.0；五层测试环境（A0/A/B/C/D）；故障注入 FI-V-01..10；37 个 G1..G7 case；P0 冒烟 5 条；覆盖矩阵逐 ITEM 落点。
