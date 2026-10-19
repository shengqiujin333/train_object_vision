# ITEM-004 InferenceEvidence — YOLOv8-seg 轨道分割后处理

- Evidence type: InferenceEvidence
- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-004`
- 日期: 2026-10-19
- 权威轮次: **r1**（`20261019-item004-r1`，首次执行即通过，无返工轮）
- 事实口径: 下列均为本次在 RK3588 上真实执行（夹具张量）；未执行项见 §6。

## 1. 结论（本次验收范围）

| 用例 | 期望 | 实测 | 证据 |
|---|---|---|---|
| 13 输出解析 + 三尺度解码 | 1 个 rail 框 | `boxes=1 candidates=1 dropped_score=8399` | `item004-fixture-rail` |
| 框坐标（DFL + 逆 letterbox） | (0.45625,0.2,0.1,0.177778) | `xywh=(0.45625,0.19999,0.10001,0.17779) score=0.9000 cls=0 label=rail` | 同上 |
| per-box 系数·proto 生成掩码 | 非空二值掩码 | `positive=7040` | 同上 / selftest |
| 掩码为原图尺寸 + stride + 所有权 | 1280×720，stride=1280，size=921600 | `mask=1280x720 stride=1280 size=921600` | 同上 |
| 掩码二值 | 值域 {0,1} | `mask binary + has positives` | selftest |
| 掩码与框一一对应 | masks==boxes 且 box_index 关联 | `boxes=1 masks=1`；`box<->mask one-to-one` | selftest |
| 掩码坐标与上屏一致 | 框内左半 1 / 右半 0 / 框外 0 | `px(600,240)=1`、`px(680,240)=0`、`px(400,240)=0` | 同上 |
| 阈值化为二值 | 系数全 0 → 掩码全 0（框保留） | `positive=0` | `item004-fixture-zero_coeff` |
| mask_threshold 可配 | 阈值 100 → 全 0 | `positive=0` | selftest |
| crop_to_box 可配 | false → 框外也置正 | `positive=460800`（整幅左半） | `item004-fixture-nocrop` |
| NMS 后掩码跟随 | 2 重叠框 → 1 框 1 掩码 | `boxes=1 masks=1 nms_suppressed=1` | `item004-fixture-nms` |
| 空图 | SEG_EMPTY，无框无掩码 | `reason=SEG_EMPTY boxes=0 masks=0` | `item004-fixture-empty` |
| 张量数量不符 | MODEL_MISMATCH | `MODEL_MISMATCH: tensor count=12 expected=13` | `item004-fixture-badcount` |
| seg 系数通道不符 | MODEL_MISMATCH | `branch[0].seg channels=16 expected=32` | `item004-fixture-badsegshape` |
| proto 形状不符 | MODEL_MISMATCH | `proto shape 32x80x160 expected 32x160x160` | `item004-fixture-badproto` |
| 非 NCHW 布局 | MODEL_MISMATCH | `branch[0].unsupported fmt (expect nchw)` | `item004-fixture-badfmt` |
| box 通道非 4*dfl | MODEL_MISMATCH | `branch[0].box channels=63 not 4*dfl` | selftest |
| int8 量化路径 | 1 框 + 对齐掩码 | `boxes=1 masks=1 positive=7040`、`px(600,240)=1` | `item004-fixture-int8` |
| 阈值来自配置 | `confidence_min=0.5` | `score_used=0.500000 (from thresholds.confidence_min)` | `item004-config` |
| core 单测（含分割后处理） | 全绿 | `tests=1059 failures=0` | `item004-core-tests-r1.xml` |
| 探针 selftest | 全绿 | `RESULT: PASS failures=0`（22 判例） | `item004-selftest-r1.log` |

## 2. 执行环境（实测）

| 项 | 值 |
|---|---|
| 目标 | RK3588，Linux 6.1.141 aarch64，8 核；主机指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU` |
| 编译器 / CMake | g++ 11.4.0 / cmake 3.22.1 |
| 输入 | 夹具张量（13 个 float32 / int8），letterbox 1280×720→640×640（scale=0.5, pad_y=140） |
| 入口 | `app/tools/board_vision_seg.py`（登记 Paramiko SSH；仅写 /tmp，结束清理） |

## 3. 解码约定

- NCHW 通道优先；13 = 3 分支 × {box(4*16 DFL logits), score(nc), score_sum(1), seg(32)} + proto(32×160×160)。
- DFL softmax 加权和 → 距离；`x1=(-d0+j+0.5)*stride`。
- 掩码 = per-box 32 系数 · proto；`> mask_threshold(0)` 为 1；默认按框裁剪；proto→model 双线性后逐原图像素逆 letterbox 采样。
- 输出每框独立二值 plane（0/1），`stride_bytes=width`，原图尺寸。

## 4. 性能/资源测量范围

本增量为解码正确性，**不测量** RKNN 前向耗时/内存/FPS（未执行前向）；也不做多框大批量解码计时。按框生成 640×640 浮点中间量（~1.6MB/框）与 921600B 掩码 plane，属实现特征；真实负载下的耗时/内存优化属 ITEM-009/执行轮。

## 5. 证据文件（`docs/vision/test_evidence/ITEM-004/`）

- `vision-item004-…-r1.transcript.json` / `.log` — 全流程命令/退出码/输出/主机指纹
- `…-r1.app_core-evidence_item004-core-tests-…-r1.xml` — core 单测 JUnit（1059/0）
- `…-r1.app_core-evidence_item004-selftest-…-r1.{log,json}` — 22 判例自检
- `…-r1.app_core-evidence_item004-fixture-{rail,zero_coeff,nocrop,nms,empty,badcount,badsegshape,badproto,badfmt,int8}-…-r1.{log,json}`
- `…-r1.app_core-evidence_item004-config-…-r1.{log,json}` — 配置阈值

## 6. 未执行 / 未声称

1. **未执行 RKNN 前向推理**：无真实图像输入的端到端掩码；夹具为手工编码张量。真实 `rknn_run` 与端到端掩码归 ITEM-009。
2. 未连接相机（ENV-D 未执行）；未做真实 rail 掩码质量/人工视觉确认（用户核验）。
3. 未实现同帧两阶段判定、结果发布、语义白名单、跟踪、P5 视图、测距。
4. 未测多框/大批量解码耗时与内存；未测掩码重采样在真实目标边界上的精度（夹具为阶跃图案）。
5. `crop_to_box`/`mask_threshold`/`nms_iou` 为参数（可配，有溯源）；score 阈值产品路径取 `thresholds.confidence_min`。
6. 本结果不替代验证能力的独立复现；夹具 pass ≠ 真实模型掩码质量。
