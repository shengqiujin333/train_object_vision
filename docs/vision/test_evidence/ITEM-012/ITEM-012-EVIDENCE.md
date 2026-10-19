# ITEM-012 证据 — AI 调试视图数据与掩码叠加

- Capability: `computer_vision_engineer.vision_inference_implementation`
- 工作项: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-012`
- 轮次标记: `20261021-item012-r4`（终轮）；r1/r2 为返工留痕
- 日期: 2026-10-21
- 实现文档: `docs/vision/implementation/ITEM-012_DEBUG_VIEW.md`
- 环境: RK3588 `192.168.0.103`（`SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU`），Linux 6.1.141 aarch64、g++ 11.4.0、librknnrt `2.3.2 (429f97ae6b@2025-04-09T09:09:27)` + 驱动 `0.9.8`
- 执行方式: 登记 Paramiko SSH 入口 `app/tools/board_vision_debug.py`（凭据仅经 `TRAIN_RK3588_SSH_CREDENTIAL`；受信任 known_hosts + RejectPolicy；全部有限超时；只写 `/tmp`；结束清理）

## 1. 本轮执行步骤（一次有界会话）

| 步骤 | 命令 | 退出 | 结果 |
|---|---|---|---|
| 上传 | app/ + 两个 `.rknn` 资产 | 0 | 仅 `/tmp/railhmi_vision_item012_20261021-item012-r4` |
| 构建 CoreTests / MediaCheck | `bash build.sh CoreTests` / `MediaCheck` | 0 | 通过 |
| 单元测试 | `railhmi-core-tests --junit ...` | 0 | `tests=1412 failures=0`（ITEM-011 的 1347 + 本增量 65） |
| 调试视图自检 | `railhmi-vision-debug --selftest` | 0 | 17/17 PASS |
| 6 fixtures | `--fixture fields/unavailable/mask_on/mask_off/no_mask/driver` | 0×6 | 全部 `RESULT: PASS` |
| 配置口径 ×3 | `--config ./config --role developer/driver/maintainer --overlays ...` | 0×3 | 4/2/4 判据 PASS |
| media 自检 | `railhmi-vision-pipeline --selftest` | 0 | 30/30 PASS（含掩码抓取门控） |
| 真实链路（12s + `--debug-overlays mask`） | `--mode chain --lanes CAM-01 --debug-overlays mask --debug-role developer` | 0 | 13/13 PASS（含运行态 P5 与停止后不可用） |
| 清理 | `rm -rf /tmp/railhmi_vision_item012_20261021-item012-r4` | 0 | `cleaned` |

## 2. 字段齐备（TC-T-DBG-01）

真实链路（运行态）P5 单路数据：

```json
{"camera_id":"CAM-01","profile":"front","profile_label":"前方障碍/人员","available":true,
 "unavailable_reason":"","model_version":"rknn-2.3.2/yolov5s+rknn-2.3.2/yolov8s-seg",
 "fps_text":"5.0 fps","latency_text":"141 ms","confidence_text":"—","distance_text":"—",
 "last_result_text":"0 秒前","box_count_text":"0","fps":5.0,"latency_ms":141,
 "boxes":[],"overlays":{"boxes":false,"roi":false,"text":false,"mask":true},
 "has_roi":false,"roi_unavailable_reason":"未配置 ROI","mask_available":true,
 "mask":{"width":1280,"height":720,"stride_bytes":1280,"kind":"rail",
         "positive_pixels":54867,"plane_present":true}}
```

- 字段来源均为真实推理：`model_version` 来自 ITEM-001 核验后的模型登记、`fps/latency` 来自工作线程实测、
  `boxes/confidence` 来自本帧 envelope、`mask` 来自本帧 rail 分割（`positive_pixels` 由 core 逐行统计）。
- 测试图案无人形目标：`confidence_text="—"`、`box_count_text="0"`、`distance_text="—"`（如实未知/零目标，不伪造）。

fixtures 与单测另外覆盖：`boxes` 有值时 `confidence_text="0.87"`、`box_count_text="1"`、`fps_text="5.0 fps"`、
`latency_text="142 ms"`（`fixture fields`）。

## 3. 无模型/不可用时显式原因 + 未知不等于 0（TC-T-DBG-03）

三职责 lane（无核验模型）在真实链路上的 P5 行：

```json
{"camera_id":"CAM-04","profile":"pantograph","available":false,
 "unavailable_reason":"PROFILE_UNSUPPORTED","model_version":"—","fps_text":"—",
 "latency_text":"—","confidence_text":"—","distance_text":"—","last_result_text":"—",
 "box_count_text":"—"}
```

单测/fixture 断言（`fixture unavailable`、`debug_unknown_not_zero` 等）：

- `model_version/fps/latency/confidence/distance/last_result/box_count` 全部为 `"—"`；
- 断言不等于 `"0"`、`"0.0"`、`"0 ms"`、`"0.0 fps"`、`"0.0 m"` 任一形式；
- `available=false` 时 `unavailable_reason` 非空（`PROFILE_UNSUPPORTED`/`等待结果`/`AI 不可用`）。

真实配置口径（无推理结果）输出：

```
[PASS] config_unknown_and_reason :: 无结果路：原因在、数值未知
```

## 4. 司机角色不显示 AI 调试（TC-T-DBG-02 隔离）

`--config ./config --role driver --overlays 'boxes,roi,text,mask'`：

```json
{"visible":false,
 "hidden_reason":"driver role cannot view AI debug (P5 hidden; overlays off)",
 "switches":{"boxes":true,"roi":true,"text":true,"mask":true},
 "switches_desc":"boxes,roi,text,mask","overlays_effective":false,"channels":[]}
```

```
[PASS] config_role_visibility :: visible=false
[PASS] config_hidden_no_data :: driver role cannot view AI debug (P5 hidden; overlays off)
```

→ 即使四个开关全开，司机角色拿不到任何通道数据、也不产生叠加（本层保证；界面隔离由桌面 UI 实现）。

对照（维护角色 + 全关）：`visible=true`、`overlays_effective=false`、4/4 PASS。

## 5. 四开关默认关闭

- `OverlaySwitches{}` 全 false；解析 `none`/`off`/空串 → 全关（单测 `debug_switches_default_all_off` /
  `debug_parse_switches_none_explicit`）。
- 真实配置口径 `--overlays none`（维护角色）：`visible=true`、`overlays_effective=false`、
  `[PASS] config_roi_overlay_gate :: roi_switch=off`。

## 6. 掩码叠加只认真实掩码（TC-T-DBG-02）

| 场景 | 观测 | 结果 |
|---|---|---|
| 开关关闭 + 掩码可用 | `overlay_mask=false`、`mask.plane=nullptr`、`mask_available=true` | `[PASS] mask_switch_off_not_provided` |
| 开关开启 + 掩码可用（真实链路） | `overlay_mask=true`、`1280×720`、`positive_pixels=54867`、`mask_updates=51` | 见 §2 JSON |
| 开关开启 + 无掩码（分割未产出） | `overlay_mask=false`、`mask_unavailable_reason="SEG_UNAVAILABLE"` | `fixture no_mask` / `[PASS] mask_unavailable_no_overlay` |
| 开关开启 + plane 尺寸不足 | 视为不可用（原因非空、不叠加） | `[PASS] debug_mask_size_mismatch_rejected` |
| 未开掩码抓取 | `mask_unavailable_reason="NOT_CAPTURED"` | `[PASS] debug_mask_capture_off_not_captured`（media selftest） |

media selftest（真实发帧路径 + 替代检测器，无掩码）：

```
[PASS] debug_mask_capture_unavailable_reason :: 无掩码 -> 不可用原因（非 NOT_CAPTURED）/无 plane
[PASS] debug_mask_capture_off_not_captured  :: 开关关闭 -> NOT_CAPTURED
```

## 7. ROI 叠加门控

- 有 ROI 配置 + `roi` 开关：`overlay_roi=true` 且 `roi_rect` 来自 `roi.yaml`（单测 `debug_roi_rect` 校验 `x=0.08`）。
- 无 ROI 配置 + `roi` 开关：`overlay_roi=false` + `roi_unavailable_reason="未配置 ROI"`（真实链路上三个非前方 lane 的实测输出）。
- `roi` 开关关闭：`overlay_roi=false`（`[PASS] config_roi_overlay_gate :: roi_switch=off`）。

## 8. 停止后 AI 不可用（不继续显示“可用”）

链路停止后再生成同一数据面：

```
type=debug_view_stopped json={"visible":true,...,"channels":[{"available":false,
  "unavailable_reason":"AI 不可用",...}]}
[PASS] chain_debug_stopped_unavailable :: 停止后每路 available=false 且有原因
```

## 9. 返工留痕

| 轮次 | 保留文件 | 失败 | 修正 |
|---|---|---|---|
| r1 | （中间轮，已清理） | MediaCheck：`InferenceEnvelope has no member named 'available'` | 改用 `availability == AiAvailability::Ready` |
| r2 | `…-r2.log`、`…r2…config-maint-default-off….log` | (a) `vector<EnvelopeBox>` 无法赋给 `vector<DetectionBox>`；(b) `--overlays none` 被拒（`CFG_EXIT=2`） | (a) 逐字段转换；(b) 解析器接受 `none`/`off`/空串为全关（并补单测） |

## 10. 验收判据对照

| 任务书要求 | 证据 |
|---|---|
| 为 P5 提供每路 model_version/available/unavailableReason/fps/latency/最近结果/置信度/距离/boxes/ROI | §2（真实链路运行态 JSON）+ §3（不可用行）+ 单测 `fields_present_and_formatted` |
| 仅在掩码真实可用时提供掩码叠加数据 | §6：真实掩码（1280×720/54867 正像素）才提供；无掩码/尺寸不符只给原因 |
| 叠加层四开关默认关闭 | §5：默认全关；`none/off` 解析；真实配置 `overlays_effective=false` |
| 司机角色不显示 AI 调试 | §4：`visible=false` + `channels=[]` + 无叠加（即使开关全开） |
| 无模型时显式显示不可用原因 | §3：`PROFILE_UNSUPPORTED` / `等待结果` / `AI 不可用` 均非空 |
| 未知数值不格式化为 0 | §3：统一 `"—"` 且断言不等于任何 0 形式 |

## 11. 覆盖边界（本轮不声称的范围）

- 未连接真实相机（ENV-D）；链路使用 test 源 + 真实 RKNN（掩码来自真实分割输出）。
- 未做 P5 的 Qt/QML 渲染、配色、透明度、图层顺序与交互（桌面 UI 职责）；未做 6 路掩码叠加的渲染性能测试。
- 未做人工视觉确认；`positive_pixels` 只作调试统计，不表示掩码质量结论。
- 职责状态（受电弓/车门/轮轨 unknown/正常/异常）来自 ITEM-010 数据；本轮 P5 行只显示其不可用原因，独立字段渲染留给 P5 布局。
- 掩码抓取在开关开启时每帧一次约 0.9MB 拷贝（1280×720），未评估 6 路并发内存带宽。

## 12. 证据清单（本目录）

**r4（终轮）**

- `vision-item012-192_168_0_103-20261021-item012-r4.transcript.json` / `.log` — 结构化/人类可读 transcript
- `…-r4.app_core-evidence_item012-core-tests-20261021-item012-r4.xml` — JUnit（`tests=1412 failures=0`）
- `…-r4.app_core-evidence_item012-debug-selftest-20261021-item012-r4.log` / `.json` — 调试视图自检 17/17
- `…-r4.app_core-evidence_item012-fixture-{fields,unavailable,mask_on,mask_off,no_mask,driver}-….log` — 6 个 fixture
- `…-r4.app_core-evidence_item012-config-{dev-all-overlays,driver-all-overlays,maint-default-off}-….log` / `.json` — 三组配置口径
- `…-r4.app_core-evidence_item012-media-selftest-20261021-item012-r4.log` / `.json` — media 自检 30/30（含掩码抓取门控）
- `…-r4.app_core-evidence_item012-chain-20261021-item012-r4.log` / `.json` — 真实链路 13/13（运行态 P5 + 停止后不可用 + 真实掩码）

**返工留痕**

- `…-r2.log` — MediaCheck 两处编译/参数失败
- `…r2…config-maint-default-off….log` — `--overlays none` 被拒（`CFG_EXIT=2`）
