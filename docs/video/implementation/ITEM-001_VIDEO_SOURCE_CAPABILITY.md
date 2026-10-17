# 视频源能力探测与配置绑定 — 实现交付（VideoSystemImplementation, ITEM-001）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-001「视频源能力探测与配置绑定」
- 上游: `docs/video/VIDEO_SYSTEM_DESIGN.md`（VideoSystemDesignArtifact）、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0、`docs/video/VIDEO_TEST_DESIGN.md`（G1 / TC-V-CAP-01..05）
- 基线 commit: `057206c`（本实现新增文件见下；运行时这些文件为工作区新增/修改，Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | 说明 |
|---|---|---|
| `app/src/media/include/railhmi/media/video_capability.h` | 新增 | 能力探测/配置绑定接口：`ElementProbeResult`/`LibraryVersions`/`CapsRecord`/`CameraCapability`/`CapabilityReport` + 纯逻辑辅助 |
| `app/src/media/src/video_capability.cpp` | 新增 | 实现：直接 `gst-inspect-1.0 <element>` 探测、库版本、凭据判定、就绪性、有界 RTSP 协商 caps 探测、JSON/文本序列化 |
| `app/src/media/tools/video_capability_tool.cpp` | 新增 | 运行入口 `railhmi-video-capability`（含 `--selftest`） |
| `app/src/media/CMakeLists.txt` | 修改 | 将 `video_capability.cpp` 编入 `railhmi_media`，新增可执行 `railhmi-video-capability` |
| `app/tools/board_video_capability.py` | 新增 | 板端执行入口：上传 `app/` → `build.sh MediaCheck` → selftest/可达性/无凭据/有界 caps 探测 → 取回证据 → 清理 |

配置来源不变（不新增配置字段）：`app/config/cameras.yaml`、`app.yaml`、`thresholds.yaml`、`roi.yaml` 由 `railhmi-core` 的 `load_config_dir()` 整体加载并校验，缺字段报具体字段路径。

版本摘要（运行期）：
- `video_capability.h` sha256 `5447a262…1982`
- `video_capability.cpp` sha256 `3277f5d2…936b`
- `video_capability_tool.cpp` sha256 `e9b0fe5e…2123`
- `media/CMakeLists.txt` sha256 `c4b74991…e5cf`
- `board_video_capability.py` sha256 `65b4f775…f26b`

## 2. 行为（与工作项与设计一致）

1. **配置绑定**：加载 4 份 YAML；URL 保持"不含凭据"；凭据只按 `credential_env` 指向的环境变量**判存在性**，绝不读取/记录其值。
2. **元素探测（直接 gst-inspect，不用 `--exists`）**：对 `rtspsrc/rtph265depay/h265parse/rtph264depay/h264parse/decodebin/mppvideodec/tee/queue/appsink/fakesink` 逐个执行 `gst-inspect-1.0 <element>`，`exit==0 && 含 "Factory Details:"` 判 Present 并解析 `Long-name`；否则 Absent 并记录首行诊断。**明确不使用 `gst-inspect-1.0 --exists` 退出码**（本轮实测该退出码会假否，见 §4）。
3. **版本记录**：GStreamer 运行时 `gst_version()`；librga 编译期 `RGA_API_FULL_VERSION`；librknnrt 经 `dlopen`+`dladdr` 定位共享库并提取内嵌 `librknnrt version:` 串（止于首个非可打印字节）。
4. **逐路能力记录**：`camera_id/name/position/spectrum/ai_enabled/ai_profile/display_fps/rtsp_url/credential_env/credential/readiness/caps`。缺凭据→`readiness=blocked_credential_missing`（caps=`to_be_measured`）；缺元素→`blocked_element_missing:<el>`；否则 `ready`。
5. **协商 caps 探测（有界、可关）**：`--probe-caps` 时对 `ready` 路构造 `rtspsrc(location=<no-cred>, user-id/user-pw=凭据) ! decodebin(caps=video/x-raw) ! fakesink`，PAUSED→PLAYING 后轮询已协商 raw caps，取 `codec/format/width/height/fps/colorimetry`；失败/超时对瞬时错误**有界重试一次**（2s 后）。凭据仅经 `gst_parse_launch` 进程内字符串，构造后立即擦除，错误消息用 `scrub_secret` 去除凭据。
6. **退出码**：`0` 报告产出；`2` 参数/配置错误；`3` 必需元素缺失。

## 3. 运行入口

```bash
# 板端构建（GStreamer + librga，无 Qt）
app/build.sh MediaCheck

# 纯逻辑自检（无相机）
build-media/tools/railhmi-video-capability --selftest

# 能力报告（默认：元素/版本/凭据状态/待实测 caps）
build-media/tools/railhmi-video-capability --config ./config --json cap.json --out cap.log

# 有界真实协商（凭据经进程环境，不落命令行/日志）
# 例：TRAIN_FRONT_RTSP_CREDENTIAL=… TRAIN_SIDE_RTSP_CREDENTIAL=… \
#     build-media/tools/railhmi-video-capability --config ./config --probe-caps --caps-timeout-ms 12000
```

板端一键（含上传/构建/取证/清理）：
```bash
python app/tools/board_video_capability.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-001 --stamp <tag>
```

## 4. 验证（本能力范围内）

- **主机/板端构建**：`build.sh MediaCheck` 在 RK3588 上成功（GNU 11.4 / GStreamer 1.20.3 / librga 可用），产出 `librailhmi_media.a` 与 `railhmi-video-capability`。
- **纯逻辑自检**：`--selftest` 8/8 PASS（元素输出解析 present/absent、缺元素 detail、凭据缺失/元素缺失/ready 三种就绪性、必需元素列表）。
- **板端真实协商（ENV-B/D 档）**：六路全部 `measured`，见 `docs/video/test_evidence/ITEM-001/ITEM-001-ENV-B-20261017-r1.md`。
- **重要实测事实**：
  - `gst-inspect-1.0 --exists mppvideodec` 返回 exit 1（假否），而直接 `gst-inspect-1.0 mppvideodec` 存在。**已按任务要求回避 `--exists`**。
  - 六路实际协商：**H.265 / NV12 / 1280×720 / 25fps**；其中 `/h264/ch1/main/av_stream` 路径的四路实际为 **H.265** → 证实"不按 URL 路径/文档预期写死编码"。
  - 版本：GStreamer 1.20.3；librga API 1.10.1；librknnrt 2.3.2。
  - 六路均有音频轨（PCMA/其他），探测时以 `decodebin caps=video/x-raw` 仅解码视频。
- **安全**：证据扫描确认相机 RTSP 凭据（长度 8/6）在 `docs/video` 与媒体源码中 0 命中；临时 `/tmp` 凭据文件以 0600 写入并在命令结束即删除，命令串与日志均不含凭据。

## 5. 调试过程（本轮，已修复）

| 观察 | 根因 | 修复 |
|---|---|---|
| caps=error `no element "rtspsrc"` + GStreamer-CRITICAL `gst_is_initialized` | 未调用 `gst_init` 就 `gst_parse_launch` | `probe_camera_caps` 起始调用 `gst_init()` |
| librknnrt 版本串粘入其他二进制串 | 版本提取按换行截断，遇 NUL 未停 | 改为止于首个非可打印字符 |
| `decodebin ! video/x-raw ! fakesink` 报 `Delayed linking failed` 并链到音频 pad | parse_launch 延迟链接选错 pad | 改为设置 `decodebin caps=video/x-raw` 并手动只链接视频 pad |
| 未链接音频 pad 触发 `Internal data stream error` | decodebin 暴露音频流无人消费 | 保留音频 drain fakesink，同时用 `caps=video/x-raw` 限制输出 |
| live 源 `get_state` 返回 `NO_PREROLL`，等待 `ASYNC_DONE` 超时 | rtspsrc 为 live 源，不 preroll | 改为进入 PLAYING 后轮询已协商 caps |
| 个别相机瞬时 `Internal data stream error`/`Could not read from resource` | 会话残留/瞬时抖动 | 相机间 300ms 间隔 + 瞬时错误有界重试一次 |

## 6. 边界与移交

- 本项只实现**能力探测与配置绑定**；解码/帧分发/录像/回放/恢复由 ITEM-002..017 承担。
- 未实现：真实帧持续吞吐/延迟测量（ITEM-017）、RGA 色彩转换（ITEM-005）、多路 worker（ITEM-006/007）。
- `caps` 为协商/首帧解码 caps，**不是**持续 fps/吞吐实测；`display_fps` 仍是展示上限。
- 平台观察（非本项职责）：板端 `eth1` 链路速率本轮实测为 **100 Mbps**（设计声明 A-B 为千兆，端口协商与线上吞吐属平台/网络职责，另见能力外的只读观测）。
- 不修改 core/wire 契约、SQLite DDL、既有 Qt 桥接签名。
