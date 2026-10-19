# ITEM-010 证据 — 其它职责可用性语义（pantograph / wheel_rail / cab_door）

- Capability: `computer_vision_engineer.vision_inference_implementation`
- 工作项: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-010`
- 轮次标记: `20261021-item010-r4`（终轮）；r2/r3 为返工留痕
- 日期: 2026-10-21
- 实现文档: `docs/vision/implementation/ITEM-010_DUTY_AVAILABILITY.md`
- 环境: RK3588 `192.168.0.103`（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`），Linux 6.1.141 aarch64、g++ 11.4.0、librknnrt `2.3.2 (429f97ae6b@2025-04-09T09:09:27)` + 驱动 `0.9.8`
- 执行方式: 登记 Paramiko SSH 入口 `app/tools/board_vision_duty.py`（凭据仅经 `TRAIN_RK3588_SSH_CREDENTIAL`；受信任 known_hosts + RejectPolicy；全部有限超时；只写 `/tmp`；结束清理）

## 1. 本轮执行步骤（一次有界会话）

| 步骤 | 命令 | 退出 | 结果 |
|---|---|---|---|
| 上传 | app/ 325 文件 + `yolov5s.rknn`、`yolov8s-seg.rknn` | 0 | 仅 `/tmp/railhmi_vision_item010_20261021-item010-r4` |
| 构建 CoreTests / MediaCheck | `bash build.sh CoreTests` / `MediaCheck` | 0 | 通过 |
| 单元测试 | `railhmi-core-tests --junit ...` | 0 | `tests=1300 failures=0`（ITEM-009 的 1215 + 本增量 85） |
| 职责自检 | `railhmi-vision-duty --selftest` | 0 | 17/17 PASS |
| 真实配置口径 | `railhmi-vision-duty --config ./config` | 0 | 三职责 unavailable+unknown（`unavailable=3 unknown=3`） |
| 5 个 fixture | `--fixture no_model/config_only/missing/mismatch/ready` | 0×5 | 全部 `RESULT: PASS` |
| 真实描述 drill | `railhmi-rknn-probe` → `railhmi-vision-duty --descriptor ... --expect unavailable` | 0×4 | 两个真实描述均 `MODEL_MISMATCH`（详见 §4） |
| media duty（无声明） | `railhmi-vision-pipeline --mode duty` | 0 | 三职责 `PROFILE_UNSUPPORTED`+unknown，3/3 PASS |
| media duty（声明+真实模型） | `--duty-declare ... --duty-model ...` | 0 | pantograph/cab_door `MODEL_MISMATCH`、wheel_rail `PROFILE_UNSUPPORTED`，3/3 PASS |
| 4 路链路（`--budget 1`，6s） | `railhmi-vision-pipeline --mode chain --lanes CAM-01,CAM-04,CAM-05,CAM-06 --budget 1` | 0 | 9/9 PASS；不可用路不占预算 |
| 清理 | `rm -rf /tmp/railhmi_vision_item010_20261021-item010-r4` | 0 | `cleaned` |

## 2. 无专用模型时：三职责全部不可用且状态 unknown（TC-T-MIS-01/02）

`--config ./config`（真实 `app.yaml` 里 pantograph/wheel_rail/cab_door 都登记了模型名）：

```
type=duty name=pantograph registered=yolov5s available=false reason=PROFILE_UNSUPPORTED state=unknown display=未知
type=duty name=wheel_rail registered=yolov8s-seg available=false reason=PROFILE_UNSUPPORTED state=unknown display=未知
type=duty name=cab_door   registered=yolov5s available=false reason=PROFILE_UNSUPPORTED state=unknown display=未知
[PASS] config_three_duties_unavailable_unknown :: unavailable=3 unknown=3
RESULT: PASS failures=0
```

detail 字段明确写出“登记不是能力”：

```
"detail":"config registers 'yolov5s' but no duty capability is declared/verified; registration is not evidence"
"state":"unknown","state_display":"未知","semantics_unknown":true
```

`railhmi-vision-pipeline --mode duty`（真实检测器侧，同一口径）：

```
[PASS] duty_pantograph_no_model :: reason=PROFILE_UNSUPPORTED state=unknown
[PASS] duty_wheel_rail_no_model  :: reason=PROFILE_UNSUPPORTED state=unknown
[PASS] duty_cab_door_no_model    :: reason=PROFILE_UNSUPPORTED state=unknown
RESULT: PASS failures=0
```

## 3. 提供声明 + 核验模型后才 ready（TC-T-MIS-03），且 READY 不等于 normal

`--fixture ready`（夹具描述：1 输入 640×640 int8 + 3 个单元素类分数输出；声明 3 标签）：

```
[PASS] declared_verified_ready_but_unknown :: READY != normal
RESULT: PASS failures=0
```

`--selftest` 里的映射判据（板端实测）：

```
[PASS] undetermined_prediction_stays_unknown :: 未声明/低分不产生 normal
[PASS] declared_label_sets_normal            :: normal 只能来自已声明标签
[PASS] declared_door_label_sets_ajar         :: ajar
[PASS] unavailable_rejects_prediction        :: unknown 保持、不产判定
[PASS] unavailable_no_detection              :: 无判定输出
[PASS] json_unknown_not_normal               :: "state_display":"未知"（不出现“正常”）
```

`--selftest` 共 17/17 PASS，覆盖：三职责无模型、登记非能力（含 detail）、声明无描述→`MODEL_MISSING`、真实 yolov5s 形态→`MODEL_MISMATCH`（布局无解码器）、输出个数不符→`MODEL_MISMATCH`、READY 仍 unknown、标签映射/阈值/最高分、适配器拒绝不可用结果、JSON 三职责齐全。

## 4. 真实模型描述核验（`rknn_query`，MATCH/MISMATCH 可观察）

| 用例 | 声明 | 实际描述 | 结论 |
|---|---|---|---|
| `--descriptor yolov8s-seg.descriptor.txt --duty pantograph --outputs 3 --labels pantograph_not_raised/...` | 3 类受电弓 | 13 输出（含 proto 32×160×160） | `MODEL_MISMATCH`，`mismatched=["output_count13!=3"]`，`detail` 记录 `detected=yolov8_seg` |
| `--descriptor yolov5s.descriptor.txt --duty cab_door --outputs 2 --labels door_closed/door_ajar` | 2 类车门 | 3 输出 255×{80,40,20} | `MODEL_MISMATCH`（输出个数） |

样例（真实板端日志）：

```
type=evaluation json={"duty":"pantograph","available":false,"reason":"MODEL_MISMATCH",
 "detail":"descriptor does not match declared duty capability (detected=yolov8_seg;13 outputs with 1 proto(32x160x160))",
 "state":"unknown","semantics_unknown":true,"evidence_source":"declaration+descriptor",
 "matched":["declaration","input_count","input_c","input_hw","input_quantized"],
 "mismatched":["output_count13!=3"]}
type=descriptor api=2.3.2 (429f97ae6b@2025-04-09T09:09:27) drv=0.9.8 in=1 out=13
RESULT: PASS failures=0
```

media 侧同源（`--mode duty --duty-declare ... --duty-model ...`）：

```
type=duty name=pantograph available=false reason=MODEL_MISMATCH state=unknown display=未知 registered=yolov5s declared=yolov5s evidence=declaration+descriptor
type=duty name=wheel_rail available=false reason=PROFILE_UNSUPPORTED state=unknown display=未知 registered=yolov8s-seg declared= evidence=none
type=duty name=cab_door available=false reason=MODEL_MISMATCH state=unknown display=未知 registered=yolov5s declared=yolov8s-seg evidence=declaration+descriptor
RESULT: PASS failures=0
```

（pantograph 的 `mismatched=["unsupported_decode_layout"]`：yolov5s 输出不是“每输出 1 元素”的类分数布局，故无解码器、不置 ready。）

## 5. 不可用路不占路数预算（ITEM-009 交接项，TC-T-MIS-04 邻接）

`--mode chain --lanes CAM-01,CAM-04,CAM-05,CAM-06 --budget 1`（6s，test 源）：

```
type=lane camera=CAM-01 … ready=true  consumed=30 inferred=30 unavailable=4  (TWO_STAGE_UNAVAILABLE:4)
type=lane camera=CAM-04 … ready=false reason=PROFILE_UNSUPPORTED consumed=30 unavailable=30
type=lane camera=CAM-05 … ready=false reason=PROFILE_UNSUPPORTED consumed=30 unavailable=30
type=lane camera=CAM-06 … ready=false reason=PROFILE_UNSUPPORTED consumed=30 unavailable=30
type=summary submitted=603 consumed=120 published=120 unavailable=94 round=361 budget=1
```

调度决策 JSON（终轮）：

```
"last_round":{"run":["CAM-01"],"unavailable":["CAM-04","CAM-05","CAM-06"],"skipped":[],"idle":[],
 "budget_configured":1,"budget_effective":1,"temperature_guard":false, …}
```

- 预算 1 全部给唯一就绪的 front（30 次真实推理 = 5 fps × 6s），3 路不可用职责在预算外各消费 30 帧（`~1–2µs`/次，不触发 NPU），`skipped` 为空 —— 不再出现“不可用路吃掉预算”。
- 检测器 JSON 内含 `duty_status`（available/reason/state=unknown/evidence/stats），P5（ITEM-012）可直接消费。
- 链路其余判据（模型 ready、发布、停止后 AI 不可用、温度实测、预算/温度一致性）9/9 PASS。

## 6. 返工留痕

| 轮次 | 保留文件 | 失败 | 修正 |
|---|---|---|---|
| r1 | （中间轮，已清理） | 仅构建 + core-tests，无产品缺陷 | — |
| r2 | `…-r2.log` | CoreTests 编译失败：新增调度测试的 JSON 断言字符串引号未转义（`operator""unavailable`） | 转义为 `"\"unavailable\":[...]"` |
| r3 | `…r3…media-mismatch-….log` | (a) probe selftest `mismatch_reports_output_count` 断言写错（yolov5s 与 3 类声明输出数相同，实际是布局不匹配）；(b) media duty 情形 cab_door 报 `MODEL_MISSING` —— `load_duties` 的 `duty_model_paths` 匹配误写 `break`，第二职责拿不到自身模型路径 | (a) 断言改 `unsupported_decode_layout` 并补“输出个数不符”用例；(b) `break`→`continue`；r4 复核 cab_door 正确 `MODEL_MISMATCH` |

## 7. 验收判据对照

| 任务书要求 | 证据 |
|---|---|
| 为无专用模型证据的职责实现适配器入口，明确返回 unavailable + reason（UNSUPPORTED/MODEL_MISMATCH） | §2（`PROFILE_UNSUPPORTED`）、§4（`MODEL_MISMATCH`）；`DutyAdapter` 为每职责入口，`railhmi-vision-duty` 与 media `--mode duty` 双通道呈证 |
| 输出语义 unknown | §2/§3：`state=unknown`、`state_display=未知`、`semantics_unknown=true`；单测覆盖“空/低分/未声明标签 → unknown” |
| 绝不把 unknown 映射为 normal | §3 `[PASS] json_unknown_not_normal`、`undetermined_prediction_stays_unknown`；`to_detection()` 在 unknown 时返回 false；无 `default→normal` 代码路径 |
| 绝不因配置登记了模型名就宣称具备能力 | §2：登记 `yolov5s`/`yolov8s-seg` 仍 `PROFILE_UNSUPPORTED`，detail 明写 registration is not evidence；单测 `duty_registered_*_unknown` |
| 仅当提供并核验了声明能力专用模型后才 ready | §3 夹具 READY 路径（结构逐项 matched）+ §4 真实模型逐项 mismatch；READY 仍需标签过阈值才产生状态 |
| 三职责无模型时全部不可用且 unknown；提供匹配模型后按声明能力转为可用 | §2（全部不可用）、§3（声明+匹配描述 → ready，标签→状态）；本仓库暂无真实职责模型，故 READY 仅夹具证明（已声明） |

## 8. 覆盖边界（本轮不声称的范围）

- 未连接任何真实相机（ENV-D）；`chain` 仍用 test 源。
- 未提供任何真实职责模型（受电弓/轮轨/车门）：`ready` 路径只用夹具描述证明；未训练/未替换生产模型，未测职责模型精度/召回。
- 未做 P5 界面渲染（ITEM-012）、未做测距（ITEM-011）、未做无相机自检归档（ITEM-013）。
- 职责 `Detection` 到 `RuleEngine` 的下发通道已实现但未在工作线程启用（无真实模型时无候选，避免噪声）——见实现文档 §8-3。
- 未做长跑/热稳定/多路全职责并发；`npu_budget` 仍不作算力容量解释。

## 9. 证据清单（本目录）

**r4（终轮）**

- `vision-item010-192_168_0_103-20261021-item010-r4.transcript.json` / `.log` — 结构化/人类可读 transcript（指纹、逐命令退出码、上传/构建/测试/drill/清理）
- `…-r4.app_core-evidence_item010-core-tests-20261021-item010-r4.xml` — JUnit（`tests=1300 failures=0`）
- `…-r4.app_core-evidence_item010-duty-selftest-20261021-item010-r4.log` / `.json` — 职责自检 17/17
- `…-r4.app_core-evidence_item010-duty-config-20261021-item010-r4.log` / `.json` — 真实配置三职责 unavailable+unknown
- `…-r4.app_core-evidence_item010-fixture-{no_model,config_only,missing,mismatch,ready}-20261021-item010-r4.log` — 5 个 fixture
- `…-r4.app_core-evidence_item010-rknn-{yolov5s,yolov8s-seg}-20261021-item010-r4.json` — 板端 `rknn_query` 描述 JSON
- `…-r4.descriptors_{yolov5s,yolov8s-seg}.descriptor.txt` — 真实描述文本（drill 输入）
- `…-r4.app_core-evidence_item010-descriptor-{seg,det}-20261021-item010-r4.log` — 真实描述 vs 声明能力 → MODEL_MISMATCH
- `…-r4.app_core-evidence_item010-media-{no-model,mismatch}-20261021-item010-r4.log` / `.json` — media 侧职责语义（含 `duty_status`）
- `…-r4.app_core-evidence_item010-chain4-20261021-item010-r4.log` / `.json` — 4 路预算 1：`run=[CAM-01]`、`unavailable=[CAM-04/05/06]`、`skipped=[]`

**返工留痕**

- `…-r2.log` — CoreTests 编译失败留痕（字符串转义）
- `…r3.app_core-evidence_item010-media-mismatch-20261021-item010-r3.log` — `break` 缺陷留痕（cab_door 误报 MODEL_MISSING）
