# 视觉推理 ITEM-003 实现 — YOLOv5 人形检测后处理

- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-003`
- 版本: v1（2026-10-19）
- 上游: `docs/vision/VISION_DESIGN.md` §7.2、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0 §6、`docs/vision/VISION_TEST_DESIGN.md` G3（TC-T-DET-01..05）、ITEM-001（真实输入张量 dtype/scale/zp）、ITEM-002（letterbox 参数）、已部署历史解码 `code/yolov5_postprocess.cc`
- 范围: 只实现 ITEM-003（人形检测后处理）。未实现分割后处理、同帧两阶段、结果发布、语义接入、真实 RKNN 前向（后续 ITEM）。

## 1. 交付内容

| 交付 | 路径 | 说明 |
|---|---|---|
| 后处理纯逻辑 | `app/src/core/include/railhmi/core/vision_detect_postprocess.h`、`src/vision_detect_postprocess.cpp` | 三尺度 YOLOv5 头解码、置信度过滤、类别白名单、letterbox 逆变换归一化、按类别 NMS、统计与稳定 reason。仅标准库。 |
| 探针 | `app/src/core/tools/detect_probe.cpp`（`railhmi-vision-detect`） | `--selftest`（19 判例）、`--fixture person|nonperson|nms|empty|padbox|int8`、`--config DIR`（阈值取自 `thresholds.confidence_min`）。 |
| 单元测试 | `app/tests/core/test_vision_detect_postprocess.cpp` | 解码/白名单/空图/阈值/NMS/越界/截断/IoU/逆变换/int8。 |
| 板端入口 | `app/tools/board_vision_detect.py` | 一次有界会话：CoreTests→core-tests→selftest→6 fixtures→config→取回证据→清理。 |

构建接入：`app/src/core/CMakeLists.txt`、`app/tests/core/CMakeLists.txt`、`app/tests/core/main.cpp`（仅新增条目）。

## 2. 如何满足工作项

| 要求 | 实现 | 证据 |
|---|---|---|
| 三尺度 YOLOv5 头解码 | 每个尺度 `[1,255,H,W]`（通道优先，channel(85a+ch)·grid_len）；cx/cy/w/h/obj/类别 | `--fixture person`：模型框 (200,300)-(264,364) → 归一化 (0.3125,0.44444,0.1,0.17778) |
| 置信度过滤 | `score = obj*cls`（输出已含 sigmoid），`score < score_threshold` 丢弃并计数 | person 夹具 score=0.81；conf=0.5（配置）保留、conf=0.9 过滤 |
| NMS（阈值来自配置，不写死） | `DetectConfig.nms_iou` 传入；探针 `--nms-iou`（默认 0.45 记录在证据）；按类别贪心、+1 像素 IoU（与历史解码一致） | nms 夹具：iou 0.45 保留 1、0.99 保留 2；`nms_suppressed` 计数 |
| 阈值来自配置 | `--config` 读 `thresholds.confidence_min=0.5` 作为 score 阈值（CLI `--conf` 可覆盖）；解码路径无硬编码阈值 | `item003-config`：`confidence_min=0.500000 score_used=0.500000 (from thresholds.confidence_min)` |
| letterbox 逆变换归一化 | `x=(mx-pad_x)/scale/src_w`…；`inverse_letterbox_normalize`；x+w≤1、y+h≤1 | person 夹具坐标精确；`inverse clamps to frame` |
| 只保留 person，其余丢弃并可计数 | `class_whitelist={0}`；非白名单 `dropped_class++` | `--fixture nonperson`：boxes=0 dropped_class=1；白名单可配置（{0,2} 保留 car） |
| 空图产生空框而非错误 | 全零 float 张量 → `ok=true reason=DETECT_EMPTY boxes=0` | `--fixture empty`；core 单测 `empty image -> ok empty` |
| 夹具张量确定可复现 | `--selftest` 19 判例全 PASS，坐标/分数/计数固定 | `item003-selftest` |

## 3. 解码约定（对齐已部署历史解码，避免训练/部署不一致）

- **通道布局**：`[1, 255, H, W]`，通道优先；`idx(a,ch) = (85*a + ch)*grid_len + i*grid_w + j`；`ch`: 0=cx,1=cy,2=w,3=h,4=obj,5..84=类别。
- **输出已含 sigmoid**：box 值∈[0,1]（`bx=(v*2-0.5+j)*stride`，`bw=(v*2)^2*anchor_w`），obj/cls 为概率（`score=obj*cls`）。`DetectConfig.apply_sigmoid` 保留为切换项。
- **anchors**：默认 yolov5s 标准集（`10,13 / 16,30 / 33,23 / 30,61 / 62,45 / 59,119 / 116,90 / 156,198 / 373,326`），可覆盖。
- **NMS**：按类别分组、分数降序贪心，IoU 采用与历史实现一致的 **+1 像素**约定；NMS 在模型像素空间进行（归一化空间等价性不足）。
- **越界策略**：逆 letterbox 后裁剪到原图；完全落在补边区/画面外的框丢弃并计 `dropped_bounds`。
- **输出**：分数降序，`max_detections` 截断并计 `truncated`。

## 4. 运行入口

```
cd app && bash build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-vision-detect --selftest --out selftest.log --json selftest.json
./build-core/tools/railhmi-vision-detect --fixture person  --out person.log
./build-core/tools/railhmi-vision-detect --fixture nonperson --out nonperson.log
./build-core/tools/railhmi-vision-detect --fixture nms --out nms.log
./build-core/tools/railhmi-vision-detect --fixture empty --out empty.log
./build-core/tools/railhmi-vision-detect --fixture padbox --out padbox.log
./build-core/tools/railhmi-vision-detect --fixture int8 --out int8.log
./build-core/tools/railhmi-vision-detect --config ./config --out config.log
```

## 5. 返工记录（保留失败证据）

| 轮次 | 现象 | 根因 | 修复 | 复核 |
|---|---|---|---|---|
| r1 | `--release` 编译失败：`RawDetectTensor has no member named zp` | 探针夹具误用 `t.zp`，实际字段为 `zero_point` | 改正为 `t.zero_point` | r2 build=0 |
| r2 | core-tests 1035/3：`max_detections truncates`、`boxes sorted by score desc` | 测试夹具把 3 个框放在 letterbox 补边区（模型 y<140），逆变换后全被越界丢弃 → 只剩 1 框 | 框移到画面内（y=260/280/400） | r3 core-tests 1035/0 |
| r2 | selftest `int8 dequant coords` + core-test `int8 decode one box` 失败 | int8 张量按字节 0 填充，zp=-128 时反量化≈0.5，obj*cls≈0.252>阈值 → 全网格伪候选 300 框 | 空背景改填 `q(0.0)`（=字节 0x80 / −128） | r3 int8 夹具 boxes=1 |

## 6. 观察与移交

1. **int8 空背景语义**：zp=−128 时，输出张量字节 0 反量化约 0.49（≈0.5），并非“无目标”；空背景应对应 `q(0.0)`（−128）。夹具已按此修正，真实模型输出无需特殊处理。
2. **真实 RKNN 前向未在本增量执行**：ITEM-003 的用户可观察项为夹具张量；真实 `rknn_run` + 图像输入 + 端到端框属 ITEM-009（真实检测器接入）与执行轮。本实现已提供 `DetectConfig`/`decode_yolov5` 供其直接调用。
3. **`nms_iou` 来源**：产品路径的 score 阈值取 `thresholds.confidence_min`；NMS IoU 无配置字段，以 `DetectConfig` 参数传入（探针默认 0.45 且在证据中记录来源），解码路径不写死。若需纳入配置，属后续配置项扩展（不改本增量）。
4. **NMS +1 像素约定**与历史解码一致；若未来统一为标准 IoU，需版本化并重跑回归。
5. **未实现**：分割后处理、同帧两阶段、InferenceEnvelope 发布、语义白名单接入、跟踪、P5 视图、测距（后续 ITEM）。
6. `class_whitelist` 默认仅 person（COCO 0）；`label` 输出 `person`/`cls_<id>`，供语义层映射使用。
