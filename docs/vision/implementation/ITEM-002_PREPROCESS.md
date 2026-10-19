# 视觉推理 ITEM-002 实现 — 帧租约消费与预处理

- Capability: `computer_vision_engineer.vision_inference_implementation`
- Work item: `TL-2253ff62-6914-4107-9b3f-56d1d6d04814-tasks-ITEM-002`
- 版本: v1（2026-10-19）
- 上游: `docs/vision/VISION_DESIGN.md` §7.1、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0 §5/§6、`docs/vision/VISION_TEST_DESIGN.md` G2（TC-T-PRE-01..07 / FI-AI-04/05）、`docs/video/VIDEO_SYSTEM_DESIGN.md` §2、ITEM-001 实现（输入张量 dtype/scale/zp 来源）
- 范围: 只实现 ITEM-002（帧租约消费与预处理）。未实现检测/分割后处理、同帧判定、结果发布、语义接入、线程调度（后续 ITEM）。

## 1. 交付内容

| 交付 | 路径 | 说明 |
|---|---|---|
| 预处理纯逻辑 | `app/src/core/include/railhmi/core/vision_preprocess.h`、`src/vision_preprocess.cpp` | letterbox 参数、双线性采样（带 stride）、BGR→RGB、`(v-mean)/std` 归一化、按 dtype/scale/zp 量化、NV12 参考转换；非法输入返回稳定 reason。仅标准库。 |
| 帧租约消费 | `app/src/media/include/railhmi/media/frame_preprocess.h`、`src/frame_preprocess.cpp` | `FramePreprocessor::prepare(const FrameLease&)`：layout 校验 → NV12 走 librga → 交 core 预处理；空租约/未映射平面/非法格式显式拒绝并计数。**不提供 FrameRef 重载**。 |
| 探针 | `app/src/media/tools/preprocess_probe.cpp`（`railhmi-vision-preprocess`） | `--selftest`（24 判例）、`--fixture bgr|nv12`（合成图 + padding）、`--source test|file|rtsp`（真实 FrameLease + 耗时）。 |
| 单元测试 | `app/tests/core/test_vision_preprocess.cpp` | letterbox/量化/RGB 顺序/stride/非法输入/NV12 参考/float32/uint8。 |
| 板端入口 | `app/tools/board_vision_preprocess.py` | 一次有界会话：CoreTests→MediaCheck→selftest→fixture bgr/nv12→真实租约→取回证据→清理。 |

构建接入：`app/src/core/CMakeLists.txt`、`app/tests/core/CMakeLists.txt`、`app/tests/core/main.cpp`、`app/src/media/CMakeLists.txt`（仅新增条目）。

## 2. 如何满足工作项

| 要求 | 实现 | 证据 |
|---|---|---|
| 从 `FrameLease`/`FrameDescriptor` 取像素 | `FramePreprocessor::prepare(const FrameLease&)` 是唯一像素入口；读 `descriptor().layout` 的 width/height/format/planes，像素经 `plane_data(i)`，仅租约存活期有效 | `item002-lease-source`：真实 lease `format=nv12 size=1280x720 plane0_stride=1280 plane1_offset=921600 memory_kind=cpu` |
| NV12 走已有 librga 转 BGR | `prefer_rga_for_nv12=true` 调 `media::nv12_to_bgr(lease,…)`（视频 ITEM-005 的 BT601_FULL 路径，接受两个成功码） | 真实租约路径 prepared=101/101 |
| BGR→RGB 仅在适配器内显式转换 | core `convert_to_rgb` 在采样后交换 B/R；源顺序由 `ChannelOrder` 显式给出 | 纯红 BGR → 通道均值 R=65.3 vs B=−78.1（`item002-fixture-bgr`） |
| letterbox 等比缩放并记录 scale/pad | `compute_letterbox`：scale=min(…)，resized=round，补边居中 | 1280×720→640×640：`scale=0.5 resized=640x360 pad_x=0 pad_y=140`（bgr 与 nv12 一致） |
| 按 dtype/scale/zp 写量化值 | `quantize_value`：`q=clip(round(((v-mean)/std)/scale)+zp)`；`mean=0/std=255` | tensor `640x640x3 int8 zp=-128 scale=0.0039215689`，vmin=-128/vmax=127 |
| 禁止 FrameRef 推像素 / 复用显示缓冲裸地址 | 无 FrameRef 重载；像素只经 `plane_data`，立即拷贝进自有张量，不保留指针 | 空租约 → `NOT_READY`（`empty_lease_rejected_not_ready`） |
| 非法格式/stride/尺寸显式错误不静默丢帧 | `PRE_STRIDE_TOO_SMALL`/`PRE_INVALID_DIMENSIONS`/`PRE_NULL_BUFFER`/`PRE_UNSUPPORTED_FORMAT`/`PRE_BAD_CONFIG`/`PRE_CONVERSION_FAILED`/`PRE_UNMAPPED`；`rejected` 计数按 reason | selftest 逐条拒绝判例 + `type=reject_reasons NOT_READY=1` |
| 已知合成图 + 带 padding NV12 输出正确 | `--fixture bgr --stride 4000`、`--fixture nv12 --stride 1296` | 两份 fixture 证据；stride padding 哨兵未被当像素（`stride_padding_not_read_as_pixels`） |

实现要点：

- **量化写入**：单字节 int8/uint8（two's complement）或 4 字节 float32；量化范围按 dtype 裁剪。
- **补边值**：`pad_value=114`（YOLO 灰边），量化后字节 = `q(114/255)= -14`（byte 242），selftest 锚定。
- **stride 安全**：显式校验 `src_stride>=w*3`（packed）/`y_stride>=w`、`uv_stride>=w`（NV12）；夹具构造也防止小 stride 越界。
- **NV12 参考实现**：core 提供带 stride 的参考转换（full-range BT.601），用于夹具/一致性校验/无 RGA 环境；**板端主链路仍是 librga**（真实租约路径已证）。

## 3. 运行入口

```
cd app && bash build.sh CoreTests && ./build-core/tests/railhmi-core-tests
./build-media/tools/railhmi-vision-preprocess --selftest --out selftest.log --json selftest.json
./build-media/tools/railhmi-vision-preprocess --fixture bgr  --src 1280x720 --stride 4000 --color red \
    --target 640x640 --dtype int8 --scale 0.003921568627 --zp -128 --out bgr.log
./build-media/tools/railhmi-vision-preprocess --fixture nv12 --src 1280x720 --stride 1296 --color red \
    --target 640x640 --dtype int8 --scale 0.003921568627 --zp -128 --out nv12.log
./build-media/tools/railhmi-vision-preprocess --source test --src 1280x720 --duration 4 \
    --target 640x640 --dtype int8 --scale 0.003921568627 --zp -128 --out lease.log
```

## 4. 返工记录（保留失败证据）

| 轮次 | 现象 | 根因 | 修复 | 复核 |
|---|---|---|---|---|
| r1 | core-tests 1000/3：`rgb order R>B` R=−129 B=0；`nv12 rgb order`；`rgb source preserves order` | `write_value` 把三通道都累加进 `sum[0]`（应为 `sum[channel_index%3]`），通道均值错误 | 改为按通道累加 | r2 起 core-tests 1003/0 |
| r1 | media-check 编译失败：`preprocess_reason has not been declared` | 命名空间在匿名 namespace 内 using 不可用于外层函数 | 移入 `railhmi::media` 作用域 | — |
| r2 | media-check 编译失败：`using-declaration may not name namespace` | `preprocess_reason` 是命名空间不是类型 | 改用 `namespace preprocess_reason = railhmi::core::preprocess_reason;` | r3 build-media=0 |
| r3 | 探针 selftest `SELFTEST_JSON_EXIT=1`（`tensor_size_w*h*3`） | 判例拿 **源** 尺寸比张量，实际 target=640×640 | 改用 `in.width*in.height*3` | r4 selftest=0 |

## 5. 观察与移交

1. **预处理耗时（板端实测，含 librga NV12→BGR + letterbox + 量化）**：p50 ≈ 22.3 ms、p95 ≈ 23.9 ms/帧（1280×720→640×640，test 源）。这是**当前实现**的真实数值，不是目标性能结论。当前标量双线性 letterbox 是主要成本；后续可优化（RGA 直接缩放、定点/查表双线性、或复用视频层已转 BGR 结果），并需按 4 路推理预算实测。属 ITEM-009/执行轮。
2. **与测试设计的数值差异（需测试侧注意）**：`VISION_TEST_DESIGN.md` TC-T-PRE-01 写「1280×720→640×640 → pad_y=80」。按居中 letterbox 数学，`scale=0.5`、`resized=640×360`、`pad_y=(640−360)/2=140`；`pad_y=80` 与任何等比居中约定都不自洽（80 对应 resized=480，则宽度=853>640）。本实现采用正确居中值 140 并在证据中锚定。
3. **检测器接入**：`FramePreprocessor` 产出 `core::PreprocessedInput`（HWC int8 张量 + letterbox 参数）；把张量喂给 RKNN 的 Detector 属 ITEM-003/004（后处理）与 ITEM-009（线程/调度接入）。
4. 未实现：检测/分割后处理、同帧两阶段、InferenceEnvelope 发布、语义白名单、跟踪、P5 视图、测距（后续 ITEM）。
