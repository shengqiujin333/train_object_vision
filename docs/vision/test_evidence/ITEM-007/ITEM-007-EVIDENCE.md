# ITEM-007 InferenceEvidence — 语义适配与规则接入（front）

- Evidence type: InferenceEvidence
- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-007`
- 日期: 2026-10-19
- 权威轮次: **r1**（`20261019-item007-r1`，首次执行即通过，无返工轮）
- 事实口径: 下列均为本次在 RK3588 上真实执行（夹具 envelope）；未执行项见 §6。

## 1. 结论（本次验收范围）

| 用例 | 期望 | 实测 | 证据 |
|---|---|---|---|
| person 在轨语义 | 3 帧确认 → PersonOnRail L1 | `events=1`；`event type=person_on_rail level=1 track=1` | `item007-fixture-person_on_rail` |
| 未知 label 拦截 | 不产生报警 | `events=0 unknown=3 mapped=0` | `item007-fixture-unknown_label` |
| 多目标不重复开事件 | 每 track 独立事件 | `events=2`（track 1/2），`mapped=6` | `item007-fixture-multi_target` |
| 离轨目标不误清其他目标 | 同帧离轨目标不影响在轨目标的确认 | `events=1`（track1） | `item007-fixture-off_rail` |
| generation 变化清理确认 | 新代重新累计，不跨代复用 | `gen_reset_keys=1 events=0/0/1` | `item007-fixture-generation_reset` |
| 无 track 不得用 0 冒充 | 丢弃并计数，不产生 Detection | `events=0 no_track=3 det_last=0` | `item007-fixture-no_track` |
| 在轨但低置信 | 不下发 | `events=0`（selftest low_conf=3） | `item007-fixture-low_confidence` / selftest |
| obstacle 白名单 | FrontObstacle | selftest `obstacle -> FrontObstacle` | `item007-fixture-obstacle` |
| envelope 不可用 | 无判定/无报警 | `unavailable -> no detections/alarms` | `item007-fixture-unavailable` |
| 同 track 聚合 | 同帧同 track 合并（取最高分） | `aggregate`：det=2 agg=1 | `item007-fixture-aggregate` |
| 按 track 排序 | 升序 | `sorted`：track 2 在前、5 在后 | `item007-fixture-sorted` |
| 阈值来自配置 | `thresholds` 的 confirm/confidence/suppress | `confirm_frames=3 confidence_min=0.5 suppress_window_ms=3000` | `item007-config` |
| core 单测（含语义适配） | 全绿 | `tests=1142 failures=0` | `item007-core-tests-r1.xml` |
| 探针 selftest | 全绿 | `RESULT: PASS failures=0`（19 判例） | `item007-selftest-r1.log` |

## 2. 执行环境（实测）

| 项 | 值 |
|---|---|
| 目标 | RK3588，Linux 6.1.141 aarch64，8 核；主机指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU` |
| 编译器 / CMake | g++ 11.4.0 / cmake 3.22.1 |
| 输入 | 夹具 envelope（front profile，`in_roi` 由 ITEM-005 语义直接给出；`track_id` 显式） |
| 配置 | `app/config/thresholds.yaml`（confirm_frames/confirm_window_ms/confidence_min/suppress_window_ms） |
| 入口 | `app/tools/board_vision_semantic.py`（登记 Paramiko SSH；仅写 /tmp，结束清理） |

## 3. 语义与规则

- 白名单：front `person` / `obstacle`；未知 label 拦截并计数，不进入 `RuleEngine`。
- 候选条件：`in_roi && confidence >= thresholds.confidence_min`；候选 → `on_detection`（`RuleEngine` 做 3 帧确认与抑制窗口）；非候选 → 仅按 track 受控清理，不下发。
- `track_id<0`（null）：`require_track=true` 时丢弃，**禁止**用 0 冒充；跟踪由 ITEM-008 提供。
- generation 变化：`RuleEngine::invalidate_camera` 受控清理该相机确认状态（不删事件）；单目标失帧：`invalidate_track` 只清该目标。
- 同帧同 track 聚合（取最高分）后按 track_id 升序下发。

## 4. 性能/资源测量范围

本增量为语义/规则正确性，**不测量** RKNN 前向耗时/内存/FPS（未执行前向）；也不测多线程/多相机并发。规则确认/抑制为既有 `RuleEngine` 逻辑，未做新性能测量。

## 5. 证据文件（`docs/vision/test_evidence/ITEM-007/`）

- `vision-item007-…-r1.transcript.json` / `.log` — 全流程命令/退出码/输出/主机指纹
- `…-r1.app_core-evidence_item007-core-tests-…-r1.xml` — core 单测 JUnit（1142/0）
- `…-r1.app_core-evidence_item007-selftest-…-r1.{log,json}` — 19 判例自检
- `…-r1.app_core-evidence_item007-fixture-{person_on_rail,unknown_label,multi_target,off_rail,generation_reset,no_track,low_confidence,obstacle,unavailable,aggregate,sorted}-…-r1.{log,json}`
- `…-r1.app_core-evidence_item007-config-…-r1.{log,json}`

## 6. 未执行 / 未声称

1. **未执行 RKNN 前向推理**：未从真实帧生成 envelope；夹具 `in_roi`/`track_id` 显式给定。
2. **无真实跟踪**：`track_id` 由夹具提供；跟踪（ITEM-008）未实现，真实运行时无 track 的框按设计不报警。
3. 未连接相机（ENV-D 未执行）；无真实画面与人工视觉确认。
4. 未实现线程/调度、P5 调试视图、测距（后续 ITEM）。
5. 未改 `RuleEngine::on_detection` 既有非候选 reset 语义；仅在适配器层避开，调用方须只下发适配器输出（交接 ITEM-009）。
6. 本结果不替代验证能力的独立复现。
