# 视觉推理 ITEM-009 实现 — 真实检测器接入推理工作线程与调度

- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-009`
- 版本: v1（2026-10-20）
- 上游: `docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0 §6（IF-04/IF-05：真实路径必须关闭合成 tick、发布结果、按职责检查模型能力；latency=完成−接收，含排队）、`docs/vision/VISION_DESIGN.md` §2.3/§6.4、`docs/vision/VISION_TEST_DESIGN.md` G9（TC-T-DISP-01..06）、ITEM-001（能力核验）/002（租约预处理）/003/004（后处理）/005（两阶段）/006（envelope）/008（跟踪）
- 范围: 只实现 ITEM-009（真实检测器 + 工作线程 + 派发队列 + 预算/温度调度）。其它职责可用性语义（ITEM-010）、测距（ITEM-011）、P5 调试视图（ITEM-012）、无相机自检归档（ITEM-013）不在本增量内。

## 1. 交付内容

| 交付 | 路径 | 说明 |
|---|---|---|
| 工作线程调度与温度语义（纯逻辑） | `app/src/core/include/railhmi/core/vision_worker_schedule.h`、`src/vision_worker_schedule.cpp` | 毫摄氏度解析/多 zone 取最大/实测·注入·不可用三态区分；`AiLaneScheduler` 复用 `plan_inference` 的优先级+预算+温度守卫，并在其上补**无饥饿**轮转；每轮决策（run/skipped/idle/budget/thermal）可归档 JSON。仅标准库。 |
| 温度采样（target） | `app/src/media/include/railhmi/media/thermal_sensor.h`、`src/thermal_sensor.cpp` | 只读 sysfs `/sys/class/thermal/thermal_zone*/temp`（最多 16 个候选，有限次打开）；失败给稳定 reason，**不伪造温度**。 |
| 最小 RKNN 会话 | `app/src/media/include/railhmi/media/rknn_session.h`、`src/rknn_session.cpp` | dlopen `librknnrt.so`：`rknn_init`/`rknn_query`/`rknn_inputs_set`/`rknn_run`/`rknn_outputs_get(want_float=1)`/`rknn_outputs_release`/`rknn_destroy`；有官方头时 `static_assert` 校验自声明 ABI 尺寸/偏移。 |
| 真实检测器 | `app/src/media/include/railhmi/media/vision_rknn_detector.h`、`src/vision_rknn_detector.cpp` | `VisionDetector` 实现：`FrameLease`→ITEM-002 预处理→`rknn_run`→ITEM-003/004 解码→ITEM-005 同帧两阶段→ITEM-006 envelope；模型名/版本来自配置，按 ITEM-001 结构核验后才 ready。 |
| 生产推理工作线程 | `app/src/media/include/railhmi/media/vision_ai_worker.h`、`src/vision_ai_worker.cpp` | 消费 `AiFrameRouter`（ai_dispatcher 的带身份有界丢旧队列）；`plan_inference`+无饥饿调度；**infer 在锁外**；每路轻量跟踪分配 track_id；envelope 入发布队列；语义适配→`RuleEngine`；温度与决策入遥测；`request_stop()` 立即不可用。 |
| 关闭自造帧 tick | `app/src/media/include/railhmi/media/ai_worker.h`、`src/ai_worker.cpp` | `AiWorkerConfig::synthetic_tick` 默认 **false**（生产模式不再自造帧）；历史替代输入链路（`railhmi-ai-runner`）显式开启并在输出标注 `synthetic_tick=on`。 |
| 探针 | `app/src/media/tools/vision_pipeline_probe.cpp`（`railhmi-vision-pipeline`） | `--selftest`（22 判例：无造帧/非阻塞/丢旧/预算/温度/不可用语义/**模型缺失与非法模型的异常降级**）与 `--mode chain`（test/file 源→派发→RKNN→envelope→规则）；`emit` 逐行 flush，崩溃/异常路径也保留已产生证据。 |
| 单元测试 | `app/tests/core/test_vision_worker_schedule.cpp` | 温度解析/多 zone/注入标记/年龄 + 调度器 54 判例（预算、优先级、无饥饿、守卫、idle、边界、确定性、JSON）。 |
| 板端入口 | `app/tools/board_vision_pipeline.py` | 一次有界会话：上传→CoreTests→core-tests→MediaCheck→selftest→chain（单路）→chain4（4 路 + 预算裁剪）→取回证据→清理。 |

构建接入：`app/src/core/CMakeLists.txt`、`app/src/media/CMakeLists.txt`（含 `${CMAKE_DL_LIBS}`）、`app/tests/core/CMakeLists.txt`、`app/tests/core/main.cpp`。

## 2. 链路与数据流

```
CameraStream(file/test/rtsp)                 AiFrameRouter（ai_dispatcher）
  └─ FrameStore.publish → FrameLease  ─────► on_frame(lease): 身份键
                                             (session_id,camera_id,generation,seq)
                                             有界丢旧队列（depth=app.ai.queue_depth）
                                                    │  pop()（每轮，预算内）
                                                    ▼
                                       VisionAiWorker::step()   [线程]
                                         1) ThermalSensor.read()（锁外）
                                         2) AiLaneScheduler.plan() → run/skipped
                                         3) VisionDetector::infer(frame)（**锁外**）
                                              FramePreprocessor → rknn_inputs_set → rknn_run
                                              → outputs(want_float) → decode → 两阶段
                                         4) LightweightTracker.update() → track_id（>=1 或 -1）
                                         5) EnvelopePublisher.submit() → 有界结果队列（丢旧+计数）
                                         6) SemanticAdapter.adapt_and_dispatch() → RuleEngine
                                         7) 短锁更新遥测（fps/latency/reason 分布）
```

- 像素**只**来自 `FrameLease`（`FramePreprocessor::prepare`）；队列只搬运租约与身份元数据，不搬裸地址。
- 结果发布为 `InferenceEnvelope`：身份、`capture_mono_us/result_mono_us`、`latency_ms`（完成−接收，**含排队**）、`availability+reason`、`model_version`、带 `track_id` 的 boxes、rail 掩码零拷贝引用。
- 不可用（模型未就绪/预处理失败/推理失败/解码失败/分割失败或掩码为空）→ `availability=unavailable` + reason + **空框**；与 `ready`+空框在字段上可区分，且不产生任何报警。

## 3. 线程与锁纪律（TC-T-DISP-03）

| 路径 | 锁 | 说明 |
|---|---|---|
| `submit(lease)` | `AiFrameRouter` 每路短锁 | 只入队/丢旧并计数；**不触碰** worker 状态锁，永不因 infer 阻塞 |
| `VisionAiWorker::step()` 推理 | **无锁** | `detector_->infer()`、跟踪、发布、语义/规则全在 `mu_` 之外执行 |
| 遥测写入 | `mu_` 短临界区 | 每路计数/last_* /envelope 统计拷贝 |
| `snapshot()` | `mu_` 短临界区 | 仅拷贝遥测；不等待推理 |
| `request_stop()` | 无锁（原子） | 立即 `running=false`、`worker_alive=false` → 快照即刻 `AI 不可用` |
| `stop()` | 无锁 + join | 最多等待**当前一次**推理返回（实测 200ms 注入推理下 join=28–32ms，因 infer 已在返回途中） |

板端实测（`--selftest`，注入单次推理 200ms、237 次提交）：`submit` max=1.0ms、`snapshot` p95=0ms/max=1.0ms、`request_stop`=0ms、`join`=28ms。

## 4. 调度：预算与温度守卫

- **预算来源是 `core::plan_inference`**（不重写预算数学）：按 `ai_priority`（front 1 > wheel_rail 2 > pantograph 3 > cab_door 4）排序，`npu_budget` 截断；`temp_c > temp_limit_c` 时 `temperature_guard=true` 且预算减半（`max(1, budget/2)`，降载不停）。
- **无饥饿轮转**（本增量新增）：在预算内按 `(等待轮数降序, 优先级升序, 注册顺序)` 选路。因为 `wait = round - last_run_round`，同一路运行后等待清零，低优先级路在后续轮次被优先补上；`npu_budget < 启用路数` 时不再长期饿死低优先级职责。`max_wait_rounds()`/`waits()` 可观察。
- **温度**：每轮从 sysfs 读实测样本（7 个 zone，取最大值）。样本 `valid=false` 时 `thermal_known=false`，**不宣称温度正常、也不声称守卫生效**；注入样本经 `core::injected_thermal_sample` 显式标记 `injected=true`，JSON 中 `measured=false`，不与实测混淆。
- **`npu_budget` 语义**：是**调度路数预算**，不是 NPU 核数、也不是已验证算力容量（沿用设计 §6.4 口径）。

## 5. 板端实测（本轮真实执行范围）

环境：RK3588 `192.168.0.103`（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`），librknnrt `2.3.2 (429f97ae6b@2025-04-09T09:09:27)`，驱动 `0.9.8`，g++ 11.4.0。

| 项 | 实测 | 说明 |
|---|---|---|
| 模型能力核验 | 两个模型均 `READY`（yolov5s：1 in/3 out、INT8 NHWC 640×640 zp=-128 scale=0.00392157；yolov8s-seg：1 in/13 out 含 proto 32×160×160） | `rknn_query` 逐项核验，非按文件名 |
| 单路真实链路（test 源 1280×720@25、interval=5、12s） | submitted=301 → consumed=60 → published=60；`unavailable=10`（reason=TWO_STAGE_UNAVAILABLE/`rail mask empty`） | 分割在某些帧无 rail 掩码 → 按 ITEM-005 判不可用（**不是“轨道内安全”**） |
| 单帧推理耗时（det+seg 同一帧） | `last_infer_us≈140ms`（含预处理+两模型 `rknn_run`+后处理） | 测试图案，非现场精度/性能结论 |
| 链路 FPS | 5.0/s/路 | **由 `inference_interval=5` 降频决定**（25/5），不是模型上限 |
| 4 路 + `npu_budget=1`（10s） | 4 路各 consumed≈50（50/50/50/49），published=199，`budget_effective=1` 每轮 | 预算生效且**无饥饿**（各路基线下降频帧均被处理） |
| 不可用语义 | pantograph/wheel_rail/cab_door：每帧 `unavailable` + `PROFILE_UNSUPPORTED`（`profile_accepts_kind` 证据地图） | 不因配置登记了模型名而宣称具备能力 |
| 温度 | 实测 41.6–43.5°C（7 zone 取最大，zone0–3 轮动）；guard=false | 守卫判定与实测一致（`measured > limit` ⇔ `temperature_guard`） |
| 停止后 | `request_stop` 0ms，`ai_status_text`=`AI 不可用`，视频侧不受影响 | TC-T-DISP-02 |

## 6. 返工记录（保留失败证据）

| 轮次 | 现象 | 根因 | 修复 | 复核 |
|---|---|---|---|---|
| r1 | MediaCheck 编译失败：`vision_ai_worker.h:48` 无类型、`thermal_` 重复声明、`TransportStats` 赋值类型不符 | (a) 缺 `vision_seg_postprocess.h`（`SegResult` 未声明）；(b) 成员 `thermal_`（传感器）与 `thermal_`（样本）同名；(c) `lane->sem` 误赋 `SemanticResult` 而非 `.stats` | 补 include；样本成员改名 `thermal_sample`；改 `sem.stats`；顺手修正 `run_model` 声明与 `InputTensorSpec::zero_point` 字段名 | r2 编译通过 |
| r2 | MediaCheck 编译失败：`core::steady_now_us` 未找到 | 该函数是 `core::TimeBase::steady_now_us()` 静态成员 | 两处改为 `core::TimeBase::steady_now_us()` | r3 编译通过 |
| r3 | selftest 3 项失败：`bounded_drop_oldest` depth=0/dropped=0；`rule_engine_receives_detections` events=0 | (a) 该用例 `inference_interval=5`，`frame_due(seq=1,5)=false` → 帧被降频丢弃，队列本就为空（非丢旧）；(b) 替代检测器框 `in_roi=false`，适配器候选要求 in_roi → 不下发 | 用例改 `interval=1` 并按真实帧推进；替代检测器置 `in_roi_flags=1` 以覆盖规则链路（标注非能力声明）；同时把分析路由的规则引擎改为共享实例 | r4 上述两项通过 |
| r4 | `drop_oldest_keeps_newest :: seq=5 last_pushed=6` | 断言写错：depth=2 丢旧后**保留最新两帧**，首次 pop 得到倒数第二新（5），第二次才是最新（6） | 改为连续 pop 两次比对 `pushed[n-2], pushed[n-1]` | r5 通过 |
| r5 | 全绿（selftest 19/19，chain PASS，chain4 PASS） | — | — | r6 终轮独立复跑 |
| r8 | 验证轮 D-01：`--mode chain` 模型缺失时 **SIGSEGV（exit 139）**，stdout 0 字节 | `load_kind` 五条失败路径同构：`models_[kind] = std::move(m)` 后仍 `*err = m->reason`（空 `unique_ptr` 解引用）；`run_chain` 恒传 `&load_err`，模型缺失必崩 | (a) 新增局部 `fail_with(model, reason, detail)`：先写 reason/detail 再**拷贝** reason，最后移交所有权，5 处统一收口；(b) 探针 `emit` 加 `fflush`，异常路径保留证据；(c) 自检新增 3 判例（`model_missing_graceful` / `model_missing_reason_visible` / `invalid_model_graceful`）并新增板端负例步骤 `neg-missing-models` 直接复现原命令 | r8：同一命令 exit **1**（非 139），`MODEL_MISSING` 在 `type=models` JSON 可见，降级后 40 帧仍以 unavailable 发布；selftest 22/22 |

## 7. 观察与移交

1. **`SimDetector` 保留但退出生产路径**：`SimDetector` 与旧 `AiWorker` 仍存在，因为视频 ITEM-017 的 `railhmi-ai-runner` 归档证据需要可复跑；生产装配改用 `VisionAiWorker`+`RknnVisionDetector`，且 `AiWorkerConfig::synthetic_tick` 默认 **false**（不再自造帧）。删除旧类会破坏既有证据可复现性，故以“默认关闭 + 文档声明”处理；本轮 r7 已回归验证替代链路在显式开启后仍 4/4 路通过。
2. **`MaskRef.box_index = -1`**：front envelope 的 boxes 是 person 框，masks 是 rail 掩码（`kind="rail"`），两者不是一一对应；本增量显式置 -1 并在 P5 叠加（ITEM-012）时按 `kind` 处理。**不改** ITEM-006 的字段语义。
3. **掩码生命周期**：`VisionInferOutcome::seg`（`shared_ptr<const SegResult>`）持有 plane；工作线程在同一 `step()` 内提交并立即 drain 发布队列，因此 envelope 不跨越 SegResult 生命周期。若后续消费者需长期持有 envelope，必须同时持有该 `seg` 引用（已在头注释声明）。
4. **发布计数单生产者**：`EnvelopePublisher` 仅由工作线程访问（`submit` 入队走 router，不触 publisher）；跨线程读统计走 `snapshot()` 拷贝。若未来多工作线程共享一个 publisher，需要加锁——当前结构上不共享。
5. **FPS 语义**：报告的 5.0/s 是**降频后的实际处理率**（`inference_interval=5`），不是模型/整机上限；单帧 det+seg ≈140ms 是测试图案下的实测，不能外推为六路并发或现场帧率。
6. **4 路不可用路仍消耗调度配额**：pantograph/wheel_rail/cab_door 每轮被选中后立刻返回 `PROFILE_UNSUPPORTED`（~1–2µs），不占用 NPU，但会占用一次“路数预算”。ITEM-010 落地职责语义时应在调度输入处直接排除未就绪职责，避免无谓配额消耗（本增量保持与 `profile_accepts_kind` 一致，未擅自改调度语义）。
7. **未执行/不外推**：未连接任何真实相机（ENV-D）；未测真实画面检出率/误报/掩码质量；未测 4 路全模型并发（当前仅 front 有模型证据）；未做热稳态长跑；未设置 NPU core mask；`npu_budget` 不作算力容量解释。以上均为待执行项，不在本增量声称通过。
8. **同帧两次预处理**：front 的 det 与 seg 各持一个 `FramePreprocessor`，同一帧做了两次 letterbox/量化（实测 det+seg 合计 ≈144ms）。可优化为一次预处理供两模型共用（纯性能优化，不影响正确性/契约），留待性能轮评估。
9. **未做模型热重载**：模型在构造时加载一次；配置变更需重启进程（`load()` 幂等，不做运行中替换）。
10. **异常路径语义（D-01 修复后）**：模型缺失/非法 -> `load()` 返回 false、`err` 汇总每类模型 reason、`status_json()` 逐模型 `present/ready/reason/detail`，profile 转 `MODEL_MISSING`/`MODEL_LOAD_FAILED`；探针 `chain` 继续以 unavailable 发布空框 envelope，并把 `chain_models_ready` 判 FAIL（exit 1）。**不得**用崩溃/静默无输出代替显式 reason。
11. **失败路径的代码约束**：`load_kind` 内所有失败分支统一走 `fail_with`（先拷贝 reason 再 move）；新增失败分支时不要先 `models_[kind]=std::move(m)` 再解引用 `m`。
