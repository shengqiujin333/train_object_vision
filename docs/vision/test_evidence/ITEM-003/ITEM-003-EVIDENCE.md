# ITEM-003 InferenceEvidence — YOLOv5 人形检测后处理

- Evidence type: InferenceEvidence
- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-003`
- 日期: 2026-10-19
- 权威轮次: **r3**（`20261019-item003-r3`）；r1/r2 为返工前证据（保留）
- 事实口径: 下列均为本次在 RK3588 上真实执行（夹具张量）；未执行项见 §6。

## 1. 结论（本次验收范围）

| 用例 | 期望 | 实测 | 证据 |
|---|---|---|---|
| 三尺度解码 + letterbox 逆变换 | 模型框 (200,300)-(264,364) → 原图归一化 | `xywh=(0.31250,0.44444,0.10000,0.17778) score=0.8100 cls=0 label=person` | `item003-fixture-person` |
| 分数 = obj*cls | 0.9×0.9=0.81 | `score=0.8100` | selftest / person |
| 非 person 类别丢弃并计数 | boxes=0，dropped_class≥1 | `boxes=0 dropped_class=1` | `item003-fixture-nonperson` |
| 空图 → 空框非错误 | ok=true, DETECT_EMPTY, boxes=0 | `ok=1 reason=DETECT_EMPTY boxes=0` | `item003-fixture-empty` |
| NMS 抑制重叠（阈值可配） | iou 0.45→1 框；0.99→2 框 | `boxes=1 suppressed=1` / `boxes=2` | `item003-fixture-nms` + selftest |
| 补边区框越界丢弃 | boxes=0，dropped_bounds≥1 | `dropped_bounds=1` | `item003-fixture-padbox` |
| 阈值来自配置 | `thresholds.confidence_min=0.5` | `score_used=0.500000 (from thresholds.confidence_min)`，`boxes=1` | `item003-config` |
| 量化路径（int8 zp=-128） | 同框解码，量化误差内 | `xywh=(0.31228,0.44409,0.10050,0.17831) score=0.8065` | `item003-fixture-int8` |
| 非法张量显式错误 | DET_BAD_TENSOR | `scale[0] channels=252 expected=255` | selftest |
| core 单测（含后处理测试） | 全绿 | `tests=1035 failures=0` | `item003-core-tests-r3.xml` |
| 探针 selftest | 全绿 | `RESULT: PASS failures=0`（19 判例） | `item003-selftest-r3.log` |

## 2. 执行环境（实测）

| 项 | 值 |
|---|---|
| 目标 | RK3588，Linux 6.1.141 aarch64，8 核；主机指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU` |
| 编译器 / CMake | g++ 11.4.0 / cmake 3.22.1 |
| 输入 | 夹具张量（float32 与 int8 两种），letterbox 1280×720→640×640（scale=0.5, pad_y=140） |
| 入口 | `app/tools/board_vision_detect.py`（登记 Paramiko SSH；仅写 /tmp，结束清理） |

## 3. 解码约定（与已部署历史解码一致）

- 输出 `[1,255,H,W]` 通道优先；`idx(a,ch)=(85a+ch)·grid_len + i·W + j`。
- 输出已含 sigmoid；`score=obj*cls`；`bx=(v·2−0.5+j)·stride`，`bw=(v·2)²·anchor_w`。
- anchors 为 yolov5s 标准集；NMS 按类别贪心、+1 像素 IoU。
- 逆 letterbox 后裁剪到原图，越界丢弃；分数降序；`max_detections` 截断。

## 4. 性能/资源测量范围

本增量为解码正确性，**不测量** RKNN 前向耗时/内存/FPS（未执行前向）。解码为纯 CPU 数学；容器为夹具张量。真实前向与端到端耗时属 ITEM-009/执行轮。

## 5. 证据文件（`docs/vision/test_evidence/ITEM-003/`）

r3（权威）：
- `vision-item003-…-r3.transcript.json` / `.log` — 全流程命令/退出码/输出/指纹
- `…-r3.app_core-evidence_item003-core-tests-…-r3.xml` — core 单测 JUnit（1035/0）
- `…-r3.app_core-evidence_item003-selftest-…-r3.{log,json}` — 19 判例自检
- `…-r3.app_core-evidence_item003-fixture-{person,nonperson,nms,empty,padbox,int8}-…-r3.{log,json}`
- `…-r3.app_core-evidence_item003-config-…-r3.{log,json}` — 配置阈值

返工前证据（保留）：r1 transcript/log（`zp` 字段编译失败）、r2 transcript/log + 17 份产物（补边区夹具缺陷 + int8 零填充缺陷；core-tests 1035/3）。

## 6. 未执行 / 未声称

1. **未执行 RKNN 前向推理**：无真实图像输入的端到端 box/mask；夹具为手工编码张量。真实 `rknn_run` 与端到端检测归 ITEM-009。
2. 未连接相机（ENV-D 未执行）；未测真实检出率/误报（属模型精度与现场，用户核验）。
3. 未做分割后处理、同帧两阶段、结果发布、语义白名单、跟踪、P5 视图、测距。
4. 未测多尺度/大批量解码耗时与内存；未测 `max_detections` 之外的实际目标数量分布。
5. `nms_iou` 无配置字段，以参数传入（探针默认 0.45，证据记录）；score 阈值取自 `thresholds.confidence_min`。
6. 本结果不替代验证能力的独立复现。
