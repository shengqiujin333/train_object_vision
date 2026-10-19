// railhmi-vision-seg (视觉推理 ITEM-004)
//
// YOLOv8-seg 轨道分割后处理探针：13 输出解码（3 尺度 box/score/score_sum/seg 系数 + proto）、
// per-box 系数·proto 点积、阈值化、逆 letterbox 到原图、每框独立二值 plane；
// 张量数量/形状不符显式返回 MODEL_MISMATCH。
//
// 用法:
//   railhmi-vision-seg --selftest [--out log] [--json file]
//   railhmi-vision-seg --fixture rail|zero_coeff|empty|badcount|badsegshape|badproto|badfmt|int8|nms|nocrop
//                       [--conf X] [--nms-iou Y] [--mask-threshold T]
//   railhmi-vision-seg --config DIR [--conf X] [--mask-threshold T]
//
// 退出码: 0 通过；1 失败；2 参数错误。

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

#include "railhmi/core/config.h"
#include "railhmi/core/vision_seg_postprocess.h"

#ifndef RAILHMI_CONFIG_DIR
#define RAILHMI_CONFIG_DIR "config"
#endif

using namespace railhmi::core;

namespace {

int g_fail = 0;
std::vector<std::string> g_lines;
std::ofstream g_fo;

void emit(const std::string& l) {
    std::printf("%s\n", l.c_str());
    g_lines.push_back(l);
    if (g_fo) g_fo << l << "\n";
}
void check(bool ok, const std::string& name, const std::string& detail = "") {
    emit(std::string("[") + (ok ? "PASS" : "FAIL") + "] " + name + (detail.empty() ? "" : (" :: " + detail)));
    if (!ok) ++g_fail;
}
bool near(double a, double b, double tol = 1e-3) { return std::fabs(a - b) <= tol; }
int sh(int s) { return 80 >> s; }

struct SegFixture {
    std::vector<std::vector<float>> planes;
    std::vector<std::vector<std::uint8_t>> iplanes;
    std::vector<RawDetectTensor> views;
    bool int8_mode = false;
    int32_t zp = 0;
    float scale = 1.0f;

    void alloc(const SegConfig& cfg, bool int8m, int32_t z, float sc) {
        int8_mode = int8m;
        zp = z;
        scale = sc;
        for (int s = 0; s < 3; ++s) {
            const int H = sh(s);
            const int ch[4] = {4 * 16, cfg.num_classes, 1, cfg.proto_channels};
            for (int b = 0; b < 4; ++b) {
                RawDetectTensor t;
                t.channels = ch[b];
                t.height = H;
                t.width = H;
                t.dtype = int8_mode ? TensorType::Int8 : TensorType::Float32;
                t.is_float = !int8_mode;
                t.zero_point = int8_mode ? zp : 0;
                t.scale = int8_mode ? scale : 1.0f;
                if (int8_mode) iplanes.emplace_back(static_cast<std::size_t>(ch[b]) * H * H, 0);
                else planes.emplace_back(static_cast<std::size_t>(ch[b]) * H * H, 0.0f);
                views.push_back(t);
            }
        }
        RawDetectTensor p;
        p.channels = cfg.proto_channels;
        p.height = cfg.proto_h;
        p.width = cfg.proto_w;
        p.dtype = int8_mode ? TensorType::Int8 : TensorType::Float32;
        p.is_float = !int8_mode;
        p.zero_point = int8_mode ? zp : 0;
        p.scale = int8_mode ? scale : 1.0f;
        if (int8_mode)
            iplanes.emplace_back(static_cast<std::size_t>(cfg.proto_channels) * cfg.proto_h * cfg.proto_w, 0);
        else
            planes.emplace_back(static_cast<std::size_t>(cfg.proto_channels) * cfg.proto_h * cfg.proto_w, 0.0f);
        views.push_back(p);
    }
    void bind() {
        for (std::size_t i = 0; i < views.size(); ++i)
            views[i].data = int8_mode ? iplanes[i].data()
                                      : reinterpret_cast<const std::uint8_t*>(planes[i].data());
    }
    void put(int vi, std::size_t idx, double v) {
        if (int8_mode) {
            double q = std::round(v / static_cast<double>(scale)) + zp;
            q = std::max(-128.0, std::min(127.0, q));
            iplanes[vi][idx] = static_cast<std::uint8_t>(static_cast<std::int8_t>(q));
        } else {
            planes[vi][idx] = static_cast<float>(v);
        }
    }
    // proto：half 模式左半 +1、右半 -1；uniform 模式全 +1
    void fill_proto(const SegConfig& cfg, bool half) {
        const std::size_t pl = static_cast<std::size_t>(cfg.proto_h) * cfg.proto_w;
        for (int k = 0; k < cfg.proto_channels; ++k)
            for (int y = 0; y < cfg.proto_h; ++y)
                for (int x = 0; x < cfg.proto_w; ++x) {
                    double v = 1.0;
                    if (half && x >= cfg.proto_w / 2) v = -1.0;
                    put(12, static_cast<std::size_t>(k) * pl + static_cast<std::size_t>(y) * cfg.proto_w + x,
                        v);
                }
    }
    // 在尺度 s 的网格 (i,j) 编码一个框：四向 DFL bin + 分数 + seg 系数
    bool encode(int s, int i, int j, int dl, int dt, int dr, int db, double score, double coeff) {
        const int H = sh(s);
        if (i < 0 || j < 0 || i >= H || j >= H) return false;
        const std::size_t gl = static_cast<std::size_t>(H) * H;
        const std::size_t pix = static_cast<std::size_t>(i) * H + j;
        const int base = s * 4;
        // box：4*dfl_len，DFL logits（选定 bin 高、其余 0）
        const int dfl = views[base + 0].channels / 4;
        for (int b = 0; b < 4; ++b) {
            const int bin = (b == 0 ? dl : b == 1 ? dt : b == 2 ? dr : db);
            for (int k = 0; k < dfl; ++k)
                put(base + 0, static_cast<std::size_t>(b * dfl + k) * gl + pix, k == bin ? 12.0 : 0.0);
        }
        // score / score_sum
        for (int c = 0; c < views[base + 1].channels; ++c)
            put(base + 1, static_cast<std::size_t>(c) * gl + pix, c == 0 ? score : 0.0);
        put(base + 2, pix, score);
        // seg 系数
        for (int k = 0; k < views[base + 3].channels; ++k)
            put(base + 3, static_cast<std::size_t>(k) * gl + pix, coeff);
        return true;
    }
};

SegConfig base_cfg(float thr, float nms, float mask_thr, bool crop) {
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

LetterboxParams lb() {
    LetterboxParams p;
    compute_letterbox(1280, 720, 640, 640, &p, nullptr);
    return p;
}

// rail 夹具：模型框 (292,212)-(356,276)；期望归一化 x=0.45625 y=0.2 w=0.1 h=0.177778
void mk_rail(SegFixture* fx, const SegConfig& cfg, double coeff = 1.0, bool int8m = false,
             int32_t zp = 0, float sc = 0.1f) {
    fx->alloc(cfg, int8m, zp, sc);
    fx->fill_proto(cfg, true);
    fx->encode(0, 30, 40, 4, 4, 4, 4, 0.9, coeff);
    fx->bind();
}

std::string mask_summary(const SegResult& r, int x, int y) {
    char b[256];
    const SegMask* m = r.masks.empty() ? nullptr : &r.masks[0];
    int v = -1;
    if (m && x >= 0 && y >= 0 && x < m->width && y < m->height)
        v = m->plane[static_cast<std::size_t>(y) * m->stride_bytes + x];
    std::snprintf(b, sizeof(b), "boxes=%zu masks=%zu mask=%dx%d stride=%d size=%zu positive=%lld px(%d,%d)=%d",
                  r.boxes.size(), r.masks.size(), m ? m->width : 0, m ? m->height : 0,
                  m ? m->stride_bytes : 0, m ? m->plane.size() : 0,
                  m ? m->positive : -1, x, y, v);
    return b;
}

std::string result_line(const SegResult& r) {
    char b[400];
    std::snprintf(b, sizeof(b),
                  "type=seg ok=%d reason=%s boxes=%zu masks=%zu candidates=%lld dropped_score=%lld "
                  "dropped_class=%lld nms_suppressed=%lld dropped_bounds=%lld truncated=%lld",
                  r.ok ? 1 : 0, r.reason.c_str(), r.boxes.size(), r.masks.size(), r.candidates,
                  r.dropped_score, r.dropped_class, r.nms_suppressed, r.dropped_bounds, r.truncated);
    return b;
}

int run_selftest() {
    const SegConfig cfg = base_cfg(0.25f, 0.45f, 0.0f, true);
    const LetterboxParams p = lb();
    const int sw = 1280, shh = 720;

    // 主路径：1 框 + 1 掩码 + 坐标/二值/对齐
    {
        SegFixture fx;
        mk_rail(&fx, cfg);
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, cfg, p, sw, shh, &r, &e), "rail decode ok", e);
        check(r.boxes.size() == 1 && r.masks.size() == 1, "box<->mask one-to-one", result_line(r));
        if (r.boxes.size() == 1 && r.masks.size() == 1) {
            const SegBox& b = r.boxes[0];
            check(near(b.x, 0.45625) && near(b.y, 0.2) && near(b.w, 0.1) && near(b.h, 0.177778),
                  "box normalized coords",
                  std::to_string(b.x) + "," + std::to_string(b.y) + "," + std::to_string(b.w) + "," +
                      std::to_string(b.h));
            check(near(b.score, 0.9, 2e-3) && b.class_id == 0 && b.label == "rail", "score/class/label");
            const SegMask& m = r.masks[0];
            check(m.width == sw && m.height == shh && m.stride_bytes == sw &&
                      m.plane.size() == static_cast<std::size_t>(sw) * shh,
                  "mask dims/stride/size = original", mask_summary(r, -1, -1));
            bool binary = true;
            for (auto v : m.plane) if (v > 1) binary = false;
            check(binary && m.positive > 0, "mask binary + has positives", mask_summary(r, -1, -1));
            check(m.plane[static_cast<std::size_t>(240) * m.stride_bytes + 600] == 1,
                  "mask positive inside box (left half)", mask_summary(r, 600, 240));
            check(m.plane[static_cast<std::size_t>(240) * m.stride_bytes + 680] == 0,
                  "mask zero inside box (right half)", mask_summary(r, 680, 240));
            check(m.plane[static_cast<std::size_t>(240) * m.stride_bytes + 400] == 0,
                  "mask zero outside box", mask_summary(r, 400, 240));
        }
    }
    // 阈值化：系数全 0 -> 掩码全 0，但框仍在
    {
        SegFixture fx;
        mk_rail(&fx, cfg, 0.0);
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, cfg, p, sw, shh, &r, &e) && r.boxes.size() == 1 &&
                  r.masks.size() == 1 && r.masks[0].positive == 0,
              "zero coefficients -> all-zero mask, box kept", mask_summary(r, -1, -1));
    }
    // mask_threshold 可配：> 幅度（32）时掩码全 0
    {
        SegFixture fx;
        mk_rail(&fx, cfg);
        SegConfig c = cfg;
        c.mask_threshold = 100.0f;
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, c, p, sw, shh, &r, &e) && r.boxes.size() == 1 &&
                  r.masks[0].positive == 0,
              "mask_threshold configurable", mask_summary(r, -1, -1));
    }
    // crop_to_box=false：框外也会出现正像素
    {
        SegFixture fx;
        mk_rail(&fx, cfg);
        SegConfig c = cfg;
        c.crop_to_box = false;
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, c, p, sw, shh, &r, &e) && r.boxes.size() == 1 &&
                  r.masks[0].positive > 0,
              "crop_to_box configurable", mask_summary(r, -1, -1));
    }
    // 空图
    {
        SegFixture fx;
        fx.alloc(cfg, false, 0, 1.0f);
        fx.bind();
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, cfg, p, sw, shh, &r, &e) && r.ok && r.boxes.empty() &&
                  r.masks.empty() && r.reason == seg_reason::kEmpty(),
              "empty -> SEG_EMPTY");
    }
    // NMS：两重叠框 -> 1 框 1 掩码
    {
        SegFixture fx;
        fx.alloc(cfg, false, 0, 1.0f);
        fx.fill_proto(cfg, true);
        fx.encode(0, 30, 40, 4, 4, 4, 4, 0.9, 1.0);
        fx.encode(0, 30, 41, 4, 4, 4, 4, 0.8, 1.0);
        fx.bind();
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, cfg, p, sw, shh, &r, &e) && r.boxes.size() == 1 &&
                  r.masks.size() == 1 && r.nms_suppressed >= 1,
              "nms keeps 1 (mask follows)", result_line(r));
    }
    // MODEL_MISMATCH：数量/形状/布局
    {
        SegFixture fx;
        mk_rail(&fx, cfg);
        std::vector<RawDetectTensor> fewer(fx.views.begin(), fx.views.end() - 1);
        SegResult r;
        std::string e;
        check(!decode_yolov8_seg(fewer, cfg, p, sw, shh, &r, &e) && r.reason == seg_reason::kModelMismatch(),
              "12 tensors -> MODEL_MISMATCH", e);
    }
    {
        SegFixture fx;
        mk_rail(&fx, cfg);
        fx.views[3].channels = 16;  // seg 系数通道错
        SegResult r;
        std::string e;
        check(!decode_yolov8_seg(fx.views, cfg, p, sw, shh, &r, &e) && r.reason == seg_reason::kModelMismatch(),
              "bad seg channels -> MODEL_MISMATCH", e);
    }
    {
        SegFixture fx;
        mk_rail(&fx, cfg);
        fx.views[12].height = 80;
        SegResult r;
        std::string e;
        check(!decode_yolov8_seg(fx.views, cfg, p, sw, shh, &r, &e) && r.reason == seg_reason::kModelMismatch(),
              "bad proto shape -> MODEL_MISMATCH", e);
    }
    {
        SegFixture fx;
        mk_rail(&fx, cfg);
        fx.views[0].fmt = TensorFormat::Nhwc;
        SegResult r;
        std::string e;
        check(!decode_yolov8_seg(fx.views, cfg, p, sw, shh, &r, &e) && r.reason == seg_reason::kModelMismatch(),
              "non-NCHW -> MODEL_MISMATCH", e);
    }
    {
        SegFixture fx;
        mk_rail(&fx, cfg);
        fx.views[0].channels = 63;  // 非 4*dfl
        SegResult r;
        std::string e;
        check(!decode_yolov8_seg(fx.views, cfg, p, sw, shh, &r, &e) && r.reason == seg_reason::kModelMismatch(),
              "box channels not 4*dfl -> MODEL_MISMATCH", e);
    }
    // int8 量化路径（zp=0/scale=0.1 使 ±1/12/0.9 精确可表）
    {
        SegFixture fx;
        mk_rail(&fx, cfg, 1.0, true, 0, 0.1f);
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, cfg, p, sw, shh, &r, &e) && r.boxes.size() == 1 &&
                  r.masks.size() == 1 && r.masks[0].positive > 0,
              "int8 decode + mask", mask_summary(r, -1, -1));
        if (r.masks.size() == 1) {
            const SegMask& m = r.masks[0];
            check(near(r.boxes[0].x, 0.45625, 0.01) && near(r.boxes[0].w, 0.1, 0.01),
                  "int8 box within quant tol");
            check(m.plane[static_cast<std::size_t>(240) * m.stride_bytes + 600] == 1,
                  "int8 mask aligned");
        }
    }
    emit("RESULT: " + std::string(g_fail == 0 ? "PASS" : "FAIL") + " failures=" + std::to_string(g_fail));
    return g_fail == 0 ? 0 : 1;
}

int run_fixture(const std::string& name, float conf, float nms, float mask_thr) {
    const int sw = 1280, shh = 720;
    const LetterboxParams p = lb();
    SegConfig cfg = base_cfg(conf, nms, mask_thr, true);
    emit("type=config score_threshold=" + std::to_string(conf) + " nms_iou=" + std::to_string(nms) +
         " mask_threshold=" + std::to_string(mask_thr) + " crop_to_box=1 whitelist=[0]");
    SegFixture fx;
    SegResult r;
    std::string e;
    bool decode_called = true;
    if (name == "empty") {
        fx.alloc(cfg, false, 0, 1.0f);
        fx.bind();
    } else if (name == "badcount") {
        fx.alloc(cfg, false, 0, 1.0f);
        fx.bind();
        fx.views.pop_back();
    } else if (name == "badsegshape") {
        mk_rail(&fx, cfg);
        fx.views[3].channels = 16;
    } else if (name == "badproto") {
        mk_rail(&fx, cfg);
        fx.views[12].height = 80;
    } else if (name == "badfmt") {
        mk_rail(&fx, cfg);
        fx.views[0].fmt = TensorFormat::Nhwc;
    } else if (name == "zero_coeff") {
        mk_rail(&fx, cfg, 0.0);
    } else if (name == "nocrop") {
        mk_rail(&fx, cfg);
        cfg.crop_to_box = false;
    } else if (name == "nms") {
        fx.alloc(cfg, false, 0, 1.0f);
        fx.fill_proto(cfg, true);
        fx.encode(0, 30, 40, 4, 4, 4, 4, 0.9, 1.0);
        fx.encode(0, 30, 41, 4, 4, 4, 4, 0.8, 1.0);
        fx.bind();
    } else if (name == "int8") {
        mk_rail(&fx, cfg, 1.0, true, 0, 0.1f);
    } else {  // rail
        mk_rail(&fx, cfg);
    }
    const bool ok = decode_yolov8_seg(fx.views, cfg, p, sw, shh, &r, &e);
    (void)decode_called;
    emit("type=decoder ok=" + std::string(ok ? "1" : "0") + " reason=" + r.reason +
         " detail=" + r.detail + (e.empty() ? "" : (" err=" + e)));
    emit(result_line(r));
    for (std::size_t i = 0; i < r.boxes.size(); ++i) {
        char b[200];
        std::snprintf(b, sizeof(b), "box[%zu] xywh=(%.5f,%.5f,%.5f,%.5f) score=%.4f cls=%d label=%s",
                      i, r.boxes[i].x, r.boxes[i].y, r.boxes[i].w, r.boxes[i].h, r.boxes[i].score,
                      r.boxes[i].class_id, r.boxes[i].label.c_str());
        emit(b);
    }
    if (!r.masks.empty())
        emit("mask[0] " + mask_summary(r, 600, 240));
    const bool expect_ok = (name != "badcount" && name != "badsegshape" && name != "badproto" &&
                            name != "badfmt");
    const bool pass = expect_ok ? r.ok : (!r.ok && r.reason == seg_reason::kModelMismatch());
    emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
    return pass ? 0 : 1;
}

int run_config(const std::string& dir, float conf_override, float mask_thr) {
    ConfigLoadResult cfg = load_config_dir(dir);
    if (!cfg.ok) {
        emit("type=error config invalid");
        for (const auto& e : cfg.errors) emit("  " + e.str());
        return 1;
    }
    const float score = conf_override > 0.f ? conf_override
                                            : static_cast<float>(cfg.settings.thresholds.confidence_min);
    emit("type=config dir=" + dir + " confidence_min=" +
         std::to_string(cfg.settings.thresholds.confidence_min) + " score_used=" +
         std::to_string(score) + " mask_threshold=" + std::to_string(mask_thr) +
         (conf_override > 0.f ? " (cli override)" : " (from thresholds.confidence_min)"));
    const int sw = 1280, shh = 720;
    const LetterboxParams p = lb();
    SegFixture fx;
    SegConfig c = base_cfg(score, 0.45f, mask_thr, true);
    mk_rail(&fx, c);
    SegResult r;
    std::string e;
    decode_yolov8_seg(fx.views, c, p, sw, shh, &r, &e);
    emit(result_line(r));
    if (!r.masks.empty()) emit("mask[0] " + mask_summary(r, 600, 240));
    emit("RESULT: " + std::string((r.ok && r.boxes.size() == 1 && r.masks.size() == 1) ? "PASS" : "FAIL"));
    return (r.ok && r.boxes.size() == 1 && r.masks.size() == 1) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode, name = "rail", out, json, dir = RAILHMI_CONFIG_DIR;
    float conf = -1.f, nms = 0.45f, mask_thr = 0.0f;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--selftest") mode = "selftest";
        else if (a == "--fixture") { next(&name); mode = "fixture"; }
        else if (a == "--config") { next(&dir); mode = "config"; }
        else if (a == "--conf") { std::string v; next(&v); conf = std::atof(v.c_str()); }
        else if (a == "--nms-iou") { std::string v; next(&v); nms = std::atof(v.c_str()); }
        else if (a == "--mask-threshold") { std::string v; next(&v); mask_thr = std::atof(v.c_str()); }
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-vision-seg --selftest | --fixture NAME | --config DIR "
                        "[--conf X] [--nms-iou Y] [--mask-threshold T]\n");
            return 0;
        } else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }
    if (mode.empty()) { std::fprintf(stderr, "need --selftest/--fixture/--config\n"); return 2; }
    if (!out.empty()) g_fo.open(out, std::ios::trunc);
    int rc = 0;
    if (mode == "selftest") rc = run_selftest();
    else if (mode == "fixture") rc = run_fixture(name, conf > 0.f ? conf : 0.25f, nms, mask_thr);
    else rc = run_config(dir, conf, mask_thr);
    if (!json.empty()) {
        std::ofstream fj(json, std::ios::trunc);
        if (fj) {
            fj << "{\"mode\":\"" << mode << "\",\"failures\":" << g_fail << ",\"lines\":[";
            for (std::size_t i = 0; i < g_lines.size(); ++i) {
                if (i) fj << ",";
                std::string esc;
                for (char ch : g_lines[i]) { if (ch == '"' || ch == '\\') esc += '\\'; esc += ch; }
                fj << "\"" << esc << "\"";
            }
            fj << "]}";
        }
    }
    return rc;
}
