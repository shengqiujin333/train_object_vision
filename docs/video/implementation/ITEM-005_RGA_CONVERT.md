# NV12→BGR(librga) 格式转换 — 实现交付（VideoSystemImplementation, ITEM-005）

- Artifact: VideoSystemImplementation（视频代码配置及执行入口）
- Capability: `video_systems_engineer.video_system_implementation`
- 工作项: 视频任务 ITEM-005「librga NV12→BGR 格式转换」
- 上游: `docs/video/VIDEO_SYSTEM_DESIGN.md`、`docs/interfaces/INTERFACE_CONTRACT.md` v1.0.0（RGA/色彩）、`docs/video/VIDEO_TEST_DESIGN.md`（G3/TC-V-RGA-01..04）、历史 `code/main.cc::nv12_to_bgr_rga`
- 基线 commit: `0055fc3`（本次新增/修改文件见下；Runtime 负责交接提交）
- 日期: 2026-10-17

## 1. 交付内容

| 文件 | 状态 | sha256(16) | 说明 |
|---|---|---|---|
| `app/src/media/include/railhmi/media/rga_convert.h` | 新增 | `42ae8aee7684f121` | `BgrImage` + `rga_hstride`/`nv12_to_bgr_raw`/`nv12_to_bgr`/`bgr_channel_means`/`bgr_channel_spread`（GStreamer-free 公开头） |
| `app/src/media/src/rga_convert.cpp` | 新增 | `8eb0ab97f6eca317` | librga `imcvtcolor(BT601_FULL)` 实现；padding（ystride/uv_offset/hstride）；双成功码 |
| `app/src/media/include/railhmi/media/camera_stream.h` | 修改 | `6a809a7462978b15` | `CameraStreamConfig.convert_bgr` + `last_bgr/bgr_converted/bgr_failed/bgr_last_error` |
| `app/src/media/src/camera_stream.cpp` | 修改 | `de4d5a5542c28692` | 取流 worker 内 NV12→BGR；失败记脱敏错误、不丢帧 |
| `app/src/media/tools/rga_convert_probe.cpp` | 新增 | `72ace0f37a09c749` | 运行入口 `railhmi-rga-convert`（合成 padded/纯蓝、失败路径、worker 转换统计、`--selftest`） |
| `app/tools/board_rga_convert_check.py` | 新增 | `c1367fd096db51f4` | 板端执行入口（MediaCheck + selftest + test/rtsp 运行） |

## 2. 契约与行为

1. **转换调用**：`imcvtcolor(RK_FORMAT_YCbCr_420_SP → RK_FORMAT_BGR_888, IM_YUV_TO_RGB_BT601_FULL)`；**接受 `IM_STATUS_SUCCESS` 与 `IM_STATUS_NOERROR` 两个成功码**；**不使用 `rgaconvert` 插件**（selftest 断言）。
2. **padding / MPP 对齐处理**：
   - `ystride = planes[0].stride_bytes`（列 padding）；
   - `uv_offset = planes[1].offset`（行/平面 padding，非连续 `width*height`）；
   - `hstride = uv_offset / ystride`（`rga_hstride`；非整数倍或 ystride≤0 显式拒绝）。
   这与历史 `code/main.cc::nv12_to_bgr_rga` 的 `hstride = uv_offset/ystride` 一致。
3. **显式校验**：空指针、非法/奇数宽高、`stride<width`、`uv_offset` 非整数倍/不足 高度、非 NV12 描述符、空租约（`NOT_READY`）均返回 `false` 且给出稳定原因 token，**不静默丢帧**。
4. **worker 内转换**：`CameraStreamConfig.convert_bgr=true` 时，取流 worker 每帧在发布租约后转换；成功累加 `bgr_converted`、失败累加 `bgr_failed` 并记录 `bgr_last_error`（经 `redact_secret`）；帧本身仍正常发布/呈现。
5. **可见光判据**：中性内容转换后 BGR 三通道均值近似相等（`bgr_channel_spread` 观测）；彩色内容不做等值断言，仅做方向性检查（纯蓝 B 主导，防绿帧回归）。
6. **与 core 的边界**：core 仍只见 `FrameDescriptor`（NV12 元数据）；`BgrImage` 是 media 内部/消费者数据，BNR 转换不改变帧身份/时间。

## 3. 运行入口

```bash
app/build.sh MediaCheck
build-media/tools/railhmi-rga-convert --selftest
build-media/tools/railhmi-rga-convert --source test --duration 3 --out rga.log
build-media/tools/railhmi-rga-convert --source rtsp --url 'rtsp://<cam>/…' --user admin \
    --password-env TRAIN_FRONT_RTSP_CREDENTIAL --duration 3
```

板端一键：
```bash
python app/tools/board_rga_convert_check.py --host <addr> --user <user> \
    --outdir docs/video/test_evidence/ITEM-005 --stamp <tag>
```

## 4. 板端验证结果（RK3588）

- `railhmi-rga-convert --selftest`：**8/8 PASS**（`hstride_padded/hstride_contiguous/hstride_non_multiple_rejected/hstride_zero_stride_rejected/no_rgaconvert_plugin/auto_uses_decodebin/explicit_uses_mppvideodec/explicit_no_rgaconvert`）。
- 合成 padded NV12 灰图（stride=1408、uv_offset=1408×720、padding 列=0）：`b=g=r=200 spread=0 min_px=200 max_px=200 padding_not_leaked=true` → 行 padding 未泄漏。
- 合成纯蓝 NV12：`b=239 g=15 r=15 blue_dominant=true` → 无绿帧回归。
- 失败路径：`null_arg / odd_dimensions / NOT_READY` 全部显式拒绝、无静默。
- worker 内转换：
  - test 源：`published=76 bgr_converted=76 bgr_failed=0 bgr_means=138/123/124 spread=16`（videotestsrc 彩条为非中性内容）；
  - 真实 RTSP CAM-01：`published=72 bgr_converted=72 bgr_failed=0 bgr_means=119/111/106 spread=12`。
- 失败不丢帧：`published_before=76 → published_after=86 still_producing=true`。
- 证据：`docs/video/test_evidence/ITEM-005/ITEM-005-ENV-B-C-D-20261017-r1.md` 与同目录原始日志。

## 5. 调试过程（本轮）

| 观察 | 根因 | 处理 |
|---|---|---|
| 首次板端构建失败：`im2d_buffer.h` 宏内 `memset` 未声明 | `wrapbuffer_virtualaddr` 宏使用 `memset`，需在 `<im2d.h>` 前引入 `<cstring>` | `rga_convert.cpp` 顶部加 `#include <cstring>` |
| selftest `uses_mppvideodec` FAIL | auto 路径用 `decodebin`（运行时选 mppvideodec），字符串不含 `mppvideodec` | selftest 改为：auto 含 `decodebin`、显式 H265 含 `mppvideodec`、两者均无 `rgaconvert` |

## 6. 边界与移交

- 本项交付 **NV12→BGR 转换**；显示/推理/快照消费者（ITEM-008/009/010）在此之上取 `BgrImage`；转换不改变帧身份/时间。
- `colorimetry` 归一与 BT601/BT709 选择：本项目优先复用 BT601_FULL 基线；若 caps 明确非 BT601，应显式报告/转换（本项记录 caps 归一 token，不静默改写）。
- 不修改 core/wire 契约、SQLite DDL、既有 Qt 桥接签名；未新增配置字段（`convert_bgr` 为媒体配置项）。
