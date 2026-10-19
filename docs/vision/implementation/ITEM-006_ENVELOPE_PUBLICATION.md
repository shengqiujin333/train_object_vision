# 视觉推理 ITEM-006 实现 — 结果关联与发布（InferenceEnvelope）

- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-006`
- 版本: v1（2026-10-19）
- 上游: `docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0 §6（InferenceEnvelope / 同帧同代 / 不可用语义）、`docs/vision/VISION_DESIGN.md` §2.2/§7.6、`docs/vision/VISION_TEST_DESIGN.md` G6（TC-T-PUB-01..05）、ITEM-003（检测框）、ITEM-004（掩码）、ITEM-005（两阶段判定）、`core::FrameGate`（视频 ITEM-003）、`core::BoundedQueue`（视频 ITEM-008）、`app.ai.queue_depth`
- 范围: 只实现 ITEM-006（结果关联与发布）。未实现真实 RKNN 前向/线程调度/语义白名单/跟踪/P5（后续 ITEM）。

## 1. 交付内容

| 交付 | 路径 | 说明 |
|---|---|---|
| Envelope + 接纳/队列纯逻辑 | `app/src/core/include/railhmi/core/vision_inference_envelope.h`、`src/vision_inference_envelope.cpp` | `InferenceEnvelope`/`EnvelopeBox`/`MaskRef`、`build_envelope`、`make_unavailable_envelope`、`EnvelopePublisher`（FrameGate + 有界丢旧队列）、JSON。仅标准库。 |
| 探针 | `app/src/core/tools/envelope_probe.cpp`（`railhmi-vision-envelope`） | `--selftest`（24 判例）、10 个 fixture、`--config`（depth 取自 `app.ai.queue_depth`）。 |
| 单元测试 | `app/tests/core/test_vision_inference_envelope.cpp` | 延迟/身份/可用与空框/逐类拒绝/有界队列/计数/JSON。 |
| 板端入口 | `app/tools/board_vision_envelope.py` | 一次有界会话：CoreTests→core-tests→selftest→10 fixtures→config→取回证据→清理。 |

构建接入：`app/src/core/CMakeLists.txt`、`app/tests/core/CMakeLists.txt`、`app/tests/core/main.cpp`（仅新增条目）。

## 2. 如何满足工作项

| 要求 | 实现 | 证据 |
|---|---|---|
| 携带 (session_id,camera_id,generation,seq) 关联键 | `InferenceEnvelope.frame`（`FrameId`）原样保留 | `ready` JSON `session_id=s-env camera_id=CAM-01 generation=1 seq=1` |
| capture_mono_us/result_mono_us + latency | 两时刻字段 + `latency_ms_of(capture,result)`＝(完成−接收)/1000，**含排队**（result 为出队+推理完成时刻） | `capture_mono_us=1000000 result_mono_us=1033000 latency_ms=33` |
| availability(ready\|unavailable)+reason | `AiAvailability` + 稳定 reason（`READY`/`AI_UNAVAILABLE`/`SEG_UNAVAILABLE`，可覆盖） | `unavailable` JSON；`seg_unavailable`（front 需要掩码） |
| model_version | 字段来自运行时模型身份（不由配置名覆盖） | `model_version="rknn-2.3.2/yolov5s"` |
| 带 track_id 的 boxes | `EnvelopeBox.track_id`；**未知=-1（null）**，禁止 0 冒充 | `no_track`：`track_id=-1`；`ready`：`track_id=7` |
| 掩码引用 | `MaskRef`（box_index/尺寸/stride/kind + 零拷贝 plane 指针） | `ready` JSON `masks:[{box_index:0,...,"plane_present":true}]` |
| 旧 session/generation、重复/乱序 seq 分别计数 | `EnvelopePublisher` 用 `FrameGate`，逐类计数并返回 admission 名称 | `identity`：`duplicate_seq=1 out_of_order_seq=1 stale_generation=1`，`accepted=1 rejected=3` |
| 结果队列有界、超载丢最旧并计数 | `BoundedQueue<InferenceEnvelope>`（容量 `app.ai.queue_depth`），`queue_dropped` 计数 | `queue_overflow --depth 2`：`queued=5 queue_dropped=3`，`pop_first_seq=4`（保留最新） |
| 不可用 → unavailable+reason+空框；空框与不可用可区分 | `build_envelope` 门控（detect_ready / requires_segmentation&&seg_ready）；两者都以 `availability`+`reason` 区分 | `empty`：`availability=ready box_count=0 reason=READY`；`unavailable`：`availability=unavailable reason=AI_UNAVAILABLE box_count=0` |
| 判定可复现 | `--selftest` 24 判例全 PASS | `item006-selftest` |

字段语义：

- `ready()` = `availability==Ready`；`empty_boxes()` = `boxes.empty()`。
- **不可用 ≠ 无目标**：不可用时 `boxes`/`masks` 必为空且 `reason` 非 `READY`；可用但本次无目标时 `availability=ready reason=READY boxes=[]`。
- `track_id`：跟踪未就绪必须是 `-1`；不得把全部目标映射为 0。
- `MaskRef.plane` 为**零拷贝引用**：指向调用方持有、在 envelope 有效期内有效的缓冲；`envelope_json` 只输出 `plane_present`，不序列化像素。
- `latency_ms` 为完成−接收且含排队；接收时刻用单调微秒（`capture_mono_us`），不使用 UTC/wall。

## 3. 运行入口

```
cd app && bash build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-vision-envelope --selftest --out selftest.log --json selftest.json
./build-core/tools/railhmi-vision-envelope --fixture ready --out ready.log
./build-core/tools/railhmi-vision-envelope --fixture identity --depth 2 --out identity.log
./build-core/tools/railhmi-vision-envelope --config ./config --out config.log
```

## 4. 返工记录（保留失败证据）

| 轮次 | 现象 | 根因 | 修复 | 复核 |
|---|---|---|---|---|
| r1 | `BUILD_EXIT=3`：`DetectBox has no member confidence/in_roi/distance_m` | 误用语义层 `core::DetectionBox` 的字段去读后处理层 `core::DetectBox`（后者只有 `score`） | 置信度取 `b.score`；`in_roi`/`distance_m` 默认 false/-1，由 `in_roi_flags`/`distances_m` 覆盖 | r2 build=0 |
| r2 | core-tests `track_id null default` 失败；`queue_overflow`/`config` 判 FAIL | ① 测试 helper 在 `with_box` 时总是写 `track_ids={5}`，导致“无 track”用例实际有 track；② 探针在 `pop` **之后**才读 `queue_size()`，期望 depth 恒不成立 | ① helper 增加 `track_id` 参数（默认 -1）；② pop 前记录 `size_before` | r3 core-tests 1118/0、全部 fixture PASS |

## 5. 观察与移交

1. **真实 RKNN 前向未执行**：本增量用夹具 envelope/队列；真实 `rknn_run` 产出的结果填 envelope 归 ITEM-009。
2. **掩码零拷贝**：`MaskRef` 只存指针，envelope 生命周期内缓冲必须有效；跨线程/跨队列传递时所有权与生命周期需在 ITEM-009 明确（如需深拷贝则由调用方负责）。
3. **计数器线程安全**：`EnvelopePublisher` 的计数器与 `FrameGate` 为单生产者/单消费者假设；多线程 submit 的加锁与 handoff 归 ITEM-009。
4. **队列深度来自配置**：`app.ai.queue_depth`（`--config` 已实测读取）；`--depth` 可覆盖。
5. **未实现**：语义白名单/跟踪、线程与调度接入、P5 调试视图、测距（后续 ITEM）。
6. `FrameGate` 与 `BoundedQueue` 复用视频 ITEM-003/008 的既有语义，不重复实现。
