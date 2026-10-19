// railhmi-core: YOLOv8-seg 轨道分割后处理实现（视觉推理 ITEM-004）。纯逻辑，仅标准库。

#include "railhmi/core/vision_seg_postprocess.h"

#include <algorithm>
#include <cmath>

namespace railhmi {
namespace core {

namespace {

bool fail(std::string* err, const char* reason, const std::string& detail = std::string()) {
    if (err) *err = detail.empty() ? std::string(reason) : (std::string(reason) + ": " + detail);
    return false;
}

bool is_whitelisted(const SegConfig& cfg, int cls) {
    return std::find(cfg.class_whitelist.begin(), cfg.class_whitelist.end(), cls) !=
           cfg.class_whitelist.end();
}

std::string seg_label(int cls) { return cls == 0 ? std::string("rail") : ("cls_" + std::to_string(cls)); }

// DFL：对每个方向在 dfl_len 个 bin 上做 softmax 并取加权和（对齐历史实现）。
void compute_dfl(const float* t, int dfl_len, float* box4) {
    for (int b = 0; b < 4; ++b) {
        const float* p = t + static_cast<std::size_t>(b) * dfl_len;
        float mx = p[0];
        for (int i = 1; i < dfl_len; ++i) mx = std::max(mx, p[i]);
        double sum = 0.0, acc = 0.0;
        for (int i = 0; i < dfl_len; ++i) {
            const double e = std::exp(static_cast<double>(p[i] - mx));
            sum += e;
            acc += e * i;
        }
        box4[b] = sum > 0.0 ? static_cast<float>(acc / sum) : 0.f;
    }
}

// 双线性重采样（align_corners=false），与预处理/历史 resize 口径一致。
void resize_bilinear(const std::vector<float>& src, int sw, int sh, std::vector<float>* dst, int dw,
                     int dh) {
    dst->assign(static_cast<std::size_t>(dw) * dh, 0.f);
    const double rx = static_cast<double>(sw) / dw;
    const double ry = static_cast<double>(sh) / dh;
    for (int y = 0; y < dh; ++y) {
        double fy = (y + 0.5) * ry - 0.5;
        fy = std::max(0.0, std::min(fy, static_cast<double>(sh - 1)));
        int y0 = static_cast<int>(std::floor(fy));
        int y1 = std::min(y0 + 1, sh - 1);
        double wy = fy - y0;
        for (int x = 0; x < dw; ++x) {
            double fx = (x + 0.5) * rx - 0.5;
            fx = std::max(0.0, std::min(fx, static_cast<double>(sw - 1)));
            int x0 = static_cast<int>(std::floor(fx));
            int x1 = std::min(x0 + 1, sw - 1);
            double wx = fx - x0;
            const double v00 = src[static_cast<std::size_t>(y0) * sw + x0];
            const double v10 = src[static_cast<std::size_t>(y0) * sw + x1];
            const double v01 = src[static_cast<std::size_t>(y1) * sw + x0];
            const double v11 = src[static_cast<std::size_t>(y1) * sw + x1];
            (*dst)[static_cast<std::size_t>(y) * dw + x] = static_cast<float>(
                (1 - wx) * (1 - wy) * v00 + wx * (1 - wy) * v10 + (1 - wx) * wy * v01 + wx * wy * v11);
        }
    }
}

double sample_bilinear(const std::vector<float>& m, int w, int h, double x, double y) {
    x = std::max(0.0, std::min(x, static_cast<double>(w - 1)));
    y = std::max(0.0, std::min(y, static_cast<double>(h - 1)));
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const double wx = x - x0;
    const double wy = y - y0;
    const double v00 = m[static_cast<std::size_t>(y0) * w + x0];
    const double v10 = m[static_cast<std::size_t>(y0) * w + x1];
    const double v01 = m[static_cast<std::size_t>(y1) * w + x0];
    const double v11 = m[static_cast<std::size_t>(y1) * w + x1];
    return (1 - wx) * (1 - wy) * v00 + wx * (1 - wy) * v10 + (1 - wx) * wy * v01 + wx * wy * v11;
}

struct Cand {
    float mx1 = 0, my1 = 0, mx2 = 0, my2 = 0;
    float score = 0;
    int cls = 0;
    int scale = -1, ci = -1, cj = -1;
    std::vector<float> coeff;  // proto_channels
};

// 校验分支张量形状；不通过时给出具体项。
bool check_branch(const std::vector<RawDetectTensor>& o, int base, const SegConfig& cfg, int* dfl_len,
                  std::string* why) {
    const RawDetectTensor& box = o[base + 0];
    const RawDetectTensor& score = o[base + 1];
    const RawDetectTensor& ssum = o[base + 2];
    const RawDetectTensor& seg = o[base + 3];
    const std::string tag = "branch[" + std::to_string(base / 4) + "].";
    for (const auto* t : {&box, &score, &ssum, &seg}) {
        if (!t->data || t->height <= 0 || t->width <= 0) {
            *why = tag + "null/empty tensor";
            return false;
        }
        if (t->fmt != TensorFormat::Nchw) {
            *why = tag + "unsupported fmt (expect nchw)";
            return false;
        }
    }
    if (box.channels <= 0 || box.channels % 4 != 0) {
        *why = tag + "box channels=" + std::to_string(box.channels) + " not 4*dfl";
        return false;
    }
    if (score.channels != cfg.num_classes) {
        *why = tag + "score channels=" + std::to_string(score.channels) +
               " expected=" + std::to_string(cfg.num_classes);
        return false;
    }
    if (ssum.channels != 1) {
        *why = tag + "score_sum channels=" + std::to_string(ssum.channels) + " expected=1";
        return false;
    }
    if (seg.channels != cfg.proto_channels) {
        *why = tag + "seg channels=" + std::to_string(seg.channels) +
               " expected=" + std::to_string(cfg.proto_channels);
        return false;
    }
    if (score.height != box.height || score.width != box.width || ssum.height != box.height ||
        seg.height != box.height) {
        *why = tag + "spatial mismatch across branch tensors";
        return false;
    }
    *dfl_len = box.channels / 4;
    return true;
}

}  // namespace

bool decode_yolov8_seg(const std::vector<RawDetectTensor>& outputs, const SegConfig& cfg,
                       const LetterboxParams& letterbox, int src_w, int src_h, SegResult* out,
                       std::string* err) {
    if (err) err->clear();
    if (!out) return fail(err, seg_reason::kBadConfig(), "null output");
    *out = SegResult{};
    if (cfg.input_w <= 0 || cfg.input_h <= 0 || cfg.num_classes <= 0 || cfg.proto_channels <= 0 ||
        cfg.proto_h <= 0 || cfg.proto_w <= 0 || cfg.class_whitelist.empty()) {
        out->reason = seg_reason::kBadConfig();
        return fail(err, seg_reason::kBadConfig(), "non-positive input/classes/proto or empty whitelist");
    }
    if (cfg.score_threshold < 0.f || cfg.score_threshold > 1.f || cfg.nms_iou < 0.f ||
        cfg.nms_iou > 1.f) {
        out->reason = seg_reason::kBadConfig();
        return fail(err, seg_reason::kBadConfig(), "thresholds must be in [0,1]");
    }

    const int expected = expected_seg_tensor_count();
    if (static_cast<int>(outputs.size()) != expected) {
        out->reason = seg_reason::kModelMismatch();
        return fail(err, seg_reason::kModelMismatch(),
                    "tensor count=" + std::to_string(outputs.size()) + " expected=" +
                        std::to_string(expected));
    }
    for (int s = 0; s < 3; ++s) {
        int dfl = 0;
        std::string why;
        if (!check_branch(outputs, s * 4, cfg, &dfl, &why)) {
            out->reason = seg_reason::kModelMismatch();
            return fail(err, seg_reason::kModelMismatch(), why);
        }
    }
    const RawDetectTensor& proto_t = outputs[12];
    if (!proto_t.data || proto_t.fmt != TensorFormat::Nchw || proto_t.channels != cfg.proto_channels ||
        proto_t.height != cfg.proto_h || proto_t.width != cfg.proto_w) {
        out->reason = seg_reason::kModelMismatch();
        return fail(err, seg_reason::kModelMismatch(),
                    "proto shape " + std::to_string(proto_t.channels) + "x" +
                        std::to_string(proto_t.height) + "x" + std::to_string(proto_t.width) +
                        " expected " + std::to_string(cfg.proto_channels) + "x" +
                        std::to_string(cfg.proto_h) + "x" + std::to_string(cfg.proto_w));
    }

    // proto 反量化为 float（COLS_B = proto_h*proto_w）。
    const std::size_t proto_plane =
        static_cast<std::size_t>(cfg.proto_h) * static_cast<std::size_t>(cfg.proto_w);
    std::vector<float> proto(static_cast<std::size_t>(cfg.proto_channels) * proto_plane);
    for (int k = 0; k < cfg.proto_channels; ++k)
        for (std::size_t i = 0; i < proto_plane; ++i)
            proto[static_cast<std::size_t>(k) * proto_plane + i] =
                static_cast<float>(tensor_dequant(proto_t, static_cast<std::size_t>(k) * proto_plane + i));

    // 三尺度解码。
    std::vector<Cand> cands;
    for (int s = 0; s < 3; ++s) {
        const int base = s * 4;
        const RawDetectTensor& box = outputs[base + 0];
        const RawDetectTensor& score = outputs[base + 1];
        const RawDetectTensor& ssum = outputs[base + 2];
        const RawDetectTensor& seg = outputs[base + 3];
        const int H = box.height, W = box.width;
        const int dfl_len = box.channels / 4;
        const std::size_t gl = static_cast<std::size_t>(H) * W;
        const double stride_w = static_cast<double>(cfg.input_w) / W;
        const double stride_h = static_cast<double>(cfg.input_h) / H;
        for (int i = 0; i < H; ++i) {
            for (int j = 0; j < W; ++j) {
                const std::size_t pix = static_cast<std::size_t>(i) * W + j;
                if (tensor_dequant(ssum, pix) < static_cast<double>(cfg.score_threshold)) {
                    ++out->dropped_score;
                    continue;
                }
                double best = -1e30;
                int best_cls = -1;
                for (int c = 0; c < cfg.num_classes; ++c) {
                    const double v = tensor_dequant(score, static_cast<std::size_t>(c) * gl + pix);
                    if (v > static_cast<double>(cfg.score_threshold) && v > best) {
                        best = v;
                        best_cls = c;
                    }
                }
                if (best_cls < 0) {
                    ++out->dropped_score;
                    continue;
                }
                ++out->candidates;
                if (!is_whitelisted(cfg, best_cls)) {
                    ++out->dropped_class;
                    continue;
                }
                std::vector<float> logits(static_cast<std::size_t>(dfl_len) * 4);
                for (std::size_t k = 0; k < logits.size(); ++k)
                    logits[k] = static_cast<float>(tensor_dequant(box, k * gl + pix));
                float d[4] = {0, 0, 0, 0};
                compute_dfl(logits.data(), dfl_len, d);
                Cand c;
                c.mx1 = static_cast<float>((-d[0] + j + 0.5) * stride_w);
                c.my1 = static_cast<float>((-d[1] + i + 0.5) * stride_h);
                c.mx2 = static_cast<float>((d[2] + j + 0.5) * stride_w);
                c.my2 = static_cast<float>((d[3] + i + 0.5) * stride_h);
                c.score = static_cast<float>(best);
                c.cls = best_cls;
                c.scale = s;
                c.ci = i;
                c.cj = j;
                c.coeff.resize(static_cast<std::size_t>(cfg.proto_channels));
                for (int k = 0; k < cfg.proto_channels; ++k)
                    c.coeff[static_cast<std::size_t>(k)] =
                        static_cast<float>(tensor_dequant(seg, static_cast<std::size_t>(k) * gl + pix));
                cands.push_back(std::move(c));
            }
        }
    }

    if (cands.empty()) {
        out->ok = true;
        out->reason = seg_reason::kEmpty();
        out->detail = "no candidate above threshold";
        return true;
    }

    // 类别分组 + 贪心 NMS（模型像素空间，+1 IoU 与历史实现一致）。
    std::vector<std::size_t> order(cands.size());
    for (std::size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(),
                     [&cands](std::size_t a, std::size_t b) { return cands[a].score > cands[b].score; });
    std::vector<char> removed(cands.size(), 0);
    std::vector<int> classes;
    for (const auto& c : cands)
        if (std::find(classes.begin(), classes.end(), c.cls) == classes.end()) classes.push_back(c.cls);
    for (int cls : classes) {
        for (std::size_t oi = 0; oi < order.size(); ++oi) {
            const std::size_t i = order[oi];
            if (removed[i] || cands[i].cls != cls) continue;
            for (std::size_t oj = oi + 1; oj < order.size(); ++oj) {
                const std::size_t j = order[oj];
                if (removed[j] || cands[j].cls != cls) continue;
                const Cand& a = cands[i];
                const Cand& b = cands[j];
                if (box_iou_plus1(a.mx1, a.my1, a.mx2, a.my2, b.mx1, b.my1, b.mx2, b.my2) > cfg.nms_iou) {
                    removed[j] = 1;
                    ++out->nms_suppressed;
                }
            }
        }
    }
    std::vector<std::size_t> kept;
    for (std::size_t oi = 0; oi < order.size(); ++oi) {
        if (!removed[order[oi]]) kept.push_back(order[oi]);
    }
    if (static_cast<int>(kept.size()) > cfg.max_detections) {
        out->truncated = static_cast<long long>(kept.size()) - cfg.max_detections;
        kept.resize(static_cast<std::size_t>(cfg.max_detections));
    }

    // 逐框生成掩码。
    std::vector<float> proto_mask(proto_plane);
    std::vector<float> model_mask;
    for (const std::size_t idx : kept) {
        const Cand& c = cands[idx];
        // 1) 系数 · proto（COLS_A=proto_channels）
        for (std::size_t i = 0; i < proto_plane; ++i) {
            double acc = 0.0;
            for (int k = 0; k < cfg.proto_channels; ++k)
                acc += static_cast<double>(c.coeff[static_cast<std::size_t>(k)]) *
                       static_cast<double>(proto[static_cast<std::size_t>(k) * proto_plane + i]);
            proto_mask[i] = static_cast<float>(acc);
        }
        // 2) 逆 letterbox + 框裁剪 + 阈值化 -> 原图尺寸二值 plane
        resize_bilinear(proto_mask, cfg.proto_w, cfg.proto_h, &model_mask, cfg.input_w, cfg.input_h);
        SegBox b;
        float x = 0, y = 0, w = 0, h = 0;
        if (!inverse_letterbox_normalize(c.mx1, c.my1, c.mx2, c.my2, letterbox, src_w, src_h, &x, &y,
                                         &w, &h)) {
            ++out->dropped_bounds;
            continue;
        }
        b.x = x; b.y = y; b.w = w; b.h = h;
        b.score = c.score;
        b.class_id = c.cls;
        b.label = seg_label(c.cls);
        b.scale_index = c.scale;
        b.cell_i = c.ci;
        b.cell_j = c.cj;
        SegMask m;
        m.box_index = static_cast<int>(out->boxes.size());
        m.width = src_w;
        m.height = src_h;
        m.stride_bytes = src_w;
        m.plane.assign(static_cast<std::size_t>(src_w) * src_h, 0);
        for (int oy = 0; oy < src_h; ++oy) {
            const double my = (oy + 0.5) * letterbox.scale - 0.5 + letterbox.pad_y;
            for (int ox = 0; ox < src_w; ++ox) {
                const double mx = (ox + 0.5) * letterbox.scale - 0.5 + letterbox.pad_x;
                if (cfg.crop_to_box &&
                    (mx < c.mx1 || mx >= c.mx2 || my < c.my1 || my >= c.my2)) {
                    continue;  // 框外为 0
                }
                const double v = sample_bilinear(model_mask, cfg.input_w, cfg.input_h, mx, my);
                if (v > static_cast<double>(cfg.mask_threshold)) {
                    m.plane[static_cast<std::size_t>(oy) * src_w + ox] = 1;
                    ++m.positive;
                }
            }
        }
        out->boxes.push_back(b);
        out->masks.push_back(std::move(m));
    }

    out->ok = true;
    out->reason = out->boxes.empty() ? seg_reason::kEmpty() : seg_reason::kOk();
    out->detail = "boxes=" + std::to_string(out->boxes.size()) + " masks=" +
                  std::to_string(out->masks.size());
    return true;
}

}  // namespace core
}  // namespace railhmi
