# 视觉推理 ITEM-008 实现 — 跟踪与多帧确认协同

- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-008`
- 版本: v1（2026-10-19）
- 上游: `docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0 §6（track_id 语义、多帧确认、禁止 0 冒充）、`docs/vision/VISION_DESIGN.md` §7.5、`docs/vision/VISION_TEST_DESIGN.md` G8（TC-T-TRK-01..04）、ITEM-006（Envelope/track_id）、ITEM-007（语义适配/受控清理）、`core::RuleEngine`
- 范围: 只实现 ITEM-008（跟踪与多帧确认协同）。未实现真实 RKNN 前向/线程调度/P5/测距（后续 ITEM）。

## 1. 交付内容

| 交付 | 路径 | 说明 |
|---|---|---|
| 轻量跟踪器 | `app/src/core/include/railhmi/core/vision_tracker.h`、`src/vision_tracker.cpp` | 标签 + IoU 贪心关联（可选中心距回退），稳定 `track_id`（从 1 单调，**永不 0**）；丢失淘汰、时间鸿沟过期、容量上限、`reset()`。仅标准库。 |
| 适配器集成 | `app/src/core/include/railhmi/core/vision_semantic_adapter.h`、`src/vision_semantic_adapter.cpp` | 新增 `TrackPolicy{EnvelopeOnly, Lightweight, CameraLevel}` 与 `TrackerConfig`；generation 变化时先 `reset()` 跟踪器再映射；`camera_level_track_id()=-1`。 |
| 探针 | `app/src/core/tools/tracking_probe.cpp`（`railhmi-vision-tracking`） | `--selftest`（23 判例）、8 个 fixture、`--config`（thresholds）+ `--policy lightweight|camera|envelope`。 |
| 单元测试 | `app/tests/core/test_vision_tracker.cpp` | 跟踪器本体（新/续/丢/删/reset/鸿沟/上限/标签隔离）+ 适配器三种策略 + generation 清理。 |
| 板端入口 | `app/tools/board_vision_tracking.py` | 一次有界会话：CoreTests→core-tests→selftest→8 fixtures→config→取回证据→清理。 |

构建接入：`app/src/core/CMakeLists.txt`、`app/tests/core/CMakeLists.txt`、`app/tests/core/main.cpp`（新增条目）。

## 2. 如何满足工作项

| 要求 | 实现 | 证据 |
|---|---|---|
| 无专用跟踪模型时提供显式可验证的关联策略 | `LightweightTracker`：同标签 + IoU≥`iou_match_min` 贪心匹配；可选中心距回退（`center_dist_max`）；`max_misses`/`max_gap_ms`/`max_tracks` 显式参数 | `--selftest` 跟踪器 12 判例；`same_target` |
| 使 RuleEngine 的 (camera,type,track_id) 抑制与多帧确认可用 | 适配器 `TrackPolicy::Lightweight` 为每框填稳定 track_id；`RuleEngine` 按 key 确认/抑制 | `same_target`：track=1 连续 4 帧、第 3 帧报警、第 4 帧不重复 |
| 同一目标连续命中只更新同一事件、不重复开新事件 | 稳定 id + 抑制窗口；`_same_target` 4 帧 events_total 停在 1 | `same_target` |
| 不同目标互不清除对方确认 | 每目标独立 track key；非候选只按 track 清理（ITEM-007） | `two_targets`：2 个不同 track 各 1 事件 |
| 跟踪不可用时明确 `track_id=null` 并走相机级适配路径 | `TrackPolicy::CameraLevel`：显式共享 `-1`（**不是 0**）；`EnvelopeOnly`：null 则丢弃 | `camera_level`：`det_id=-1 events=1`；`envelope_only`：`det=0 no_track=1` |
| 禁止把所有目标映射为 track_id=0 | id 从 1 单调递增；相机级用 -1；超上限用 -1 | `ids never 0`、`max_tracks`：`ids=1,-1 untracked=1` |
| 重连后旧目标状态被清理 | generation 变化：先 `LightweightTracker::reset()`（id 继续单调、不复用）再映射；`RuleEngine::invalidate_camera` 清确认 | `generation_reset`：`tracker_resets=1 gen_keys=1 new_id=2 events=0/0/1` |
| 标签变化不串 id | 匹配要求 `label` 相同 | `label_guard`：person→obstacle 新 id 2 |
| 目标丢失后重现不复用旧 id | `max_misses` 淘汰；`next_id_` 不重置 | `miss_drop`：`dropped=1 new_id=2` |

跟踪语义：

- 匹配：同标签 → IoU 贪心（按 IoU 降序）；未匹配且 `center_fallback` → 中心距 ≤ `center_dist_max`。
- 生命周期：未匹配 `misses++`，`> max_misses` 删除；`ts - last_ts > max_gap_ms` 过期不匹配。
- id：`next_id_` 从 1 开始单调递增，`reset()` 不清零（旧 generation id 绝不复用）；`max_tracks` 超限的框显式 `-1`（untracked），不伪造 id。
- 三种策略：`EnvelopeOnly`（沿用 envelope id，null 丢弃）、`Lightweight`（本跟踪器分配）、`CameraLevel`（共享 -1，显式相机级；会合并同相机多目标，仅在配置显式开启时使用）。

## 3. 运行入口

```
cd app && bash build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-vision-tracking --selftest --out selftest.log --json selftest.json
./build-core/tools/railhmi-vision-tracking --fixture same_target --policy lightweight --out same.log
./build-core/tools/railhmi-vision-tracking --fixture camera_level --out cam.log
./build-core/tools/railhmi-vision-tracking --config ./config --policy lightweight --out config.log
```

## 4. 返工记录（保留失败证据）

| 轮次 | 现象 | 根因 | 修复 | 复核 |
|---|---|---|---|---|
| r1 | core-tests/selftest/same_target/two_targets/camera_level/config 失败：无事件 | 夹具 `EnvelopeBox.in_roi` 默认 false（未设），适配器候选要求 `in_roi && confidence` → 无候选 | 夹具 `eb()` 置 `in_roi=true`（ITEM-005 语义） | r2 上述场景全通过 |
| r2 | `generation_reset`/core selftest 失败：新代 `new_id=1`、events `0/0/0` | `adapt_and_dispatch` 先 `adapt`（跟踪器仍持有旧代 track，新代帧被旧 track 匹配）后 `note_generation`（才 reset）→ id 未更新且计数被清 | 先 `note_generation`（reset 跟踪器/清规则）再 `adapt`；并把 generation 统计合并回结果 | r3 `tracker_resets=1 new_id=2 events=0/0/1` |

## 5. 观察与移交

1. **非外观跟踪**：只用标签 + IoU/中心距，不含 ReID/外观特征；目标交叉或长时间遮挡可能换 id。若现场需要更强关联，属后续模型/跟踪能力，不改本增量默认。
2. **相机级模式会合并目标**：`CameraLevel` 用一个相机级 key（-1）确认，**明确合并同相机多目标**，仅作“跟踪不可用时的显式适配路径”，默认关闭；生产默认应为 `Lightweight` 或上游真实 track。
3. **按相机单实例**：`SemanticAdapter` 内含单个跟踪器；多相机需每路一个适配器（ITEM-009 接入时按相机实例化）。
4. **generation 清理语义**：先 reset 跟踪器再映射，`tracker_resets`/`generation_reset_keys` 可观察；旧 id 不复用。
5. **真实前向/线程未执行**：夹具 envelope；真实 `rknn_run`→跟踪→规则归 ITEM-009。
6. 顺带订正 ITEM-007 文档中 selftest 判例数（30 → 实测 19），与验证轮 O-1 一致。
7. **未实现**：线程/调度、P5 调试视图、测距（后续 ITEM）。
