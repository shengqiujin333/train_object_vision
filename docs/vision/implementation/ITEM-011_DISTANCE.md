# 视觉推理 ITEM-011 实现 — 距离测量（可选后置）

- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-011`
- 版本: v1（2026-10-21）
- 上游: `docs/vision/VISION_DESIGN.md` §7.6（历史标定 500 m/89 px、只有含变焦档位才换算、未标定恒 -1、规则未消费距离）、§0.3-5、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0 §6（`distance_m=-1` 表示未知）、`docs/vision/VISION_TEST_DESIGN.md` G11（TC-T-DIST-01..03）、ITEM-007/008（语义适配与 envelope）
- 范围: 只实现 ITEM-011。未做测距精度验收（需现场标定与人工核验）、未实现 P5 渲染（ITEM-012）、未实现无相机自检归档（ITEM-013）。

## 1. 交付内容

| 交付 | 路径 | 说明 |
|---|---|---|
| 距离测量（纯逻辑） | `app/src/core/include/railhmi/core/vision_distance.h`、`src/vision_distance.cpp` | `CameraCalibration`（相机 + **变焦档位** + 参考距离/参考像素高/真实身高 + 有效范围 + 来源）、`DistanceCalibrationSet`、`estimate_distance`（门控顺序固定）、`distance_from_box_height_px`（公开公式）、`parse_calibration_spec`（CLI/配置字符串）、`distance_text`/`distance_json`。仅标准库。 |
| worker 集成 | `app/src/media/{include,src}/vision_ai_worker.{h,cpp}` | `VisionWorkerConfig::calibrations` + `camera_zoom`（当前档位）；每帧对**人员框**按 `box.h × 图像高` 换算，写入 `EnvelopeBox.distance_m`；不可用时 `-1`；新增距离遥测（valid/unknown/按 reason 分布/last 文本）。 |
| core 探针 | `app/src/core/tools/distance_probe.cpp`（`railhmi-vision-distance`） | `--selftest`（14 判例）、6 fixtures、`--calib`/`--zoom`/`--box-h`/`--camera`/`--label`/`--expect` 直接复算并可归档。 |
| media 探针 | `app/src/media/tools/vision_pipeline_probe.cpp` | 新增 `--calib`/`--camera-zoom`；`--selftest` 增“真实发帧路径 + 人员框像素高 → distance_m”用例；链路日志/JSON 输出每路距离统计。 |
| 单元测试 | `app/tests/core/test_vision_distance.cpp` | 标定校验/派生量、公式、六类门控、范围不 clamp、集合查询、字符串解析、规则不消费距离、P5 未知占位（47 判例）。 |
| 板端入口 | `app/tools/board_vision_distance.py` | 上传→CoreTests→core-tests→MediaCheck→distance selftest→6 fixtures→四种换算形态→media selftest→带标定链路→取回→清理。 |

构建接入：`app/src/core/CMakeLists.txt`（库源 + 探针）、`app/tests/core/CMakeLists.txt`、`app/tests/core/main.cpp`。

## 2. 换算模型（公开、可逐项核对）

```
distance_m = reference_distance_m × reference_height_px / box_height_px
px_per_m(参考档位) = reference_height_px / reference_object_height_m
```

- 依据设计 §7.6 的历史锚点：CAM-01 在 **500 m** 处站立人框高 **≈89 px**，站立身高按 **1.70 m**
  记录 → `px_per_m ≈ 52.35`（与“52 px/m”一致），换算常数 `500×89 = 44500 px·m`。
- 101 语义：`box_height_px = 归一化框高 h × 图像高(像素)`（图像高取自 `FrameDescriptor.layout.height`，
  与像素来源同一租约，不用元数据臆造）。
- **只对 `label=="person"` 换算**：其它标签没有身高先验，不做换算（`NOT_PERSON`）。
- 不做透视/俯仰/地面几何修正、不做分块标定：本轮只承诺“按标定线性换算”，精度须现场核验。

## 3. 门控顺序与 reason（任一不满足 → `distance_m = -1`，不 clamp、不猜）

| 顺序 | 条件 | reason |
|---|---|---|
| 1 | 标签不是 `person` | `NOT_PERSON` |
| 2 | 该相机没有任何标定 | `NO_CALIBRATION` |
| 3 | 当前变焦档位未知/为空（标定存在也不换算） | `ZOOM_UNKNOWN` |
| 4 | 当前档位与标定档位不一致 | `ZOOM_MISMATCH` |
| 5 | 标定参数自相矛盾（非正/范围倒置） | `BAD_CALIBRATION` |
| 6 | 框像素高 ≤ 0 / 非有限 | `INVALID_BOX` |
| 7 | 结果超出标定有效范围 `[min,max]`（0 表示不限） | `OUT_OF_RANGE` |
| 8 | 全部通过 | `DISTANCE_OK` + `metres` |

- 有效范围用“返回未知”而不是截断到边界，避免把超范围目标显示成边界值（也是“不臆造米值”的一部分）。
- 调试/证据文案：`distance_text()` 有效 → `"500.0 m"`；**无效 → `"—"`**（未知占位，绝不出现 `"0"`/`"0.0 m"`）。

## 4. 应用链路

```
FrameLease(图像高) ─┐
VisionDetector ─────┴─► VisionAiWorker::step()
                          1) infer → envelope.boxes
                          2) 距离：仅 person 框，按标定+当前档位换算 → box.distance_m（否则 -1）
                          3) 跟踪 → track_id
                          4) EnvelopePublisher（distance_m 随 envelope 发布）
                          5) SemanticAdapter → core::Detection.distance_m（原样透传）
                          6) RuleEngine（**不消费** distance_m，见 §5）
                          7) P5：build_ai_debug 取最近有效距离 → "123.4 m"；全未知 → "—"
```

## 5. 规则与 P5 语义（不改既有契约）

- `RuleEngine` 的候选判定（Front/Pantograph/WheelRail/CabDoor）与 `danger_proximity_m` **当前都不读** `distance_m`；
  单元测试用同一 Detection（仅 distance_m 不同）验证事件类型/级别/数量完全一致，事件里的 `distance_m` 保持 -1。
  本轮**不**声称“已实施 5 m 门槛”。
- P5 `build_ai_debug.distance_text`：无有效距离 → `"—"`；有 → `"%.1f m"`（沿用既有实现，本轮加测试锁定“未知非 0”）。
- 语义适配器把 `EnvelopeBox.distance_m` 原样映射到 `Detection.distance_m`（ITEM-007 既有行为，无需改动）。

## 6. 板端实测（r3 终轮，RK3588 192.168.0.103）

| 项 | 实测 |
|---|---|
| core 单测 | `tests=1347 failures=0`（ITEM-010 的 1300 + 本增量 47） |
| `railhmi-vision-distance --selftest` | 14/14 PASS（锚点 89px=500m、半像素双距离、px/m=52.35、六类门控、JSON、未知占位） |
| 6 fixtures（板端） | 全部 `RESULT: PASS`（no_calib/zoom_unknown/zoom_mismatch/non_person/out_of_range/valid） |
| 形态核对（真实可复算） | `89px → 500.0 m`、`178px → 250.0 m`（线性）；`--no-zoom → -1 / ZOOM_UNKNOWN`；`--camera CAM-09（未标定）→ -1 / NO_CALIBRATION`；三者 debug 文本均为 `—` |
| media `--selftest`（真实发帧路径） | 28/28 PASS；其中 `distance_with_calibration_matches_formula`：`got=1236.111084 expected=1236.111111`（框高 0.20×180px=36px）、`distance_valid_counted valid=1`、`distance_worker_text 1236.1 m`；无标定分支 `distance_m=-1 + NO_CALIBRATION + "—"` |
| 真实链路带标定（test 源 6s、`--calib`+`--camera-zoom CAM-01=Z1`） | 30 帧推理、30 次发布；`distance_valid=0 / distance_unknown=0 / distance_text=—`（测试图案无人员目标 → 无换算对象，如实未知），链路判据 9/9 PASS |

环境：librknnrt `2.3.2 (429f97ae6b@2025-04-09T09:09:27)`、驱动 `0.9.8`、g++ 11.4.0。

## 7. 返工记录（保留失败证据）

| 轮次 | 现象 | 根因 | 修复 | 复核 |
|---|---|---|---|---|
| r1 | MediaCheck 编译失败：`‘fabs’ is not a member of ‘std’` | 新增的 worker 距离用例使用 `std::fabs` 但媒体探针未包含 `<cmath>` | 补 `#include <cmath>` | r2 编译通过 |
| r2 | 板端全绿但 `form-no-calib` 步骤输出的是 selftest（14 PASS）而非“无标定”求值 | 该步骤命令漏了 `--calib`，探针在既无 fixture 又无 calib 时按既定行为回退到 selftest（探针契约本身正确，是运行入口参数错误） | 改为 `--calib <anchor> --camera CAM-09 ...`（用其它相机的标定集合验证 `NO_CALIBRATION`） | r3：`form-no-calib` 正确输出 `distance_m=-1 reason=NO_CALIBRATION text=—` |

## 8. 观察与移交

1. **标定来源目前是 CLI**：`app/config` 无标定字段（不新增 YAML schema）；生产建议在配置中登记每相机每档位的
   `ref_m/ref_h_px/obj_m/min/max` 并在启动时转成 `DistanceCalibrationSet`（本模块已提供 `parse_calibration_spec`）。
2. **当前变焦档位需外部提供**：本仓库没有读取镜头变焦状态的接口/设备协议，故档位未知时一律 `-1`
   （`ZOOM_UNKNOWN`）；接入变焦读数的责任在设备/视频侧，本层只消费显式档位字符串。
3. **单一身高先验**：换算基于站立成人身高 1.70 m；蹲伏/儿童/多人重叠会偏差，属方法边界，不是实现缺陷。
   现场若需要更高精度，应提供分块/地面几何标定（另行立项），本增量不臆造。
4. **精度未验收**：本轮只证明“公式与门控正确、未知不臆造”，未做与真实距离的误差测量（需 ENV-D 现场标定 + 人工核验）。
5. **规则不消费距离**：`danger_proximity_m` 仍未接入；若将来接入，应由规则侧显式读取 `Detection.distance_m` 并定义
   `-1` 的处理（不可当 0 或当“很近”），本轮不改 `rules.h`。
6. **P5 渲染属 ITEM-012**：本轮只提供 `distance_text` 与每路统计；`"—"` 占位沿用既有实现。
