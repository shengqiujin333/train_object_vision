# 视觉推理 ITEM-007 实现 — 语义适配与规则接入（front）

- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-007`
- 版本: v1（2026-10-19）
- 上游: `docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0 §6（白名单/track/受控清理）、`docs/vision/VISION_DESIGN.md` §3/§7.6、`docs/vision/VISION_TEST_DESIGN.md` G7（TC-T-SEM-01..05）、ITEM-005（in_roi）、ITEM-006（InferenceEnvelope）、`core::RuleEngine` / `RuleConfig`
- 范围: 只实现 ITEM-007（语义适配与规则接入）。未实现跟踪（ITEM-008）、真实前向/线程调度/P5/测距（后续 ITEM）。

## 1. 交付内容

| 交付 | 路径 | 说明 |
|---|---|---|
| 语义适配纯逻辑 | `app/src/core/include/railhmi/core/vision_semantic_adapter.h`、`src/vision_semantic_adapter.cpp` | 白名单映射为 `core::Detection`、未知 label 拦截、同 track 聚合 + 按 track 排序、候选/非候选分流、generation 受控清理、下发 `RuleEngine`。仅标准库。 |
| 受控清理接口 | `app/src/core/include/railhmi/core/rules.h`、`src/rules.cpp` | `RuleEngine::invalidate_track(camera, track)` / `invalidate_camera(camera)`：只清该目标/该相机的确认与抑制状态，**不删除事件**。 |
| 探针 | `app/src/core/tools/semantic_probe.cpp`（`railhmi-vision-semantic`） | `--selftest`（19 判例）、11 个 fixture、`--config`（thresholds 的 confirm/confidence/suppress）。 |
| 单元测试 | `app/tests/core/test_vision_semantic_adapter.cpp` | person/未知/多目标/离轨不误清/generation/无 track/低置信/obstacle/不可用/聚合排序/清理作用域。 |
| 板端入口 | `app/tools/board_vision_semantic.py` | 一次有界会话：CoreTests→core-tests→selftest→11 fixtures→config→取回证据→清理。 |

构建接入：`app/src/core/CMakeLists.txt`、`app/tests/core/CMakeLists.txt`、`app/tests/core/main.cpp`（新增条目）。

## 2. 如何满足工作项

| 要求 | 实现 | 证据 |
|---|---|---|
| 按受支持标签白名单严格映射为 `core::Detection` | `SemanticConfig.known_labels` / `allowed_labels`（front：`person`、`obstacle`）；映射 camera/profile/t_ms/track_id/in_roi/confidence/distance/label | `person_on_rail`：`event person_on_rail level=1 track=1` |
| 未知 label 被拦截 | `known_labels` 之外 → `blocked_unknown_label++`，不进入规则 | `unknown_label`：`events=0 unknown=3 mapped=0` |
| 同帧多目标先聚合再按稳定 track 排序 | 同 track 取最高分合并（`aggregated`），`std::map<int,Detection>` 天然按 track_id 升序 | selftest `same track aggregated` / `sorted by track_id`；`multi_target` 2 事件 |
| 禁止用 track_id=0 冒充同一目标 | `require_track=true` 时 `track_id<0` 的框被丢弃并计 `dropped_no_track`，不产生 Detection | `no_track`：`events=0 no_track=3 det_last=0` |
| generation 变化受控清理确认状态 | 适配器按相机记录 generation；变化时调用 `RuleEngine::invalidate_camera`（不删事件），计 `generation_reset_keys` | `generation_reset`：`gen_reset_keys=1 events=0/0/1`（新代重新累计 3 帧才报警） |
| 失帧/目标消失只清该目标，不整路误清 | 非候选目标走 `RuleEngine::invalidate_track(camera,track)`；候选走 `on_detection` | `off_rail`：同帧 track2 离轨，track1 仍在第 3 帧报警（`events=1`） |
| 多目标命中不重复开事件 | `RuleEngine` 抑制窗口（`suppress_window_ms`）内同 key 只更新已有事件 | selftest `no duplicate events within suppress window` |
| 判定可复现 | `--selftest` 19 判例全 PASS | `item007-selftest` |

语义映射（front）：

| envelope label | 条件 | Detection | RuleEngine 结果 |
|---|---|---|---|
| `person` | `in_roi && confidence>=thresholds.confidence_min` | `label=person`, `in_roi=true` | `PersonOnRail`（L1） |
| `obstacle` | 同上 | `label=obstacle` | `FrontObstacle`（L1） |
| 其它（`car`/`dog`/空串…） | — | 拦截（`blocked_unknown_label`） | 无 |
| 已知但 profile 不支持 | — | 拦截（`blocked_not_allowed`） | 无 |
| `track_id<0` | `require_track=true` | 丢弃（`dropped_no_track`） | 无 |
| 在轨但置信度不足 | — | 不下发（`dropped_low_confidence`），并按 track 清理 | 无 |
| 不在轨道 | — | 不下发（`dropped_off_rail`），并按 track 清理 | 无 |

## 3. 受控清理（RuleEngine 增量）

```cpp
int RuleEngine::invalidate_track(const std::string& camera_id, int track_id);  // 只清该目标
int RuleEngine::invalidate_camera(const std::string& camera_id);               // 只清该相机
```
- key 结构为 `camera|type|track_id`；按前缀（相机）+ 后缀（track）匹配清理 `confirm_`/`last_emit_ms_`/`key_event_id_`。
- **不删除事件**（`events_` 不变，确认状态与历史保留）。
- `track_id<0` 不形成 key，`invalidate_track` 直接返回 0。

## 4. 运行入口

```
cd app && bash build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-vision-semantic --selftest --out selftest.log --json selftest.json
./build-core/tools/railhmi-vision-semantic --fixture person_on_rail --out p.log
./build-core/tools/railhmi-vision-semantic --fixture unknown_label --out u.log
./build-core/tools/railhmi-vision-semantic --config ./config --out config.log
```

## 5. 验证记录

- 本轮实现**首次板端执行即通过**（r1：build=0、core-tests 1142/0、selftest 19/19、11 fixtures 与 config 全部符合预期）；无返工轮。

## 6. 观察与移交

1. **跟踪仍缺失（ITEM-008）**：`require_track=true` 时无 track 的框不报警（宁可不报也不合并目标）。真实运行时须由 ITEM-008 提供稳定 track_id；在此之前 front 的在轨人员报警只能在夹具/受控输入下产生。
2. **既有 `RuleEngine::on_detection` 的整路 reset 行为未改动**：其非候选分支仍调用 `reset_camera_confirm(camera)`。本增量通过适配器**只把候选目标送入 on_detection**、非候选走按 track 清理来避免该问题；**调用方必须只下发适配器输出**（ITEM-009 接入时遵守）。若未来要改 on_detection 自身语义，需版本化并重跑规则回归。
3. **generation 清理按相机**：generation 变化即整路重建，清该相机全部目标状态是正确语义；与“单个目标失帧只清该目标”区分。
4. **真实前向未执行**：夹具 envelope；真实 `rknn_run`→envelope→适配器归 ITEM-009。
5. **未实现**：跟踪、线程/调度、P5 调试视图、测距（后续 ITEM）。
6. 白名单/`confidence_min` 来源：front 白名单为代码常量（设计 §3），`confidence_min` 来自 `thresholds.confidence_min`（`--config` 实测）。
