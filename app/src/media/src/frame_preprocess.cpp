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
            ok = railhmi::core::prepare_from_packed(bgr.data.data(), bgr.width, bgr.height,
                                                    bgr.width * 3, railhmi::core::ChannelOrder::Bgr,
                                                    cfg_.core, &r.input, &err);
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
