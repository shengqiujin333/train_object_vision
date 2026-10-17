#include "railhmi/media/rga_convert.h"

#include <cstring>  // im2d_buffer.h 宏使用 memset，需在其之前引入

#include <im2d.h>
#include <RgaUtils.h>

#include <algorithm>
#include <cstddef>

namespace railhmi {
namespace media {

int rga_hstride(int ystride, std::uint64_t uv_offset) {
    if (ystride <= 0 || uv_offset == 0) return -1;
    if (uv_offset % static_cast<std::uint64_t>(ystride) != 0) return -1;
    return static_cast<int>(uv_offset / static_cast<std::uint64_t>(ystride));
}

bool nv12_to_bgr_raw(const std::uint8_t* nv12, int width, int height, int ystride,
                     std::uint64_t uv_offset, BgrImage* out, std::string* err) {
    if (err) err->clear();
    if (!nv12 || !out) {
        if (err) *err = "null_arg";
        return false;
    }
    if (width <= 0 || height <= 0) {
        if (err) *err = "invalid_dimensions";
        return false;
    }
    if (width % 2 != 0 || height % 2 != 0) {
        if (err) *err = "odd_dimensions";
        return false;
    }
    if (ystride < width) {
        if (err) *err = "stride_too_small";
        return false;
    }
    const int hstride = rga_hstride(ystride, uv_offset);
    if (hstride < height) {
        if (err) *err = "invalid_uv_offset";
        return false;
    }

    out->width = width;
    out->height = height;
    out->data.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 3u, 0);

    rga_buffer_t src = wrapbuffer_virtualaddr(const_cast<std::uint8_t*>(nv12), width, height,
                                              RK_FORMAT_YCbCr_420_SP, ystride, hstride);
    rga_buffer_t dst = wrapbuffer_virtualaddr(out->data.data(), width, height, RK_FORMAT_BGR_888,
                                              width, height);
    const IM_STATUS st = imcvtcolor(src, dst, RK_FORMAT_YCbCr_420_SP, RK_FORMAT_BGR_888,
                                    IM_YUV_TO_RGB_BT601_FULL);
    // librga 有两个成功码：SUCCESS(1) 与 NOERROR(2)。
    if (st != IM_STATUS_SUCCESS && st != IM_STATUS_NOERROR) {
        if (err) *err = std::string("rga_error:") + imStrError(st);
        out->data.clear();
        out->width = 0;
        out->height = 0;
        return false;
    }
    return true;
}

bool nv12_to_bgr(const FrameLease& lease, BgrImage* out, std::string* err) {
    if (err) err->clear();
    if (!lease.valid()) {
        if (err) *err = "NOT_READY";
        return false;
    }
    const FrameDescriptor& d = lease.descriptor();
    if (d.layout.format != core::PixelFormat::Nv12) {
        if (err) *err = std::string("unsupported_format:") + core::to_string(d.layout.format);
        return false;
    }
    if (d.layout.planes.size() != 2) {
        if (err) *err = "invalid_planes";
        return false;
    }
    const std::uint8_t* base = lease.plane_data(0);
    if (!base) {
        if (err) *err = "plane_unavailable";
        return false;
    }
    return nv12_to_bgr_raw(base, d.layout.width, d.layout.height,
                           static_cast<int>(d.layout.planes[0].stride_bytes),
                           d.layout.planes[1].offset, out, err);
}

bool bgr_channel_means(const BgrImage& img, double* b, double* g, double* r) {
    const std::size_t px = static_cast<std::size_t>(img.width) * static_cast<std::size_t>(img.height);
    if (px == 0 || img.data.size() < px * 3u) return false;
    long long sb = 0, sg = 0, sr = 0;
    for (std::size_t i = 0; i < px; ++i) {
        sb += img.data[i * 3 + 0];
        sg += img.data[i * 3 + 1];
        sr += img.data[i * 3 + 2];
    }
    if (b) *b = static_cast<double>(sb) / static_cast<double>(px);
    if (g) *g = static_cast<double>(sg) / static_cast<double>(px);
    if (r) *r = static_cast<double>(sr) / static_cast<double>(px);
    return true;
}

double bgr_channel_spread(const BgrImage& img) {
    double b = 0, g = 0, r = 0;
    if (!bgr_channel_means(img, &b, &g, &r)) return -1.0;
    const double hi = std::max(b, std::max(g, r));
    const double lo = std::min(b, std::min(g, r));
    return hi - lo;
}

}  // namespace media
}  // namespace railhmi
