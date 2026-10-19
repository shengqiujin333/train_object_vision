# ITEM-008 InferenceEvidence — 跟踪与多帧确认协同

- Evidence type: InferenceEvidence
- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-008`
- 日期: 2026-10-19
- 权威轮次: **r4**（`20261019-item008-r4`）；r1/r2/r3 为返工前证据（保留）
- 事实口径: 下列均为本次在 RK3588 上真实执行（夹具 envelope/跟踪输入）；未执行项见 §6。

## 1. 结论（本次验收范围）

| 用例 | 期望 | 实测 | 证据 |
|---|---|---|---|
| 同目标连续命中 | 稳定 track、单事件、不重复 | track=1 连续 4 帧；`events_total` 0→0→1→1 | `item008-fixture-same_target` |
| 多目标互不干扰 | 各自 track/id、各自事件 | `events=2`（两个不同 track） | `item008-fixture-two_targets` |
| 标签变化不串 id | person→obstacle 新 id | `ids=2` | `item008-fixture-label_guard` |
| 丢失后重现不复用 id | 删除后新 id | `dropped=1 new_id=2` | `item008-fixture-miss_drop` |
| generation 变化清理 | reset 跟踪器 + 清确认 + 新 id | `tracker_resets=1 gen_keys=1 new_id=2 events=0/0/1` | `item008-fixture-generation_reset` |
| 相机级适配路径 | 单相机事件、关联键 null(-1) | `det_id=-1 events=1`，事件 `track_id=-1` | `item008-fixture-camera_level` |
| 跟踪不可用 | null 丢弃（回归 ITEM-007） | `det=0 no_track=1` | `item008-fixture-envelope_only` |
| 容量上限 | 超限显式 untracked(-1) | `ids=1,-1 untracked=1` | `item008-fixture-max_tracks` |
| id 永不 0 | 所有分配 >=1 或 -1 | selftest `ids never 0` | `item008-selftest` |
| 阈值来自配置 | confirm/confidence/suppress | `confirm_frames=3 confidence_min=0.5 suppress_window_ms=3000`；轻量跟踪下 3 帧 1 事件 | `item008-config` |
| core 单测（含跟踪/协同） | 全绿 | `tests=1161 failures=0` | `item008-core-tests-r4.xml` |
| 探针 selftest | 全绿 | `RESULT: PASS failures=0`（23 判例） | `item008-selftest-r4.log` |

## 2. 执行环境（实测）

| 项 | 值 |
|---|---|
| 目标 | RK3588，Linux 6.1.141 aarch64，8 核；主机指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU` |
| 编译器 / CMake | g++ 11.4.0 / cmake 3.22.1 |
| 输入 | 夹具 envelope（front，`in_roi=true`，envelope 无 track_id → 由轻量跟踪器分配） |
| 配置 | `app/config/thresholds.yaml`（confirm/confidence/suppress） |
| 入口 | `app/tools/board_vision_tracking.py`（登记 Paramiko SSH；仅写 /tmp，结束清理） |

## 3. 跟踪语义

- 匹配：同标签 + IoU 贪心；未匹配且启用回退时按中心距（≤0.25）匹配。
- 生命周期：`max_misses=3` 淘汰；`max_gap_ms=1000` 时间鸿沟过期；`max_tracks=32` 超限显式 -1。
- id：从 1 单调递增；`reset()`（generation 变化）清空 track 但**不重置** id 计数 → 旧 id 绝不复用。
- 策略：`EnvelopeOnly`（null 丢弃）/`Lightweight`（本跟踪器）/`CameraLevel`（共享 -1，显式相机级，会合并多目标，默认关闭）。

## 4. 性能/资源测量范围

本增量为跟踪/协同正确性，**不测量** RKNN 前向耗时/内存/FPS（未执行前向），也不做长时间跟踪稳定性/多路并发测量。匹配为 O(tracks×boxes) 线性扫描，具体开销归 ITEM-009/执行轮。

## 5. 证据文件（`docs/vision/test_evidence/ITEM-008/`）

r4（权威）：
- `vision-item008-…-r4.transcript.json` / `.log`
- `…-r4.app_core-evidence_item008-core-tests-…-r4.xml`（1161/0）
- `…-r4.app_core-evidence_item008-selftest-…-r4.{log,json}`（23 判例）
- `…-r4.app_core-evidence_item008-fixture-{same_target,two_targets,label_guard,miss_drop,generation_reset,camera_level,envelope_only,max_tracks}-…-r4.{log,json}`
- `…-r4.app_core-evidence_item008-config-…-r4.{log,json}`

返工前证据（保留）：
- r1 transcript/log + core-tests xml + selftest log（夹具 `in_roi=false` 导致无事件）
- r2 transcript/log + core-tests xml + selftest log（generation reset 顺序错误：先 adapt 后 reset）
- r3 transcript/log + core-tests xml + selftest log（顺序修复后通过，仅策略标签文案订正前）

## 6. 未执行 / 未声称

1. **未执行 RKNN 前向推理**：没有真实帧/检测输入；夹具 envelope 用于验证关联与确认协同。
2. 未连接相机（ENV-D 未执行）；无真实画面与人工视觉确认。
3. 未实现外观/ReID 跟踪：交叉与长遮挡可能换 id（已声明）。
4. 未测多路并发、长时间跟踪稳定性、真实目标运动下的 id 保持率。
5. 未实现线程/调度、P5 调试视图、测距（后续 ITEM）。
6. 本结果不替代验证能力的独立复现。
