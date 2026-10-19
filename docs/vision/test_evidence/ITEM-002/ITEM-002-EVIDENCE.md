# ITEM-002 InferenceEvidence — 帧租约消费与预处理

- Evidence type: InferenceEvidence
- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-002`
- 日期: 2026-10-19
- 权威轮次: **r4**（`20261019-item002-r4`）；r1/r2/r3 为返工前证据（保留）
- 事实口径: 下列均为本次在 RK3588 上真实执行或夹具确定性输出；未执行项见 §6。

## 1. 结论（本次验收范围）

| 用例 | 期望 | 实测 | 证据 |
|---|---|---|---|
| letterbox 参数（1280×720→640×640） | 等比 + 居中补边，记录 scale/pad | `scale=0.5 resized=640x360 pad_x=0 pad_y=140` | selftest / fixture（bgr+nv12 一致） |
| 合成 BGR 图输出尺寸 | 640×640×3 | `bytes=1228800` | `item002-fixture-bgr` |
| BGR→RGB 显式转换 | 纯红 R 通道均值 > B | `means=[65.312,-78.125,-78.125]` | `item002-fixture-bgr` |
| 带 padding NV12（y/uv stride=1296） | 输出尺寸/letterbox 正确 | `tensor=640x640x3 ... pad_y=140`，R 均值 64.75 > B | `item002-fixture-nv12` |
| 量化范围 | 限定 int8，符合 (v/255)/scale+zp | `vmin=-128 vmax=127`，`scale=0.0039215689 zp=-128` | fixture / lease |
| 真实 FrameLease（含 librga NV12→BGR） | 预处理成功且形状正确 | `format=nv12 size=1280x720 plane0_stride=1280 plane1_offset=921600 memory_kind=cpu` → tensor 640×640×3；prepared=101/101 | `item002-lease-source` |
| 空租约拒绝（禁止元数据推像素） | NOT_READY，不产伪张量 | `[PASS] empty_lease_rejected_not_ready :: NOT_READY` | `item002-lease-source` |
| 非法 stride/尺寸/空指针/配置 | 显式 reason，不静默丢帧 | `PRE_STRIDE_TOO_SMALL` / `PRE_INVALID_DIMENSIONS` / `PRE_NULL_BUFFER` / `PRE_BAD_CONFIG` | selftest 逐条 |
| NV12 非连续 stride 不读 padding | 与紧排结果一致 | `[PASS] stride_padding_not_read_as_pixels` | selftest |
| 补边为 pad_value | 首像素 = q(114) | `byte=242 expect=242` | selftest |
| core 单测（含新预处理测试） | 全绿 | `tests=1003 failures=0` | `item002-core-tests-r4.xml` |
| 探针 selftest | 全绿 | `RESULT: PASS failures=0`（24 判例） | `item002-selftest-r4.log` |

## 2. 执行环境（实测）

| 项 | 值 |
|---|---|
| 目标 | RK3588，Linux 6.1.141 aarch64，8 核；主机指纹 `SHA256:NzHFHZ0Z9qmYDIyiTViAOUQrXQRKa1gNijyPceNZ5qU` |
| 编译器 / CMake | g++ 11.4.0 / cmake 3.22.1 |
| 取流源 | `videotestsrc`（test 源）1280×720，经 pipeline_factory 统一为 NV12；origin=simulation |
| 入口 | `app/tools/board_vision_preprocess.py`（登记 Paramiko SSH；仅写 /tmp，结束清理） |

## 3. 预处理耗时（板端实测，真实租约路径）

| 指标 | 值 | 说明 |
|---|---|---|
| 每帧预处理 p50 | **22.3 ms** | 含 librga NV12→BGR(1280×720) + 标量双线性 letterbox→640×640 + 归一化/量化 |
| p95 / max | 23.9 ms / 24.6 ms | 101 帧样本 |
| 帧数 | prepared=101/101 | 无失败、无静默丢帧 |

**范围声明**：这是当前实现的真实数值，**不是**目标性能结论，也不含 RKNN 前向。当前标量 letterbox 是主要成本；优化（RGA 缩放/定点双线性/复用已转 BGR）与 4 路推理预算实测属 ITEM-009/执行轮。未测内存占用。

## 4. 关键实现事实

- 唯一像素入口 `FramePreprocessor::prepare(const FrameLease&)`；无 FrameRef 重载。
- NV12 主链路：`media::nv12_to_bgr`（librga BT601_FULL，视频 ITEM-005）；core 另有带 stride 的**参考** NV12 转换用于夹具。
- 量化：`q = clip(round(((v-mean)/std)/scale)+zp)`，`mean=0/std=255`；int8 写 two's complement 字节。
- 补边 `pad_value=114`；letterbox 居中，`src_stride>=w*3`/`y_stride>=w`,`uv_stride>=w` 显式校验。

## 5. 证据文件（`docs/vision/test_evidence/ITEM-002/`）

r4（权威）：
- `vision-item002-…-r4.transcript.json` / `.log` — 全流程命令/退出码/输出/指纹
- `…-r4.app_core-evidence_item002-core-tests-…-r4.xml` — core 单测 JUnit（1003/0）
- `…-r4.app_core-evidence_item002-selftest-…-r4.log` / `.json` — 24 判例自检
- `…-r4.app_core-evidence_item002-fixture-bgr-…-r4.log` / `.json`
- `…-r4.app_core-evidence_item002-fixture-nv12-…-r4.log` / `.json`
- `…-r4.app_core-evidence_item002-lease-source-…-r4.log` / `.json` — 真实租约 + librga + 耗时

返工前证据（保留）：r1 transcript/log + core-tests xml（`sum[0]` 通道均值缺陷 1000/3、media 编译失败）、r2 transcript/log（命名空间 using 编译错误）、r3 transcript/log 与全部产物（探针 `tensor_size` 判例错误）。

## 6. 未执行 / 未声称

1. 未执行 RKNN 前向推理（无 box/mask 输出）；未做检测/分割后处理（后续 ITEM）。
2. 未连接真实相机（ENV-D 未执行）；`--source rtsp` 未运行；只用 test 源。
3. 未做 4 路并发预处理/推理预算、内存与热稳态测量；§3 为单路 test 源数值。
4. 未做人工视觉确认（本增量无画面内容需核验）。
5. `VISION_TEST_DESIGN.md` TC-T-PRE-01 的 `pad_y=80` 与居中 letterbox 数学不符（正确 140）；本实现采用 140 并在证据锚定，差异已在实现文档 §5 记录。
6. 本结果不替代验证能力的独立复现。
