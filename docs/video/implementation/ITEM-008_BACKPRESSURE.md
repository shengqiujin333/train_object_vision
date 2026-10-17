# 三类消费者有界队列与背压 — 实现交付（VideoSystemImplementation, ITEM-008）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-008「三类消费者的有界队列与背压」
- 上游: `docs/video/VIDEO_SYSTEM_DESIGN.md`、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0（§5 分发/背压）、`docs/video/VIDEO_TEST_DESIGN.md`（G4/TC-V-REC-01）
- 基线 commit: `f2f4a48`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/core/include/railhmi/core/bounded_queue.h` | 新增 | `6e25eda65891da6d` | 线程安全有界队列（丢最旧，可配条数/字节上限）；纯标准库 |
| `app/tests/core/test_bounded_queue.cpp` | 新增 | `4390fa05f3e913fd` | core 单测：容量 1 / 有界丢旧 / 字节预算 / 计数 |
| `app/src/media/include/railhmi/media/frame_dispatcher.h` | 新增 | `4efccec4c73e374b` | 三类消费者分发接口（显示/推理/录像）+ 计数 |
| `app/src/media/src/frame_dispatcher.cpp` | 新增 | `4229cfae4303ae86` | 分发实现：各自有界队列与独立计数 |
| `app/src/media/include/railhmi/media/camera_stream.h` | 修改 | `5951245333555b9f` | 配置加 ai_queue_depth/recording_queue_*；`StreamStats` 加三类计数；新增 `dispatcher()` |
| `app/src/media/src/camera_stream.cpp` | 修改 | `774014a93ae3e35b` | 每帧非阻塞分发到三类消费者；计数入 stats |
| `app/src/media/include/railhmi/media/camera_worker.h` | 修改 | `72dca253480fa59f` | worker 暴露 `dispatcher()` |
| `app/src/media/src/camera_worker.cpp` | 修改 | `50f102fa7f815b1c` | `dispatcher()` 实现（移出头文件避免不完整类型） |
| `app/src/media/tools/backpressure_probe.cpp` | 新增 | `fdfd05886cb25107` | 运行入口 `railhmi-backpressure`（慢显示/慢 AI/停录像；验收判定；`--selftest`） |
| `app/tools/board_backpressure_check.py` | 新增 | `02330aa5b5c99c42` | 板端执行入口（CoreTests + selftest + 背压运行） |

## 2. 契约与行为

1. **有界队列（`core::BoundedQueue<T>`）**：非阻塞、线程安全；满时按**丢最旧**淘汰队首保留最新，可叠加字节上限；`pushed/dropped/popped/size/bytes` 可观测。
2. **三类消费者（`FrameDispatcher`）**：
   - 显示：容量 1（latest-only），旧帧被替换即 `display_dropped++`；
   - 推理：容量 `app.ai.queue_depth`（默认 2，来自 `CameraStreamConfig.ai_queue_depth`），`ai_dropped++`；
   - 录像：独立有界分支（`recording_queue_frames` 条数 + 可选 `recording_queue_bytes` 字节上限），溢出 `recording_overflow++`（ITEM-011/012 将映射为录像缺口）。
3. **非阻塞与隔离**：`dispatch()` 在取流 worker 内调用，三类队列各自按策略丢帧，互不影响，也不阻塞取流；UI/AI 慢只让其自身丢帧，录像队列满只记自身溢出。
4. **计数出口**：`StreamStats` 增加 `display_dropped/ai_dropped/recording_overflow/ai_depth/recording_depth`；`FrameDispatcher` 提供同名查询与 `dispatched()`。
5. **录像分支独立性**：录像消费者停止导致 `recording_overflow` 增长，而显示/推理继续按各自策略丢帧；显示/推理丢帧不影响录像队列（除其自身容量/字节上限）。
6. 录像队列当前承载 `FrameLease`（NV12 帧引用约占位）；ITEM-011 将改为压缩包（`GstBuffer`）并接字节/码率预算（ITEM-017）。本项交付的是**队列/背压/计数机制**。

## 3. 运行入口

```bash
app/build.sh CoreTests && build-core/tests/railhmi-core-tests     # 含 bounded_queue 组
app/build.sh MediaCheck
build-media/tools/railhmi-backpressure --selftest
build-media/tools/railhmi-backpressure --source test --duration 8 \
    --display-hz 1 --ai-hz 1 --stop-recording-after 4 \
    --ai-depth 2 --recording-frames 8 --out bp.log --json bp.json
```

板端一键：
```bash
python app/tools/board_backpressure_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-008 --stamp <tag>
```

## 4. 板端验证结果（RK3588）

- `railhmi-core-tests`：**744 passed, 0 failed**（含 bounded_queue 组：cap1 latest-only、cap2 有界丢旧、字节预算、计数）。
- `railhmi-backpressure --selftest`：**4/4 PASS**。
- test 源背压运行（显示 1Hz、AI 1Hz；t=4s 停止录像消费者）：
  - 慢消费下取流 `fps` 爬升到 ~25–26 并保持；`capture_rate=30`（窗口含爬坡），**未塌陷**；
  - `display_dropped=225`、`ai_dropped=216`、`ai_depth=2`（有界）；
  - 录像消费者活跃期（0–4s）：`recording_overflow=0`（显示/AI 大量丢帧时录像不受影响）；
  - 停止录像消费者后：`recording_overflow` 从 0 增至 117，`recording_depth=8`（有界）；
  - acceptance：`capture_rate_ok=true display_dropped_ok=true ai_dropped_ok=true recording_independent_of_display_ai=true recording_overflows_when_consumer_stopped=true result=PASS`。
- 证据：`docs/video/test_evidence/ITEM-008/ITEM-008-ENV-A0-C-20261017-r1.md` 与同目录原始日志。

## 5. 边界与移交

- 本项交付**队列/背压/计数机制与最小接线**；实际压缩录像分支（tap 编码流、片段落盘、缺口时间范围）属 ITEM-011/012；NVR 属 IF-07。
- `display_dropped` 与 `FrameStore` 的 latest-only 语义互补：`FrameStore` 提供 generation 感知的最新帧租约，`FrameDispatcher` 提供带计数的三消费者分发。
- AI 工作线程（`ai_worker`）与录像 provider 的接入分别属 ITEM-010/011；本项只提供其消费的有界队列与计数。
- 未改 core/wire 契约、SQLite DDL、既有 Qt 桥接签名。
