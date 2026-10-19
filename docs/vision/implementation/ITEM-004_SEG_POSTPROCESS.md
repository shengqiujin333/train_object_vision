# 视觉推理 ITEM-004 实现 — YOLOv8-seg 轨道分割后处理

- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-004`
- 版本: v1（2026-10-19）
- 上游: `docs/vision/VISION_DESIGN.md` §7.3、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0 §6（掩码独立二值 plane / 不进 core DetectionResult）、`docs/vision/VISION_TEST_DESIGN.md` G4（TC-T-SEG-01..06）、ITEM-001（真实 13 输出形状）、ITEM-002（letterbox 参数）、ITEM-003（张量视图与反量化复用）、历史解码 `code/rknpu2_postprocess.cc`
- 范围: 只实现 ITEM-004（轨道分割后处理）。未实现同帧两阶段判定、结果发布、语义接入、真实 RKNN 前向（后续 ITEM）。

## 1. 交付内容

| 交付 | 路径 | 说明 |
|---|---|---|
| 分割后处理纯逻辑 | `app/src/core/include/railhmi/core/vision_seg_postprocess.h`、`src/vision_seg_postprocess.cpp` | 13 输出解析（3 尺度 box/score/score_sum/seg 系数 + proto）、DFL 解码、score/score_sum 过滤、per-box 系数·proto 点积、阈值化、逆 letterbox、**每框独立**原图二值 plane；形状不符显式 `MODEL_MISMATCH`。仅标准库。 |
| 共享张量工具 | `vision_detect_postprocess.h/.cpp` 增 `RawDetectTensor.fmt`、`tensor_dequant`、`tensor_index_nchw` | 检测/分割共用同一张量视图与反量化，避免重复实现。 |
| 探针 | `app/src/core/tools/seg_probe.cpp`（`railhmi-vision-seg`） | `--selftest`（22 判例）、`--fixture rail/zero_coeff/nocrop/nms/empty/badcount/badsegshape/badproto/badfmt/int8`、`--config`。 |
| 单元测试 | `app/tests/core/test_vision_seg_postprocess.cpp` | 解码/掩码对齐/二值/阈值/裁剪/NMS/空图/形状不符/int8。 |
| 板端入口 | `app/tools/board_vision_seg.py` | 一次有界会话：CoreTests→core-tests→selftest→10 fixtures→config→取回证据→清理。 |

构建接入：`app/src/core/CMakeLists.txt`、`app/tests/core/CMakeLists.txt`、`app/tests/core/main.cpp`（仅新增条目）。

## 2. 如何满足工作项

| 要求 | 实现 | 证据 |
|---|---|---|
| 三尺度各 box/score/score_sum/seg 系数 + 1 proto（13） | 分支张量 `base=s*4`：box `[1,4*dfl,H,W]`、score `[1,nc,H,W]`、score_sum `[1,1,H,W]`、seg `[1,32,H,W]`；proto `[1,32,160,160]` | `--fixture rail`：`candidates=1 dropped_score=8399` |
| 分割头解码（DFL） | `dfl_len=box.channels/4`；对每方向 16 bin 做 softmax 加权和；`x1=(-d0+j+0.5)*stride` | 框 `xywh=(0.45625,0.19999,0.10001,0.17779)`（模型框 (292,212)-(356,276)） |
| per-box 系数·proto 点积生成掩码 | `mask_proto[p] = Σ_k coeff[k]·proto[k][p]`（32×160×160） | rail 掩码 `positive=7040` |
| 阈值化为二值 | `> mask_threshold` 判 1，否则 0；值域 {0,1} | `mask binary + has positives`；`mask_threshold=100 -> positive=0` |
| 逆 letterbox 到原图坐标 | proto→model（双线性）→ 按框裁剪（模型空间）→ 逐原图像素 `mx=(ox+0.5)*scale-0.5+pad_x` 采样 | `px(600,240)=1`（框内左半）、`px(680,240)=0`（框内右半）、`px(400,240)=0`（框外） |
| 原图尺寸独立二值 plane（stride/所有权符合帧契约） | 每框 `SegMask{width,height,stride_bytes=width,plane(stride*height),positive}`，独立 `std::vector` 所有权 | `mask=1280x720 stride=1280 size=921600`，`masks==boxes` |
| 与 rail 框/分数一一对应 | `SegMask.box_index` + `masks` 与 `boxes` 同序；NMS 后每保留框生成一个掩码 | `box<->mask one-to-one`；nms 后 `boxes=1 masks=1` |
| 数量/形状不符显式 MODEL_MISMATCH | 13 计数、分支通道（box%4、score=nc、score_sum=1、seg=32）、proto 形状、fmt=NCHW、空间一致 | `12 tensors`/`seg channels=16`/`proto 32x80x160`/`non-NCHW`/`box channels=63` 五类负例逐条 |
| 夹具可复现 | `--selftest` 22 判例全 PASS | `item004-selftest` |

## 3. 解码约定（对齐已部署历史实现）

- **张量布局**：NCHW 通道优先 `idx(c)=c*H*W + i*W + j`；非 NCHW 显式 `MODEL_MISMATCH`（不猜布局）。
- **DFL**：box 张量为 logits；`softmax(16 bin)` 加权和 → 各方向距离（网格单位）。
- **score**：类别概率（sigmoid 后）；`score_sum` 仅作快速预筛（同 `score_threshold`）。
- **掩码**：per-box 32 维系数 · proto；`> mask_threshold`（默认 0，与历史 `>0` 一致）为 1。
- **裁剪**：默认 `crop_to_box=true`（历史 `crop_mask`）；可配置。
- **逆 letterbox**：proto(160×160)→model(640×640) 双线性，再逐原图像素按 `scale/pad` 反查 —— 等价于历史的 “resize→crop→seg_reverse”，避免中间大数组多次分配（实现了两步：proto→model 后采样）。
- **框**：与 ITEM-003 相同的类别分组 + `+1` IoU NMS；原图归一化 `x/y/w/h`。

## 4. 运行入口

```
cd app && bash build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-core/tools/railhmi-vision-seg --selftest --out selftest.log --json selftest.json
./build-core/tools/railhmi-vision-seg --fixture rail --out rail.log
./build-core/tools/railhmi-vision-seg --fixture badcount --out badcount.log   # 期望 MODEL_MISMATCH
./build-core/tools/railhmi-vision-seg --config ./config --out config.log
```

## 5. 验证记录

- 本轮实现**首次板端执行即通过**（r1：build=0、core-tests 1059/0、selftest 22/22、10 fixtures 全部符合预期、config=0）；无返工轮。
- 板端已执行：CoreTests 构建 + 单测、`--selftest`、10 个 fixture、`--config`（阈值从 `thresholds.confidence_min` 读取）。

## 6. 观察与移交

1. **本增量为夹具张量解码**；真实 `rknn_run` 前向 + 端到端掩码属 ITEM-009/执行轮。真实模型 13 输出的形状/量化已由 ITEM-001 归档，可供直接对接。
2. **性能未测**：本增量不测量前向耗时；解码含 per-box 双线性重采样（640×640 浮点中间量 ~1.6MB/框），真实框数量下的耗时/内存在 ITEM-009/执行轮测量并按需优化（如 RGA 缩放或按框裁剪后再 resize）。
3. **夹具 DFL 残差**：夹具用 logit 12 近似 one-hot，softmax 仍有极小残差，坐标与解析解相差 <1e-3（selftest 容差内），非解码缺陷。
4. **掩码交接**：`SegMask` 为独立原图坐标二值 plane；按接口契约，掩码不进 core `DetectionResult`，由语义适配器消费（ITEM-005/007），UI 掩码叠加属 ITEM-012。
5. **未实现**：同帧两阶段判定、InferenceEnvelope 发布、语义白名单、跟踪、P5 视图、测距（后续 ITEM）。
6. `crop_to_box`/`mask_threshold`/`nms_iou` 均为 `SegConfig` 参数（可配、有溯源字段）；score 阈值产品路径取 `thresholds.confidence_min`。
