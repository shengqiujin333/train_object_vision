// railhmi-core YOLOv8-seg 分割后处理测试（视觉推理 ITEM-004 纯逻辑）。
// 覆盖 TC-T-SEG-01..06：13 输出解析/点积掩码/逆 letterbox/二值与所有权/框掩码对应/形状不符。

#include <cmath>
#include <string>
#include <vector>

#include "test_util.h"

#include "railhmi/core/vision_seg_postprocess.h"

namespace {

using namespace railhmi::core;

int sh(int s) { return 80 >> s; }

LetterboxParams lb() {
    LetterboxParams p;
    compute_letterbox(1280, 720, 640, 640, &p, nullptr);
    return p;
}

SegConfig cfg(float thr = 0.25f, float nms = 0.45f, float mask_thr = 0.0f, bool crop = true) {
    SegConfig c;
    c.input_w = 640;
    c.input_h = 640;
    c.score_threshold = thr;
    c.nms_iou = nms;
    c.num_classes = 1;
    c.class_whitelist = {0};
    c.proto_channels = 32;
    c.proto_h = 160;
    c.proto_w = 160;
    c.mask_threshold = mask_thr;
    c.crop_to_box = crop;
    return c;
}

struct Fx {
    std::vector<std::vector<float>> planes;
    std::vector<RawDetectTensor> views;
    void alloc(const SegConfig& c) {
        for (int s = 0; s < 3; ++s) {
            const int H = sh(s);
            const int ch[4] = {64, c.num_classes, 1, c.proto_channels};
            for (int b = 0; b < 4; ++b) {
                planes.emplace_back(static_cast<std::size_t>(ch[b]) * H * H, 0.0f);
                RawDetectTensor t;
                t.channels = ch[b];
                t.height = H;
                t.width = H;
                t.dtype = TensorType::Float32;
                t.is_float = true;
                views.push_back(t);
            }
        }
        planes.emplace_back(static_cast<std::size_t>(c.proto_channels) * c.proto_h * c.proto_w, 0.0f);
        RawDetectTensor p;
        p.channels = c.proto_channels;
        p.height = c.proto_h;
        p.width = c.proto_w;
        p.dtype = TensorType::Float32;
        p.is_float = true;
        views.push_back(p);
    }
    void bind() {
        for (std::size_t i = 0; i < views.size(); ++i)
            views[i].data = reinterpret_cast<const std::uint8_t*>(planes[i].data());
    }
    void fill_proto(const SegConfig& c, bool half) {
        const std::size_t pl = static_cast<std::size_t>(c.proto_h) * c.proto_w;
        for (int k = 0; k < c.proto_channels; ++k)
            for (int y = 0; y < c.proto_h; ++y)
                for (int x = 0; x < c.proto_w; ++x)
                    planes[12][static_cast<std::size_t>(k) * pl + static_cast<std::size_t>(y) * c.proto_w + x] =
                        (half && x >= c.proto_w / 2) ? -1.0f : 1.0f;
    }
    void encode(int s, int i, int j, int dl, int dt, int dr, int db, double score, double coeff) {
        const int H = sh(s);
        const std::size_t gl = static_cast<std::size_t>(H) * H;
        const std::size_t pix = static_cast<std::size_t>(i) * H + j;
        const int base = s * 4;
        const int dfl = views[base + 0].channels / 4;
        for (int b = 0; b < 4; ++b) {
            const int bin = (b == 0 ? dl : b == 1 ? dt : b == 2 ? dr : db);
            for (int k = 0; k < dfl; ++k)
                planes[base + 0][static_cast<std::size_t>(b * dfl + k) * gl + pix] = (k == bin) ? 12.f : 0.f;
        }
        planes[base + 1][pix] = static_cast<float>(score);
        planes[base + 2][pix] = static_cast<float>(score);
        for (int k = 0; k < views[base + 3].channels; ++k)
            planes[base + 3][static_cast<std::size_t>(k) * gl + pix] = static_cast<float>(coeff);
    }
};

Fx mk_rail(const SegConfig& c, double coeff = 1.0) {
    Fx fx;
    fx.alloc(c);
    fx.fill_proto(c, true);
    fx.encode(0, 30, 40, 4, 4, 4, 4, 0.9, coeff);
    fx.bind();
    return fx;
}

int px(const SegMask& m, int x, int y) {
    return m.plane[static_cast<std::size_t>(y) * m.stride_bytes + x];
}

void test_decode_and_mask() {
    const SegConfig c = cfg();
    Fx fx = mk_rail(c);
    SegResult r;
    std::string e;
    railhmi_test::check(decode_yolov8_seg(fx.views, c, lb(), 1280, 720, &r, &e), "decode ok", e);
    railhmi_test::check(r.boxes.size() == 1 && r.masks.size() == 1, "box/mask one-to-one",
                        std::to_string(r.boxes.size()) + "/" + std::to_string(r.masks.size()));
    if (r.boxes.empty() || r.masks.empty()) return;
    const SegBox& b = r.boxes[0];
    railhmi_test::check(std::fabs(b.x - 0.45625) < 1e-3 && std::fabs(b.y - 0.2) < 1e-3 &&
                            std::fabs(b.w - 0.1) < 1e-3 && std::fabs(b.h - 0.177778) < 1e-3,
                        "box coords");
    railhmi_test::check(std::fabs(b.score - 0.9) < 2e-3 && b.class_id == 0 && b.label == "rail",
                        "score/class/label");
    const SegMask& m = r.masks[0];
    railhmi_test::check(m.box_index == 0, "mask box_index links box");
    railhmi_test::check(m.width == 1280 && m.height == 720 && m.stride_bytes == 1280 &&
                            m.plane.size() == 1280u * 720,
                        "mask dims/stride/size = original");
    bool binary = true;
    for (auto v : m.plane) if (v > 1) binary = false;
    railhmi_test::check(binary && m.positive > 0, "binary + positives");
    railhmi_test::check(px(m, 600, 240) == 1, "positive inside box left");
    railhmi_test::check(px(m, 680, 240) == 0, "zero inside box right");
    railhmi_test::check(px(m, 400, 240) == 0, "zero outside box");
}

void test_threshold_and_crop() {
    {
        const SegConfig c = cfg();
        Fx fx = mk_rail(c, 0.0);
        SegResult r;
        std::string e;
        railhmi_test::check(decode_yolov8_seg(fx.views, c, lb(), 1280, 720, &r, &e) &&
                                r.boxes.size() == 1 && r.masks.size() == 1 && r.masks[0].positive == 0,
                            "zero coeff -> all-zero mask");
    }
    {
        const SegConfig c = cfg(0.25f, 0.45f, 100.0f);
        Fx fx = mk_rail(c);
        SegResult r;
        std::string e;
        railhmi_test::check(decode_yolov8_seg(fx.views, c, lb(), 1280, 720, &r, &e) &&
                                r.masks.size() == 1 && r.masks[0].positive == 0,
                            "mask_threshold filters");
    }
    {
        const SegConfig c = cfg(0.25f, 0.45f, 0.0f, false);
        Fx fx = mk_rail(c);
        SegResult r;
        std::string e;
        railhmi_test::check(decode_yolov8_seg(fx.views, c, lb(), 1280, 720, &r, &e) &&
                                r.masks.size() == 1 && r.masks[0].positive > 0,
                            "crop_to_box=false keeps outside positives");
    }
}

void test_empty_and_nms() {
    {
        const SegConfig c = cfg();
        Fx fx;
        fx.alloc(c);
        fx.bind();
        SegResult r;
        std::string e;
        railhmi_test::check(decode_yolov8_seg(fx.views, c, lb(), 1280, 720, &r, &e) && r.ok &&
                                r.boxes.empty() && r.masks.empty() &&
                                r.reason == seg_reason::kEmpty(),
                            "empty -> SEG_EMPTY");
    }
    {
        const SegConfig c = cfg();
        Fx fx;
        fx.alloc(c);
        fx.fill_proto(c, true);
        fx.encode(0, 30, 40, 4, 4, 4, 4, 0.9, 1.0);
        fx.encode(0, 30, 41, 4, 4, 4, 4, 0.8, 1.0);
        fx.bind();
        SegResult r;
        std::string e;
        railhmi_test::check(decode_yolov8_seg(fx.views, c, lb(), 1280, 720, &r, &e) &&
                                r.boxes.size() == 1 && r.masks.size() == 1 && r.nms_suppressed >= 1,
                            "nms keeps 1, mask follows");
    }
}

void test_model_mismatch() {
    const SegConfig c = cfg();
    {
        Fx fx = mk_rail(c);
        std::vector<RawDetectTensor> fewer(fx.views.begin(), fx.views.end() - 1);
        SegResult r;
        std::string e;
        railhmi_test::check(!decode_yolov8_seg(fewer, c, lb(), 1280, 720, &r, &e) &&
                                r.reason == seg_reason::kModelMismatch(),
                            "12 tensors -> MODEL_MISMATCH", e);
    }
    {
        Fx fx = mk_rail(c);
        fx.views[3].channels = 16;
        SegResult r;
        std::string e;
        railhmi_test::check(!decode_yolov8_seg(fx.views, c, lb(), 1280, 720, &r, &e) &&
                                r.reason == seg_reason::kModelMismatch(),
                            "bad seg channels -> MODEL_MISMATCH");
    }
    {
        Fx fx = mk_rail(c);
        fx.views[12].height = 80;
        SegResult r;
        std::string e;
        railhmi_test::check(!decode_yolov8_seg(fx.views, c, lb(), 1280, 720, &r, &e) &&
                                r.reason == seg_reason::kModelMismatch(),
                            "bad proto shape -> MODEL_MISMATCH");
    }
    {
        Fx fx = mk_rail(c);
        fx.views[0].fmt = TensorFormat::Nhwc;
        SegResult r;
        std::string e;
        railhmi_test::check(!decode_yolov8_seg(fx.views, c, lb(), 1280, 720, &r, &e) &&
                                r.reason == seg_reason::kModelMismatch(),
                            "non-NCHW -> MODEL_MISMATCH");
    }
    {
        Fx fx = mk_rail(c);
        fx.views[4].channels = 63;
        SegResult r;
        std::string e;
        railhmi_test::check(!decode_yolov8_seg(fx.views, c, lb(), 1280, 720, &r, &e) &&
                                r.reason == seg_reason::kModelMismatch(),
                            "box channels not 4*dfl -> MODEL_MISMATCH");
    }
    {
        const SegConfig bad = cfg(1.5f, 0.45f, 0.0f, true);
        Fx fx = mk_rail(cfg());
        SegResult r;
        std::string e;
        railhmi_test::check(!decode_yolov8_seg(fx.views, bad, lb(), 1280, 720, &r, &e) &&
                                r.reason == seg_reason::kBadConfig(),
                            "bad config -> SEG_BAD_CONFIG");
    }
}

void test_int8_path() {
    // int8 张量（zp=0, scale=0.1 使 0/±1/12/0.9 精确可表），验证反量化 + 掩码链路
    const SegConfig c = cfg();
    const int32_t zp = 0;
    const float scale = 0.1f;
    std::vector<std::vector<std::uint8_t>> planes;
    std::vector<RawDetectTensor> views;
    for (int s = 0; s < 3; ++s) {
        const int H = sh(s);
        const int ch[4] = {64, c.num_classes, 1, c.proto_channels};
        for (int b = 0; b < 4; ++b) {
            planes.emplace_back(static_cast<std::size_t>(ch[b]) * H * H, 0);
            RawDetectTensor t;
            t.channels = ch[b];
            t.height = H;
            t.width = H;
            t.dtype = TensorType::Int8;
            t.zero_point = zp;
            t.scale = scale;
            views.push_back(t);
        }
    }
    planes.emplace_back(static_cast<std::size_t>(c.proto_channels) * c.proto_h * c.proto_w, 0);
    {
        RawDetectTensor p;
        p.channels = c.proto_channels;
        p.height = c.proto_h;
        p.width = c.proto_w;
        p.dtype = TensorType::Int8;
        p.zero_point = zp;
        p.scale = scale;
        views.push_back(p);
    }
    for (std::size_t i = 0; i < views.size(); ++i) views[i].data = planes[i].data();
    auto put = [&](int vi, std::size_t idx, double v) {
        double q = std::round(v / scale) + zp;
        q = std::max(-128.0, std::min(127.0, q));
        planes[vi][idx] = static_cast<std::uint8_t>(static_cast<std::int8_t>(q));
    };
    const std::size_t pl = static_cast<std::size_t>(c.proto_h) * c.proto_w;
    for (int k = 0; k < c.proto_channels; ++k)
        for (int y = 0; y < c.proto_h; ++y)
            for (int x = 0; x < c.proto_w; ++x)
                put(12, static_cast<std::size_t>(k) * pl + static_cast<std::size_t>(y) * c.proto_w + x,
                    x >= c.proto_w / 2 ? -1.0 : 1.0);
    const int H = sh(0);
    const std::size_t gl = static_cast<std::size_t>(H) * H;
    const std::size_t pix = static_cast<std::size_t>(30) * H + 40;
    for (int b = 0; b < 4; ++b)
        for (int k = 0; k < 16; ++k)
            put(0, static_cast<std::size_t>(b * 16 + k) * gl + pix, k == 4 ? 12.0 : 0.0);
    put(1, pix, 0.9);
    put(2, pix, 0.9);
    for (int k = 0; k < c.proto_channels; ++k) put(3, static_cast<std::size_t>(k) * gl + pix, 1.0);

    SegResult r;
    std::string e;
    railhmi_test::check(decode_yolov8_seg(views, c, lb(), 1280, 720, &r, &e) && r.boxes.size() == 1 &&
                            r.masks.size() == 1,
                        "int8 decode one box + one mask", e);
    if (!r.boxes.empty())
        railhmi_test::check(std::fabs(r.boxes[0].x - 0.45625) < 0.01, "int8 box within quant tol");
    if (!r.masks.empty()) {
        railhmi_test::check(px(r.masks[0], 600, 240) == 1 && px(r.masks[0], 680, 240) == 0,
                            "int8 mask aligned + binary");
    }
}

}  // namespace

void run_vision_seg_postprocess_tests() {
    test_decode_and_mask();
    test_threshold_and_crop();
    test_empty_and_nms();
    test_model_mismatch();
    test_int8_path();
}
