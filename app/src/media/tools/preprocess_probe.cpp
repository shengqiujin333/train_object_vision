// railhmi-vision-preprocess (视觉推理 ITEM-002)
//
// 帧租约消费与预处理探针：
//   * --selftest：纯逻辑夹具（letterbox 参数/量化范围/RGB 顺序/stride/非法输入/NV12 参考路径）；
//   * --fixture bgr|nv12：已知合成图 + 带 padding 的 NV12 -> 预处理输出尺寸/范围/letterbox 参数；
//   * --source test|file|rtsp：真实 FrameLease（含 librga NV12→BGR）-> 预处理张量 + 逐帧耗时。
//
// 用法:
//   railhmi-vision-preprocess --selftest [--out log] [--json file]
//   railhmi-vision-preprocess --fixture bgr --src 1280x720 --target 640x640 [--stride N]
//                             [--color red|gray|gradient] [--dtype int8] [--scale S] [--zp Z]
//   railhmi-vision-preprocess --source test --src 1280x720 --duration 3 [--out log]
//   railhmi-vision-preprocess --source rtsp --url 'rtsp://…' --user admin --password-env VAR
//
// 退出码: 0 通过；1 失败；2 参数错误；3 取流启动失败。

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/timebase.h"
#include "railhmi/core/vision_preprocess.h"
#include "railhmi/media/camera_stream.h"
#include "railhmi/media/frame_preprocess.h"

using namespace railhmi;

namespace {

int g_fail = 0;
std::vector<std::string> g_lines;
std::ofstream g_fo;

void emit(const std::string& line) {
    std::printf("%s\n", line.c_str());
    g_lines.push_back(line);
    if (g_fo) g_fo << line << "\n";
}

void check(bool ok, const std::string& name, const std::string& detail = "") {
    emit(std::string("[") + (ok ? "PASS" : "FAIL") + "] " + name +
         (detail.empty() ? "" : (" :: " + detail)));
    if (!ok) ++g_fail;
}

core::InputTensorSpec spec_from(const std::string& dtype, double scale, int zp) {
    core::InputTensorSpec s;
    if (dtype == "uint8") s.dtype = core::TensorType::Uint8;
    else if (dtype == "float32") s.dtype = core::TensorType::Float32;
    else s.dtype = core::TensorType::Int8;
    s.scale = static_cast<float>(scale);
    s.zero_point = zp;
    core::normalize_input_spec(&s);
    return s;
}

core::PreprocessConfig cfg_from(int tw, int th, const std::string& dtype, double scale, int zp) {
    core::PreprocessConfig c;
    c.target_w = tw;
    c.target_h = th;
    c.mean = 0.0;
    c.std = 255.0;
    c.pad_value = 114;
    c.convert_to_rgb = true;
    c.input = spec_from(dtype, scale, zp);
    return c;
}

std::vector<std::uint8_t> make_bgr(int w, int h, int stride, const std::string& color) {
    if (stride < w * 3) stride = w * 3;  // 防御：夹具缓冲区不得因小 stride 越界
    std::vector<std::uint8_t> b(static_cast<std::size_t>(stride) * h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            std::size_t o = static_cast<std::size_t>(y) * stride + x * 3;
            if (color == "gray") {
                b[o + 0] = 128; b[o + 1] = 128; b[o + 2] = 128;
            } else if (color == "gradient") {
                b[o + 0] = static_cast<std::uint8_t>(x * 255 / std::max(1, w - 1));
                b[o + 1] = static_cast<std::uint8_t>(y * 255 / std::max(1, h - 1));
                b[o + 2] = 64;
            } else {  // red (BGR)
                b[o + 0] = 0; b[o + 1] = 0; b[o + 2] = 255;
            }
        }
        // 行尾 padding 填成一个非零哨兵，验证未被当像素读取
        for (int x = w * 3; x < stride; ++x) b[static_cast<std::size_t>(y) * stride + x] = 0xAB;
    }
    return b;
}

struct Nv12 {
    std::vector<std::uint8_t> y;
    std::vector<std::uint8_t> uv;
};

// 由纯色 RGB 构造 BT.601 full-range NV12，带行 padding。
Nv12 make_nv12(int w, int h, int y_stride, int uv_stride, const std::string& color) {
    if (y_stride < w) y_stride = w;
    if (uv_stride < w) uv_stride = w;
    Nv12 n;
    n.y.assign(static_cast<std::size_t>(y_stride) * h, 0xAB);
    n.uv.assign(static_cast<std::size_t>(uv_stride) * (h / 2), 0xAB);
    int R = 255, G = 0, B = 0;
    if (color == "gray") { R = G = B = 128; }
    const double Y = 0.299 * R + 0.587 * G + 0.114 * B;
    int U = static_cast<int>(std::lround(128 + (B - Y) * 0.564));
    int V = static_cast<int>(std::lround(128 + (R - Y) * 0.713));
    U = std::max(0, std::min(255, U));
    V = std::max(0, std::min(255, V));
    const int Yq = std::max(0, std::min(255, static_cast<int>(std::lround(Y))));
    for (int j = 0; j < h; ++j)
        for (int i = 0; i < w; ++i) n.y[static_cast<std::size_t>(j) * y_stride + i] = static_cast<std::uint8_t>(Yq);
    for (int j = 0; j < h / 2; ++j) {
        for (int i = 0; i < w / 2; ++i) {
            std::size_t o = static_cast<std::size_t>(j) * uv_stride + i * 2;
            n.uv[o + 0] = static_cast<std::uint8_t>(U);
            n.uv[o + 1] = static_cast<std::uint8_t>(V);
        }
    }
    return n;
}

std::string summary(const core::PreprocessedInput& in) {
    char buf[512];
    std::snprintf(buf, sizeof(buf),
                  "tensor=%dx%dx%d bytes=%zu dtype=%s zp=%d scale=%.8g letterbox(scale=%.6g pad_x=%d "
                  "pad_y=%d resized=%dx%d) vmin=%.4f vmax=%.4f means=[%.3f,%.3f,%.3f]",
                  in.width, in.height, in.channels, in.data.size(), core::to_string(in.dtype),
                  in.zero_point, static_cast<double>(in.scale), in.letterbox.scale, in.letterbox.pad_x,
                  in.letterbox.pad_y, in.letterbox.resized_w, in.letterbox.resized_h, in.value_min,
                  in.value_max, in.channel_mean[0], in.channel_mean[1], in.channel_mean[2]);
    return buf;
}

int run_selftest() {
    using namespace railhmi::core;
    const PreprocessConfig base = cfg_from(640, 640, "int8", 1.0 / 255.0, -128);

    {
        LetterboxParams lb; std::string e;
        check(compute_letterbox(1280, 720, 640, 640, &lb, &e), "lb_1280x720_ok", e);
        check(lb.scale == 0.5 && lb.resized_w == 640 && lb.resized_h == 360 && lb.pad_x == 0 &&
                  lb.pad_y == 140 && lb.pad_bottom == 140,
              "lb_1280x720_centered_pad",
              "scale=" + std::to_string(lb.scale) + " pad_y=" + std::to_string(lb.pad_y));
    }
    {
        LetterboxParams lb;
        check(compute_letterbox(640, 640, 640, 640, &lb, nullptr) && lb.scale == 1.0 && lb.pad_x == 0 &&
                  lb.pad_y == 0,
              "lb_same_size_no_pad");
    }
    {
        LetterboxParams lb;
        check(compute_letterbox(1280, 1024, 640, 640, &lb, nullptr) && lb.resized_h == 512 &&
                  lb.pad_y == 64,
              "lb_1280x1024_resized512_pad64");
    }
    {
        InputTensorSpec s = spec_from("int8", 1.0 / 255.0, -128);
        check(quantize_value(0.0, s) == -128, "quant_v0_to_-128");
        check(quantize_value(1.0, s) == 127, "quant_v255_to_127");
        check(quantize_value(128.0 / 255.0, s) == 0, "quant_mid_to_0");
        check(std::fabs(dequantize_value(-128, s) - 0.0) < 1e-6 && std::fabs(dequantize_value(127, s) - 1.0) < 1e-3,
              "dequant_bounds");
    }
    {
        const int w = 64, h = 64, stride = w * 3;
        std::vector<std::uint8_t> src = make_bgr(w, h, stride, "red");
        PreprocessedInput in; std::string e;
        check(prepare_from_packed(src.data(), w, h, stride, ChannelOrder::Bgr, base, &in, &e),
              "bgr_red_prepare_ok", e);
        check(in.data.size() == static_cast<std::size_t>(in.width) * in.height * 3, "tensor_size_w*h*3",
              "bytes=" + std::to_string(in.data.size()) + " shape=" + std::to_string(in.width) + "x" +
                  std::to_string(in.height));
        check(in.channel_mean[0] > in.channel_mean[2] + 50, "rgb_order_R_gt_B",
              "R=" + std::to_string(in.channel_mean[0]) + " B=" + std::to_string(in.channel_mean[2]));
        check(in.value_min >= -128 && in.value_max <= 127, "quant_range_within_int8");
    }
    {
        // stride：带 padding 与紧排的可见像素必须一致（padding 哨兵不得被读成像素）。
        const int w = 32, h = 16;
        const int s1 = w * 3, s2 = w * 3 + 21;
        std::vector<std::uint8_t> tight = make_bgr(w, h, s1, "gradient");
        std::vector<std::uint8_t> strided = make_bgr(w, h, s2, "gradient");
        PreprocessedInput a, b; std::string e;
        const bool ok1 = prepare_from_packed(tight.data(), w, h, s1, ChannelOrder::Bgr, base, &a, &e);
        const bool ok2 = prepare_from_packed(strided.data(), w, h, s2, ChannelOrder::Bgr, base, &b, &e);
        check(ok1 && ok2 && a.data == b.data, "stride_padding_not_read_as_pixels");
    }
    {
        const int w = 32, h = 16;
        PreprocessedInput in; std::string e;
        check(!prepare_from_packed(make_bgr(w, h, w * 3, "gray").data(), w, h, w * 3 - 1,
                                   ChannelOrder::Bgr, base, &in, &e) &&
                  e.find(preprocess_reason::kStrideTooSmall()) == 0,
              "stride_too_small_rejected", e);
        check(!prepare_from_packed(make_bgr(w, h, w * 3, "gray").data(), 0, h, w * 3,
                                   ChannelOrder::Bgr, base, &in, &e) &&
                  e.find(preprocess_reason::kInvalidDimensions()) == 0,
              "invalid_dim_rejected", e);
        check(!prepare_from_packed(nullptr, w, h, w * 3, ChannelOrder::Bgr, base, &in, &e) &&
                  e.find(preprocess_reason::kNullBuffer()) == 0,
              "null_buffer_rejected", e);
        PreprocessConfig bad = base;
        bad.std = 0.0;
        check(!prepare_from_packed(make_bgr(w, h, w * 3, "gray").data(), w, h, w * 3,
                                   ChannelOrder::Bgr, bad, &in, &e) &&
                  e.find(preprocess_reason::kBadConfig()) == 0,
              "bad_config_rejected", e);
    }
    {
        // 补边必须是 pad_value（灰）而不是源像素。
        const int w = 1280, h = 720;
        std::vector<std::uint8_t> src = make_bgr(w, h, w * 3, "red");
        PreprocessedInput in; std::string e;
        check(prepare_from_packed(src.data(), w, h, w * 3, ChannelOrder::Bgr, base, &in, &e),
              "letterbox_red_prepare_ok", e);
        const int expect_q = quantize_value((114.0 - base.mean) / base.std, base.input);
        const std::uint8_t expect_byte = static_cast<std::uint8_t>(static_cast<std::int8_t>(expect_q));
        check(in.data[0] == expect_byte, "pad_pixel_is_pad_value",
              "byte=" + std::to_string(in.data[0]) + " expect=" + std::to_string(expect_byte));
        check(in.letterbox.pad_y == 140 && in.width == 640 && in.height == 640,
              "letterbox_output_shape");
    }
    {
        // NV12 参考路径（带 padding）。
        const int w = 128, h = 64, ys = w + 16, uvs = w + 8;
        Nv12 n = make_nv12(w, h, ys, uvs, "red");
        PreprocessedInput in; std::string e;
        check(prepare_from_nv12(n.y.data(), n.uv.data(), w, h, ys, uvs, cfg_from(64, 64, "int8", 1.0 / 255.0, -128),
                                &in, &e),
              "nv12_padded_reference_ok", e);
        check(in.data.size() == 64u * 64 * 3 && in.width == 64 && in.height == 64, "nv12_tensor_shape");
        check(in.channel_mean[0] > in.channel_mean[2], "nv12_rgb_order_R_gt_B");
        check(!prepare_from_nv12(n.y.data(), n.uv.data(), 127, h, ys, uvs, base, &in, &e) &&
                  e.find(preprocess_reason::kInvalidDimensions()) == 0,
              "nv12_odd_dim_rejected", e);
    }
    {
        // float32 非量化路径。
        const int w = 16, h = 16;
        PreprocessedInput in; std::string e;
        check(prepare_from_packed(make_bgr(w, h, w * 3, "gray").data(), w, h, w * 3,
                                   ChannelOrder::Bgr, cfg_from(64, 64, "float32", 1.0, 0), &in, &e),
              "float32_prepare_ok", e);
        check(in.data.size() == static_cast<std::size_t>(64) * 64 * 3 * 4, "float32_tensor_bytes");
        check(in.value_min >= 0.0 && in.value_max <= 1.0, "float32_normalized_range");
    }
    emit("RESULT: " + std::string(g_fail == 0 ? "PASS" : "FAIL") + " failures=" +
         std::to_string(g_fail));
    return g_fail == 0 ? 0 : 1;
}

int run_fixture(const std::string& kind, int src_w, int src_h, int stride, const std::string& color,
                int tw, int th, const std::string& dtype, double scale, int zp) {
    using namespace railhmi::core;
    const PreprocessConfig c = cfg_from(tw, th, dtype, scale, zp);
    PreprocessedInput in;
    std::string e;
    bool ok = false;
    if (kind == "nv12") {
        const int ys = (stride > 0) ? stride : src_w + 16;
        const int uvs = ys;
        Nv12 n = make_nv12(src_w, src_h, ys, uvs, color);
        emit("type=fixture kind=nv12 src=" + std::to_string(src_w) + "x" + std::to_string(src_h) +
             " y_stride=" + std::to_string(ys) + " uv_stride=" + std::to_string(uvs) +
             " color=" + color + " target=" + std::to_string(tw) + "x" + std::to_string(th) +
             " dtype=" + dtype + " scale=" + std::to_string(scale) + " zp=" + std::to_string(zp));
        ok = prepare_from_nv12(n.y.data(), n.uv.data(), src_w, src_h, ys, uvs, c, &in, &e);
    } else {
        const int st = (stride > 0) ? stride : src_w * 3;
        std::vector<std::uint8_t> b = make_bgr(src_w, src_h, st, color);
        emit("type=fixture kind=bgr src=" + std::to_string(src_w) + "x" + std::to_string(src_h) +
             " stride=" + std::to_string(st) + " color=" + color +
             " target=" + std::to_string(tw) + "x" + std::to_string(th) + " dtype=" + dtype +
             " scale=" + std::to_string(scale) + " zp=" + std::to_string(zp));
        ok = prepare_from_packed(b.data(), src_w, src_h, st, ChannelOrder::Bgr, c, &in, &e);
    }
    if (!ok) {
        emit("type=fixture_result ok=false reason=" + e);
        return 1;
    }
    emit("type=fixture_result ok=true " + summary(in));
    return 0;
}

int run_source(const std::string& src, media::CameraStreamConfig& cfg, long long duration_s,
               int tw, int th, const std::string& dtype, double scale, int zp) {
    using namespace railhmi::media;
    core::TimeBase tb;
    tb.calibrate(core::TimeBase::system_now_ms(), core::TimeBase::steady_now_us());
    cfg.timebase = &tb;
    if (cfg.session_id.empty()) cfg.session_id = core::make_session_id();

    FramePreprocessConfig pcfg;
    pcfg.core = cfg_from(tw, th, dtype, scale, zp);
    pcfg.prefer_rga_for_nv12 = true;
    FramePreprocessor prep(pcfg);

    CameraStream stream(cfg);

    // TC-T-PRE-06（结构）：唯一像素入口是 FrameLease；空租约必须显式拒绝，不得产出伪张量。
    {
        LeasePreprocessResult r0 = prep.prepare(FrameLease{});
        check(!r0.ok && r0.reason == core::preprocess_reason::kNotReady(),
              "empty_lease_rejected_not_ready", r0.reason + " " + r0.detail);
    }

    std::string err;
    if (!stream.start(&err)) {
        std::fprintf(stderr, "stream start failed: %s\n", err.c_str());
        return 3;
    }
    emit("type=injection source=" + src + " camera=" + cfg.camera_id +
         " duration_s=" + std::to_string(duration_s) + " session_id=" + cfg.session_id +
         " target=" + std::to_string(tw) + "x" + std::to_string(th) + " dtype=" + dtype);

    std::vector<double> times_us;
    long long frames = 0, ok_frames = 0, not_ready = 0;
    std::string first_line;
    bool first_reported = false;
    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count() <
           duration_s * 1000) {
        stream.pump(50);
        FrameLease lease = stream.latest_frame();
        ++frames;
        const auto s0 = clock::now();
        LeasePreprocessResult r = prep.prepare(lease);
        const auto s1 = clock::now();
        times_us.push_back(std::chrono::duration<double, std::micro>(s1 - s0).count());
        if (r.ok) {
            ++ok_frames;
            if (!first_reported) {
                const FrameDescriptor& d = lease.descriptor();
                char b[512];
                std::snprintf(b, sizeof(b),
                              "type=lease format=%s size=%dx%d plane0_stride=%llu plane1_offset=%llu "
                              "memory_kind=%s origin=%s",
                              r.source_format.c_str(), d.layout.width, d.layout.height,
                              static_cast<unsigned long long>(d.layout.planes[0].stride_bytes),
                              static_cast<unsigned long long>(d.layout.planes[1].offset),
                              d.memory_kind.c_str(), d.origin.c_str());
                emit(b);
                first_line = "type=preprocess " + summary(r.input);
                emit(first_line);
                first_reported = true;
            }
        } else if (r.reason == core::preprocess_reason::kNotReady()) {
            ++not_ready;
        }
    }
    stream.stop();

    std::sort(times_us.begin(), times_us.end());
    auto pct = [&](double p) -> double {
        if (times_us.empty()) return -1.0;
        std::size_t i = static_cast<std::size_t>(p * (times_us.size() - 1));
        return times_us[i];
    };
    char b[400];
    std::snprintf(b, sizeof(b),
                  "type=preprocess_stats frames=%lld prepared=%lld not_ready=%lld rejected=%lld "
                  "prep_us_p50=%.1f prep_us_p95=%.1f prep_us_max=%.1f",
                  frames, ok_frames, not_ready, prep.rejected(), pct(0.50), pct(0.95),
                  times_us.empty() ? -1.0 : times_us.back());
    emit(b);
    std::string reasons = "type=reject_reasons";
    for (const auto& kv : prep.rejected_by_reason()) reasons += " " + kv.first + "=" + std::to_string(kv.second);
    emit(reasons);

    if (ok_frames == 0) {
        emit("RESULT: FAIL no frame prepared");
        return 1;
    }
    emit("RESULT: PASS");
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode, kind = "bgr", color = "red", dtype = "int8", out, json, src = "test";
    int src_w = 1280, src_h = 720, stride = 0, tw = 640, th = 640, zp = -128;
    double scale = 1.0 / 255.0;
    long long duration_s = 3;
    media::CameraStreamConfig cfg;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--selftest") mode = "selftest";
        else if (a == "--fixture") { next(&kind); mode = "fixture"; }
        else if (a == "--source") { next(&src); mode = "source"; }
        else if (a == "--src") { std::string v; next(&v); std::sscanf(v.c_str(), "%dx%d", &src_w, &src_h); }
        else if (a == "--target") { std::string v; next(&v); std::sscanf(v.c_str(), "%dx%d", &tw, &th); }
        else if (a == "--stride") { std::string v; next(&v); stride = std::atoi(v.c_str()); }
        else if (a == "--color") next(&color);
        else if (a == "--dtype") next(&dtype);
        else if (a == "--scale") { std::string v; next(&v); scale = std::atof(v.c_str()); }
        else if (a == "--zp") { std::string v; next(&v); zp = std::atoi(v.c_str()); }
        else if (a == "--duration") { std::string v; next(&v); duration_s = std::atoll(v.c_str()); }
        else if (a == "--file") next(&cfg.file);
        else if (a == "--url") next(&cfg.url);
        else if (a == "--user") next(&cfg.username);
        else if (a == "--camera") next(&cfg.camera_id);
        else if (a == "--password-env") { std::string v; next(&v); const char* p = std::getenv(v.c_str()); if (p) cfg.password = p; }
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-vision-preprocess --selftest | --fixture bgr|nv12 | --source test|file|rtsp\n");
            return 0;
        } else {
            std::fprintf(stderr, "unknown arg: %s\n", a.c_str());
            return 2;
        }
    }
    if (mode.empty()) { std::fprintf(stderr, "need --selftest/--fixture/--source\n"); return 2; }

    if (!out.empty()) g_fo.open(out, std::ios::trunc);
    int rc = 0;
    if (mode == "selftest") {
        rc = run_selftest();
    } else if (mode == "fixture") {
        rc = run_fixture(kind, src_w, src_h, stride, color, tw, th, dtype, scale, zp);
    } else {
        if (src == "file") cfg.source = media::SourceKind::File;
        else if (src == "rtsp") cfg.source = media::SourceKind::Rtsp;
        else cfg.source = media::SourceKind::Test;
        cfg.test_width = src_w;
        cfg.test_height = src_h;
        rc = run_source(src, cfg, duration_s, tw, th, dtype, scale, zp);
    }
    if (!json.empty()) {
        std::ofstream fj(json, std::ios::trunc);
        if (fj) {
            fj << "{\"mode\":\"" << mode << "\",\"failures\":" << g_fail << ",\"lines\":[";
            for (std::size_t i = 0; i < g_lines.size(); ++i) {
                if (i) fj << ",";
                std::string s = g_lines[i];
                std::string esc;
                for (char ch : s) { if (ch == '"' || ch == '\\') esc += '\\'; esc += ch; }
                fj << "\"" << esc << "\"";
            }
            fj << "]}";
        }
    }
    return rc;
}
