# ITEM-005 InferenceEvidence — 同帧两阶段判定（front）

- Evidence type: InferenceEvidence
- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-005`
- 日期: 2026-10-19
- 权威轮次: **r4**（`20261019-item005-r4`）；r1/r2/r3 为返工前证据（保留）
- 事实口径: 下列均为本次在 RK3588 上真实执行（夹具张量/掩码）；未执行项见 §6。

## 1. 结论（本次验收范围）

| 用例 | 期望 | 实测 | 证据 |
|---|---|---|---|
| 人员框中心在 rail 掩码内 | mask_hit + person 语义 + 报警 | `ok=1 available=1 reason=TWO_STAGE_OK persons=1 mask_hit=1 alarms=1`；det `(0.45,0.75,0.1,0.1) mask_hit=1 alarm=1` | `item005-fixture-center_in` |
| 中心不在掩码 | 不报警、计数 | `mask_hit=0 dropped_no_mask=1 alarms=0 reason=TWO_STAGE_NO_ALARM` | `item005-fixture-center_out` |
| 无人员 | NO_PERSON，可用 | `available=1 alarms=0 reason=TWO_STAGE_NO_PERSON` | `item005-fixture-no_person` |
| 分割不可用 | 不可用（不输出安全结论） | `ok=0 available=0 reason=TWO_STAGE_UNAVAILABLE`，无 det | `item005-fixture-seg_unavailable` |
| 掩码为空 | 不可用 | `reason=TWO_STAGE_UNAVAILABLE` | `item005-fixture-mask_empty` |
| 检测不可用 | 不可用 | `reason=TWO_STAGE_UNAVAILABLE` | `item005-fixture-detect_unavailable` |
| 分割帧 ≠ 当前帧 | 帧不一致 | `reason=TWO_STAGE_FRAME_MISMATCH available=0` | `item005-fixture-frame_mismatch` |
| danger_zone 命中 | zone=1 且报警 | `mask_hit=1 alarms=1`；det `zone=1 alarm=1` | `item005-fixture-danger_zone_hit` |
| danger_zone 未命中（在轨道但不在危险区） | mask_hit=1、zone=0、不报警 | `mask_hit=1 dropped_zone=1 alarms=0`；det `zone=0 alarm=0` | `item005-fixture-danger_zone_miss` |
| 重复/乱序 seq、旧 generation | 拒绝 | `reason=TWO_STAGE_STALE available=0`（selftest 三判例） | `item005-selftest` |
| 缓存默认关闭 | 不复用 | `UNAVAILABLE used_cache=0` | `item005-fixture-cache_disabled` |
| 缓存启用且同 generation 近期 | 复用 | `available=1 used_cache=1 cache_age_ms=100 alarms=1` | `item005-fixture-cache_hit` |
| 缓存过期 | 不复用 | `available=0 used_cache=0` | `item005-fixture-cache_expired` |
| 缓存跨 generation | 不复用 | `available=0 used_cache=0` | `item005-fixture-cache_gen_change` |
| 人员框中心越界 | 丢弃并计数 | `dropped_bounds=1 alarms=0` | `item005-fixture-bounds` |
| 多目标聚合 | 3 人员 → 2 命中 1 未命中 | `persons=3 mask_hit=2 alarms=2 dropped_no_mask=1` | `item005-fixture-multi` |
| 掩码与原图尺寸不符 | 不可用 | `reason=TWO_STAGE_UNAVAILABLE` | `item005-fixture-mask_misaligned` |
| danger_zone 来自 roi.yaml | 读取 CAM-01 | `danger_zone=found x=0.080000 y=0.820000 w=0.840000 h=0.160000` | `item005-config` |
| core 单测（含两阶段判定） | 全绿 | `tests=1084 failures=0` | `item005-core-tests-r4.xml` |
| 探针 selftest | 全绿 | `RESULT: PASS failures=0`（30 判例） | `item005-selftest-r4.log` |

## 2. 执行环境（实测）

| 项 | 值 |
|---|---|
| 目标 | RK3588，Linux 6.1.141 aarch64，8 核；主机指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU` |
| 编译器 / CMake | g++ 11.4.0 / cmake 3.22.1 |
| 输入 | 夹具：人员 `DetectBox` + rail 二值掩码（1280×720，stride=1280）；图像尺寸 1280×720 |
| 配置 | `app/config/roi.yaml`（CAM-01 danger_zone） |
| 入口 | `app/tools/board_vision_two_stage.py`（登记 Paramiko SSH；仅写 /tmp，结束清理） |

## 3. 判定语义

- `available`＝检测与分割同帧可用；`TWO_STAGE_UNAVAILABLE`/`FRAME_MISMATCH`/`STALE` 一律 `available=false` 且**不产出任何“轨道内安全”结论**（`detections` 为空）。
- 命中＝人员框中心点（归一化）在任一 rail 掩码内（掩码须与原图同尺寸、stride 正确）；配置 danger_zone 时还需落在该 ROI 内才计报警。
- 身份接纳复用 `core::FrameGate`：DuplicateSeq/OutOfOrderSeq/StaleGeneration/StaleSession → `TWO_STAGE_STALE`。
- 跨帧缓存默认关闭；启用须登记 `max_age_ms`，跨 generation 一律拒绝。

## 4. 性能/资源测量范围

本增量为判定正确性，**不测量** RKNN 前向耗时/内存/FPS（未执行前向）。掩码查找为单点 O(掩码数)；缓存复用会把整幅掩码（921600B）拷贝一次（仅启用缓存时）。真实负载耗时/内存归 ITEM-009/执行轮。

## 5. 证据文件（`docs/vision/test_evidence/ITEM-005/`）

r4（权威）：
- `vision-item005-…-r4.transcript.json` / `.log` — 全流程命令/退出码/输出/主机指纹
- `…-r4.app_core-evidence_item005-core-tests-…-r4.xml` — core 单测 JUnit（1084/0）
- `…-r4.app_core-evidence_item005-selftest-…-r4.{log,json}` — 30 判例自检
- `…-r4.app_core-evidence_item005-fixture-{center_in,center_out,no_person,seg_unavailable,mask_empty,detect_unavailable,frame_mismatch,danger_zone_hit,danger_zone_miss,stale,cache_disabled,cache_hit,cache_expired,cache_gen_change,bounds,multi,mask_misaligned}-…-r4.{log,json}`
- `…-r4.app_core-evidence_item005-config-…-r4.{log,json}`

返工前证据（保留）：
- r1 transcript/log（`TwoStageCache` 默认构造重复定义，`BUILD_EXIT=3`）
- r2 transcript/log + core-tests xml + selftest log（`evaluate_two_stage` 对不可用返回 false 与实现语义不符）
- r3 transcript/log + core-tests xml + selftest log（探针悬垂临时数组导致 danger_zone/multi/bounds 结果错乱）

## 6. 未执行 / 未声称

1. **未执行 RKNN 前向推理**：无真实视频帧的端到端两阶段；夹具为手工构造框/掩码。真实前向与端到端归 ITEM-009。
2. 未连接相机（ENV-D 未执行）；真实画面命中/误报与人工视觉确认属用户/现场。
3. 未实现结果发布、语义白名单、跟踪、调度/P5、测距（后续 ITEM）。
4. 未测缓存启用下的真实耗时/内存；缓存默认关闭。
5. 未做实时性能与多路并发测量。
6. 本结果不替代验证能力的独立复现。
