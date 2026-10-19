// railhmi-vision-detect (视觉推理 ITEM-003)
//
// YOLOv5 人形检测后处理探针：三尺度解码 / 置信度过滤 / 类别白名单（person）/
// letterbox 逆变换归一化 / NMS。阈值来自配置或 CLI，不写死历史常量。
//
// 用法:
//   railhmi-vision-detect --selftest [--out log] [--json file]
//   railhmi-vision-detect --fixture person|nonperson|nms|empty|padbox|int8 [--conf X] [--nms-iou Y]
//   railhmi-vision-detect --config DIR [--conf X] [--nms-iou Y]
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
#include "railhmi/core/vision_detect_postprocess.h"
#include "railhmi/core/vision_preprocess.h"

#ifndef RAILHMI_CONFIG_DIR
#define RAILHMI_CONFIG_DIR "config"
#endif

using namespace railhmi::core;

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
bool near(double a, double b, double tol = 1e-3) { return std::fabs(a - b) <= tol; }

int scale_hw(int s) { return 80 >> s; }  // 80, 40, 20

struct FloatFixture {
    std::vector<std::vector<float>> planes;
    std::vector<RawDetectTensor> views;
    void add_scale(int s) {
        const int H = scale_hw(s);
        planes.emplace_back(static_cast<std::size_t>(3) * 85 * H * H, 0.0f);
        RawDetectTensor t;
        t.channels = 3 * 85;
        t.height = H;
        t.width = H;
        t.dtype = TensorType::Float32;
        t.is_float = true;
        t.zero_point = 0;
        t.scale = 1.0f;
        t.data = nullptr;  // 绑定在 bind() 中完成
        views.push_back(t);
    }
    void bind() {
        for (std::size_t s = 0; s < planes.size(); ++s) {
            views[s].data = reinterpret_cast<const std::uint8_t*>(planes[s].data());
        }
    }
};

struct Int8Fixture {
    std::vector<std::vector<std::uint8_t>> planes;
    std::vector<RawDetectTensor> views;
    int32_t zp = -128;
    float scale = 1.0f / 255.0f;
    void add_scale(int s) {
        const int H = scale_hw(s);
        // int8 张量的“零”不是空：zp=-128 时字节 0 反量化约 0.5。空背景必须写 q(0.0)。
        planes.emplace_back(static_cast<std::size_t>(3) * 85 * H * H,
                            static_cast<std::uint8_t>(q(0.0)));
        RawDetectTensor t;
        t.channels = 3 * 85;
        t.height = H;
        t.width = H;
        t.dtype = TensorType::Int8;
        t.is_float = false;
        t.zero_point = zp;
        t.scale = scale;
        t.data = nullptr;
        views.push_back(t);
    }
    void bind() {
        for (std::size_t s = 0; s < planes.size(); ++s)
            views[s].data = planes[s].data();
    }
    std::int8_t q(double v) const {
        double q = std::round(v / static_cast<double>(scale)) + zp;
        q = std::max(-128.0, std::min(127.0, q));
        return static_cast<std::int8_t>(q);
    }
};

// 在尺度 s 的锚点 a、网格 (i,j) 写入一个框；中心 (mx,my) 与尺寸 (wpx,hpx) 为模型像素。
template <typename Fixture, typename Setter>
bool encode_box(Fixture* fx, int s, int a, double mx, double my, double wpx, double hpx, double obj,
                int cls, double cls_prob, const Setter& set) {
    const int H = scale_hw(s);
    const double stride = 640.0 / H;
    const double uh = mx / stride;
    const double uv = my / stride;
    const int j = static_cast<int>(std::floor(uh));
    const int i = static_cast<int>(std::floor(uv));
    if (i < 0 || j < 0 || i >= H || j >= H) return false;
    const double v_cx = (uh - j + 0.5) / 2.0;
    const double v_cy = (uv - i + 0.5) / 2.0;
    const std::vector<int> anchors = default_yolov5_anchors();
    const double aw = anchors[static_cast<std::size_t>(s) * 6 + a * 2 + 0];
    const double ah = anchors[static_cast<std::size_t>(s) * 6 + a * 2 + 1];
    const double v_bw = std::sqrt(wpx / aw) / 2.0;
    const double v_bh = std::sqrt(hpx / ah) / 2.0;
    if (v_cx < 0 || v_cx > 1 || v_cy < 0 || v_cy > 1 || v_bw < 0 || v_bw > 1 || v_bh < 0 ||
        v_bh > 1)
        return false;
    const long long grid_len = static_cast<long long>(H) * H;
    const long long base = static_cast<long long>(a) * 85 * grid_len;
    const long long pix = static_cast<long long>(i) * H + j;
    set(fx, s, base + 0 * grid_len + pix, v_cx);
    set(fx, s, base + 1 * grid_len + pix, v_cy);
    set(fx, s, base + 2 * grid_len + pix, v_bw);
    set(fx, s, base + 3 * grid_len + pix, v_bh);
    set(fx, s, base + 4 * grid_len + pix, obj);
    for (int k = 0; k < 80; ++k)
        set(fx, s, base + static_cast<long long>(5 + k) * grid_len + pix, 0.0);
    set(fx, s, base + static_cast<long long>(5 + cls) * grid_len + pix, cls_prob);
    return true;
}

template <typename Fixture>
void bind_fixture(Fixture* fx) {
    fx->bind();
}
LetterboxParams lb_for(int sw, int sh) {
    LetterboxParams lb;
    compute_letterbox(sw, sh, 640, 640, &lb, nullptr);
    return lb;
}

FloatFixture make_person_fixture(int* scale, int* anchor, int* ci, int* cj) {
    FloatFixture fx;
    for (int s = 0; s < 3; ++s) fx.add_scale(s);
    auto set = [](FloatFixture* f, int s, long long idx, double v) { f->planes[s][idx] = static_cast<float>(v); };
    // 模型像素框 (200,300)-(264,364)，尺度 0（stride 8）、锚点 2（33,23）。
    *scale = 0; *anchor = 2;
    const bool ok = encode_box(&fx, 0, 2, 232.0, 332.0, 64.0, 64.0, 0.9, 0, 0.9, set);
    (void)ok;
    *ci = static_cast<int>(std::floor(332.0 / 8.0));
    *cj = static_cast<int>(std::floor(232.0 / 8.0));
    fx.bind();
    return fx;
}

DetectConfig base_cfg(float thr, float nms_iou, int sw, int sh) {
    DetectConfig cfg;
    cfg.input_w = 640;
    cfg.input_h = 640;
    cfg.score_threshold = thr;
    cfg.nms_iou = nms_iou;
    cfg.num_classes = 80;
    cfg.class_whitelist = {0};
    cfg.anchors = default_yolov5_anchors();
    cfg.score_threshold_source = "cli";
    (void)sw; (void)sh;
    return cfg;
}

std::string box_line(const DetectBox& b) {
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "box xywh=(%.5f,%.5f,%.5f,%.5f) score=%.4f cls=%d label=%s scale=%d anchor=%d "
                  "cell=%d,%d",
                  b.x, b.y, b.w, b.h, b.score, b.class_id, b.label.c_str(), b.scale_index,
                  b.anchor_index, b.cell_i, b.cell_j);
    return buf;
}

std::string result_line(const DetectResult& r) {
    char buf[400];
    std::snprintf(buf, sizeof(buf),
                  "type=detect ok=%d reason=%s boxes=%zu candidates=%lld dropped_score=%lld "
                  "dropped_class=%lld nms_suppressed=%lld dropped_bounds=%lld truncated=%lld",
                  r.ok ? 1 : 0, r.reason.c_str(), r.boxes.size(), r.candidates, r.dropped_score,
                  r.dropped_class, r.nms_suppressed, r.dropped_bounds, r.truncated);
    return buf;
}

int run_selftest() {
    const int sw = 1280, sh = 720;
    const LetterboxParams lb = lb_for(sw, sh);
    // 期望：模型框 (200,300)-(264,364) -> 原图 (400,320)-(528,448) -> 归一化
    const double ex = 400.0 / 1280.0, ey = 320.0 / 720.0, ew = 128.0 / 1280.0, eh = 128.0 / 720.0;

    // TC-T-DET-01/03：单 person 框解码 + letterbox 逆变换
    {
        int s, a, ci, cj;
        FloatFixture fx = make_person_fixture(&s, &a, &ci, &cj);
        DetectResult r;
        std::string e;
        const DetectConfig cfg = base_cfg(0.25f, 0.45f, sw, sh);
        check(decode_yolov5(fx.views, cfg, lb, sw, sh, &r, &e), "person decode ok", e);
        check(r.boxes.size() == 1, "person boxes==1", std::to_string(r.boxes.size()));
        if (r.boxes.size() == 1) {
            const DetectBox& b = r.boxes[0];
            check(near(b.x, ex) && near(b.y, ey) && near(b.w, ew) && near(b.h, eh),
                  "person normalized coords",
                  box_line(b) + " expect=" + std::to_string(ex) + "," + std::to_string(ey) + "," +
                      std::to_string(ew) + "," + std::to_string(eh));
            check(near(b.score, 0.81, 2e-3), "person score=obj*cls", std::to_string(b.score));
            check(b.class_id == 0 && b.label == "person", "person class label");
            check(b.scale_index == 0 && b.anchor_index == 2 && b.cell_i == ci && b.cell_j == cj,
                  "person source cell/anchor");
            check(b.x >= 0 && b.y >= 0 && b.x + b.w <= 1.0f + 1e-6 && b.y + b.h <= 1.0f + 1e-6,
                  "person bounds invariant");
        }
    }
    // TC-T-DET-04：非 person 类别被白名单丢弃并可计数
    {
        FloatFixture fx;
        for (int s = 0; s < 3; ++s) fx.add_scale(s);
        auto set = [](FloatFixture* f, int s, long long idx, double v) { f->planes[s][idx] = static_cast<float>(v); };
        encode_box(&fx, 0, 2, 232.0, 332.0, 64.0, 64.0, 0.9, 2 /*car*/, 0.95, set);
        fx.bind();
        DetectResult r;
        std::string e;
        const DetectConfig cfg = base_cfg(0.25f, 0.45f, sw, sh);
        check(decode_yolov5(fx.views, cfg, lb, sw, sh, &r, &e), "nonperson decode ok", e);
        check(r.boxes.empty() && r.dropped_class >= 1, "nonperson dropped",
              "boxes=" + std::to_string(r.boxes.size()) + " dropped_class=" + std::to_string(r.dropped_class));
    }
    // TC-T-DET-05：空图 -> ready 空框，不是错误
    {
        FloatFixture fx;
        for (int s = 0; s < 3; ++s) fx.add_scale(s);
        fx.bind();
        DetectResult r;
        std::string e;
        const DetectConfig cfg = base_cfg(0.25f, 0.45f, sw, sh);
        check(decode_yolov5(fx.views, cfg, lb, sw, sh, &r, &e) && r.ok && r.boxes.empty() &&
                  r.reason == detect_reason::kEmpty(),
              "empty image -> empty boxes ok");
    }
    // TC-T-DET-02：NMS 抑制重叠同类别框（阈值来自配置）
    {
        FloatFixture fx;
        for (int s = 0; s < 3; ++s) fx.add_scale(s);
        auto set = [](FloatFixture* f, int s, long long idx, double v) { f->planes[s][idx] = static_cast<float>(v); };
        encode_box(&fx, 0, 2, 232.0, 332.0, 64.0, 64.0, 0.9, 0, 0.9, set);   // 高分
        encode_box(&fx, 0, 2, 240.0, 340.0, 64.0, 64.0, 0.8, 0, 0.8, set);   // 重叠
        fx.bind();
        DetectResult r;
        std::string e;
        const DetectConfig cfg = base_cfg(0.25f, 0.45f, sw, sh);
        check(decode_yolov5(fx.views, cfg, lb, sw, sh, &r, &e), "nms decode ok", e);
        check(r.boxes.size() == 1 && r.nms_suppressed >= 1, "nms keeps 1",
              "boxes=" + std::to_string(r.boxes.size()) + " suppressed=" + std::to_string(r.nms_suppressed));
        // 关闭 NMS（阈值 0.99）时应保留两个
        DetectConfig cfg2 = cfg;
        cfg2.nms_iou = 0.99f;
        DetectResult r2;
        check(decode_yolov5(fx.views, cfg2, lb, sw, sh, &r2, &e) && r2.boxes.size() == 2,
              "nms threshold from config changeable", "boxes=" + std::to_string(r2.boxes.size()));
    }
    // 补边区域内的框 -> 逆 letterbox 后越界丢弃
    {
        FloatFixture fx;
        for (int s = 0; s < 3; ++s) fx.add_scale(s);
        auto set = [](FloatFixture* f, int s, long long idx, double v) { f->planes[s][idx] = static_cast<float>(v); };
        encode_box(&fx, 0, 0, 100.0, 60.0, 32.0, 32.0, 0.9, 0, 0.9, set);  // my=60 < pad_y=140
        fx.bind();
        DetectResult r;
        std::string e;
        const DetectConfig cfg = base_cfg(0.25f, 0.45f, sw, sh);
        check(decode_yolov5(fx.views, cfg, lb, sw, sh, &r, &e), "padbox decode ok", e);
        check(r.boxes.empty() && r.dropped_bounds >= 1, "pad-region box dropped",
              "dropped_bounds=" + std::to_string(r.dropped_bounds));
    }
    // 阈值来自配置：conf 0.9 时同一 person 框被滤掉
    {
        int s, a, ci, cj;
        FloatFixture fx = make_person_fixture(&s, &a, &ci, &cj);
        DetectResult r;
        std::string e;
        const DetectConfig cfg = base_cfg(0.9f, 0.45f, sw, sh);
        check(decode_yolov5(fx.views, cfg, lb, sw, sh, &r, &e) && r.boxes.empty() &&
                  r.dropped_score >= 1,
              "score threshold from config filters", "dropped_score=" + std::to_string(r.dropped_score));
    }
    // 非法张量（通道数不符）-> 显式错误
    {
        FloatFixture fx;
        fx.add_scale(0);
        fx.views[0].channels = 84 * 3;
        fx.bind();
        DetectResult r;
        std::string e;
        const DetectConfig cfg = base_cfg(0.25f, 0.45f, sw, sh);
        check(!decode_yolov5(fx.views, cfg, lb, sw, sh, &r, &e) &&
                  r.reason == detect_reason::kBadTensor(),
              "bad tensor channels rejected", e);
    }
    // int8 量化路径（真实模型 scale/zp 口径）
    {
        Int8Fixture fx;
        for (int s = 0; s < 3; ++s) fx.add_scale(s);
        auto set = [](Int8Fixture* f, int s, long long idx, double v) {
            f->planes[s][static_cast<std::size_t>(idx)] =
                static_cast<std::uint8_t>(f->q(v));
        };
        encode_box(&fx, 0, 2, 232.0, 332.0, 64.0, 64.0, 0.9, 0, 0.9, set);
        fx.bind();
        DetectResult r;
        std::string e;
        const DetectConfig cfg = base_cfg(0.25f, 0.45f, sw, sh);
        check(decode_yolov5(fx.views, cfg, lb, sw, sh, &r, &e), "int8 decode ok", e);
        check(r.boxes.size() == 1 && near(r.boxes[0].x, ex, 0.01) && near(r.boxes[0].w, ew, 0.01),
              "int8 dequant coords",
              r.boxes.empty() ? "no box" : box_line(r.boxes[0]));
    }
    emit("RESULT: " + std::string(g_fail == 0 ? "PASS" : "FAIL") + " failures=" +
         std::to_string(g_fail));
    return g_fail == 0 ? 0 : 1;
}

int run_fixture(const std::string& name, float thr, float nms_iou) {
    const int sw = 1280, sh = 720;
    const LetterboxParams lb = lb_for(sw, sh);
    DetectResult r;
    std::string e;
    const DetectConfig cfg = base_cfg(thr, nms_iou, sw, sh);
    emit("type=config score_threshold=" + std::to_string(thr) + " nms_iou=" +
         std::to_string(nms_iou) + " whitelist=[0] classes=80 source=" + cfg.score_threshold_source);

    if (name == "empty") {
        FloatFixture fx;
        for (int s = 0; s < 3; ++s) fx.add_scale(s);
        fx.bind();
        decode_yolov5(fx.views, cfg, lb, sw, sh, &r, &e);
    } else if (name == "nonperson") {
        FloatFixture fx;
        for (int s = 0; s < 3; ++s) fx.add_scale(s);
        auto set = [](FloatFixture* f, int s, long long idx, double v) { f->planes[s][idx] = static_cast<float>(v); };
        encode_box(&fx, 0, 2, 232.0, 332.0, 64.0, 64.0, 0.9, 2, 0.95, set);
        fx.bind();
        decode_yolov5(fx.views, cfg, lb, sw, sh, &r, &e);
    } else if (name == "nms") {
        FloatFixture fx;
        for (int s = 0; s < 3; ++s) fx.add_scale(s);
        auto set = [](FloatFixture* f, int s, long long idx, double v) { f->planes[s][idx] = static_cast<float>(v); };
        encode_box(&fx, 0, 2, 232.0, 332.0, 64.0, 64.0, 0.9, 0, 0.9, set);
        encode_box(&fx, 0, 2, 240.0, 340.0, 64.0, 64.0, 0.8, 0, 0.8, set);
        fx.bind();
        decode_yolov5(fx.views, cfg, lb, sw, sh, &r, &e);
    } else if (name == "padbox") {
        FloatFixture fx;
        for (int s = 0; s < 3; ++s) fx.add_scale(s);
        auto set = [](FloatFixture* f, int s, long long idx, double v) { f->planes[s][idx] = static_cast<float>(v); };
        encode_box(&fx, 0, 0, 100.0, 60.0, 32.0, 32.0, 0.9, 0, 0.9, set);
        fx.bind();
        decode_yolov5(fx.views, cfg, lb, sw, sh, &r, &e);
    } else if (name == "int8") {
        Int8Fixture fx;
        for (int s = 0; s < 3; ++s) fx.add_scale(s);
        auto set = [](Int8Fixture* f, int s, long long idx, double v) {
            f->planes[s][static_cast<std::size_t>(idx)] = static_cast<std::uint8_t>(f->q(v));
        };
        encode_box(&fx, 0, 2, 232.0, 332.0, 64.0, 64.0, 0.9, 0, 0.9, set);
        fx.bind();
        decode_yolov5(fx.views, cfg, lb, sw, sh, &r, &e);
    } else {  // person
        int s, a, ci, cj;
        FloatFixture fx = make_person_fixture(&s, &a, &ci, &cj);
        decode_yolov5(fx.views, cfg, lb, sw, sh, &r, &e);
    }
    emit("type=decoder reason=" + r.reason + (e.empty() ? "" : (" detail=" + r.detail)));
    emit(result_line(r));
    for (const auto& b : r.boxes) emit(box_line(b));
    const bool pass = r.ok && (r.boxes.size() >= 1 || name == "empty" || name == "nonperson" ||
                               name == "padbox");
    emit("RESULT: " + std::string(pass ? "PASS" : "FAIL"));
    return pass ? 0 : 1;
}

int run_config(const std::string& dir, float conf_override, float nms_iou) {
    ConfigLoadResult cfg = load_config_dir(dir);
    if (!cfg.ok) {
        emit("type=error config invalid");
        for (const auto& e : cfg.errors) emit("  " + e.str());
        return 1;
    }
    const float score = (conf_override > 0.f) ? conf_override
                                              : static_cast<float>(cfg.settings.thresholds.confidence_min);
    emit("type=config dir=" + dir + " confidence_min=" +
         std::to_string(cfg.settings.thresholds.confidence_min) + " score_used=" +
         std::to_string(score) + " nms_iou=" + std::to_string(nms_iou) +
         (conf_override > 0.f ? " (cli override)" : " (from thresholds.confidence_min)"));
    // 用配置阈值跑 person 夹具：conf=0.5 时 0.81 仍保留。
    const int sw = 1280, sh = 720;
    const LetterboxParams lb = lb_for(sw, sh);
    int s, a, ci, cj;
    FloatFixture fx = make_person_fixture(&s, &a, &ci, &cj);
    DetectResult r;
    std::string e;
    DetectConfig c = base_cfg(score, nms_iou, sw, sh);
    c.score_threshold_source = conf_override > 0.f ? "cli" : "thresholds.confidence_min";
    c.nms_iou_source = "cli/default";
    decode_yolov5(fx.views, c, lb, sw, sh, &r, &e);
    emit(result_line(r));
    for (const auto& b : r.boxes) emit(box_line(b));
    emit("RESULT: " + std::string((r.ok && r.boxes.size() == 1) ? "PASS" : "FAIL"));
    return (r.ok && r.boxes.size() == 1) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    std::string mode, name = "person", out, json, config = RAILHMI_CONFIG_DIR, dir;
    float conf = -1.f, nms_iou = 0.45f;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string* d) { if (i + 1 < argc) *d = argv[++i]; };
        if (a == "--selftest") mode = "selftest";
        else if (a == "--fixture") { next(&name); mode = "fixture"; }
        else if (a == "--config") { next(&dir); mode = "config"; }
        else if (a == "--conf") { std::string v; next(&v); conf = std::atof(v.c_str()); }
        else if (a == "--nms-iou") { std::string v; next(&v); nms_iou = std::atof(v.c_str()); }
        else if (a == "--out") next(&out);
        else if (a == "--json") next(&json);
        else if (a == "--help" || a == "-h") {
            std::printf("usage: railhmi-vision-detect --selftest | --fixture NAME | --config DIR "
                        "[--conf X] [--nms-iou Y]\n");
            return 0;
        } else { std::fprintf(stderr, "unknown arg: %s\n", a.c_str()); return 2; }
    }
    if (mode.empty()) { std::fprintf(stderr, "need --selftest/--fixture/--config\n"); return 2; }
    if (!out.empty()) g_fo.open(out, std::ios::trunc);
    int rc = 0;
    if (mode == "selftest") rc = run_selftest();
    else if (mode == "fixture") rc = run_fixture(name, conf > 0.f ? conf : 0.25f, nms_iou);
    else rc = run_config(dir, conf, nms_iou);
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
