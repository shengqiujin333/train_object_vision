#pragma once

// railhmi-media: NV12 → BGR888 格式转换（视频 ITEM-005，target 专用）。
//
// 在取流 worker 内用 librga imcvtcolor(RK_FORMAT_YCbCr_420_SP → RK_FORMAT_BGR_888,
// IM_YUV_TO_RGB_BT601_FULL)；按 caps 处理列/行 padding（ystride / uv_offset / hstride）
// 与 MPP 输出对齐；接受 IM_STATUS_SUCCESS 与 IM_STATUS_NOERROR 两个成功码；
// 不使用 rgaconvert 插件。失败返回已脱敏原因，不静默丢帧（帧仍由租约/缓冲区持有）。
//
// 公开头不包含 GStreamer/librga（BgrImage 为纯数据）。

#include <cstdint>
#include <string>
#include <vector>

#include "railhmi/media/frame_lease.h"

namespace railhmi {
namespace media {

struct BgrImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> data;  // BGR888，width*height*3
};

// RGA 垂直跨度（Y 平面行数到 UV 平面起点）：hstride = uv_offset / ystride。
// ystride<=0、uv_offset==0 或 uv_offset 非 ystride 整数倍时返回 -1。
int rga_hstride(int ystride, std::uint64_t uv_offset);

// 原生 NV12（可带 padding）-> BGR888。err 为已脱敏原因。
bool nv12_to_bgr_raw(const std::uint8_t* nv12, int width, int height, int ystride,
                     std::uint64_t uv_offset, BgrImage* out, std::string* err);

// 从帧租约转换：要求 descriptor.format==NV12 且两平面；使用其 planes[0].stride_bytes
// 与 planes[1].offset。租约无效返回 false 且 err="NOT_READY"。
bool nv12_to_bgr(const FrameLease& lease, BgrImage* out, std::string* err);

// BGR 三通道均值（可见光判据：近似相等）与 spread = max(mean)-min(mean)。
bool bgr_channel_means(const BgrImage& img, double* b, double* g, double* r);
double bgr_channel_spread(const BgrImage& img);

}  // namespace media
}  // namespace railhmi
