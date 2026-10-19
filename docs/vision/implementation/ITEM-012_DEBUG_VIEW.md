# 视觉推理 ITEM-012 实现 — AI 调试视图数据与掩码叠加

- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-012`
- 版本: v1（2026-10-21）
- 上游: `docs/vision/VISION_DESIGN.md` §8.3（P5 消费 `build_ai_debug`；叠加四开关默认关闭；司机不可见；掩码只在真实可用时提供；未知值不显示 0）、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0 §6、`docs/hmi/INTERACTION_DESIGN.md` §5.5（P5 显示框/ROI/置信度/距离/帧率/延迟/模型版本；叠加开关 框/ROI/文字/掩码）、`docs/vision/VISION_TEST_DESIGN.md` G12（TC-T-DBG-01..03）、ITEM-006（envelope/MaskRef）、ITEM-007（build_ai_debug）、ITEM-009（掩码零拷贝引用）、ITEM-011（距离文本占位）
- 范围: 只实现 ITEM-012 的**数据面**。Qt/QML 渲染、页面布局、司机视图隔离的界面实现属桌面 UI 职责；本层不画任何像素。

## 1. 交付内容

| 交付 | 路径 | 说明 |
|---|---|---|
| 调试视图数据面（纯逻辑） | `app/src/core/include/railhmi/core/vision_debug_view.h`、`src/vision_debug_view.cpp` | `DebugOverlay{Boxes,Roi,Text,Mask}` + `OverlaySwitches`（**默认全关**，支持 `none/off`）、`OverlayMaskInput`（零拷贝，尺寸/stride 校验）、`DebugChannelInput`、`DebugViewModel`/`DebugChannelView`（字段 + 占位文本 + 叠加门控）、`build_debug_view`（复用 `build_ai_debug` 的可用性/陈旧语义）、`debug_view_json`。仅标准库。 |
| worker 掩码快照 | `app/src/media/{include,src}/vision_ai_worker.{h,cpp}` | `VisionWorkerConfig::capture_latest_mask`（默认 **false**，只有 P5 打开掩码开关时才抓取）；每路 `mask_available/mask_unavailable_reason/mask_width/mask_height/mask_stride/mask_updates` + 共享所有权 `mask_plane`；不可用时只给原因，不伪造空掩码。 |
| core 探针 | `app/src/core/tools/debug_probe.cpp`（`railhmi-vision-debug`） | `--selftest`（17 判例）、6 fixtures（fields/unavailable/mask_on/mask_off/no_mask/driver）、`--config DIR --role ... --overlays ... --mask-demo --json`（真实 cameras/roi 配置口径）。 |
| media 探针 | `app/src/media/tools/vision_pipeline_probe.cpp` | 新增 `--debug-overlays`/`--debug-role`；链路结束时输出 `type=debug_view`（运行态）与 `type=debug_view_stopped`（停止后 AI 不可用），chain JSON 增 `debug_view`；selftest 增掩码抓取门控 2 判例。 |
| 单元测试 | `app/tests/core/test_vision_debug_view.cpp` | 开关默认/解析、司机隔离、字段与占位、掩码门控（关/开/不可用/尺寸不符）、ROI 门控、JSON 形状（65 判例）。 |
| 板端入口 | `app/tools/board_vision_debug.py` | 上传→CoreTests→core-tests→MediaCheck→debug selftest→6 fixtures→3 组配置口径（dev 全开/driver/维护默认关）→media selftest→真实链路带 `mask` 开关→取回→清理。 |

构建接入：`app/src/core/CMakeLists.txt`（库源 + 探针）、`app/tests/core/CMakeLists.txt`、`app/tests/core/main.cpp`。

## 2. 数据模型与不变量

```
P5 每路字段（来自真实推理）：
  model_version / available / unavailable_reason / fps / latency_ms /
  last_result(age) / max_confidence / distance / boxes[] / ROI(rect)
  + 叠加：boxes / roi / text / mask（四开关，默认全关）
  + 掩码：available / unavailable_reason / width / height / stride / plane(零拷贝) / positive_pixels
```

1. **四开关默认关闭**：`OverlaySwitches{}` 全 false；`overlays_effective = visible && switches.any()`。
2. **司机不可见**：`ai_debug_visible(role)` 为假时 `visible=false`、`channels` 为空、`overlays_effective=false`，
   即使调用方强行打开四个开关也不产生任何叠加数据（`hidden_reason` 说明原因）。
3. **掩码叠加只认真实掩码**：`overlay_mask = 开关开 && plane 非空 && width>0 && height>0 && stride≥width &&
   plane.size() ≥ stride×height`；否则只给 `mask_unavailable_reason`（`SEG_UNAVAILABLE` / `NOT_CAPTURED` / `推理不可用` 等）。
4. **未知≠0**：`model_version/fps/latency/confidence/distance/last_result/box_count` 无值时统一 `"—"`；
   `available=false` 时 `unavailable_reason` 必须非空（`"AI 不可用"/"等待结果"/"推理陈旧"/`PROFILE_UNSUPPORTED``）。
5. **不越权渲染**：本层只给数据与门控结论；颜色/透明度/图层顺序/字体由桌面渲染决定。

## 3. 角色可见性（与既有权限矩阵一致）

| 角色 | `visible` | 叠加数据 |
|---|---|---|
| driver（司机） | false | 无（`channels` 空、`overlays_effective=false`） |
| maintainer（维护） | true | 仅按开关提供 |
| developer（研发） | true | 仅按开关提供 |
| admin（管理员） | true | 仅按开关提供 |

实现复用 `core::ai_debug_visible(role)`（`role_can_access(role, Page::AiDebug)`），不新增权限规则。

## 4. 掩码叠加的取数与生命周期

- 掩码来源：ITEM-004 的 `SegResult::masks[i].plane`（原图坐标二值，0/非零），经 ITEM-006 `MaskRef` 进入 envelope，
  `kind="rail"`、`box_index=-1`（掩码属于 rail 框集合，不与 person boxes 一一对应，ITEM-009 已登记）。
- 工作线程只在 `capture_latest_mask=true` 时复制“最近一帧”的掩码到共享所有权的 `std::vector`（约 0.9MB@1280×720），
  因此 P5 快照不依赖 envelope 的即时生命周期；开关关闭时不做任何拷贝（`NOT_CAPTURED`）。
- `positive_pixels` 在 core 里按 stride 逐行统计，仅作调试/证据用途，不改变像素本身。
- 掩码不可用（分割失败/本帧无 rail 掩码）时：`mask_available=false` + 原因，`overlay_mask=false`。

## 5. 板端实测（r4 终轮，RK3588 192.168.0.103）

| 项 | 实测 |
|---|---|
| core 单测 | `tests=1412 failures=0`（ITEM-011 的 1347 + 本增量 65） |
| `railhmi-vision-debug --selftest` | 17/17 PASS |
| 6 fixtures（板端） | 全部 `RESULT: PASS`（fields/unavailable/mask_on/mask_off/no_mask/driver） |
| 真实配置口径（dev 全开 + 合成掩码） | `visible=true`、`overlays_effective=true`；4/4 判据 PASS（角色可见/未知+原因/掩码门控/ROI 门控） |
| 真实配置口径（driver 全开） | `visible=false`、`channels=[]`、`overlays_effective=false`；2/2 PASS |
| 真实配置口径（维护 + 全关） | `visible=true`、`overlays_effective=false`；4/4 PASS |
| media `--selftest` | 30/30 PASS（含掩码抓取门控：无掩码→原因、开关关→`NOT_CAPTURED`） |
| 真实链路带 `--debug-overlays mask`（12s，test 源） | 13/13 PASS；P5 行：`available=true`、`model_version=rknn-2.3.2/yolov5s+rknn-2.3.2/yolov8s-seg`、`fps_text=5.0 fps`、`latency_text=141 ms`、`confidence_text=—`（无人形目标）、`box_count_text=0`、`distance_text=—`；**掩码真实可用**：`1280×720`、`positive_pixels=54867`、`mask_updates=51`、`overlay_mask=true` |
| 停止后视图 | 每路 `available=false` + `unavailable_reason="AI 不可用"`（不继续显示“可用”） |

环境：librknnrt `2.3.2 (429f97ae6b@2025-04-09T09:09:27)`、驱动 `0.9.8`、g++ 11.4.0。

## 6. 返工记录（保留失败证据）

| 轮次 | 现象 | 根因 | 修复 | 复核 |
|---|---|---|---|---|
| r1 | MediaCheck 编译失败：`InferenceEnvelope has no member named 'available'` | 新增掩码抓取代码沿用了旧字段名（实际是 `availability == AiAvailability::Ready`） | 改为枚举比较 | r2 编译该处通过 |
| r2 | MediaCheck 编译失败：`vector<EnvelopeBox>` 无法赋给 `vector<DetectionBox>`；`--overlays none` 被拒（`CFG_EXIT=2`） | (a) chain 汇总时直接赋了 envelope 框类型；(b) 开关解析器不认识显式“全关”写法 | (a) 逐字段转换 EnvelopeBox→DetectionBox；(b) 解析器接受 `none`/`off`/空串为全关（并加单测） | r3/r4 全绿 |

## 7. 观察与移交

1. **渲染归桌面**：本增量只提供数据与门控（含 `overlay_*` 布尔与掩码 plane 指针）；P5 的图层、配色、透明度、
   字体与交互属桌面 UI。司机视图隔离需 UI 同时遵守 `visible=false`（本层已保证不下发数据）。
2. **职责状态文本**：受电弓/车门/轮轨的 `unknown/正常/异常` 语义来自 ITEM-010 的 `DutyStatusItem`（`duty_state_display_zh`），
   本轮 P5 行内只显示 `unavailable_reason=PROFILE_UNSUPPORTED`；把职责状态作为独立字段渲染留给 P5 布局实现（数据已具备）。
3. **掩码开销**：`capture_latest_mask=true` 时每帧一次约 0.9MB 拷贝（1280×720），仅在研发打开掩码开关时发生；
   若后续要在 6 路同时叠加，需要评估内存带宽并考虑按需降频拷贝。
4. **掩码与框的对应**：`box_index=-1`，P5 叠加应按 `kind="rail"` 与坐标渲染，不要把掩码绑到 person 框。
5. **未执行/不外推**：未连接真实相机（ENV-D）；未做 P5 实机渲染/人工视觉确认；未测 6 路掩码叠加的渲染性能。
