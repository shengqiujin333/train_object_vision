// railhmi-media: 帧租约消费与预处理实现（视觉推理 ITEM-002）。

#include "railhmi/media/frame_preprocess.h"

#include "railhmi/core/frame_layout.h"
#include "railhmi/media/rga_convert.h"

namespace railhmi {
namespace media {

using railhmi::core::LayoutStatus;
using railhmi::core::PixelFormat;
namespace preprocess_reason = railhmi::core::preprocess_reason;

namespace {

const char* format_name(PixelFormat f) {
    switch (f) {
        case PixelFormat::Nv12: return "nv12";
        case PixelFormat::Bgr888: return "bgr888";
        case PixelFormat::Rgb888: return "rgb888";
        default: return "unknown";
    }
}

const char* reason_for_layout(LayoutStatus s) {
    switch (s) {
        case LayoutStatus::Ok: return preprocess_reason::kOk();
        case LayoutStatus::InvalidDimensions:
        case LayoutStatus::OddDimensions: return preprocess_reason::kInvalidDimensions();
        case LayoutStatus::StrideTooSmall: return preprocess_reason::kStrideTooSmall();
        case LayoutStatus::UnsupportedFormat:
        case LayoutStatus::TooManyPlanes: return preprocess_reason::kUnsupportedFormat();
        case LayoutStatus::PlaneTooSmall:
        case LayoutStatus::OverlappingPlanes:
        case LayoutStatus::ExceedsBuffer: return preprocess_reason::kInvalidDimensions();
        default: return preprocess_reason::kUnsupportedFormat();
    }
}

}  // namespace

FramePreprocessor::FramePreprocessor(FramePreprocessConfig cfg) : cfg_(std::move(cfg)) {
    railhmi::core::normalize_input_spec(&cfg_.core.input);
}

LeasePreprocessResult FramePreprocessor::prepare(const FrameLease& lease) {
    LeasePreprocessResult r;
    auto reject = [&](const char* reason, const std::string& detail) {
        r.ok = false;
        r.reason = reason;
        r.detail = detail;
        ++rejected_;
        ++rejects_[r.reason];
        return r;
    };

    if (!lease.valid()) {
        r.source_format = "none";
        return reject(preprocess_reason::kNotReady(), std::string("lease status=") + lease.status());
    }
    const FrameDescriptor& d = lease.descriptor();
    r.source_format = format_name(d.layout.format);
    if (d.memory_kind != "cpu") {
        return reject(preprocess_reason::kUnsupportedFormat(),
                      "memory_kind=" + d.memory_kind + " (only cpu pixels are consumed)");
    }
    const LayoutStatus ls = railhmi::core::validate_layout(d.layout);
    if (ls != LayoutStatus::Ok) {
        return reject(reason_for_layout(ls), std::string("layout=") + railhmi::core::to_string(ls));
    }

    const int w = d.layout.width;
    const int h = d.layout.height;
    std::string err;
    bool ok = false;

    // 裁剪窗：只在“打包像素（RGA 转出来的 BGR）”路径上生效；NV12 直通路径不支持裁剪，
    // 此时显式回退为整帧（不静默改变语义，调用方可从结果里的 crop_w=0 看出来）。
    int cx = 0, cy = 0, cw = 0, ch = 0;
    const bool want_crop = cfg_.crop.enabled();
    auto apply_crop = [&](const std::uint8_t* base, int bw, int bh, int bstride,
                          railhmi::core::ChannelOrder order, bool* done) -> const std::uint8_t* {
        if (!want_crop) return base;
        if (!railhmi::core::crop_pixels(cfg_.crop, bw, bh, &cx, &cy, &cw, &ch)) return base;
        const int bpp = 3;
        *done = true;
        r.crop_x = cx;
        r.crop_y = cy;
        r.crop_w = cw;
        r.crop_h = ch;
        return base + static_cast<std::size_t>(cy) * bstride + static_cast<std::size_t>(cx) * bpp;
    };

    if (d.layout.format == PixelFormat::Nv12) {
        if (cfg_.prefer_rga_for_nv12) {
            BgrImage bgr;
            std::string cerr;
            if (!nv12_to_bgr(lease, &bgr, &cerr)) {
                // nv12_to_bgr 的错误已由视频层脱敏。
                const bool not_ready = (cerr == "NOT_READY");
                return reject(not_ready ? preprocess_reason::kNotReady()
                                        : preprocess_reason::kConversionFailed(),
                              cerr);
            }
            bool cropped = false;
            const std::uint8_t* base = apply_crop(
                bgr.data.data(), bgr.width, bgr.height, bgr.width * 3,
                railhmi::core::ChannelOrder::Bgr, &cropped);
            ok = railhmi::core::prepare_from_packed(
                base, cropped ? cw : bgr.width, cropped ? ch : bgr.height, bgr.width * 3,
                railhmi::core::ChannelOrder::Bgr, cfg_.core, &r.input, &err);
        } else {
            const std::uint8_t* y = lease.plane_data(0);
            const std::uint8_t* uv = lease.plane_data(1);
            if (!y || !uv) return reject(preprocess_reason::kUnmapped(), "NV12 plane not mapped");
            const int y_stride = static_cast<int>(d.layout.planes[0].stride_bytes);
            const int uv_stride = static_cast<int>(d.layout.planes[1].stride_bytes);
            ok = railhmi::core::prepare_from_nv12(y, uv, w, h, y_stride, uv_stride, cfg_.core,
                                                  &r.input, &err);
        }
    } else {
        const std::uint8_t* p = lease.plane_data(0);
        if (!p) return reject(preprocess_reason::kUnmapped(), "packed plane not mapped");
        const int stride = static_cast<int>(d.layout.planes[0].stride_bytes);
        const railhmi::core::ChannelOrder order = (d.layout.format == PixelFormat::Bgr888)
                                                      ? railhmi::core::ChannelOrder::Bgr
                                                      : railhmi::core::ChannelOrder::Rgb;
        ok = railhmi::core::prepare_from_packed(p, w, h, stride, order, cfg_.core, &r.input, &err);
    }

    if (!ok) {
        // err 形如 "PRE_XXX: detail"；拆出 token 便于计数，保留整条（已脱敏）。
        const std::size_t sp = err.find(':');
        const std::string token = (sp == std::string::npos) ? err : err.substr(0, sp);
        const char* reason = preprocess_reason::kConversionFailed();
        if (token == preprocess_reason::kStrideTooSmall()) reason = preprocess_reason::kStrideTooSmall();
        else if (token == preprocess_reason::kInvalidDimensions())
            reason = preprocess_reason::kInvalidDimensions();
        else if (token == preprocess_reason::kNullBuffer()) reason = preprocess_reason::kNullBuffer();
        else if (token == preprocess_reason::kBadConfig()) reason = preprocess_reason::kBadConfig();
        else if (token == preprocess_reason::kUnsupportedFormat())
            reason = preprocess_reason::kUnsupportedFormat();
        return reject(reason, err);
    }

    r.ok = true;
    r.reason = preprocess_reason::kOk();
    r.detail = "letterbox+normalize+quantize";
    ++prepared_;
    return r;
}

}  // namespace media
}  // namespace railhmi
