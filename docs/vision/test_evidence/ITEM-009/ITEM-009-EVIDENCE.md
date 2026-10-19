# ITEM-009 证据 — 真实检测器接入推理工作线程与调度

- Capability: `computer_vision_engineer.vision_inference_implementation`
- 工作项: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-009`
- 轮次标记: `20261021-item009-r8`（当前终轮：修复验证轮 D-01 后重跑）；`…-r6` 为上一轮交付、`…-r7` 为替代输入链路回归；r1–r4 为返工留痕
- 日期: 2026-10-21（r8）；r6/r7 为 2026-10-20
- 实现文档: `docs/vision/implementation/ITEM-009_DETECTOR_WORKER.md`
- 环境: RK3588 `192.168.0.103`（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`），Linux 6.1.141 aarch64、8 核、g++ 11.4.0、librknnrt `2.3.2 (429f97ae6b@2025-04-09T09:09:27)` + 驱动 `0.9.8`、7 个 thermal zone
- 执行方式: 登记 Paramiko SSH 入口 `app/tools/board_vision_pipeline.py`（凭据仅经 `TRAIN_RK3588_SSH_CREDENTIAL` 环境变量；受信任 known_hosts + RejectPolicy；全部有限超时；只写 `/tmp`；结束清理）

## 1. 本轮执行步骤（一次有界会话）

> r8 终轮（修复 D-01）在同一有界会话内完成下表全部步骤；r6/r7 为上一轮遗留证据，保留作对比留痕。

| 步骤 | 命令 | 退出 | 结果 |
|---|---|---|---|
| 上传 | app/ 320 文件 + `yolov5s.rknn`、`yolov8s-seg.rknn` | 0 | 仅 `/tmp/railhmi_vision_item009_20261021-item009-r8` |
| 构建 CoreTests | `bash build.sh CoreTests` | 0 | 通过 |
| 构建 MediaCheck | `bash build.sh MediaCheck` | 0 | GStreamer + librga + dlopen RKNN 会话全部编译通过 |
| 单元测试 | `railhmi-core-tests --junit ...` | 0 | `tests=1215 failures=0`（ITEM-008 的 1161 + 本增量 54） |
| 自检 | `railhmi-vision-pipeline --selftest` | 0 | **22/22 PASS**（含 r8 新增 3 个异常路径判例） |
| 真实链路（单路 front，test 源，12s） | `--mode chain --source test --duration 12 --lanes CAM-01` | 0 | 9/9 判据 PASS |
| 4 路 + 预算裁剪（`npu_budget=1`，10s） | `--mode chain --lanes CAM-01,CAM-04,CAM-05,CAM-06 --budget 1` | 0 | 9/9 判据 PASS |
| **D-01 回归**（模型缺失） | `--mode chain --models-dir /tmp/railhmi_item009_absent_models --duration 2` | **1（期望）** | 修前 **139/SIGSEGV**；修后 `MODEL_MISSING` 显式、降级后 40 帧 unavailable 发布、`chain_models_ready` 判 FAIL；不再崩溃 |
| 替代输入链路回归（r7，独立会话） | `railhmi-ai-runner --config ./config --duration 4` | 0 | 4/4 路 PASS、停止后 `AI 不可用`（`synthetic_tick=on` 显式标注） |
| 清理 | `rm -rf /tmp/railhmi_vision_item009_20261021-item009-r8` | 0 | `cleaned` |

## 2. 模型能力核验（`rknn_query`，非按文件名）

| 模型（来自 `app.ai.models`） | 声明 | 实际张量描述 | 结论 |
|---|---|---|---|
| `yolov5s` | 前端检测（COCO person） | 1 输入 `1×640×640×3` int8 NHWC zp=-128 scale=0.00392157；3 输出 `1×255×{80,40,20}×…` int8 NCHW | `READY`（结构逐项核验通过） |
| `yolov8s-seg` | 轨道分割 | 1 输入同上；13 输出（3 尺度 × {box64/score1/score_sum1/seg32} + proto `1×32×160×160`） | `READY` |

职责级可用性（`profile_accepts_kind` 证据地图）：`front=READY`（model_version `rknn-2.3.2/yolov5s+rknn-2.3.2/yolov8s-seg`）；`wheel_rail`/`pantograph`/`cab_door` 显式 `PROFILE_UNSUPPORTED`（职责语义归 ITEM-010）。

## 3. 真实链路（单路）

```
type=lane camera=CAM-01 profile=front ready=true consumed=60 inferred=60
  unavailable=9 unavailable_by_reason=TWO_STAGE_UNAVAILABLE:9
  last_unavailable_reason=TWO_STAGE_UNAVAILABLE last_unavailable_detail=rail mask empty
  published=60 dropped=0 skipped_interval=240 queue_depth=0
  last_infer_us=143854 latency_ms=144 fps=5.019141 events=0
type=summary submitted=300 consumed=60 published=60 unavailable=9 round=702 budget=1
  thermal={"state":"MEASURED","measured":true,"injected":false,"celsius":43.461,"source":"thermal_zone0"}
```

- **链路贯通**：test 源 → `FrameStore`/`FrameLease` → `AiFrameRouter`（身份键 session/generation/seq）→ 工作线程 → 预处理 → `rknn_init`/`rknn_inputs_set`/`rknn_run`/`rknn_outputs_get(want_float)` → ITEM-003/004 解码 → ITEM-005 同帧两阶段 → ITEM-006 envelope（`availability/reason/latency_ms/boxes/masks`）→ ITEM-007 语义适配 → `RuleEngine`。
- `submitted=300 → consumed=60` 正是 `inference_interval=5` 的降频（25/5=5 帧/s × 12s），`skipped_interval=240` 一致。
- `unavailable=9` 的 reason 是 `TWO_STAGE_UNAVAILABLE`（detail `rail mask empty`）：测试图案下分割未产出 rail 掩码 → 按设计判不可用，**不输出“轨道内安全”**。`ready` 与 `unavailable` 在 envelope 字段上可区分（示例见 §6）。
- 单帧 det+seg 实测 `last_infer_us≈143.9ms`（含预处理与两模型 `rknn_run`+后处理）；该数值是**测试图案**下的耗时，不作现场精度/并发性能结论。链路 FPS 5.0/s 由配置降频决定，不是模型上限。

## 4. 4 路派发 + 预算裁剪（`npu_budget=1`）

| 路 | profile | consumed | unavailable(reason) | published | last_infer_us |
|---|---|---|---|---|---|
| CAM-01 | front | 50 | 7 (`TWO_STAGE_UNAVAILABLE`/rail mask empty) | 50 | 142208 |
| CAM-04 | pantograph | 50 | 50 (`PROFILE_UNSUPPORTED`) | 50 | 2 |
| CAM-05 | wheel_rail | 49 | 49 (`PROFILE_UNSUPPORTED`) | 49 | 2 |
| CAM-06 | cab_door | 50 | 50 (`PROFILE_UNSUPPORTED`) | 50 | 1 |
| 合计 | — | **199** | 156 | **199** | `budget_effective=1`（每轮） |

- 4 路 × 10s，`npu_budget` 由 CLI 裁剪为 1（配置值 4）：每轮只运行 1 路，4 路各得 ≈50 次处理（50/50/49/50）——**预算生效且无饥饿**（若按静态优先级，低优先级 3 路会长期为 0）。
- `submitted=1000`（4×25fps×10s）、`consumed=199`：说明有界丢旧队列在预算不足时按设计丢帧（不积压、不阻塞取流）。
- 不可用路以 `~1–2µs` 返回 `PROFILE_UNSUPPORTED`，不占用 NPU，但消耗一次路数配额（已在实现文档 §7-6 登记为 ITEM-010 的移交项）。

## 5. 自检判例（`--selftest`，替代检测器，22/22 PASS）

| 判例 | 观测 |
|---|---|
| `selftest_frame_lease_from_stream` | 帧来自真实取流（NV12 320×180，seq=1），非合成元数据 |
| `prod_no_synthetic_frames` | 起线程 200ms 不 submit：`steps=40 infer_calls=0 consumed=0`（**生产模式无自造帧**） |
| `submit_not_blocked_by_infer` | 注入 200ms 推理、237 次提交：`max=1000µs` |
| `snapshot_not_blocked_by_infer` | `p95=0µs max=1000µs` |
| `request_stop_immediate` / `stop_ai_unavailable` | `request_stop=0µs`；快照立即 `worker_alive=false` 且 `ai_status_text=AI 不可用` |
| `stop_join_bounded_by_one_infer` | `join=28000µs`（200ms 注入推理；只等当前一次） |
| `slow_infer_did_run` | `infer_calls=5`（慢推理确实在执行） |
| `bounded_drop_oldest` / `drop_oldest_keeps_newest` | depth=2 推 5 真实帧：`depth=2 dropped=3`；pop 得到 `5,6`（保留最新两帧） |
| `budget_fairness_all_lanes_ran` / `_balanced` | `npu_budget=1`、4 路、8 轮：4/4 路各运行 2 次 |
| `temp_guard_halves_budget` / `injected_temp_not_measured` | 注入 `limit+5`：`guard=true budget 4→2`，样本 `INJECTED`（`measured=false`） |
| `thermal_source_reported_honestly` | 实测 43.461°C（7 zone 取最大，source=`thermal_zone3`，`THERMAL_OK`） |
| `track_id_never_zero` / `no_frame_lane_not_inferred` / `two_lanes_configured` | 框 `track_id != 0`；无帧路 `consumed=0`；两路配置 |
| `rule_engine_receives_detections` | 替代检测器 4 帧确认后 1 个事件（规则链路贯通；**非模型能力声明**） |
| `model_missing_graceful`（r8 新增） | 模型目录不存在：`load()=false`、`err=det:MODEL_MISSING;seg:MODEL_MISSING;`、front 不 ready、**进程不崩溃** |
| `model_missing_reason_visible`（r8 新增） | `status_json()` 中逐模型 `"ready":false` 且 reason=`MODEL_MISSING`（期望原因可见，不静默） |
| `invalid_model_graceful`（r8 新增） | 非法 `.rknn`（垃圾内容）：`err=det:MODEL_LOAD_FAILED;seg:MODEL_MISSING;`，同样只降级不崩溃 |

## 6. 关键输出样例

不可用 envelope（职责无模型证据，**空框** + reason，与 ready 可区分）：

```
{"session_id":"s1a153…","camera_id":"CAM-04","generation":1,"seq":250,"profile":"pantograph",
 "capture_mono_us":534419931408,"result_mono_us":534419941702,"latency_ms":10,"model_version":"",
 "availability":"unavailable","reason":"PROFILE_UNSUPPORTED","box_count":0,"boxes":[],"masks":[]}
```

可用 envelope（front：rail 掩码零拷贝引用；本帧无人形目标 -> `ready` + 空框）：

```
{"camera_id":"CAM-01","generation":1,"seq":250,"profile":"front","latency_ms":146,
 "model_version":"rknn-2.3.2/yolov5s+rknn-2.3.2/yolov8s-seg","availability":"ready","reason":"READY",
 "box_count":0,"boxes":[],"masks":[{"box_index":-1,"width":1280,"height":720,"stride_bytes":1280,
 "kind":"rail","plane_present":true}]}
```

调度决策/温度（每轮入账；`thermal_unknown_rounds=0` 表示本轮全部使用实测温度）：

```
type=summary … round=702 budget=1 thermal_guard_rounds=0 thermal_unknown_rounds=0
thermal={"valid":true,"state":"MEASURED","measured":true,"injected":false,"celsius":43.461,
         "source":"thermal_zone0","reason":"THERMAL_OK","zone_index":0,"read_mono_ms":534728427}
```

## 7. 验收判据对照

| 任务书要求 | 证据 |
|---|---|
| 以真实 Detector 替换 SimDetector 接入 AiWorker 与 ai_dispatcher 派发的带身份有界丢旧队列 | §2/§3：`RknnVisionDetector`（真实 `rknn_run`）+ `VisionAiWorker` 消费 `AiFrameRouter`；envelope 携带 session/camera/generation/seq；`consumed/published` 实测 |
| 生产模式关闭自造帧 tick | `AiWorkerConfig::synthetic_tick` 默认 false；自检 `prod_no_synthetic_frames`（`steps>0 infer_calls=0`）；旧 `railhmi-ai-runner` 显式开启并标注 |
| infer 不持调度锁阻塞 submit/状态快照/stop | §5：200ms 注入推理下 `submit max=1.0ms`、`snapshot p95=0ms`、`request_stop=0µs`、`join=28ms` |
| 按 plan_inference 的路数预算与温度守卫调度并记录实测温度 | §4（budget 1 生效且无饥饿）+ §6（实测 43.461°C，`MEASURED`，guard 与实测一致）；核心 54 判例覆盖预算/守卫/公平性 |
| 真实取流→派发→推理→结果发布链路贯通（file/test 源） | §3：submitted 300 → consumed 60 → published 60，envelope/infer 耗时与 reason 全量归档 |
| 停止后 AI 不可用语义成立 | `chain_stop_ai_unavailable`：`request_stop_us=0`、`ai_status_text=AI 不可用`、`worker_alive=false`；取流侧独立不受影响 |
| 异常路径必须显式降级而非崩溃（D-01） | §1 `neg-missing-models`：模型缺失 exit=1（修前 139）、`MODEL_MISSING` 可见、降级后仍发布 40 帧 unavailable envelope；§5 新增 3 个异常路径判例 |

## 8. 返工留痕（本轮内失败与修正）

| 轮次 | 保留文件 | 失败 | 修正 |
|---|---|---|---|
| r1 | `…-r1.log` | MediaCheck 编译失败：`SegResult` 未声明、成员 `thermal_` 重名、`sem` 误赋 `SemanticResult` | 补 include；改名 `thermal_sample`；改 `sem.stats`；修 `run_model` 声明与 `InputTensorSpec::zero_point` |
| r2 | `…-r2.log` | `core::steady_now_us` 不存在（应为 `core::TimeBase::steady_now_us()`） | 两处修正 |
| r3 | `…-r3.log`、`…r3.app_core-evidence_item009-selftest-…log/json` | selftest 3 项失败（丢旧用例被 `inference_interval` 降频；替代检测器 `in_roi=false` 导致无规则事件；规则引擎未共享实例） | 用例改 interval=1 且用真实帧推进；替代检测器置 `in_roi_flags=1`；共享 `RuleEngine` |
| r4 | `…r4.app_core-evidence_item009-selftest-…log/json` | `drop_oldest_keeps_newest` 断言错误（depth=2 首次 pop 得到倒数第二新） | 连续 pop 两次比对最新两帧 |
| r6 | 全部 7 份产物 | — | 终轮全绿（core 1215/0、selftest 19/19、chain 9/9、chain4 9/9） |
| r7 | `…-r7.log`、`…r7.app_core-evidence_item009-ai-runner-…log` | 无（回归轮） | 证明已被替换的替代输入链路（`SimDetector`+旧 `AiWorker`）在 `synthetic_tick=on` 显式开启下仍可运行，未因真实路径改造回退（4/4 路 PASS） |
| **r8** | `…-r8.*`（transcript/log + 8 证据） | **D-01（验证轮报告）**：`--mode chain` 模型缺失时 SIGSEGV（exit 139）、stdout 0 字节 | `load_kind` 五条失败路径改为 `fail_with`（先拷贝 reason 再 move）；探针 `emit` 加 flush；新增 3 个自检判例 + 板端负例步骤 `neg-missing-models`；r8 同一命令 exit=1、原因可见、不崩溃，其余判据全绿 |

### D-01（验证轮报告）根因与修复

- **根因**（验证侧 gdb 栈 `load_kind ← load` 与源码静态对应）：`RknnVisionDetector::load_kind` 五条失败分支均为
  `models_[kind] = std::move(m); if (err) *err = m->reason;`——`m` 已移交后解引用空 `unique_ptr`，
  在 `std::string::_M_assign` 崩溃；`run_chain` 恒传 `&load_err`，故“模型缺失 + 调用方要错误串”必崩。
- **修复**：新增局部 `fail_with(model, reason, detail)`：写 reason/detail → **拷贝** reason → 移交所有权 → 写 `*err`；
  5 处统一收口（`app/src/media/src/vision_rknn_detector.cpp`）。
- **回归防护**：探针 `emit()` 每行 `fflush`（异常路径保留已产生证据）；selftest 新增 3 判例
  （`model_missing_graceful` / `model_missing_reason_visible` / `invalid_model_graceful`）；
  板端入口新增 `neg-missing-models` 步骤，直接复现验证侧命令。
- **修前 / 修后**（板端同一命令 `--mode chain --models-dir <不存在目录> --duration 2`）：
  - 修前：`exit=139`（SIGSEGV），stdout 0 字节；
  - 修后：`exit=1`，`type=models` 中 `reason=MODEL_MISSING`（`present=false,ready=false`），
    `type=summary submitted=202 consumed=40 published=40 unavailable=40`，`[FAIL] chain_models_ready`
    （探针自身判据生效），无崩溃。

## 9. 覆盖边界（本轮不声称的范围）
## 9. 覆盖边界（本轮不声称的范围）

- 未连接任何真实相机（ENV-D 未执行）；未测真实画面检出率/误报率、掩码质量、人工视觉确认。
- 未测 4 路**全模型**并发（当前仅 front 有模型证据；其余职责 `PROFILE_UNSUPPORTED`）；未做热稳态长跑、未触发真实 >85°C 降载（守卫逻辑以注入样本 + 实测温度一致性证明，注入值显式标记）。
- 未设置 NPU core mask；`npu_budget` 不作 NPU 核数或算力容量解释；`fps=5.0` 由配置降频决定，不是吞吐上限。
- 未实现 ITEM-010（职责可用性语义细化）、ITEM-011（测距）、ITEM-012（P5 掩码叠加）、ITEM-013（无相机自检归档）。
- 探针 CLI 边界：`--source` 非法值（如 `bogus`）当前按 `test` 处理（诊断探针行为，不是产品路径）；D-01 修复后同一命令不再崩溃（因模型缺失返回 exit=1，而不是 139）。严格参数拒绝（exit 2）留待 ITEM-013 CLI 硬化时统一处理。
- ITEM-001..008 的既有 TEST_PASS 保留其原范围，不在本轮外推。

## 10. 证据清单（本目录）

**r8（当前终轮，修复 D-01）**

- `vision-item009-192_168_0_103-20261021-item009-r8.transcript.json` / `.log` — 终轮结构化/人类可读 transcript（指纹、逐命令退出码、构建/测试/自检/chain/chain4/负例/下载/清理）
- `…-r8.app_core-evidence_item009-core-tests-20261021-item009-r8.xml` — JUnit（`tests=1215 failures=0`）
- `…-r8.app_core-evidence_item009-selftest-20261021-item009-r8.log` / `.json` — 自检 **22/22**（含 3 个异常路径判例）
- `…-r8.app_core-evidence_item009-chain-20261021-item009-r8.log` / `.json` — 单路真实链路（9/9）
- `…-r8.app_core-evidence_item009-chain4-20261021-item009-r8.log` / `.json` — 4 路 + 预算裁剪（9/9）
- `…-r8.app_core-evidence_item009-neg-missing-20261021-item009-r8.log` — **D-01 回归日志**：模型缺失时 exit=1（修前 139）、`MODEL_MISSING` 显式、降级后仍发布 40 帧 unavailable

**前轮与返工留痕**

- `vision-item009-192_168_0_103-20261020-item009-r6.transcript.json` / `.log` — 上一轮（D-01 修前）结构化/人类可读 transcript
- `vision-item009-…-r6.app_core-evidence_item009-core-tests-20261020-item009-r6.xml` — 上一轮 JUnit（`tests=1215 failures=0`）
- `vision-item009-…-r6.app_core-evidence_item009-selftest-20261020-item009-r6.log` / `.json` — 上一轮自检 19/19
- `vision-item009-…-r6.app_core-evidence_item009-chain-20261020-item009-r6.log` / `.json` — 上一轮单路真实链路（9/9）
- `vision-item009-…-r6.app_core-evidence_item009-chain4-20261020-item009-r6.log` / `.json` — 上一轮 4 路 + 预算裁剪（9/9）
- `vision-item009-…-r1.log`、`…-r2.log`、`…-r3.log`、`…r3/…r4 selftest log+json` — 返工失败留痕（编译/selftest 失败与断言错误）
- `vision-item009-…-r7.log`、`…r7.app_core-evidence_item009-ai-runner-…log` — 替代输入链路回归（`railhmi-ai-runner` 4/4 PASS，停止后 AI 不可用）
