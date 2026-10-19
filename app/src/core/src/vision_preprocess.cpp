// railhmi-core: 视觉推理输入预处理实现（视觉推理 ITEM-002）。纯逻辑，仅标准库。

#include "railhmi/core/vision_preprocess.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace railhmi {
namespace core {

namespace {

bool fail(std::string* err, const char* reason, const std::string& detail = std::string()) {
    if (err) *err = detail.empty() ? std::string(reason) : (std::string(reason) + ": " + detail);
    return false;
}

double clamp_d(double v, double lo, double hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool is_quantized(TensorType t) { return t == TensorType::Int8 || t == TensorType::Uint8; }

bool validate_config(const PreprocessConfig& cfg, std::string* err) {
    if (cfg.target_w <= 0 || cfg.target_h <= 0) return fail(err, preprocess_reason::kBadConfig(),
                                                           "target size must be positive");
    if (cfg.std == 0.0) return fail(err, preprocess_reason::kBadConfig(), "std must be non-zero");
    if (is_quantized(cfg.input.dtype) && !(cfg.input.scale > 0.0f))
        return fail(err, preprocess_reason::kBadConfig(), "quantized input needs positive scale");
    return true;
}

void write_value(std::vector<std::uint8_t>* data, std::size_t channel_index, double normalized,
                 const PreprocessConfig& cfg, double* sum, double* vmin, double* vmax) {
    std::size_t off = channel_index;
    double stored = normalized;
    if (is_quantized(cfg.input.dtype)) {
        const int32_t q = quantize_value(normalized, cfg.input);
        stored = static_cast<double>(q);
        if (cfg.input.dtype == TensorType::Int8) {
            (*data)[off] = static_cast<std::uint8_t>(static_cast<std::int8_t>(q));
        } else {
            (*data)[off] = static_cast<std::uint8_t>(q);
        }
    } else {
        // float32（或其它非量化）：写 4 字节小端 float
        const float f = static_cast<float>(normalized);
        std::memcpy(data->data() + off * 4, &f, sizeof(f));
    }
    sum[channel_index % 3] += stored;
    if (stored < *vmin) *vmin = stored;
    if (stored > *vmax) *vmax = stored;
}

double index_of(std::size_t pixel, int c) { return static_cast<double>(pixel) * 3.0 + c; }

// 采样并写入一个目标像素；src 为 packed 8bit（stride 行距）。
void write_pixel(std::vector<std::uint8_t>* data, std::size_t pixel, double s0, double s1, double s2,
                 ChannelOrder order, const PreprocessConfig& cfg, double* sum, double* vmin,
                 double* vmax, bool* is_float) {
    double c0 = s0, c1 = s1, c2 = s2;
    if (cfg.convert_to_rgb) {
        if (order == ChannelOrder::Bgr) {
            c0 = s2; c2 = s0;  // B<->R -> 输出 RGB
        }
    } else {
        // 不转换：输出保持源顺序；若源是 BGR 且模型要 BGR 则保持。
        c0 = s0; c1 = s1; c2 = s2;
    }
    write_value(data, index_of(pixel, 0), (c0 - cfg.mean) / cfg.std, cfg, sum, vmin, vmax);
    write_value(data, index_of(pixel, 1), (c1 - cfg.mean) / cfg.std, cfg, sum, vmin, vmax);
    write_value(data, index_of(pixel, 2), (c2 - cfg.mean) / cfg.std, cfg, sum, vmin, vmax);
    *is_float = !is_quantized(cfg.input.dtype);
}

}  // namespace

bool compute_letterbox(int src_w, int src_h, int dst_w, int dst_h, LetterboxParams* out,
                       std::string* err) {
    if (!out) return false;
    if (src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0)
        return fail(err, preprocess_reason::kInvalidDimensions(), "non-positive dimension");
    const double sx = static_cast<double>(dst_w) / static_cast<double>(src_w);
    const double sy = static_cast<double>(dst_h) / static_cast<double>(src_h);
    const double scale = std::min(sx, sy);
    if (!(scale > 0.0)) return fail(err, preprocess_reason::kInvalidDimensions(), "non-positive scale");
    LetterboxParams p;
    p.src_w = src_w;
    p.src_h = src_h;
    p.dst_w = dst_w;
    p.dst_h = dst_h;
    p.scale = scale;
    p.resized_w = static_cast<int>(std::lround(static_cast<double>(src_w) * scale));
    p.resized_h = static_cast<int>(std::lround(static_cast<double>(src_h) * scale));
    p.resized_w = std::min(p.resized_w, dst_w);
    p.resized_h = std::min(p.resized_h, dst_h);
    const int pad_w = dst_w - p.resized_w;
    const int pad_h = dst_h - p.resized_h;
    p.pad_x = pad_w / 2;
    p.pad_y = pad_h / 2;
    p.pad_right = pad_w - p.pad_x;
    p.pad_bottom = pad_h - p.pad_y;
    *out = p;
    return true;
}

void normalize_input_spec(InputTensorSpec* spec) {
    if (!spec) return;
    spec->quantized = is_quantized(spec->dtype);
}

int32_t quantize_value(double normalized, const InputTensorSpec& spec) {
    if (spec.scale <= 0.0f) return 0;
    const double q = std::round(normalized / static_cast<double>(spec.scale)) +
                     static_cast<double>(spec.zero_point);
    int32_t lo = 0, hi = 255;
    if (spec.dtype == TensorType::Int8) {
        lo = -128;
        hi = 127;
    } else if (spec.dtype == TensorType::Uint8) {
        lo = 0;
        hi = 255;
    } else {
        return static_cast<int32_t>(std::lround(q));
    }
    if (q < lo) return lo;
    if (q > hi) return hi;
    return static_cast<int32_t>(std::lround(q));
}

double dequantize_value(int32_t q, const InputTensorSpec& spec) {
    return (static_cast<double>(q) - static_cast<double>(spec.zero_point)) *
           static_cast<double>(spec.scale);
}

bool prepare_from_packed(const std::uint8_t* src, int src_w, int src_h, int src_stride,
                         ChannelOrder order, const PreprocessConfig& cfg, PreprocessedInput* out,
                         std::string* err) {
    if (err) err->clear();
    if (!out) return fail(err, preprocess_reason::kBadConfig(), "null output");
    *out = PreprocessedInput{};
    if (!src) return fail(err, preprocess_reason::kNullBuffer(), "null source");
    if (src_w <= 0 || src_h <= 0)
        return fail(err, preprocess_reason::kInvalidDimensions(), "non-positive source size");
    if (src_stride < src_w * 3)
        return fail(err, preprocess_reason::kStrideTooSmall(),
                    "stride " + std::to_string(src_stride) + " < width*3 " +
                        std::to_string(src_w * 3));
    if (!validate_config(cfg, err)) return false;

    LetterboxParams lb;
    if (!compute_letterbox(src_w, src_h, cfg.target_w, cfg.target_h, &lb, err)) return false;

    out->width = cfg.target_w;
    out->height = cfg.target_h;
    out->channels = 3;
    out->dtype = cfg.input.dtype;
    out->zero_point = cfg.input.zero_point;
    out->scale = cfg.input.scale;
    out->quantized = is_quantized(cfg.input.dtype);
    out->letterbox = lb;

    const std::size_t pixels = static_cast<std::size_t>(out->width) * out->height;
    const std::size_t bytes_per = out->quantized ? 1u : 4u;
    out->data.assign(pixels * 3 * bytes_per, 0);

    double sum[3] = {0, 0, 0};
    double vmin = 1e30, vmax = -1e30;
    bool is_float = false;

    auto sample_src = [&](int x, int y, int c) -> double {
        return static_cast<double>(src[static_cast<std::size_t>(y) * src_stride + x * 3 + c]);
    };
    std::size_t pixel = 0;
    // 逐目标像素：resized 区域内双线性采样，区域外均用中性补边值。
    const double inv_scale = 1.0 / lb.scale;
    for (int dy = 0; dy < out->height; ++dy) {
        for (int dx = 0; dx < out->width; ++dx, ++pixel) {
            const bool inside = (dx >= lb.pad_x && dx < lb.pad_x + lb.resized_w &&
                                 dy >= lb.pad_y && dy < lb.pad_y + lb.resized_h);
            if (!inside) {
                // pad 值为中性灰，先按 pad_value 写（不做 BGR->RGB 交换的形状影响）
                write_value(&out->data, index_of(pixel, 0),
                            (static_cast<double>(cfg.pad_value) - cfg.mean) / cfg.std, cfg, sum,
                            &vmin, &vmax);
                write_value(&out->data, index_of(pixel, 1),
                            (static_cast<double>(cfg.pad_value) - cfg.mean) / cfg.std, cfg, sum,
                            &vmin, &vmax);
                write_value(&out->data, index_of(pixel, 2),
                            (static_cast<double>(cfg.pad_value) - cfg.mean) / cfg.std, cfg, sum,
                            &vmin, &vmax);
                continue;
            }
            const int rx = dx - lb.pad_x;
            const int ry = dy - lb.pad_y;
            double fx = (static_cast<double>(rx) + 0.5) * inv_scale - 0.5;
            double fy = (static_cast<double>(ry) + 0.5) * inv_scale - 0.5;
            fx = clamp_d(fx, 0.0, static_cast<double>(src_w - 1));
            fy = clamp_d(fy, 0.0, static_cast<double>(src_h - 1));
            int x0 = static_cast<int>(std::floor(fx));
            int y0 = static_cast<int>(std::floor(fy));
            int x1 = std::min(x0 + 1, src_w - 1);
            int y1 = std::min(y0 + 1, src_h - 1);
            const double wx = fx - x0;
            const double wy = fy - y0;
            double s[3];
            for (int c = 0; c < 3; ++c) {
                const double v00 = sample_src(x0, y0, c);
                const double v10 = sample_src(x1, y0, c);
                const double v01 = sample_src(x0, y1, c);
                const double v11 = sample_src(x1, y1, c);
                s[c] = (1 - wx) * (1 - wy) * v00 + wx * (1 - wy) * v10 + (1 - wx) * wy * v01 +
                       wx * wy * v11;
            }
            write_pixel(&out->data, pixel, s[0], s[1], s[2], order, cfg, sum, &vmin, &vmax,
                        &is_float);
        }
    }
    for (int c = 0; c < 3; ++c) out->channel_mean[c] = sum[c] / static_cast<double>(pixels);
    out->value_min = vmin;
    out->value_max = vmax;
    return true;
}

bool prepare_from_nv12(const std::uint8_t* y, const std::uint8_t* uv, int src_w, int src_h,
                       int y_stride, int uv_stride, const PreprocessConfig& cfg,
                       PreprocessedInput* out, std::string* err) {
    if (err) err->clear();
    if (!y || !uv) return fail(err, preprocess_reason::kNullBuffer(), "null NV12 plane");
    if (src_w <= 0 || src_h <= 0 || (src_w % 2) != 0 || (src_h % 2) != 0)
        return fail(err, preprocess_reason::kInvalidDimensions(), "NV12 needs even positive size");
    if (y_stride < src_w)
        return fail(err, preprocess_reason::kStrideTooSmall(), "y stride < width");
    if (uv_stride < src_w)
        return fail(err, preprocess_reason::kStrideTooSmall(), "uv stride < width");

    // 参考 NV12 -> BGR888（full-range BT.601）；板端主链路用 librga。
    std::vector<std::uint8_t> bgr(static_cast<std::size_t>(src_w) * src_h * 3);
    for (int j = 0; j < src_h; ++j) {
        for (int i = 0; i < src_w; ++i) {
            const int Y = y[static_cast<std::size_t>(j) * y_stride + i];
            const std::size_t uv_off =
                static_cast<std::size_t>(j / 2) * uv_stride + static_cast<std::size_t>(i / 2) * 2;
            const int uu = uv[uv_off + 0];
            const int vv = uv[uv_off + 1];
            const double du = uu - 128;
            const double dv = vv - 128;
            double r = Y + 1.402 * dv;
            double g = Y - 0.344136 * du - 0.714136 * dv;
            double b = Y + 1.772 * du;
            r = clamp_d(r, 0, 255);
            g = clamp_d(g, 0, 255);
            b = clamp_d(b, 0, 255);
            const std::size_t o = (static_cast<std::size_t>(j) * src_w + i) * 3;
            bgr[o + 0] = static_cast<std::uint8_t>(std::lround(b));
            bgr[o + 1] = static_cast<std::uint8_t>(std::lround(g));
            bgr[o + 2] = static_cast<std::uint8_t>(std::lround(r));
        }
    }
    return prepare_from_packed(bgr.data(), src_w, src_h, src_w * 3, ChannelOrder::Bgr, cfg, out, err);
}

}  // namespace core
}  // namespace railhmi
