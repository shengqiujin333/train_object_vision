// railhmi-core: YOLOv5 人形检测后处理实现（视觉推理 ITEM-003）。纯逻辑，仅标准库。

#include "railhmi/core/vision_detect_postprocess.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace railhmi {
namespace core {

double tensor_dequant(const RawDetectTensor& t, std::size_t index) {
    if (t.is_float || t.dtype == TensorType::Float32) {
        float f = 0.0f;
        std::memcpy(&f, t.data + index * sizeof(float), sizeof(float));
        return static_cast<double>(f);
    }
    double q = 0.0;
    if (t.dtype == TensorType::Int8) {
        q = static_cast<double>(reinterpret_cast<const std::int8_t*>(t.data)[index]);
    } else {
        q = static_cast<double>(t.data[index]);
    }
    return (q - static_cast<double>(t.zero_point)) * static_cast<double>(t.scale);
}

std::size_t tensor_index_nchw(const RawDetectTensor& t, int c, int h, int w) {
    return static_cast<std::size_t>(c) * static_cast<std::size_t>(t.height) * t.width +
           static_cast<std::size_t>(h) * t.width + static_cast<std::size_t>(w);
}

namespace {

bool fail(std::string* err, const char* reason, const std::string& detail = std::string()) {
    if (err) *err = detail.empty() ? std::string(reason) : (std::string(reason) + ": " + detail);
    return false;
}

double sigmoid(double x) { return 1.0 / (1.0 + std::exp(-x)); }

double dequant(const RawDetectTensor& t, std::size_t index) { return tensor_dequant(t, index); }

bool is_whitelisted(const DetectConfig& cfg, int cls) {
    return std::find(cfg.class_whitelist.begin(), cfg.class_whitelist.end(), cls) !=
           cfg.class_whitelist.end();
}

struct Cand {
    float mx1 = 0, my1 = 0, mx2 = 0, my2 = 0;  // 模型像素空间
    float score = 0;
    int cls = 0;
    int scale = -1, anchor = -1, ci = -1, cj = -1;
};

}  // namespace

std::vector<int> default_yolov5_anchors() {
    return {10, 13, 16, 30, 33, 23, 30, 61, 62, 45, 59, 119, 116, 90, 156, 198, 373, 326};
}

std::string detect_label(int class_id) {
    if (class_id == 0) return "person";
    return "cls_" + std::to_string(class_id);
}

float box_iou_plus1(float ax1, float ay1, float ax2, float ay2, float bx1, float by1, float bx2,
                    float by2) {
    const float w = std::max(0.f, std::min(ax2, bx2) - std::max(ax1, bx1) + 1.0f);
    const float h = std::max(0.f, std::min(ay2, by2) - std::max(ay1, by1) + 1.0f);
    const float i = w * h;
    const float u = (ax2 - ax1 + 1.0f) * (ay2 - ay1 + 1.0f) + (bx2 - bx1 + 1.0f) * (by2 - by1 + 1.0f) -
                    i;
    return u <= 0.f ? 0.f : (i / u);
}

bool inverse_letterbox_normalize(float mx1, float my1, float mx2, float my2,
                                 const LetterboxParams& lb, int src_w, int src_h, float* x,
                                 float* y, float* w, float* h) {
    if (!(lb.scale > 0.0) || src_w <= 0 || src_h <= 0) return false;
    const double rx1 = (static_cast<double>(mx1) - lb.pad_x) / lb.scale;
    const double ry1 = (static_cast<double>(my1) - lb.pad_y) / lb.scale;
    const double rx2 = (static_cast<double>(mx2) - lb.pad_x) / lb.scale;
    const double ry2 = (static_cast<double>(my2) - lb.pad_y) / lb.scale;
    const double cx1 = std::max(0.0, std::min(rx1, static_cast<double>(src_w)));
    const double cy1 = std::max(0.0, std::min(ry1, static_cast<double>(src_h)));
    const double cx2 = std::max(0.0, std::min(rx2, static_cast<double>(src_w)));
    const double cy2 = std::max(0.0, std::min(ry2, static_cast<double>(src_h)));
    if (!(cx2 > cx1) || !(cy2 > cy1)) return false;
    double nx = cx1 / src_w;
    double ny = cy1 / src_h;
    double nw = (cx2 - cx1) / src_w;
    double nh = (cy2 - cy1) / src_h;
    nx = std::max(0.0, std::min(nx, 1.0));
    ny = std::max(0.0, std::min(ny, 1.0));
    if (nx + nw > 1.0) nw = 1.0 - nx;
    if (ny + nh > 1.0) nh = 1.0 - ny;
    if (!(nw > 0.0) || !(nh > 0.0)) return false;
    *x = static_cast<float>(nx);
    *y = static_cast<float>(ny);
    *w = static_cast<float>(nw);
    *h = static_cast<float>(nh);
    return true;
}

bool decode_yolov5(const std::vector<RawDetectTensor>& outputs, const DetectConfig& cfg,
                   const LetterboxParams& letterbox, int src_w, int src_h, DetectResult* out,
                   std::string* err) {
    if (err) err->clear();
    if (!out) return fail(err, detect_reason::kBadConfig(), "null output");
    *out = DetectResult{};
    if (cfg.input_w <= 0 || cfg.input_h <= 0 || cfg.num_classes <= 0 ||
        cfg.class_whitelist.empty()) {
        out->reason = detect_reason::kBadConfig();
        return fail(err, detect_reason::kBadConfig(), "non-positive input/classes or empty whitelist");
    }
    if (cfg.score_threshold < 0.f || cfg.score_threshold > 1.f || cfg.nms_iou < 0.f ||
        cfg.nms_iou > 1.f) {
        out->reason = detect_reason::kBadConfig();
        return fail(err, detect_reason::kBadConfig(), "thresholds must be in [0,1]");
    }
    if (outputs.empty()) {
        out->ok = true;
        out->reason = detect_reason::kEmpty();
        out->detail = "no output tensors";
        return true;
    }
    std::vector<int> anchors = cfg.anchors.empty() ? default_yolov5_anchors() : cfg.anchors;
    if (anchors.size() < outputs.size() * 6u) {
        out->reason = detect_reason::kBadConfig();
        return fail(err, detect_reason::kBadConfig(),
                    "anchors too few: " + std::to_string(anchors.size()));
    }

    std::vector<Cand> cands;
    for (std::size_t s = 0; s < outputs.size(); ++s) {
        const RawDetectTensor& t = outputs[s];
        const std::string tag = "scale[" + std::to_string(s) + "]";
        if (!t.data || t.height <= 0 || t.width <= 0) {
            out->reason = detect_reason::kBadTensor();
            return fail(err, detect_reason::kBadTensor(), tag + " null/empty");
        }
        const int anchors_per_scale = 3;
        if (t.channels != anchors_per_scale * (5 + cfg.num_classes)) {
            out->reason = detect_reason::kBadTensor();
            return fail(err, detect_reason::kBadTensor(),
                        tag + " channels=" + std::to_string(t.channels) + " expected=" +
                            std::to_string(anchors_per_scale * (5 + cfg.num_classes)));
        }
        if (!t.is_float && t.dtype != TensorType::Int8 && t.dtype != TensorType::Uint8) {
            out->reason = detect_reason::kBadTensor();
            return fail(err, detect_reason::kBadTensor(), tag + " unsupported dtype");
        }
        const int grid_h = t.height;
        const int grid_w = t.width;
        const long long grid_len = static_cast<long long>(grid_h) * grid_w;
        const int stride_h = cfg.input_h / grid_h;
        const int stride_w = cfg.input_w / grid_w;
        if (stride_h <= 0 || stride_w <= 0) {
            out->reason = detect_reason::kBadTensor();
            return fail(err, detect_reason::kBadTensor(), tag + " stride invalid");
        }
        for (int a = 0; a < anchors_per_scale; ++a) {
            const double aw = anchors[static_cast<std::size_t>(s) * 6 + a * 2 + 0];
            const double ah = anchors[static_cast<std::size_t>(s) * 6 + a * 2 + 1];
            const long long base = static_cast<long long>(a) * (5 + cfg.num_classes) * grid_len;
            for (int i = 0; i < grid_h; ++i) {
                for (int j = 0; j < grid_w; ++j) {
                    const long long pix = static_cast<long long>(i) * grid_w + j;
                    double obj = dequant(t, static_cast<std::size_t>(base + 4 * grid_len + pix));
                    if (cfg.apply_sigmoid) obj = sigmoid(obj);
                    // 类别 argmax
                    int best_cls = 0;
                    double best = -1e30;
                    for (int k = 0; k < cfg.num_classes; ++k) {
                        double p = dequant(t, static_cast<std::size_t>(
                                                 base + static_cast<long long>(5 + k) * grid_len + pix));
                        if (cfg.apply_sigmoid) p = sigmoid(p);
                        if (p > best) {
                            best = p;
                            best_cls = k;
                        }
                    }
                    const double score = obj * best;
                    if (score < static_cast<double>(cfg.score_threshold)) {
                        ++out->dropped_score;
                        continue;
                    }
                    ++out->candidates;
                    if (!is_whitelisted(cfg, best_cls)) {
                        ++out->dropped_class;
                        continue;
                    }
                    double cx = dequant(t, static_cast<std::size_t>(base + 0 * grid_len + pix));
                    double cy = dequant(t, static_cast<std::size_t>(base + 1 * grid_len + pix));
                    double bw = dequant(t, static_cast<std::size_t>(base + 2 * grid_len + pix));
                    double bh = dequant(t, static_cast<std::size_t>(base + 3 * grid_len + pix));
                    if (cfg.apply_sigmoid) {
                        cx = sigmoid(cx);
                        cy = sigmoid(cy);
                        bw = sigmoid(bw);
                        bh = sigmoid(bh);
                    }
                    const double bx = (cx * 2.0 - 0.5 + j) * stride_w;
                    const double by = (cy * 2.0 - 0.5 + i) * stride_h;
                    const double wpx = (bw * 2.0) * (bw * 2.0) * aw;
                    const double hpx = (bh * 2.0) * (bh * 2.0) * ah;
                    Cand c;
                    c.mx1 = static_cast<float>(bx - wpx / 2.0);
                    c.my1 = static_cast<float>(by - hpx / 2.0);
                    c.mx2 = static_cast<float>(bx + wpx / 2.0);
                    c.my2 = static_cast<float>(by + hpx / 2.0);
                    c.score = static_cast<float>(score);
                    c.cls = best_cls;
                    c.scale = static_cast<int>(s);
                    c.anchor = a;
                    c.ci = i;
                    c.cj = j;
                    cands.push_back(c);
                }
            }
        }
    }

    if (cands.empty()) {
        out->ok = true;
        out->reason = detect_reason::kEmpty();
        out->detail = "no candidate above threshold";
        return true;
    }

    // 逆 letterbox -> 原图归一化（越界丢弃）。
    std::vector<DetectBox> boxes;
    boxes.reserve(cands.size());
    for (const Cand& c : cands) {
        float x = 0, y = 0, w = 0, h = 0;
        if (!inverse_letterbox_normalize(c.mx1, c.my1, c.mx2, c.my2, letterbox, src_w, src_h, &x, &y,
                                         &w, &h)) {
            ++out->dropped_bounds;
            continue;
        }
        DetectBox b;
        b.x = x; b.y = y; b.w = w; b.h = h;
        b.score = c.score;
        b.class_id = c.cls;
        b.label = detect_label(c.cls);
        b.scale_index = c.scale;
        b.anchor_index = c.anchor;
        b.cell_i = c.ci;
        b.cell_j = c.cj;
        boxes.push_back(b);
    }

    // 按类别分组 NMS（模型像素空间，+1 IoU 与历史实现一致）。
    std::vector<DetectBox> kept;
    std::vector<char> removed(boxes.size(), 0);
    std::vector<std::size_t> order(boxes.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&boxes](std::size_t a, std::size_t b) { return boxes[a].score > boxes[b].score; });
    // 每个类独立贪心（白名单通常只有 person）。
    std::vector<int> classes;
    for (const auto& b : boxes) {
        if (std::find(classes.begin(), classes.end(), b.class_id) == classes.end())
            classes.push_back(b.class_id);
    }
    for (int cls : classes) {
        for (std::size_t oi = 0; oi < order.size(); ++oi) {
            const std::size_t i = order[oi];
            if (removed[i] || boxes[i].class_id != cls) continue;
            for (std::size_t oj = oi + 1; oj < order.size(); ++oj) {
                const std::size_t j = order[oj];
                if (removed[j] || boxes[j].class_id != cls) continue;
                // 用模型空间坐标做 IoU；boxes 只存归一化，故在归一化空间用 +1 近似需要像素尺度。
                // 为保持与历史实现的 +1 像素语义，这里换算回模型像素（letterbox 线性可逆）。
                auto to_model = [&](const DetectBox& b, float* x1, float* y1, float* x2, float* y2) {
                    const double px1 = static_cast<double>(b.x) * src_w;
                    const double py1 = static_cast<double>(b.y) * src_h;
                    const double px2 = (static_cast<double>(b.x) + b.w) * src_w;
                    const double py2 = (static_cast<double>(b.y) + b.h) * src_h;
                    *x1 = static_cast<float>(px1 * letterbox.scale + letterbox.pad_x);
                    *y1 = static_cast<float>(py1 * letterbox.scale + letterbox.pad_y);
                    *x2 = static_cast<float>(px2 * letterbox.scale + letterbox.pad_x);
                    *y2 = static_cast<float>(py2 * letterbox.scale + letterbox.pad_y);
                };
                float ax1, ay1, ax2, ay2, bx1, by1, bx2, by2;
                to_model(boxes[i], &ax1, &ay1, &ax2, &ay2);
                to_model(boxes[j], &bx1, &by1, &bx2, &by2);
                if (box_iou_plus1(ax1, ay1, ax2, ay2, bx1, by1, bx2, by2) > cfg.nms_iou) {
                    removed[j] = 1;
                    ++out->nms_suppressed;
                }
            }
        }
    }
    for (std::size_t oi = 0; oi < order.size(); ++oi) {
        const std::size_t i = order[oi];
        if (!removed[i]) kept.push_back(boxes[i]);
    }
    // kept 已按 order（分数降序）收集。
    if (static_cast<int>(kept.size()) > cfg.max_detections) {
        out->truncated = static_cast<long long>(kept.size()) - cfg.max_detections;
        kept.resize(static_cast<std::size_t>(cfg.max_detections));
    }
    out->boxes = kept;
    out->ok = true;
    out->reason = kept.empty() ? detect_reason::kEmpty() : detect_reason::kOk();
    out->detail = "boxes=" + std::to_string(kept.size());
    return true;
}

}  // namespace core
}  // namespace railhmi
