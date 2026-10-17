#pragma once

// railhmi-core: 帧平面布局与校验（视频 ITEM-004 的纯逻辑部分）。
//
// 依据 caps 推导的 width/height/format/planes(offset,stride_bytes,size_bytes)/buffer_bytes，
// 并做显式校验：仅支持 NV12 / BGR888 / RGB888；NV12 需偶数宽高、stride>=width、
// Y/UV 平面足量且不重叠；packed 需 stride>=width*3；offset+size<=buffer_bytes。
// 纯标准库（可最小工具链单测）；不含 GStreamer/相机。

#include <cstdint>
#include <string>
#include <vector>

namespace railhmi {
namespace core {

enum class PixelFormat { Nv12, Bgr888, Rgb888 };
const char* to_string(PixelFormat f);
bool parse_pixel_format(const std::string& s, PixelFormat* out);

struct PlaneLayout {
    std::uint64_t offset = 0;        // 相对 buffer 起点的字节偏移
    std::uint64_t stride_bytes = 0;  // 每行字节数（>= 有效宽度）
    std::uint64_t size_bytes = 0;    // 该平面占用字节（>= stride*rows）
};

enum class LayoutStatus {
    Ok,
    InvalidDimensions,
    UnsupportedFormat,
    OddDimensions,
    StrideTooSmall,
    PlaneTooSmall,
    OverlappingPlanes,
    ExceedsBuffer,
    TooManyPlanes,
};
const char* to_string(LayoutStatus s);

struct FrameLayout {
    int width = 0;
    int height = 0;
    PixelFormat format = PixelFormat::Nv12;
    std::vector<PlaneLayout> planes;
    std::uint64_t buffer_bytes = 0;
};

// 由 caps 参数构造 NV12 布局（两平面）。uv_offset 为 UV 平面相对 buffer 的字节偏移。
LayoutStatus make_nv12_layout(int width, int height, std::uint64_t stride_y,
                              std::uint64_t stride_uv, std::uint64_t uv_offset,
                              FrameLayout* out);

// packed BGR888/RGB888 单平面布局。
LayoutStatus make_packed_layout(PixelFormat format, int width, int height,
                                std::uint64_t stride, FrameLayout* out);

// 校验既有布局；返回首个不满足项（Ok 表示通过）。
LayoutStatus validate_layout(const FrameLayout& layout);

}  // namespace core
}  // namespace railhmi
