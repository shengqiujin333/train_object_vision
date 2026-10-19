# ITEM-006 InferenceEvidence — 结果关联与发布（InferenceEnvelope）

- Evidence type: InferenceEvidence
- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-006`
- 日期: 2026-10-19
- 权威轮次: **r3**（`20261019-item006-r3`）；r1/r2 为返工前证据（保留）
- 事实口径: 下列均为本次在 RK3588 上真实执行（夹具 envelope/队列）；未执行项见 §6。

## 1. 结论（本次验收范围）

| 用例 | 期望 | 实测 | 证据 |
|---|---|---|---|
| 身份链完整 | envelope 携带 identity/capture/result/latency/model_version | `"session_id":"s-env","camera_id":"CAM-01","generation":1,"seq":1,"capture_mono_us":1000000,"result_mono_us":1033000,"latency_ms":33,"model_version":"rknn-2.3.2/yolov5s"` | `item006-fixture-ready` |
| 带 track_id 的框 | track_id 保留 | `"track_id":7` | 同上 |
| 未知 track_id = null | -1，不得 0 | `"track_id":-1` | `item006-fixture-no_track` |
| 掩码引用 | 尺寸/stride/box_index + plane 存在 | `masks:[{"box_index":0,"width":64,"height":48,"stride_bytes":64,"kind":"rail","plane_present":true}]` | `item006-fixture-ready` |
| 可用 + 无目标（空框） | ready + box_count 0 | `availability:"ready" reason:"READY" box_count:0` | `item006-fixture-empty` |
| AI 不可用 | unavailable + reason + 空框 | `availability:"unavailable" reason:"AI_UNAVAILABLE" box_count:0` | `item006-fixture-unavailable` |
| 分割不可用（front 需要掩码） | unavailable + SEG_UNAVAILABLE + 空框 | `reason:"SEG_UNAVAILABLE"` | `item006-fixture-seg_unavailable` |
| 重复/乱序/旧代分别计数 | 三类独立计数 | `submitted=4 accepted=1 rejected=3 stale_generation=1 duplicate_seq=1 out_of_order_seq=1` | `item006-fixture-identity` |
| 有界队列丢最旧 | depth 2、超载丢旧计数、保留最新 | `queued=5 queue_dropped=3`；`pop_first_seq=4`（queue 为 seq 4/5） | `item006-fixture-queue_overflow` |
| 队列深度来自配置 | `app.ai.queue_depth` | `app.ai.queue_depth=2 depth_used=2 (from app.ai.queue_depth)` | `item006-config` |
| core 单测（含 envelope 测试） | 全绿 | `tests=1118 failures=0` | `item006-core-tests-r3.xml` |
| 探针 selftest | 全绿 | `RESULT: PASS failures=0`（24 判例） | `item006-selftest-r3.log` |

## 2. 执行环境（实测）

| 项 | 值 |
|---|---|
| 目标 | RK3588，Linux 6.1.141 aarch64，8 核；主机指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU` |
| 编译器 / CMake | g++ 11.4.0 / cmake 3.22.1 |
| 输入 | 夹具 envelope（身份/时刻/框/掩码引用）与合成提交序列 |
| 配置 | `app/config/app.yaml`（`app.ai.queue_depth`） |
| 入口 | `app/tools/board_vision_envelope.py`（登记 Paramiko SSH；仅写 /tmp，结束清理） |

## 3. 字段语义

- `availability=ready|unavailable`；`reason` 为稳定 token（`READY`/`AI_UNAVAILABLE`/`SEG_UNAVAILABLE`，可覆盖）。
- `latency_ms = (result_mono_us − capture_mono_us)/1000`，result 为出队+推理完成时刻，故**含排队**；负值按 0。
- `boxes[].track_id`：`-1` = null（跟踪未就绪），不得用 0 冒充。
- `masks[]`：零拷贝 `MaskRef`（原图坐标/stride/box_index），JSON 只记录 `plane_present`。
- 不可用与“可用但无目标”在 `availability`/`reason`/`box_count` 上可区分。

## 4. 性能/资源测量范围

本增量为关联/发布正确性，**不测量** RKNN 前向耗时/内存/FPS（未执行前向）；也不测多线程 submit 竞争。`latency_ms` 为字段计算，不代表实测推理延迟（真实延迟归 ITEM-009/执行轮）。

## 5. 证据文件（`docs/vision/test_evidence/ITEM-006/`）

r3（权威）：
- `vision-item006-…-r3.transcript.json` / `.log`
- `…-r3.app_core-evidence_item006-core-tests-…-r3.xml`（1118/0）
- `…-r3.app_core-evidence_item006-selftest-…-r3.{log,json}`
- `…-r3.app_core-evidence_item006-fixture-{ready,empty,unavailable,seg_unavailable,no_track,identity,stale,duplicate,out_of_order,queue_overflow}-…-r3.{log,json}`
- `…-r3.app_core-evidence_item006-config-…-r3.{log,json}`

返工前证据（保留）：
- r1 transcript/log（`DetectBox` 字段误用，`BUILD_EXIT=3`）
- r2 transcript/log + core-tests xml + selftest log（`track_id null default` 与 `queue_overflow` 判例错误）

## 6. 未执行 / 未声称

1. **未执行 RKNN 前向推理**：未从真实帧生成 envelope；夹具 envelope 用于验证关联/发布语义。
2. 未连接相机（ENV-D 未执行）；无真实视频与人工视觉确认。
3. 未实现语义白名单/跟踪、线程调度、P5 视图、测距（后续 ITEM）。
4. 未测多线程 submit/pop 竞争与真实队列背压；计数器为单生产者假设。
5. `latency_ms` 为字段计算，不是实测端到端延迟。
6. 本结果不替代验证能力的独立复现。
