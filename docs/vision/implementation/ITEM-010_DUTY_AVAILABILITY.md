# 视觉推理 ITEM-010 实现 — 其它职责可用性语义（pantograph / wheel_rail / cab_door）

- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-010`
- 版本: v1（2026-10-21）
- 上游: `docs/vision/VISION_DESIGN.md` §1（职责判定表：三职责无专用模型证据 → unavailable）、§0.3-5/6（unknown 不当 normal；配置登记不构成能力）、§2.3/§7、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0 §6、`docs/vision/VISION_TEST_DESIGN.md` G10（TC-T-MIS-01..04）、ITEM-001（能力核验）、ITEM-009（工作线程/调度，交接项：不可用路不应占用路数预算）
- 范围: 只实现 ITEM-010。未实现测距（ITEM-011）、P5 调试视图（ITEM-012）、无相机自检归档（ITEM-013）；未训练/未替换任何生产模型。

## 1. 交付内容

| 交付 | 路径 | 说明 |
|---|---|---|
| 职责可用性语义（纯逻辑） | `app/src/core/include/railhmi/core/vision_duty_capability.h`、`src/vision_duty_capability.cpp` | `DutyKind`（pantograph/wheel_rail/cab_door）、`DutyState`（unknown/not_raised/normal/abnormal/closed/ajar）、按职责的状态词表校验、`DutyDeclaration`（能力声明：输入形状+输出个数+标签→状态映射）、`evaluate_duty`（声明+描述逐项核验）、`map_duty_prediction`（标签→状态，unknown 为缺省）、`DutyAdapter`（每职责入口，输出语义+统计）、JSON 可观察面。 |
| 调度集成（ITEM-009 交接项） | `core/vision_worker_schedule.{h,cpp}` | `AiLaneDemand::duty_ready`：职责未就绪的路**不占路数预算**，单独进入 `AiScheduleRound::unavailable`；不参与等待/饥饿统计，`skipped` 不受污染。 |
| 真实检测器职责入口 | `app/src/media/{include,src}/vision_rknn_detector.{h,cpp}` | `RknnDetectorConfig::duty_declarations` / `duty_model_paths`（来自配置/CLI，**不写死模型名**）；`load_duties()` 打开专用模型取 `rknn_query` 描述→交 core 核验；`ready()/unavailable_reason()`/`model_version()` 按职责语义返回；`infer()` 对已核验职责做**类分数解码**→声明标签→状态；`status_json()` 增 `duty_status`。 |
| 生产工作线程 | `app/src/media/src/vision_ai_worker.cpp` | 需求快照带 `duty_ready`；预算内路正常推理，`round.unavailable` 路在预算外消费一帧（无 NPU 计算）以保持 `unavailable+reason+空框` 语义与队列有界。 |
| core 探针 | `app/src/core/tools/duty_probe.cpp`（`railhmi-vision-duty`） | `--selftest`（17 判例）、`--fixture no_model|config_only|missing|mismatch|ready`、`--config DIR`（真实配置三职责口径）、`--descriptor FILE --duty ... --labels ... --predictions ...`（真实 `rknn_query` 描述核验）、`--json`。 |
| 单元测试 | `app/tests/core/test_vision_duty_capability.cpp` + 调度测试新增 | 词表/转换/声明完整性/求值分支/标签映射/适配器/JSON/项目真实登记口径；调度侧 `duty_ready` 与 `unavailable` 行为。 |
| 板端入口 | `app/tools/board_vision_duty.py` | 上传→CoreTests→core-tests→MediaCheck→duty selftest→config→5 fixtures→真实描述 drill→media duty 两种情形→4 路预算裁剪链路→取回→清理。 |

构建接入：`app/src/core/CMakeLists.txt`（库源 + `railhmi-vision-duty` 探针）、`app/tests/core/CMakeLists.txt`、`app/tests/core/main.cpp`。

## 2. 语义模型（不变量）

```
DutyState: Unknown | NotRaised | Normal | Abnormal | Closed | Ajar
  pantograph 允许：Unknown/NotRaised/Normal/Abnormal
  wheel_rail 允许：Unknown/Normal/Abnormal
  cab_door   允许：Unknown/Closed/Ajar
```

1. **缺省是 unknown**：`DutyAvailability.state` 初值 `Unknown`；`evaluate_duty` 的 READY 分支也显式保持 unknown。
2. **unknown 永不映射 normal**：`map_duty_prediction` 只在“已声明标签 + 分数达阈值”时改状态；未声明标签、低分、空输入都返回 unknown；不存在 `default -> normal` 的路径。
3. **登记不是能力**：配置里 `pantograph/cab_door → yolov5s`、`wheel_rail → yolov8s-seg` 只作为 `registered_model_name` 记录；无声明时固定 `PROFILE_UNSUPPORTED`，detail 明写 `registration is not evidence`。
4. **不可用不产判定**：`DutyAdapter::apply()` 在不可用时拒绝推理结果（`rejected_unavailable++`）并保持 unknown；`to_detection()` 在 unavailable/unknown 时返回 false —— 因此不存在“unknown 触发正常/异常报警”的路径。
5. **状态词表按职责封闭**：标签映射到越界状态（如受电弓声明里出现 `ajar`）即 `MODEL_MISMATCH`；`to_pantograph_state`/`to_door_state` 只做允许范围内的转换。

## 3. 可用性求值（`evaluate_duty`）

| 顺序 | 条件 | 结果 |
|---|---|---|
| 1 | 无能力声明（无论配置是否登记模型名） | `available=false`，`reason=PROFILE_UNSUPPORTED`，state=unknown |
| 2 | 声明不完整/标签映射非法（空名、输入形状缺失、标签→unknown、跨职责状态、输出数≠标签数） | `MODEL_MISMATCH` |
| 3 | 有声明但无描述 | `MODEL_MISSING` |
| 4 | 描述 `load_status != ok` | 取该状态（`MODEL_MISSING`/`MODEL_LOAD_FAILED`/`RKNN_RUNTIME_UNAVAILABLE`） |
| 5 | 输入个数/通道/宽高/量化不符 | `MODEL_MISMATCH`（逐项写 `mismatched`） |
| 6 | 输出个数不符，或输出非“每输出 1 元素”的类分数布局（无解码器） | `MODEL_MISMATCH` |
| 7 | 全部通过 | `available=true`，`reason=READY`，**state 仍为 unknown** |

- 描述来源不做假设：可来自板端 `rknn_query`（真实模型）或夹具 `ModelDescriptor`（`railhmi-vision-duty` 构造）。
- 支持的职责解码布局：**N 个输出、每个 1 个元素（类分数）**，标签表与输出顺序一一对应；其它布局（如 yolov5s 的 255×80×80）显式 `unsupported_decode_layout`，宁可不可用也不猜。

## 4. 调度与线程集成

- 需求快照对每路填 `duty_ready = detector->ready(profile)`。
- `AiLaneScheduler::plan`：`duty_ready=false` 且有帧的路进入 `unavailable`，**不消耗** `budget_effective`，不进入 `skipped`，不参与 `wait_rounds`/`max_wait_rounds`（否则会造成“不可用路把预算吃光、就绪路被跳过”的伪饥饿）。
- `VisionAiWorker::step()`：先按预算推理 `round.run`，再对 `round.unavailable` 各路消费一帧（`~1–2µs`，不触发 NPU），保持“不可用也要有显式 reason 与空框”的语义和队列有界。
- 锁纪律不变：推理仍在 `mu_` 之外；`submit`/`snapshot`/`request_stop` 行为与 ITEM-009 相同。

## 5. 职责状态发布通道

- `InferenceEnvelope` 是**检测框**语义载体：职责状态不是框，故职责 lane 的成功推理返回 `availability=ready` + **空 boxes**（`reason=READY`），状态本身经 `RknnVisionDetector::duties()` / `status_json().duty_status`（P5 数据源）发布。
- 每条职责记录：`available/reason/detail/state/state_display/semantics_unknown/config_registered/registered_model_name/declared_model_name/evidence_source/matched/mismatched` + 适配器统计。
- P5 渲染（ITEM-012）直接消费上述字段；`unknown` 一律显示“未知”，未被格式化为“正常/0”。

## 6. 板端实测（r4 终轮，RK3588 192.168.0.103）

| 项 | 实测 |
|---|---|
| core 单测 | `tests=1300 failures=0`（ITEM-009 的 1215 + 本增量 85） |
| `railhmi-vision-duty --selftest` | 17/17 PASS（无模型/登记非能力/缺描述/真实形态不匹配/READY 仍 unknown/标签映射/拒绝不可用/JSON） |
| `--config ./config`（真实配置） | 三职责 `available=false reason=PROFILE_UNSUPPORTED state=unknown`，detail 均为“registration is not evidence”（`unavailable=3 unknown=3`） |
| 5 个 fixture（板端） | 全部 `RESULT: PASS`（no_model / config_only / missing / mismatch / ready） |
| 真实描述 drill（`rknn_query`） | 用 `yolov8s-seg`（13 输出）声明 3 类受电弓 → `MODEL_MISMATCH`（`output_count13!=3`，detail 记录 `detected=yolov8_seg`）；用 `yolov5s`（255×H×W）声明 2 类车门 → `MODEL_MISMATCH` |
| media `--mode duty`（无声明） | 三职责 `PROFILE_UNSUPPORTED` + unknown + 空框，`RESULT: PASS` |
| media `--mode duty`（声明+真实模型） | pantograph 声明+yolov5s → `MODEL_MISMATCH`（`unsupported_decode_layout`）；cab_door 声明+seg → `MODEL_MISMATCH`（输出数不符）；wheel_rail 无声明 → `PROFILE_UNSUPPORTED`；三者均 unknown |
| 4 路链路（`--budget 1`，6s） | `run=['CAM-01']`、`unavailable=['CAM-04','CAM-05','CAM-06']`、`skipped=[]`；front 30 次真实推理（5 fps×6s），3 路各 30 次“不可用”消费；9/9 判据 PASS |

环境：librknnrt `2.3.2 (429f97ae6b@2025-04-09T09:09:27)`、驱动 `0.9.8`、g++ 11.4.0、7 个 thermal zone。

## 7. 返工记录（保留失败证据）

| 轮次 | 现象 | 根因 | 修复 | 复核 |
|---|---|---|---|---|
| r1 | 仅构建+core-tests（中间轮，无产品缺陷） | — | — | 留痕后删除中间产物 |
| r2 | CoreTests 编译失败：`unable to find string literal operator 'operator""unavailable'` | 新增调度测试中 JSON 断言字符串引号未转义（`find(""unavailable":[...")`） | 改为 `find("\"unavailable\":[\"CAM-05\"")` | r3 编译通过 |
| r3 | probe selftest 1 项失败（`mismatch_reports_output_count`）；media duty 情形 cab_door 报 `MODEL_MISSING` 而非 `MODEL_MISMATCH` | (a) yolov5s 与 3 类声明输出个数都是 3，实际触发的是“布局无解码器”，断言写错；(b) `load_duties` 里 `duty_model_paths` 匹配写成 `break`（首个非本职责条目即退出），第二个职责永远拿不到自己的模型路径 | (a) 断言改为 `unsupported_decode_layout` 并补一条真正的“输出个数不符”用例；(b) `break` → `continue` | r4：selftest 17/17、cab_door 正确 `MODEL_MISMATCH` |

## 8. 观察与移交

1. **没有真实职责模型**：项目仍未提供受电弓/轮轨/车门专用模型，因此 `ready` 路径只用夹具描述证明（`--fixture ready` + 单测）。真实模型的 `ready` 需要领域侧提供模型与标签表；本增量不训练、不替换生产模型。
2. **P5 渲染属 ITEM-012**：本增量只提供字段与 JSON（`duty_status`），不做界面叠加/文案布局；`unknown` 的“未知”文案已由 `duty_state_display_zh` 提供，避免下游把 unknown 显示成“正常”。
3. **职责报警未接通规则引擎**：`DutyAdapter::to_detection()` 已能把状态映射为 `Detection`（`pantograph`/`door`/`wheel_abnormal`，`track_id=-1`、`in_roi=true`），但工作线程当前不向 `RuleEngine` 下发职责 Detection（无真实模型时无候选，避免噪声）。接通应在 ITEM-012/规则侧确认后启用；`RuleEngine` 现有逻辑（仅 Abnormal/Ajar/wheel_abnormal 触发）不需要修改。
4. **解码布局限制**：只支持“每输出 1 元素的类分数”布局；检测/分割式职责模型（如把 yolov5 头当职责模型）会显式 `MODEL_MISMATCH`，不猜后处理。
5. **不可用路仍消费帧**：预算外消费是刻意的（保持 P5 有 reason、队列有界），代价是每帧一次 `~1–2µs` 的空转；若将来 6 路全部不可用，仍不会占用 NPU。
6. **未执行/不外推**：未连接真实相机（ENV-D）；未测职责模型的精度/召回（无模型）；未做长跑/热稳定；`chain` 链路仍用 test 源。
