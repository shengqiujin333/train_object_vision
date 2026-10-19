# 视觉推理 ITEM-005 实现 — 同帧两阶段判定（front）

- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-005`
- 版本: v1（2026-10-19）
- 上游: `docs/vision/VISION_DESIGN.md` §7.4、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0 §6（同帧同代/掩码语义/不可用≠安全）、`docs/vision/VISION_TEST_DESIGN.md` G5（TC-T-TWOPHASE-01..06）、ITEM-003（人员框）、ITEM-004（rail 框 + 每框二值掩码）、`app/config/roi.yaml`（CAM-01 danger_zone）、`core::FrameGate`（视频 ITEM-003 身份接纳）
- 范围: 只实现 ITEM-005（同帧两阶段判定）。未实现结果发布/语义白名单接入/跟踪/线程调度/P5（后续 ITEM）。

## 1. 交付内容

| 交付 | 路径 | 说明 |
|---|---|---|
| 两阶段判定纯逻辑 | `app/src/core/include/railhmi/core/vision_two_stage.h`、`src/vision_two_stage.cpp` | 同帧身份组合、中心点落掩码判定、danger_zone 求交、可用性门、身份接纳、可选跨帧缓存。仅标准库。 |
| 探针 | `app/src/core/tools/two_stage_probe.cpp`（`railhmi-vision-twostage`） | `--selftest`（30 判例）、17 个 fixture、`--config`（danger_zone 取自 `roi.yaml` CAM-01）。 |
| 单元测试 | `app/tests/core/test_vision_two_stage.cpp` | 身份/命中与未命中/可用性门/danger_zone/接纳与缓存/计数。 |
| 板端入口 | `app/tools/board_vision_two_stage.py` | 一次有界会话：CoreTests→core-tests→selftest→17 fixtures→config→取回证据→清理。 |

构建接入：`app/src/core/CMakeLists.txt`、`app/tests/core/CMakeLists.txt`、`app/tests/core/main.cpp`（仅新增条目）。

## 2. 如何满足工作项

| 要求 | 实现 | 证据 |
|---|---|---|
| 同一 FrameDescriptor 身份下组合检测与分割 | `TwoStageInput{frame, detect_frame, seg_frame}`；`require_same_frame` 下两者必须与 `frame` 全等（session/camera/generation/seq），否则 `FRAME_MISMATCH` | `frame_mismatch` fixture；selftest `seg_frame != frame`/`detect_frame != frame` |
| 人员框中心点落在 rail 掩码内 → in_roi + person 语义 | `mask_positive_at(masks, …, cx, cy)`（列优先/stride 正确、掩码必须与原图同尺寸）；命中置 `mask_hit`、`label="person"` | `center_in`：`mask_hit=1 alarms=1`，det `(0.45,0.75,0.1,0.1)`；`center_out`：`dropped_no_mask=1` |
| 配置 danger_zone 时与 roi.yaml 求交 | `cfg.danger_zone`（`RoiConfig*`）+ `core::roi_contains`；未配置则不额外求交 | `--config` 读到 `x=0.08 y=0.82 w=0.84 h=0.16`；`danger_zone_hit` zone=1 alarm=1；`danger_zone_miss` mask_hit=1 zone=0 `dropped_zone=1` alarm=0 |
| 禁止跨帧/跨 generation 缓存作为实时依据 | 严格判定只用本帧掩码；缓存 `TwoStageCacheConfig{enabled=false, max_age_ms=0}` 默认关闭；启用时必须给 `max_age_ms`，且 `reject_on_generation_change` 拒绝 session/generation 变化 | `cache_disabled`：seg 不可用 → `UNAVAILABLE used_cache=0`；`cache_hit`：同 generation 100ms 内复用 `used_cache=1 cache_age_ms=100`；`cache_expired`/`cache_gen_change` → 不可用 |
| 旧 generation/乱序帧被拒绝 | `TwoStageEvaluator` 用 `core::FrameGate`（视频 ITEM-003）接纳：DuplicateSeq/OutOfOrderSeq/StaleGeneration/StaleSession → `TWO_STAGE_STALE`，`available=false` | `stale` fixture（重复 seq）；selftest 重复/乱序/旧 generation 三条 |
| 分割失败或掩码为空 → 不输出“轨道内安全”而是不可用 | 掩码缺失/空/无正像素/与原图不对齐、检测不可用 → `ok=false available=false reason=UNAVAILABLE`，`detections` 为空（不产出任何“无目标/安全”判定） | `seg_unavailable`/`mask_empty`/`detect_unavailable`/`mask_misaligned` 四个 fixture |
| 判定可复现 | 纯逻辑 + 确定性夹具；`--selftest` 30 判例全 PASS | `item005-selftest` |

判定语义：

- `available` 表示“检测与分割同帧可用”；`alarm_hits` 表示中心点命中掩码（配置 danger_zone 时还需落在危险区内）。
- 无人员 → `TWO_STAGE_NO_PERSON`；有人员但无命中 → `TWO_STAGE_NO_ALARM`；命中 → `TWO_STAGE_OK`。
- **不可用绝不等于安全**：不可用时 `ok=false`、`detections` 为空且 `reason=UNAVAILABLE`，调用方不得据此判定“轨道内无人”。
- `evaluate_two_stage` 返回 false 仅表示参数/配置错误；不可用/帧不一致是**有效结果**（`out->available=false` + reason）。

## 3. 跨帧缓存策略（默认关闭）

| 字段 | 默认 | 含义 |
|---|---|---|
| `enabled` | false | 默认不复用跨帧掩码 |
| `max_age_ms` | 0 | ≤0 视为不允许复用；启用时必须显式登记 |
| `reject_on_generation_change` | true | session/camera/generation 变化一律拒绝（实时报警绝不跨代） |
| `policy_note` | 空 | 供证据登记实际策略 |

`TwoStageEvaluator`：本帧分割可用 → 正常判定并按配置存缓存；本帧分割不可用 → 仅当策略允许且同 generation 且年龄 ≤ `max_age_ms` 时复用，否则 `UNAVAILABLE`。

## 4. 运行入口

```
cd app && bash build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-vision-twostage --selftest --out selftest.log --json selftest.json
./build-core/tools/railhmi-vision-twostage --fixture center_in --out center_in.log
./build-core/tools/railhmi-vision-twostage --fixture seg_unavailable --out seg_unavailable.log
./build-core/tools/railhmi-vision-twostage --config ./config --out config.log
```

## 5. 返工记录（保留失败证据）

| 轮次 | 现象 | 根因 | 修复 | 复核 |
|---|---|---|---|---|
| r1 | `BUILD_EXIT=3`：`definition of explicitly-defaulted TwoStageCache::TwoStageCache()` | 头文件 `= default` 又在 .cpp 重复定义 | 删除 .cpp 中的默认构造定义 | r2 build=0 |
| r2 | core-tests/selftest 失败（6+4）：不可用/帧不一致判例期望 `evaluate_two_stage` 返回 true | 实现用 `fail(...)` 对“不可用”返回 false，与“不可用是有效结果”的语义不符 | 不可用/帧不一致改为填 `available=false`+reason 并返回 true；仅参数/配置错误返回 false | r3 selftest 0 失败 |
| r3 | `danger_zone_hit/miss`、`multi`、`bounds` 结果错乱（person_total 对但框为 (0,0,0,0)/越界） | 探针把 braced 临时 `std::vector` 传入 `mk_input` 并存指针 → 悬垂 | 夹具改用命名局部数组；头文件登记“指针须在本调用期间有效”的零拷贝契约 | r4 全部 fixture 正确 |

## 6. 观察与移交

1. **真实 RKNN 前向未执行**：本增量用夹具张量与掩码；真实 `rknn_run`+端到端两阶段归 ITEM-009。
2. **danger_zone 来源**：`roi.yaml` CAM-01/danger_zone（`--config` 已实测读取）；未配置时只做掩码判定（`danger_zone=0`）。
3. **掩码来源**：ITEM-004 的每框独立原图二值 plane；本模块只读（`const`），不持有或被持有（`TwoStageInput` 为零拷贝指针，生命周期由调用方保证）。
4. **缓存**：默认关闭；如产品需要开启，须先登记 `max_age_ms` 与失配策略（含跨代拒绝），并在证据中记录实际值。
5. **未实现**：结果发布（InferenceEnvelope）、语义白名单/跟踪、线程/调度接入、P5 调试视图、测距（后续 ITEM）。
6. `FrameGate` 复用视频 ITEM-003 的身份接纳语义，不重复实现。
