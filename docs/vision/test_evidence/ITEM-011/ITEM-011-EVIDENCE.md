# ITEM-011 证据 — 距离测量（可选后置）

- Capability: `computer_vision_engineer.vision_inference_implementation`
- 工作项: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-011`
- 轮次标记: `20261021-item011-r3`（终轮）；r1/r2 为返工留痕
- 日期: 2026-10-21
- 实现文档: `docs/vision/implementation/ITEM-011_DISTANCE.md`
- 环境: RK3588 `192.168.0.103`（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`），Linux 6.1.141 aarch64、g++ 11.4.0、librknnrt `2.3.2 (429f97ae6b@2025-04-09T09:09:27)` + 驱动 `0.9.8`
- 执行方式: 登记 Paramiko SSH 入口 `app/tools/board_vision_distance.py`（凭据仅经 `TRAIN_RK3588_SSH_CREDENTIAL`；受信任 known_hosts + RejectPolicy；全部有限超时；只写 `/tmp`；结束清理）

## 1. 本轮执行步骤（一次有界会话）

| 步骤 | 命令 | 退出 | 结果 |
|---|---|---|---|
| 上传 | app/ 330 文件 + 两个 `.rknn` 资产 | 0 | 仅 `/tmp/railhmi_vision_item011_20261021-item011-r3` |
| 构建 CoreTests / MediaCheck | `bash build.sh CoreTests` / `MediaCheck` | 0 | 通过 |
| 单元测试 | `railhmi-core-tests --junit ...` | 0 | `tests=1347 failures=0`（ITEM-010 的 1300 + 本增量 47） |
| 距离自检 | `railhmi-vision-distance --selftest` | 0 | 14/14 PASS |
| 6 个 fixture | `--fixture no_calib/zoom_unknown/zoom_mismatch/non_person/out_of_range/valid` | 0×6 | 全部 `RESULT: PASS` |
| 形态核对（4 条） | 锚点/线性/档位未知/相机未标定 | 0×4 | 全部 `RESULT: PASS`（见 §3） |
| media 自检 | `railhmi-vision-pipeline --selftest` | 0 | 28/28 PASS（含 6 条距离用例） |
| 真实链路带标定 | `--mode chain --lanes CAM-01 --calib '<anchor>' --camera-zoom CAM-01=Z1` | 0 | 9/9 PASS（距离统计如实未知） |
| 清理 | `rm -rf /tmp/railhmi_vision_item011_20261021-item011-r3` | 0 | `cleaned` |

## 2. 有标定：输出与公式一致（TC-T-DIST-02）

锚点标定（由 CLI 提供，来源标注历史文档）：

```
type=calibration json={"camera":"CAM-01","zoom":"Z1","ref_m":500.000000,"ref_h_px":89.000000,
                       "px_per_m_reference":52.352941}
type=request camera=CAM-01 label=person box_h_px=89.000000 zoom=Z1
type=estimate json={"valid":true,"distance_m":500,"text":"500.0 m","reason":"DISTANCE_OK",
                    "detail":"distance = ref_m*ref_px/px","zoom":"Z1","box_height_px":89,
                    "px_per_m_reference":52.3529}
type=debug text=500.0 m
RESULT: PASS failures=0
```

线性核对（像素高加倍 → 距离减半）：

```
type=request camera=CAM-01 label=person box_h_px=178.000000 zoom=Z1
type=estimate json={"valid":true,"distance_m":250,"text":"250.0 m",…}
RESULT: PASS failures=0
```

`52.352941 = 89 / 1.70`，与设计 §7.6 的“≈52 px/m”一致。

## 3. 未标定 / 档位未知：`distance_m = -1` 且调试文本为占位（TC-T-DIST-01）

```
# 档位未知（标定存在但不换算）
type=request camera=CAM-01 label=person box_h_px=89.000000 zoom=(unknown)
type=estimate json={"valid":false,"distance_m":-1,"text":"—","reason":"ZOOM_UNKNOWN",
                    "detail":"current zoom level is unknown; calibration exists for 1 zoom level(s)"}
type=debug text=—

# 相机无标定（集合里只有 CAM-01）
type=request camera=CAM-09 label=person box_h_px=89.000000 zoom=Z1
type=estimate json={"valid":false,"distance_m":-1,"text":"—","reason":"NO_CALIBRATION",
                    "detail":"camera 'CAM-09' has no calibration"}
type=debug text=—
```

两者 `RESULT: PASS`，且 `distance_m` 为 **-1**（不是 0），调试文本为 **`—`**（不是 `0`/`0.0 m`）。

## 4. 其它门控（fixture，板端）

| fixture | 观测 |
|---|---|
| `no_calib` | `-1` / `NO_CALIBRATION` |
| `zoom_unknown` | `-1` / `ZOOM_UNKNOWN` |
| `zoom_mismatch`（标定 Z1，请求 Z2） | `-1` / `ZOOM_MISMATCH` |
| `non_person`（label=obstacle） | `-1` / `NOT_PERSON` |
| `out_of_range`（40px→1112.5 m 超出 [5,600]） | `-1` / `OUT_OF_RANGE`（**不 clamp**） |
| `valid`（89px） | `500.0 m` / `DISTANCE_OK` |

6/6 `RESULT: PASS`。

## 5. 真实发帧路径（media 自检，替代检测器 + 真实帧租约）

```
[PASS] distance_calibration_parsed
[PASS] distance_with_calibration_matches_formula :: got=1236.111084 expected=1236.111111
[PASS] distance_valid_counted :: valid=1
[PASS] distance_worker_text :: 1236.1 m
[PASS] distance_box_present :: 替代检测器人员框在场
[PASS] distance_without_calibration_unknown :: distance_m=-1 + NO_CALIBRATION + 文本占位
```

- 输入：test 源 320×180、替代检测器框高 `h=0.20` → `36 px`；标定 `500m/89px` → `500*89/36 = 1236.111 m`。
- 无标定分支：同一路径 `distance_m=-1`、`distance_unknown_by_reason["NO_CALIBRATION"]=1`、文本 `—`。
- media 自检整体 28/28 PASS（ITEM-009/010 判据未回退）。

## 6. 真实链路带标定（6s，test 源）

```
type=calibration camera=CAM-01 zoom=Z1 ref_m=500.000000 ref_h_px=89.000000
                 px_per_m_reference=52.352941 source=README_2026-10-13:500m/89px
type=camera_zoom camera=CAM-01 zoom=Z1
type=lane camera=CAM-01 … consumed=30 inferred=30 published=30 distance_valid=0
          distance_unknown=0 distance_text=— last_distance_m=-1 …
type=summary submitted=151 consumed=30 published=30 unavailable=4 round=362 budget=1
[PASS] ×9（models/frames/envelopes/stop/rounds/budget/thermal/guard/lease）
RESULT: PASS failures=0
```

- 测试图案没有人员目标 → 无换算对象，`distance_valid=0 / distance_unknown=0`，调试文本 `—`（如实未知，不伪造）。
- 链路本身（模型 ready/发布/停止后 AI 不可用/温度实测/预算）9/9 PASS，证实距离接入未影响既有链路。

## 7. 规则不消费距离（TC-T-DIST-03）

单元测试 `distance_rules_emit_same_count` / `distance_rules_same_type_level` / `distance_rule_keeps_unknown_field`：
同一 `Detection`（`distance_m=-1` vs `3.2`）经 `RuleEngine` 产生**相同**事件数量、类型与级别，事件内 `distance_m` 保留 -1。
→ 现有规则既不读、也不改写距离；本轮**不**声称“已实施 5 m 门槛”。

## 8. P5 语义（未知非 0）

单元测试锁定 `build_ai_debug`：

```
[PASS] distance_debug_unknown_placeholder :: —            (distance_m=-1)
[PASS] distance_debug_unknown_not_zero    :: —            （不出现 "0"/"0.0"）
[PASS] distance_debug_valid_text          :: 123.4 m      (distance_m=123.4)
```

## 9. 返工留痕

| 轮次 | 保留文件 | 失败 | 修正 |
|---|---|---|---|
| r1 | `…-r1.log`、`…r1…distance-selftest….log` | MediaCheck 编译失败：`‘fabs’ is not a member of ‘std’`（媒体探针未含 `<cmath>`） | 补 `<cmath>`；r2 编译通过 |
| r2 | `…-r2.log` + 全套 r2 证据、`…r2…form-no-calib….log` | 全部判据 PASS，但 `form-no-calib` 步骤实际跑成 selftest（漏传 `--calib`，探针按既定行为回退） | 该步骤改为 `--calib <anchor> --camera CAM-09`（用其它相机标定集合验证 `NO_CALIBRATION`）；r3 正确输出 `-1/NO_CALIBRATION/—` |

## 10. 验收判据对照

| 任务书要求 | 证据 |
|---|---|
| 仅在提供相机标定（含变焦档位）时由人员框像素高度换算 distance_m | §2/§5：标定 + 档位匹配才换算；`ZOOM_UNKNOWN`/`ZOOM_MISMATCH`/`NO_CALIBRATION` 一律不换算 |
| 未标定或变焦未知时保持 -1 | §3：`distance_m=-1`（不是 0），reason 可区分 |
| 不用框尺寸臆造米值 | 门控顺序 + 超范围返回未知（不 clamp）+ 只对 person 换算；fixtures `non_person`/`out_of_range` |
| 不假设规则消费该字段 | §7：规则事件与 `distance_m` 无关；`danger_proximity_m` 未接入（未声称门槛） |
| 用户可观察：有标定时输出与换算公式一致 | §2：89px→500.0 m、178px→250.0 m、px/m=52.35；media 路径 36px→1236.111 m（与公式一致） |
| 无标定时 distance_m=-1 且调试视图显示未知而非 0 | §3/§8：`—` 占位，且断言不出现 "0"/"0.0" |

## 11. 覆盖边界（本轮不声称的范围）

- 未连接任何真实相机（ENV-D）；未做与真实距离的误差测量/精度验收（需现场标定与人工核验）。
- 标定参数来自 CLI（配置无该字段，未新增 YAML schema）；变焦档位需外部提供，仓库内无镜头变焦读数来源。
- 单身高先验（站立成人 1.70 m）；蹲伏/儿童/遮挡偏差属方法边界。
- 未做 P5 渲染（ITEM-012）、未做无相机自检归档（ITEM-013）、未改 `rules.h` 的距离消费语义。
- `chain` 使用 test 源（无人员目标），故端到端的“真实目标距离”仅由 media 自检的替代检测器路径与 core 形态核对呈证。

## 12. 证据清单（本目录）

**r3（终轮）**

- `vision-item011-192_168_0_103-20261021-item011-r3.transcript.json` / `.log` — 结构化/人类可读 transcript
- `…-r3.app_core-evidence_item011-core-tests-20261021-item011-r3.xml` — JUnit（`tests=1347 failures=0`）
- `…-r3.app_core-evidence_item011-distance-selftest-20261021-item011-r3.log` / `.json` — 距离自检 14/14
- `…-r3.app_core-evidence_item011-fixture-{no_calib,zoom_unknown,zoom_mismatch,non_person,out_of_range,valid}-….log` — 6 个门控 fixture
- `…-r3.app_core-evidence_item011-form-{anchor,scaling,zoom-unknown,no-calib}-….log` + `form-anchor….json` — 公式/线性/未知形态
- `…-r3.app_core-evidence_item011-media-selftest-20261021-item011-r3.log` / `.json` — 真实发帧路径距离用例（28/28）
- `…-r3.app_core-evidence_item011-chain-20261021-item011-r3.log` / `.json` — 带标定真实链路（距离统计 + 9/9 判据）

**返工留痕**

- `…-r1.log`、`…r1…distance-selftest….log` — `<cmath>` 缺失导致 MediaCheck 编译失败
- `…-r2.log`、r2 全套证据、`…r2…form-no-calib….log` — 运行入口参数遗漏导致 no-calib 步骤回退为 selftest
