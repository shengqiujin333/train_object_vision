# 录像压缩分支与滚动缓冲 — 实现交付（VideoSystemImplementation, ITEM-011）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-011「录像压缩分支与滚动缓冲」
- 上游: `docs/video/VIDEO_SYSTEM_DESIGN.md` §2.3/§3.1/§4/§7.1、`docs/video/VIDEO_TEST_DESIGN.md`（TC-V-RECBUF-01/02，ENV-A0/C）、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0、`app/config/{app,thresholds}.yaml`
- 基线 commit: `9091b2c`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/core/include/railhmi/core/compressed_ring.h` | 新增 | `1f2e6835a5df8053` | `CompressedPacket`/`CompressedRingStats`/`CompressedRingBuffer`（字节+时长双上限纯逻辑） |
| `app/src/core/src/compressed_ring.cpp` | 新增 | `26c68985264b0372` | 滚动缓冲实现：最旧淘汰、关键帧对齐、窗口收集、计数 |
| `app/src/core/include/railhmi/core/recording_source.h` | 新增 | `b965947b38b2f0e4` | 录像来源策略与显式状态（NVR 未配置回落本机） |
| `app/src/core/src/recording_source.cpp` | 新增 | `8dbcf0938d3ddfda` | `resolve_recording_source`/`integration_is_configured`/`describe` |
| `app/src/media/include/railhmi/media/recording_tap.h` | 新增 | `f087e234dc74e1de` | 媒体侧滚动缓冲接入（线程安全；公开头无 GStreamer） |
| `app/src/media/src/recording_tap.cpp` | 新增 | `303810a5b53ac88e` | `RecordingTap` 实现 |
| `app/src/media/include/railhmi/media/pipeline_factory.h` | 修改 | `513e1b421f554c8b` | 新增 `recording_tap`/`recording_tap_buffers` + `build_rtsp_compressed_prefix` |
| `app/src/media/src/pipeline_factory.cpp` | 修改 | `183011262695b157` | 解码前 `tee name=enc`：parse 之后接 `record_sink`，显示/AI 分支不变 |
| `app/src/media/include/railhmi/media/camera_stream.h` | 修改 | `4a5c7b40217bc987` | 新增 `recording_tap`/`clip_pre_seconds`/`record_max_bytes`/`recording_tap_buffers` 与访问器 |
| `app/src/media/src/camera_stream.cpp` | 修改 | `861e9fc4063a11cc` | 拉取 `record_sink` 压缩包 → `RecordingTap`（独立于显示/推理） |
| `app/src/media/tools/recording_tap_probe.cpp` | 新增 | `a75ad910f4ae43be` | 运行入口 `railhmi-recording-tap`（配置驱动 + `--selftest`） |
| `app/tools/board_recording_tap_check.py` | 新增 | `a1a105e602b84e24` | 板端执行入口（CoreTests + MediaCheck + 双阶段滚动缓冲运行） |
| `app/src/core/CMakeLists.txt` | 修改 | `5d491ea4adfa5ab4` | 编译新 core 源 |
| `app/src/media/CMakeLists.txt` | 修改 | `089898ac680563d9` | 编译 `recording_tap.cpp` + 注册探针 |
| `app/tests/core/CMakeLists.txt` / `main.cpp` | 修改 | `921a2fd3763ca01c` / `9b4da5c014a9bf25` | 注册新测试组 |
| `app/tests/core/test_compressed_ring.cpp` | 新增 | `708ee97546a576e8` | 缓冲双上限/淘汰/关键帧/收集纯逻辑测试 |
| `app/tests/core/test_recording_source.cpp` | 新增 | `9c354302920618b5` | 来源策略解析测试 |

## 2. 契约与行为

1. **解码前 tap**：`pipeline_factory` 在 `recording_tap=true` 时对 rtsp 源插入 `tee name=enc`：显式解码在 `h26xparse` 之后、`mppvideodec` 之前；`Auto`（decodebin 隐藏 parse）在 `application/x-rtp,media=video` 层 tap。压缩包进入 `appsink name=record_sink`，**不经过**显示/AI 的 `frame_sink`/`count_sink`。
2. **字节+时长双上限**：`CompressedRingBuffer` 同时受 `max_bytes`（字节上限）与 `max_duration_ms`（`clip_pre_seconds×1000`）约束：超时长先淘汰最旧包并计 `evicted_duration`，超字节继续淘汰最旧包并计 `evicted_bytes`；`bytes ≤ max_bytes` 与 `span_ms ≤ max_duration_ms` 为不变量。
3. **不用显示/AI 丢帧策略、不保存全部原始帧**：录像只持有最近窗口的**压缩包**（access unit），独立队列（有界）与独立淘汰计数；显示/AI 慢只影响各自队列。
4. **关键帧对齐**：`first_keyframe_at_or_after(ts)` 供 ITEM-012 片段起点对齐关键帧解码依赖；`collect(from,to)` 提供窗口固化。
5. **来源策略**：`resolve_recording_source(recording_source, nvr_configured)`；NVR 未配置（缺 protocol/channel_mapping）时 `effective=local_fallback`、`reason=NVR_UNCONFIGURED`、`local_enabled=true`——**显式回落本机保护片段，不静默**。
6. **配置化**：`clip_pre_seconds` 取自 `thresholds.yaml`；`record_max_bytes`/`recording_tap_buffers` 为配置项；探针 `--config` 读取，不写死设备。
7. **接入取流**：`CameraStream` 在 worker 线程每轮有界拉取 `record_sink`（非阻塞、最多 16 包），映射为 `CompressedPacket`（PTS→ms、`DELTA_UNIT`→关键帧）后入 `RecordingTap`；未启用或无 `record_sink` 时为 `nullptr`。

## 3. 运行入口

```bash
app/build.sh CoreTests && build-core/tests/railhmi-core-tests
app/build.sh MediaCheck
build-media/tools/railhmi-recording-tap --selftest
build-media/tools/railhmi-recording-tap --config config --pre-seconds 3 --max-bytes 1048576 \
    --duration 8 --display-pull-ms 200 --out rec.log --json rec.json
```

板端一键（含字节上限第二阶段）：
```bash
python app/tools/board_recording_tap_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-011 --stamp <tag>
```

## 4. 板端验证结果（RK3588，stamp `20261017-item011-r2`）

- `railhmi-core-tests`：**783 passed / 0 failed**（新增 `compressed_ring` 与 `recording_source` 组）。
- `railhmi-recording-tap --selftest`：**9/9 PASS**（tap 位置/分支保留/auto RTP tap/关闭无 record_sink/raw 源不附加/来源回落/双上限）。
- 板端真实压缩管线（`videotestsrc → x264enc → h264parse → tee`；一支 `avdec_h264` 慢消费当显示，一支解码前 tap 录像）：
  - 配置：`config_pre_seconds=30`（`app config`），测试注入 `effective_pre_seconds=3`；`source requested=both nvr_configured=false effective=local_fallback reason=NVR_UNCONFIGURED`。
  - **时长绑定阶段**（`max_bytes=1 MiB`）：`received=199 expected=200 display_frames=39`；`packets=76 bytes=777546 span_ms=3000 keyframes=3 evicted_duration=123 evicted_bytes=0`。
  - **字节绑定阶段**（`max_bytes=200 KiB`）：`packets=20 bytes=196093 span_ms=760 evicted_duration=0 evicted_bytes=179`（字节上限生效且 `bytes ≤ 204800`）。
  - acceptance（两阶段均）：`bytes_bounded=true duration_bounded=true evicted=true record_rate_ok=true display_independent=true source_status_explicit=true result=PASS`。
  - **独立性**：显示分支仅消费 39 帧（慢消费者），录像压缩包仍收到 ≈ 199/200（源帧率 25fps×8s），说明录像分支不受显示丢帧策略影响。
- 证据：`docs/video/test_evidence/ITEM-011/ITEM-011-ENV-A0-C-20261017-r1.md` 与同目录原始日志/JSON。

## 5. 调试过程（本轮）

| 观察 | 根因 | 处理 |
|---|---|---|
| 首版探针直接 `#include <gst/gst.h>` | `railhmi_media` 的 GStreamer include 目录为 PRIVATE，不传递给探针 | 为目标补 `${GST_INCLUDE_DIRS}`/`${GST_CFLAGS_OTHER}`（r1 通过） |
| r1 仅体现时长绑定（`evicted_bytes=0`） | `max_bytes=1 MiB` 大于 3s 压缩量 | 增加字节绑定第二阶段（`max_bytes=200 KiB`），r2 两阶段各自证明一种上限 |

## 6. 边界与移交

- 本项交付**媒体侧解码前压缩 tap 与滚动缓冲**；片段固化/原子发布/缺口记录归 ITEM-012，provider/回放归 ITEM-013。
- `collect()`/`first_keyframe_at_or_after()` 为 ITEM-012 的输入面；`recording_overflow` 与压缩来源状态供 ITEM-016 诊断。
- 真实相机 rtsp 的压缩 tap（`record_sink`）路径已实现并由自检字符串映射覆盖；本项运行时用板端真实压缩管线验证缓冲语义，**未冒充**真实相机取证（真实相机留证属 ENV-D）。
- 未改 core/wire 契约、SQLite DDL、`AiWorker`/`Detector`、生产 UI。
