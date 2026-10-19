// railhmi-core 视觉预处理测试（视觉推理 ITEM-002 纯逻辑）。
// 覆盖 TC-T-PRE-01..05/07 的确定性数学部分（租约消费在 media，见板端证据）。

#include <cmath>
#include <string>
#include <vector>

#include "test_util.h"

#include "railhmi/core/vision_preprocess.h"

namespace {

using namespace railhmi::core;

InputTensorSpec ispec(const char* dtype, double scale, int zp) {
    InputTensorSpec s;
    if (std::string(dtype) == "uint8") s.dtype = TensorType::Uint8;
    else if (std::string(dtype) == "float32") s.dtype = TensorType::Float32;
    else s.dtype = TensorType::Int8;
    s.scale = static_cast<float>(scale);
    s.zero_point = zp;
    normalize_input_spec(&s);
    return s;
}

PreprocessConfig pcfg(int tw, int th, const char* dtype, double scale, int zp) {
    PreprocessConfig c;
    c.target_w = tw;
    c.target_h = th;
    c.mean = 0.0;
    c.std = 255.0;
    c.pad_value = 114;
    c.convert_to_rgb = true;
    c.input = ispec(dtype, scale, zp);
    return c;
}

std::vector<std::uint8_t> bgr(int w, int h, int stride, int b, int g, int r) {
    std::vector<std::uint8_t> v(static_cast<std::size_t>(stride) * h, 0xAB);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::size_t o = static_cast<std::size_t>(y) * stride + x * 3;
            v[o + 0] = static_cast<std::uint8_t>(b);
            v[o + 1] = static_cast<std::uint8_t>(g);
            v[o + 2] = static_cast<std::uint8_t>(r);
        }
    return v;
}

void test_letterbox() {
    LetterboxParams lb;
    std::string e;
    railhmi_test::check(compute_letterbox(1280, 720, 640, 640, &lb, &e), "lb 1280x720 ok", e);
    railhmi_test::check(lb.scale == 0.5 && lb.resized_w == 640 && lb.resized_h == 360, "lb scale/resized");
    railhmi_test::check(lb.pad_x == 0 && lb.pad_y == 140 && lb.pad_bottom == 140, "lb centered pad",
                        "pad_y=" + std::to_string(lb.pad_y));
    railhmi_test::check(compute_letterbox(640, 640, 640, 640, &lb, nullptr) && lb.scale == 1.0 &&
                            lb.pad_x == 0 && lb.pad_y == 0,
                        "lb same size");
    railhmi_test::check(compute_letterbox(1280, 1024, 640, 640, &lb, nullptr) && lb.resized_h == 512 &&
                            lb.pad_y == 64,
                        "lb 1280x1024");
    railhmi_test::check(!compute_letterbox(0, 720, 640, 640, &lb, &e) &&
                            e.find(preprocess_reason::kInvalidDimensions()) == 0,
                        "lb invalid dims rejected", e);
    railhmi_test::check(!compute_letterbox(100, 100, 0, 640, &lb, &e), "lb zero target rejected");
}

void test_quantization() {
    const InputTensorSpec s = ispec("int8", 1.0 / 255.0, -128);
    railhmi_test::check(quantize_value(0.0, s) == -128, "quant v0");
    railhmi_test::check(quantize_value(1.0, s) == 127, "quant v255");
    railhmi_test::check(quantize_value(128.0 / 255.0, s) == 0, "quant mid");
    railhmi_test::check(std::fabs(dequantize_value(-128, s)) < 1e-9, "dequant low");
    railhmi_test::check(std::fabs(dequantize_value(127, s) - 1.0) < 1e-3, "dequant high");
    // 越界裁剪
    railhmi_test::check(quantize_value(10.0, s) == 127, "quant high clamp");
    railhmi_test::check(quantize_value(-10.0, s) == -128, "quant low clamp");
    const InputTensorSpec u = ispec("uint8", 1.0, 0);
    railhmi_test::check(quantize_value(300.0, u) == 255 && quantize_value(-5.0, u) == 0, "uint8 clamp");
}

void test_packed_and_rgb_order() {
    const PreprocessConfig c = pcfg(64, 64, "int8", 1.0 / 255.0, -128);
    // 纯红（BGR: B=0,G=0,R=255）-> 输出 RGB，R 通道均值应远大于 B。
    std::vector<std::uint8_t> src = bgr(64, 64, 192, 0, 0, 255);
    PreprocessedInput in;
    std::string e;
    railhmi_test::check(prepare_from_packed(src.data(), 64, 64, 192, ChannelOrder::Bgr, c, &in, &e),
                        "packed red ok", e);
    railhmi_test::check(in.data.size() == 64u * 64 * 3, "packed tensor bytes");
    railhmi_test::check(in.channel_mean[0] > in.channel_mean[2] + 50, "rgb order R>B",
                        "R=" + std::to_string(in.channel_mean[0]) + " B=" + std::to_string(in.channel_mean[2]));
    railhmi_test::check(in.value_min >= -128 && in.value_max <= 127, "int8 output range");
    railhmi_test::check(in.width == 64 && in.height == 64 && in.channels == 3, "packed shape");
}

void test_stride_handling() {
    const PreprocessConfig c = pcfg(64, 64, "int8", 1.0 / 255.0, -128);
    const int w = 32, h = 16;
    const std::vector<std::uint8_t> tight = bgr(w, h, w * 3, 10, 20, 30);
    const std::vector<std::uint8_t> padded = bgr(w, h, w * 3 + 21, 10, 20, 30);
    PreprocessedInput a, b;
    std::string e;
    railhmi_test::check(prepare_from_packed(tight.data(), w, h, w * 3, ChannelOrder::Bgr, c, &a, &e),
                        "tight stride ok");
    railhmi_test::check(prepare_from_packed(padded.data(), w, h, w * 3 + 21, ChannelOrder::Bgr, c, &b, &e),
                        "padded stride ok");
    railhmi_test::check(a.data == b.data, "stride padding not read as pixels");
}

void test_rejections() {
    const PreprocessConfig c = pcfg(64, 64, "int8", 1.0 / 255.0, -128);
    PreprocessedInput in;
    std::string e;
    const std::vector<std::uint8_t> src = bgr(32, 16, 96, 1, 2, 3);
    railhmi_test::check(!prepare_from_packed(src.data(), 32, 16, 95, ChannelOrder::Bgr, c, &in, &e) &&
                            e.find(preprocess_reason::kStrideTooSmall()) == 0,
                        "stride too small rejected", e);
    railhmi_test::check(!prepare_from_packed(src.data(), 0, 16, 96, ChannelOrder::Bgr, c, &in, &e) &&
                            e.find(preprocess_reason::kInvalidDimensions()) == 0,
                        "invalid dims rejected", e);
    railhmi_test::check(!prepare_from_packed(nullptr, 32, 16, 96, ChannelOrder::Bgr, c, &in, &e) &&
                            e.find(preprocess_reason::kNullBuffer()) == 0,
                        "null buffer rejected", e);
    PreprocessConfig bad = c;
    bad.std = 0.0;
    railhmi_test::check(!prepare_from_packed(src.data(), 32, 16, 96, ChannelOrder::Bgr, bad, &in, &e) &&
                            e.find(preprocess_reason::kBadConfig()) == 0,
                        "bad config rejected", e);
    PreprocessConfig bad_scale = c;
    bad_scale.input.scale = 0.0f;
    railhmi_test::check(!prepare_from_packed(src.data(), 32, 16, 96, ChannelOrder::Bgr, bad_scale, &in, &e) &&
                            e.find(preprocess_reason::kBadConfig()) == 0,
                        "zero quant scale rejected", e);
    railhmi_test::check(in.data.empty(), "failed prepare leaves no partial data");
}

struct Nv12 {
    std::vector<std::uint8_t> y;
    std::vector<std::uint8_t> uv;
};

Nv12 make_nv12(int w, int h, int ys, int uvs, int yv, int u, int v) {
    Nv12 n;
    n.y.assign(static_cast<std::size_t>(ys) * h, 0xAB);
    n.uv.assign(static_cast<std::size_t>(uvs) * (h / 2), 0xAB);
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) n.y[static_cast<std::size_t>(j) * ys + i] = static_cast<std::uint8_t>(yv);
    for (int j = 0; j < h / 2; ++j)
        for (int i = 0; i < w / 2; ++i) {
            std::size_t o = static_cast<std::size_t>(j) * uvs + i * 2;
            n.uv[o + 0] = static_cast<std::uint8_t>(u);
            n.uv[o + 1] = static_cast<std::uint8_t>(v);
        }
    return n;
}

void test_nv12_reference() {
    const PreprocessConfig c = pcfg(64, 64, "int8", 1.0 / 255.0, -128);
    const int w = 128, h = 64, ys = w + 16, uvs = w + 8;
    Nv12 n = make_nv12(w, h, ys, uvs, 76, 85, 255);  // 近似红
    PreprocessedInput in;
    std::string e;
    railhmi_test::check(prepare_from_nv12(n.y.data(), n.uv.data(), w, h, ys, uvs, c, &in, &e),
                        "nv12 padded reference ok", e);
    railhmi_test::check(in.width == 64 && in.height == 64 && in.data.size() == 64u * 64 * 3,
                        "nv12 tensor shape");
    railhmi_test::check(in.channel_mean[0] > in.channel_mean[2], "nv12 rgb order R>B");
    railhmi_test::check(!prepare_from_nv12(n.y.data(), n.uv.data(), 127, h, ys, uvs, c, &in, &e) &&
                            e.find(preprocess_reason::kInvalidDimensions()) == 0,
                        "nv12 odd dims rejected", e);
    railhmi_test::check(!prepare_from_nv12(n.y.data(), n.uv.data(), w, h, w - 1, uvs, c, &in, &e) &&
                            e.find(preprocess_reason::kStrideTooSmall()) == 0,
                        "nv12 small y stride rejected", e);
    railhmi_test::check(!prepare_from_nv12(nullptr, n.uv.data(), w, h, ys, uvs, c, &in, &e) &&
                            e.find(preprocess_reason::kNullBuffer()) == 0,
                        "nv12 null plane rejected", e);
}

void test_letterbox_pad_value() {
    const PreprocessConfig c = pcfg(640, 640, "int8", 1.0 / 255.0, -128);
    const int w = 1280, h = 720;
    std::vector<std::uint8_t> src = bgr(w, h, w * 3, 0, 0, 255);
    PreprocessedInput in;
    std::string e;
    railhmi_test::check(prepare_from_packed(src.data(), w, h, w * 3, ChannelOrder::Bgr, c, &in, &e),
                        "letterbox red prepare ok", e);
    const int q = quantize_value((114.0 - c.mean) / c.std, c.input);
    railhmi_test::check(in.data[0] == static_cast<std::uint8_t>(static_cast<std::int8_t>(q)),
                        "pad pixel equals pad_value",
                        "byte=" + std::to_string(in.data[0]) + " expect=" + std::to_string(q));
    railhmi_test::check(in.letterbox.pad_y == 140 && in.width == 640 && in.height == 640,
                        "letterbox output shape");
}

void test_float32_path() {
    const PreprocessConfig c = pcfg(64, 64, "float32", 1.0, 0);
    std::vector<std::uint8_t> src = bgr(16, 16, 48, 128, 128, 128);
    PreprocessedInput in;
    std::string e;
    railhmi_test::check(prepare_from_packed(src.data(), 16, 16, 48, ChannelOrder::Bgr, c, &in, &e),
                        "float32 prepare ok", e);
    railhmi_test::check(in.data.size() == 64u * 64 * 3 * 4, "float32 tensor bytes");
    railhmi_test::check(in.value_min >= 0.0 && in.value_max <= 1.0, "float32 normalized range",
                        "min=" + std::to_string(in.value_min) + " max=" + std::to_string(in.value_max));
}

void test_uint8_and_rgb_source() {
    const PreprocessConfig c = pcfg(32, 32, "uint8", 1.0, 0);
    std::vector<std::uint8_t> src = bgr(32, 32, 96, 200, 100, 50);  // B=200 -> 输出 R 低
    PreprocessedInput in;
    std::string e;
    railhmi_test::check(prepare_from_packed(src.data(), 32, 32, 96, ChannelOrder::Bgr, c, &in, &e),
                        "uint8 prepare ok", e);
    railhmi_test::check(in.value_min >= 0 && in.value_max <= 255, "uint8 range");
    // 源已是 RGB 且要求保持：convert_to_rgb=false 时顺序不变
    PreprocessConfig rgb_in = c;
    rgb_in.convert_to_rgb = false;
    std::vector<std::uint8_t> rgbsrc = bgr(32, 32, 96, 10, 20, 200);  // 解释为 RGB：R=10,G=20,B=200
    PreprocessedInput in2;
    railhmi_test::check(prepare_from_packed(rgbsrc.data(), 32, 32, 96, ChannelOrder::Rgb, rgb_in, &in2, &e),
                        "rgb source no-convert ok", e);
    railhmi_test::check(in2.channel_mean[2] > in2.channel_mean[0], "rgb source preserves order");
}

}  // namespace

void run_vision_preprocess_tests() {
    test_letterbox();
    test_quantization();
    test_packed_and_rgb_order();
    test_stride_handling();
    test_rejections();
    test_nv12_reference();
    test_letterbox_pad_value();
    test_float32_path();
    test_uint8_and_rgb_source();
}
