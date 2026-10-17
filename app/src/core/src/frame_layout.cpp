#include "railhmi/core/frame_layout.h"

namespace railhmi {
namespace core {

const char* to_string(PixelFormat f) {
    switch (f) {
        case PixelFormat::Nv12: return "NV12";
        case PixelFormat::Bgr888: return "BGR888";
        case PixelFormat::Rgb888: return "RGB888";
    }
    return "?";
}

bool parse_pixel_format(const std::string& s, PixelFormat* out) {
    if (s == "NV12") { *out = PixelFormat::Nv12; return true; }
    if (s == "BGR888") { *out = PixelFormat::Bgr888; return true; }
    if (s == "RGB888") { *out = PixelFormat::Rgb888; return true; }
    return false;
}

const char* to_string(LayoutStatus s) {
    switch (s) {
        case LayoutStatus::Ok: return "ok";
        case LayoutStatus::InvalidDimensions: return "invalid_dimensions";
        case LayoutStatus::UnsupportedFormat: return "unsupported_format";
        case LayoutStatus::OddDimensions: return "odd_dimensions";
        case LayoutStatus::StrideTooSmall: return "stride_too_small";
        case LayoutStatus::PlaneTooSmall: return "plane_too_small";
        case LayoutStatus::OverlappingPlanes: return "overlapping_planes";
        case LayoutStatus::ExceedsBuffer: return "exceeds_buffer";
        case LayoutStatus::TooManyPlanes: return "too_many_planes";
    }
    return "?";
}

namespace {

bool ranges_overlap(std::uint64_t a0, std::uint64_t a1, std::uint64_t b0, std::uint64_t b1) {
    return a0 < b1 && b0 < a1;
}

}  // namespace

LayoutStatus make_nv12_layout(int width, int height, std::uint64_t stride_y,
                              std::uint64_t stride_uv, std::uint64_t uv_offset,
                              FrameLayout* out) {
    if (!out) return LayoutStatus::InvalidDimensions;
    if (width <= 0 || height <= 0) return LayoutStatus::InvalidDimensions;
    if (width % 2 != 0 || height % 2 != 0) return LayoutStatus::OddDimensions;
    if (stride_y < static_cast<std::uint64_t>(width)) return LayoutStatus::StrideTooSmall;
    if (stride_uv < static_cast<std::uint64_t>(width)) return LayoutStatus::StrideTooSmall;
    const std::uint64_t rows_uv = static_cast<std::uint64_t>(height) / 2;
    const std::uint64_t y_size = stride_y * static_cast<std::uint64_t>(height);
    const std::uint64_t uv_size = stride_uv * rows_uv;
    if (uv_offset < y_size) return LayoutStatus::OverlappingPlanes;
    out->width = width;
    out->height = height;
    out->format = PixelFormat::Nv12;
    out->planes.clear();
    out->planes.push_back(PlaneLayout{0, stride_y, y_size});
    out->planes.push_back(PlaneLayout{uv_offset, stride_uv, uv_size});
    out->buffer_bytes = uv_offset + uv_size;
    return validate_layout(*out);
}

LayoutStatus make_packed_layout(PixelFormat format, int width, int height,
                                std::uint64_t stride, FrameLayout* out) {
    if (!out) return LayoutStatus::InvalidDimensions;
    if (format != PixelFormat::Bgr888 && format != PixelFormat::Rgb888) {
        return LayoutStatus::UnsupportedFormat;
    }
    if (width <= 0 || height <= 0) return LayoutStatus::InvalidDimensions;
    const std::uint64_t min_stride = static_cast<std::uint64_t>(width) * 3u;
    if (stride < min_stride) return LayoutStatus::StrideTooSmall;
    const std::uint64_t size = stride * static_cast<std::uint64_t>(height);
    out->width = width;
    out->height = height;
    out->format = format;
    out->planes.clear();
    out->planes.push_back(PlaneLayout{0, stride, size});
    out->buffer_bytes = size;
    return validate_layout(*out);
}

LayoutStatus validate_layout(const FrameLayout& l) {
    if (l.width <= 0 || l.height <= 0) return LayoutStatus::InvalidDimensions;
    if (l.planes.empty() || l.planes.size() > 2) return LayoutStatus::TooManyPlanes;
    if (l.buffer_bytes == 0) return LayoutStatus::PlaneTooSmall;

    const std::uint64_t w = static_cast<std::uint64_t>(l.width);
    const std::uint64_t h = static_cast<std::uint64_t>(l.height);

    for (const auto& p : l.planes) {
        if (p.stride_bytes == 0) return LayoutStatus::StrideTooSmall;
        if (p.size_bytes == 0) return LayoutStatus::PlaneTooSmall;
        if (p.offset + p.size_bytes > l.buffer_bytes) return LayoutStatus::ExceedsBuffer;
    }

    switch (l.format) {
        case PixelFormat::Nv12: {
            if (l.width % 2 != 0 || l.height % 2 != 0) return LayoutStatus::OddDimensions;
            if (l.planes.size() != 2) return LayoutStatus::TooManyPlanes;
            const PlaneLayout& y = l.planes[0];
            const PlaneLayout& uv = l.planes[1];
            if (y.stride_bytes < w || uv.stride_bytes < w) return LayoutStatus::StrideTooSmall;
            if (y.size_bytes < y.stride_bytes * h) return LayoutStatus::PlaneTooSmall;
            if (uv.size_bytes < uv.stride_bytes * (h / 2)) return LayoutStatus::PlaneTooSmall;
            if (ranges_overlap(y.offset, y.offset + y.size_bytes, uv.offset,
                               uv.offset + uv.size_bytes)) {
                return LayoutStatus::OverlappingPlanes;
            }
            break;
        }
        case PixelFormat::Bgr888:
        case PixelFormat::Rgb888: {
            if (l.planes.size() != 1) return LayoutStatus::TooManyPlanes;
            const PlaneLayout& p = l.planes[0];
            if (p.stride_bytes < w * 3u) return LayoutStatus::StrideTooSmall;
            if (p.size_bytes < p.stride_bytes * h) return LayoutStatus::PlaneTooSmall;
            break;
        }
    }
    return LayoutStatus::Ok;
}

}  // namespace core
}  // namespace railhmi
