// railhmi-core YOLOv5 人形检测后处理测试（视觉推理 ITEM-003 纯逻辑）。
// 覆盖 TC-T-DET-01..05：三尺度解码 / 置信度过滤 / NMS / letterbox 逆变换 / 白名单 / 空图。

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "test_util.h"

#include "railhmi/core/vision_detect_postprocess.h"
#include "railhmi/core/vision_preprocess.h"

namespace {

using namespace railhmi::core;

const float kInW = 640.f, kInH = 640.f;

LetterboxParams lb() {
    LetterboxParams p;
    compute_letterbox(1280, 720, 640, 640, &p, nullptr);
    return p;
}

struct FloatFx {
    std::vector<std::vector<float>> planes;
    std::vector<RawDetectTensor> views;
    void add(int s) {
        const int H = 80 >> s;
        planes.emplace_back(static_cast<std::size_t>(3) * 85 * H * H, 0.0f);
        RawDetectTensor t;
        t.channels = 255;
        t.height = H;
        t.width = H;
        t.dtype = TensorType::Float32;
        t.is_float = true;
        t.scale = 1.f;
        t.zero_point = 0;
        views.push_back(t);
    }
    void bind() {
        for (std::size_t i = 0; i < planes.size(); ++i)
            views[i].data = reinterpret_cast<const std::uint8_t*>(planes[i].data());
    }
};

// 编码一个框：中心 (mx,my)、尺寸 (wpx,hpx) 为模型像素；返回 false 表示超出该尺度可表示范围。
bool encode(FloatFx* fx, int s, int a, double mx, double my, double wpx, double hpx, double obj,
            int cls, double clsprob) {
    const int H = 80 >> s;
    const double stride = 640.0 / H;
    const double uh = mx / stride, uv = my / stride;
    const int j = static_cast<int>(std::floor(uh));
    const int i = static_cast<int>(std::floor(uv));
    if (i < 0 || j < 0 || i >= H || j >= H) return false;
    const double v_cx = (uh - j + 0.5) / 2.0;
    const double v_cy = (uv - i + 0.5) / 2.0;
    const std::vector<int> an = default_yolov5_anchors();
    const double aw = an[s * 6 + a * 2], ah = an[s * 6 + a * 2 + 1];
    const double v_bw = std::sqrt(wpx / aw) / 2.0, v_bh = std::sqrt(hpx / ah) / 2.0;
    if (v_cx < 0 || v_cx > 1 || v_cy < 0 || v_cy > 1 || v_bw < 0 || v_bw > 1 || v_bh < 0 || v_bh > 1)
        return false;
    const long long gl = static_cast<long long>(H) * H;
    const long long base = static_cast<long long>(a) * 85 * gl;
    const long long pix = static_cast<long long>(i) * H + j;
    auto set = [&](long long idx, double v) { fx->planes[s][idx] = static_cast<float>(v); };
    set(base + 0 * gl + pix, v_cx);
    set(base + 1 * gl + pix, v_cy);
    set(base + 2 * gl + pix, v_bw);
    set(base + 3 * gl + pix, v_bh);
    set(base + 4 * gl + pix, obj);
    set(base + static_cast<long long>(5 + cls) * gl + pix, clsprob);
    return true;
}

DetectConfig cfg(float thr = 0.25f, float nms = 0.45f, std::vector<int> wl = {0}) {
    DetectConfig c;
    c.input_w = 640;
    c.input_h = 640;
    c.score_threshold = thr;
    c.nms_iou = nms;
    c.class_whitelist = wl;
    c.num_classes = 80;
    c.anchors = default_yolov5_anchors();
    return c;
}

FloatFx person_fx() {
    FloatFx fx;
    for (int s = 0; s < 3; ++s) fx.add(s);
    encode(&fx, 0, 2, 232.0, 332.0, 64.0, 64.0, 0.9, 0, 0.9);
    fx.bind();
    return fx;
}

void test_decode_person() {
    FloatFx fx = person_fx();
    DetectResult r;
    std::string e;
    const LetterboxParams p = lb();
    railhmi_test::check(decode_yolov5(fx.views, cfg(), p, 1280, 720, &r, &e), "decode ok", e);
    railhmi_test::check(r.ok && r.boxes.size() == 1, "one person box", std::to_string(r.boxes.size()));
    if (r.boxes.size() != 1) return;
    const DetectBox& b = r.boxes[0];
    // 模型框 (200,300)-(264,364) -> 原图 (400,320)-(528,448) -> 归一化
    railhmi_test::check(std::fabs(b.x - 400.0 / 1280.0) < 1e-3, "x inverse letterbox");
    railhmi_test::check(std::fabs(b.y - 320.0 / 720.0) < 1e-3, "y inverse letterbox");
    railhmi_test::check(std::fabs(b.w - 128.0 / 1280.0) < 1e-3, "w normalized");
    railhmi_test::check(std::fabs(b.h - 128.0 / 720.0) < 1e-3, "h normalized");
    railhmi_test::check(std::fabs(b.score - 0.81) < 2e-3, "score=obj*cls",
                        std::to_string(b.score));
    railhmi_test::check(b.class_id == 0 && b.label == "person", "class whitelist person");
    railhmi_test::check(b.scale_index == 0 && b.anchor_index == 2, "scale/anchor index");
    railhmi_test::check(b.x >= 0 && b.y >= 0 && b.x + b.w <= 1.f + 1e-6 && b.y + b.h <= 1.f + 1e-6,
                        "bounds invariant");
}

void test_whitelist_and_empty() {
    {
        FloatFx fx;
        for (int s = 0; s < 3; ++s) fx.add(s);
        encode(&fx, 0, 2, 232.0, 332.0, 64.0, 64.0, 0.9, 2, 0.95);  // car
        fx.bind();
        DetectResult r;
        std::string e;
        railhmi_test::check(decode_yolov5(fx.views, cfg(), lb(), 1280, 720, &r, &e), "nonperson ok");
        railhmi_test::check(r.boxes.empty() && r.dropped_class == 1, "nonperson dropped+counted",
                            "boxes=" + std::to_string(r.boxes.size()) + " dropped=" +
                                std::to_string(r.dropped_class));
    }
    {
        // 扩大白名单后可保留 car
        FloatFx fx;
        for (int s = 0; s < 3; ++s) fx.add(s);
        encode(&fx, 0, 2, 232.0, 332.0, 64.0, 64.0, 0.9, 2, 0.95);
        fx.bind();
        DetectResult r;
        std::string e;
        railhmi_test::check(decode_yolov5(fx.views, cfg(0.25f, 0.45f, {0, 2}), lb(), 1280, 720, &r, &e) &&
                                r.boxes.size() == 1 && r.boxes[0].class_id == 2,
                            "whitelist configurable");
    }
    {
        FloatFx fx;
        for (int s = 0; s < 3; ++s) fx.add(s);
        fx.bind();
        DetectResult r;
        std::string e;
        railhmi_test::check(decode_yolov5(fx.views, cfg(), lb(), 1280, 720, &r, &e) && r.ok &&
                                r.boxes.empty() && r.reason == detect_reason::kEmpty(),
                            "empty image -> ok empty");
    }
}

void test_threshold_and_nms() {
    {
        FloatFx fx = person_fx();
        DetectResult r;
        std::string e;
        railhmi_test::check(decode_yolov5(fx.views, cfg(0.9f), lb(), 1280, 720, &r, &e) &&
                                r.boxes.empty() && r.dropped_score >= 1,
                            "score threshold filters candidate");
    }
    {
        FloatFx fx;
        for (int s = 0; s < 3; ++s) fx.add(s);
        encode(&fx, 0, 2, 232.0, 332.0, 64.0, 64.0, 0.9, 0, 0.9);
        encode(&fx, 0, 2, 240.0, 340.0, 64.0, 64.0, 0.8, 0, 0.8);
        fx.bind();
        DetectResult r;
        std::string e;
        railhmi_test::check(decode_yolov5(fx.views, cfg(), lb(), 1280, 720, &r, &e) &&
                                r.boxes.size() == 1 && r.nms_suppressed == 1,
                            "nms suppresses overlap");
        DetectResult r2;
        railhmi_test::check(decode_yolov5(fx.views, cfg(0.25f, 0.99f), lb(), 1280, 720, &r2, &e) &&
                                r2.boxes.size() == 2,
                            "nms iou threshold configurable");
    }
}

void test_bounds_and_bad_tensor() {
    {
        FloatFx fx;
        for (int s = 0; s < 3; ++s) fx.add(s);
        encode(&fx, 0, 0, 100.0, 60.0, 32.0, 32.0, 0.9, 0, 0.9);  // 在 pad 区
        fx.bind();
        DetectResult r;
        std::string e;
        railhmi_test::check(decode_yolov5(fx.views, cfg(), lb(), 1280, 720, &r, &e) &&
                                r.boxes.empty() && r.dropped_bounds >= 1,
                            "pad-region box dropped+counted");
    }
    {
        FloatFx fx;
        fx.add(0);
        fx.views[0].channels = 252;
        fx.bind();
        DetectResult r;
        std::string e;
        railhmi_test::check(!decode_yolov5(fx.views, cfg(), lb(), 1280, 720, &r, &e) &&
                                r.reason == detect_reason::kBadTensor(),
                            "bad channels -> DET_BAD_TENSOR", e);
    }
    {
        FloatFx fx;
        fx.add(0);
        fx.views[0].data = nullptr;
        DetectResult r;
        std::string e;
        railhmi_test::check(!decode_yolov5(fx.views, cfg(), lb(), 1280, 720, &r, &e) &&
                                r.reason == detect_reason::kBadTensor(),
                            "null data -> DET_BAD_TENSOR");
    }
    {
        DetectResult r;
        std::string e;
        railhmi_test::check(!decode_yolov5({}, cfg(1.5f), lb(), 1280, 720, &r, &e) &&
                                r.reason == detect_reason::kBadConfig(),
                            "bad config -> DET_BAD_CONFIG");
    }
}

void test_truncation_and_iou() {
    {
        FloatFx fx;
        for (int s = 0; s < 3; ++s) fx.add(s);
        // 同一尺度内 3 个互不重叠的人形框（均在画面内，避开 letterbox 补边区）
        encode(&fx, 0, 2, 150.0, 260.0, 40.0, 40.0, 0.9, 0, 0.9);
        encode(&fx, 0, 2, 350.0, 280.0, 40.0, 40.0, 0.8, 0, 0.8);
        encode(&fx, 0, 2, 500.0, 400.0, 40.0, 40.0, 0.7, 0, 0.7);
        fx.bind();
        DetectResult r;
        std::string e;
        DetectConfig c = cfg();
        c.max_detections = 2;
        railhmi_test::check(decode_yolov5(fx.views, c, lb(), 1280, 720, &r, &e) && r.boxes.size() == 2 &&
                                r.truncated == 1,
                            "max_detections truncates");
        // 分数降序
        railhmi_test::check(r.boxes.size() == 2 && r.boxes[0].score >= r.boxes[1].score,
                            "boxes sorted by score desc");
    }
    railhmi_test::check(box_iou_plus1(0, 0, 10, 10, 0, 0, 10, 10) > 0.99f, "iou identical");
    railhmi_test::check(box_iou_plus1(0, 0, 10, 10, 100, 100, 110, 110) == 0.f, "iou disjoint");
}

void test_inverse_letterbox_unit() {
    const LetterboxParams p = lb();
    float x, y, w, h;
    railhmi_test::check(inverse_letterbox_normalize(0, 140, 640, 500, p, 1280, 720, &x, &y, &w, &h),
                        "inverse ok");
    railhmi_test::check(std::fabs(w - 1.0f) < 1e-4 && std::fabs(y) < 1e-4, "inverse full-width",
                        std::to_string(w) + "/" + std::to_string(y));
    // 完全在 pad 区
    railhmi_test::check(!inverse_letterbox_normalize(0, 0, 100, 100, p, 1280, 720, &x, &y, &w, &h),
                        "inverse pad-only rejected");
    // 右/下越界被裁剪
    railhmi_test::check(inverse_letterbox_normalize(600, 300, 900, 800, p, 1280, 720, &x, &y, &w, &h) &&
                            x + w <= 1.f + 1e-6 && y + h <= 1.f + 1e-6,
                        "inverse clamps to frame");
}

void test_int8_dequant_path() {
    // 用 int8 + 真实模型风格 scale/zp 走一遍，确认量化反解路径可用。
    const float scale = 0.00392157f;
    const int32_t zp = -128;
    std::vector<std::vector<std::uint8_t>> planes;
    std::vector<RawDetectTensor> views;
    for (int s = 0; s < 3; ++s) {
        const int H = 80 >> s;
        // int8 空背景写 q(0.0)（zp=-128 -> byte 0x80），不是字节 0
        planes.emplace_back(static_cast<std::size_t>(255) * H * H,
                            static_cast<std::uint8_t>(static_cast<std::int8_t>(zp)));
        RawDetectTensor t;
        t.channels = 255;
        t.height = H;
        t.width = H;
        t.dtype = TensorType::Int8;
        t.is_float = false;
        t.scale = scale;
        t.zero_point = zp;
        views.push_back(t);
    }
    for (std::size_t i = 0; i < planes.size(); ++i) views[i].data = planes[i].data();
    // 直接写 int8：中心与尺寸沿用 person 夹具的编码值
    const int H = 80;
    const long long gl = static_cast<long long>(H) * H;
    const double stride = 640.0 / H;
    const double mx = 232.0, my = 332.0, wpx = 64.0, hpx = 64.0;
    const int j = static_cast<int>(mx / stride), i = static_cast<int>(my / stride);
    const std::vector<int> an = default_yolov5_anchors();
    const double v_cx = (mx / stride - j + 0.5) / 2.0, v_cy = (my / stride - i + 0.5) / 2.0;
    const double v_bw = std::sqrt(wpx / an[2 * 2]) / 2.0, v_bh = std::sqrt(hpx / an[2 * 2 + 1]) / 2.0;
    auto put = [&](long long idx, double v) {
        double q = std::round(v / scale) + zp;
        q = std::max(-128.0, std::min(127.0, q));
        planes[0][idx] = static_cast<std::uint8_t>(static_cast<std::int8_t>(q));
    };
    const long long base = 2ll * 85 * gl;
    const long long pix = static_cast<long long>(i) * H + j;
    put(base + 0 * gl + pix, v_cx);
    put(base + 1 * gl + pix, v_cy);
    put(base + 2 * gl + pix, v_bw);
    put(base + 3 * gl + pix, v_bh);
    put(base + 4 * gl + pix, 0.9);
    put(base + 5 * gl + pix, 0.9);
    DetectResult r;
    std::string e;
    railhmi_test::check(decode_yolov5(views, cfg(), lb(), 1280, 720, &r, &e) && r.boxes.size() == 1,
                        "int8 decode one box", e);
    if (!r.boxes.empty()) {
        railhmi_test::check(std::fabs(r.boxes[0].x - 400.0 / 1280.0) < 0.01, "int8 x within quant tol");
        railhmi_test::check(std::fabs(r.boxes[0].score - 0.81) < 0.02, "int8 score within quant tol");
    }
}

}  // namespace

void run_vision_detect_postprocess_tests() {
    test_decode_person();
    test_whitelist_and_empty();
    test_threshold_and_nms();
    test_bounds_and_bad_tensor();
    test_truncation_and_iou();
    test_inverse_letterbox_unit();
    test_int8_dequant_path();
}
