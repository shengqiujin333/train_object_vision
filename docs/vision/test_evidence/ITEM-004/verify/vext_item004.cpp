// vext: ITEM-004 独立验证扩展夹具（software_tester.software_verification）。
// 直驱 decode_yolov8_seg 纯逻辑 API，断言交付 core-tests/selftest 未覆盖的验收边界；
// 编码器为验证自有实现（与交付探针 encode 语法同形但独立编写）。
//
// 数字预言全部由验证侧 Python 独立推导（_oracle_calc.py）后再写成断言，不自引交付值。
//
// 判例域（vext g01..g24）:
//   g01      非平凡 DFL logits softmax (5@bin4+2@bin6 / 8@bin2 / 9@bin7 / 4,4@bins{1,2}) 独立解析坐标
//   g02-g04  掩码结构: box_index 关联 + plane stride 行独立 + 逐像素手算点积样本
//   g05-g07  crop 几何: 闭式计数 tied to bilinear 单调/对称级（正负列分界）+ offmask 独立
//   g08-g09  crop_to_box=false 全幅正 (720x640=460800) + nms 抑制者不出掩码
//   g10-g13  NMS 数学: IoU+1 手算 0.7808 恰等阈值保留、相邻网格 0.8025 抑制、多框三重独立、IoU 恰等半开
//   g14-g15  极端参数: mask_threshold=-1.0 恒 1（crop 内）、num_classes=2 双类框
//   g16-g19  跨尺度联合: s1/s2 独立解码 + 三框混排降序 + per-channel pan 通道隔离 + row-pattern
//   g20-g21  畸形张量: proto 空间维度非方 MODEL_MISMATCH + s2 分支残缺 MODEL_MISMATCH
//   g22-g24  确定性与归一化边界: 两轮逐字段恒等 (void* 平面 memcmp 安全——纯 0/1)、
//            x+w==1.0 精确边界、reason token 稳定 SEG_OK

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "railhmi/core/vision_seg_postprocess.h"
#include "railhmi/core/vision_detect_postprocess.h"
#include "railhmi/core/vision_preprocess.h"

using namespace railhmi::core;

static int g_pass = 0, g_fail = 0;

static void check(bool ok, const char* name, const char* detail = "") {
    if (ok) { ++g_pass; std::printf("vext PASS %s\n", name); }
    else { ++g_fail; std::printf("vext FAIL %s :: %s\n", name, detail); }
}

static void check_near(double a, double b, double tol, const char* name) {
    char d[160];
    std::snprintf(d, sizeof(d), "a=%.9f b=%.9f tol=%.1e", a, b, tol);
    check(std::fabs(a - b) <= tol, name, d);
}

static const int SW = 1280, SH = 720;

// 诊断摘要（结果含框/掩码/计数），追加在失败 detail 尾部
static std::string res_sum(const SegResult& r) {
    char b[220];
    std::snprintf(b, sizeof(b),
                  "[boxes=%zu masks=%zu cand=%lld dscore=%lld sup=%lld dbnd=%lld "
                  "pos0=%lld ok=%d reason=%s]", r.boxes.size(), r.masks.size(), r.candidates,
                  r.dropped_score, r.nms_suppressed, r.dropped_bounds,
                  r.masks.empty() ? -1LL : r.masks[0].positive, r.ok ? 1 : 0, r.reason.c_str());
    return std::string(b);
}

static LetterboxParams lb() {
    LetterboxParams p;
    compute_letterbox(SW, SH, 640, 640, &p, nullptr);
    return p;
}

// 验证自有夹具（与交付探针 SegFixture 语法同形、独立编写；仅 float32 路径 —— int8 已由交付覆盖）。
struct Fx {
    std::vector<std::vector<float>> planes;
    std::vector<RawDetectTensor> views;
    int nc = 1;
    void alloc(const SegConfig& c) {
        nc = c.num_classes;
        for (int s = 0; s < 3; ++s) {
            const int H = 80 >> s;
            const int ch[4] = {64, c.num_classes, 1, c.proto_channels};
            for (int b = 0; b < 4; ++b) {
                planes.emplace_back(static_cast<size_t>(ch[b]) * H * H, 0.0f);
                RawDetectTensor t;
                t.channels = ch[b];
                t.height = H;
                t.width = H;
                t.dtype = TensorType::Float32;
                t.is_float = true;
                views.push_back(t);
            }
        }
        planes.emplace_back(static_cast<size_t>(c.proto_channels) * c.proto_h * c.proto_w, 0.0f);
        RawDetectTensor p;
        p.channels = c.proto_channels;
        p.height = c.proto_h;
        p.width = c.proto_w;
        p.dtype = TensorType::Float32;
        p.is_float = true;
        views.push_back(p);
    }
    void bind() {
        for (size_t i = 0; i < views.size(); ++i)
            views[i].data = reinterpret_cast<const uint8_t*>(planes[i].data());
    }
    void proto_all(const SegConfig& c, double v) {
        for (int k = 0; k < c.proto_channels; ++k)
            std::fill(planes[12].begin() + static_cast<long>(k) * c.proto_h * c.proto_w,
                      planes[12].begin() + static_cast<long>(k + 1) * c.proto_h * c.proto_w,
                      static_cast<float>(v));
    }
    // 对照交付 fill_proto(half)：x >= 80 半平面为 -1 — 闭式计数 7040 的前提
    void proto_half(const SegConfig& c) {
        for (int k = 0; k < c.proto_channels; ++k)
            for (int y = 0; y < c.proto_h; ++y)
                for (int x = 0; x < c.proto_w; ++x)
                    planes[12][static_cast<size_t>(k) * c.proto_h * c.proto_w +
                               static_cast<size_t>(y) * c.proto_w + x] = (x >= 80) ? -1.0f : 1.0f;
    }
    void proto_pan(const SegConfig& c) {
        // 通道 pan: 通道 k 常量 (k==5 ? -3.0 : 11.0)
        for (int k = 0; k < c.proto_channels; ++k) {
            float v = (k == 5) ? -3.0f : 11.0f;
            std::fill(planes[12].begin() + static_cast<long>(k) * c.proto_h * c.proto_w,
                      planes[12].begin() + static_cast<long>(k + 1) * c.proto_h * c.proto_w, v);
        }
    }
    // proto 通道 0: 上半行 (r<80) +0.5、下半行 -0.5；其余通道 0 —— 供 e_0 系数行 pattern 判例
    void proto_rows(const SegConfig& c, bool upper) {
        for (int k = 0; k < c.proto_channels; ++k) {
            float v = k == 0 ? 0.0f : 0.0f;
            if (k == 0)
                for (int r = 0; r < c.proto_h; ++r)
                    for (int x = 0; x < c.proto_w; ++x)
                        planes[12][static_cast<size_t>(k) * c.proto_h * c.proto_w +
                                   static_cast<size_t>(r) * c.proto_w + x] =
                            ((r < 80) == upper) ? 0.5f : -0.5f;
        }
    }
    void encode(int s, int i, int j, const int bins[4], const float logit, double score,
                const float* coeffs, int nc_idx = 0) {
        const int H = 80 >> s;
        const size_t gl = static_cast<size_t>(H) * H;
        const size_t pix = static_cast<size_t>(i) * H + j;
        const int base = s * 4;
        for (int b = 0; b < 4; ++b) {
            const int bin = bins[b];
            for (int k = 0; k < 16; ++k)
                planes[base + 0][static_cast<size_t>(b * 16 + k) * gl + pix] =
                    (k == bin) ? logit : 0.0f;
        }
        for (int c = 0; c < nc; ++c)
            planes[base + 1][static_cast<size_t>(c) * gl + pix] =
                static_cast<float>(c == nc_idx ? score : 0.0);
        planes[base + 2][pix] = static_cast<float>(score);
        for (int k = 0; k < 32; ++k)
            planes[base + 3][static_cast<size_t>(k) * gl + pix] = coeffs[k];
    }
};

static SegConfig base_cfg(float thr, float nms, float mask_thr, bool crop) {
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

// 32 个同值系数（验证自有默认：全 1）
static void ones(float* c) { for (int k = 0; k < 32; ++k) c[k] = 1.0f; }

int main() {
    const LetterboxParams L = lb();

    // ---- g01 非平凡 DFL ----
    {
        // bins {4(logit 5), 6(logit 2)} / {2:8} / {7:9} / {1:4, 2:4}
        // 验证侧 Python softmax 独立求和: d=[4.40504838, 2.029372909, 7.000985454, 2.279244181]
        // 模型框 = (208.75961,147.76502)-(300.00788,182.23395)
        // 逆 letterbox->src norm (0.32618694, 0.02156953, 0.14257544, 0.09574703)
        SegConfig c = base_cfg(0.25f, 0.45f, 0.0f, true);
        Fx fx;
        fx.alloc(c);
        fx.proto_all(c, 1.0);
        const int bins[4] = {4, 2, 7, 1};
        // db 方向 bin1 与 bin2 同值 4.0
        float coeffs[32];
        ones(coeffs);
        // encode 只支持单 bin，db 双 bin 手工补写
        fx.encode(0, 20, 30, bins, 5.0f, 0.9, coeffs);
        const int H = 80;
        const size_t gl = 6400, pix = 20 * 80 + 30;
        // 覆盖写 db=bin1(4.0)、bin6(dt 附加 2.0)、d0 附加 bin6? 逐 channel 写：
        // d0: bin4=5, bin6=2 ; d1: bin2=8 ; d2: bin7=9 ; d3: bin1=4, bin2=4
        float extra[6][3] = {{0, 6, 2.0f}, {1, 2, 8.0f}, {2, 7, 9.0f}, {3, 1, 4.0f}, {3, 2, 4.0f}, {0, 4, 5.0f}};
        for (auto& e : extra)
            fx.planes[0][static_cast<size_t>(e[0] * 16 + e[1]) * gl + pix] = e[2];
        fx.bind();
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, c, L, SW, SH, &r, &e) && r.boxes.size() == 1, "g01_dfl_decodes", e.c_str());
        if (r.boxes.size() == 1) {
            check_near(r.boxes[0].x, 0.32618694, 2e-4, "g01a_x");
            check_near(r.boxes[0].y, 0.02156953, 2e-4, "g01b_y");
            check_near(r.boxes[0].w, 0.14257544, 2e-4, "g01c_w");
            check_near(r.boxes[0].h, 0.09574703, 2e-4, "g01d_h");
        }
    }

    // ---- g02-g04 掩码结构 ----
    {
        SegConfig c = base_cfg(0.25f, 0.45f, 0.0f, true);
        Fx fx;
        fx.alloc(c);
        fx.proto_all(c, 1.0);
        const int bins[4] = {4, 4, 4, 4};
        float coeffs[32];
        ones(coeffs);
        fx.encode(0, 30, 40, bins, 12.0f, 0.9, coeffs);
        fx.bind();
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, c, L, SW, SH, &r, &e) && r.boxes.size() == 1 &&
                  r.masks.size() == 1, "g02_fixture_decodes", e.c_str());
        if (r.masks.size() == 1) {
            const SegMask& m = r.masks[0];
            check(m.box_index == 0, "g03_box_index_links");
            check(m.width == SW && m.height == SH && m.stride_bytes == SW &&
                      m.plane.size() == static_cast<size_t>(SW) * SH, "g04_dims_stride_size");
            // 逐像素手算点积样本: 点积=32*1=32; bilinear 在平坦 1 区域恒 1>0 → 框内左半 1
            check(m.plane[240 * m.stride_bytes + 600] == 1, "g04a_px600_1");
            check(m.plane[240 * m.stride_bytes + 400] == 0, "g04b_px400_0");
        }
    }

    // ---- g05-g07 crop 几何闭式 ----
    {
        SegConfig c = base_cfg(0.25f, 0.45f, 0.0f, true);
        Fx fx;
        fx.alloc(c);
        fx.proto_half(c);  // 半幅 pattern 是 7040 闭式计数的预期前提
        const int bins[4] = {4, 4, 4, 4};
        float coeffs[32];
        ones(coeffs);
        fx.encode(0, 30, 40, bins, 12.0f, 0.9, coeffs);
        fx.bind();
        SegResult r;
        std::string e;
        decode_yolov8_seg(fx.views, c, L, SW, SH, &r, &e);
        if (r.masks.size() == 1) {
            check(r.masks[0].positive == 7040, "g05_crop_closed_form_7040", res_sum(r).c_str());
            int rowp = 0;
            for (int x = 0; x < SW; ++x)
                if (r.masks[0].plane[240 * r.masks[0].stride_bytes + x]) ++rowp;
            char g06d[40];
            std::snprintf(g06d, sizeof(g06d), "rowp=%d", rowp);
            check(rowp == 55, "g06_row_split_positive_55_only", g06d);
        } else {
            check(false, "g05_crop_closed_form_7040", res_sum(r).c_str());
            check(false, "g06_row_split_positive_55_only", res_sum(r).c_str());
        }
    }
    {
        // 掩码独立所有权: 两掩码 plane 内存不共享（两次独立 decode）
        SegConfig c = base_cfg(0.25f, 0.45f, 0.0f, true);
        Fx fx1;
        fx1.alloc(c);
        fx1.proto_half(c);
        const int bins[4] = {4, 4, 4, 4};
        float coeffs[32];
        ones(coeffs);
        fx1.encode(0, 30, 40, bins, 12.0f, 0.9, coeffs);
        fx1.bind();
        SegResult ra;
        std::string e;
        decode_yolov8_seg(fx1.views, c, L, SW, SH, &ra, &e);
        fx1.proto_all(c, 0.0);
        SegResult rb;
        decode_yolov8_seg(fx1.views, c, L, SW, SH, &rb, &e);
        check(ra.masks.size() == 1 && rb.masks.size() == 1 &&
              ra.masks[0].positive == 7040 && rb.masks[0].positive == 0,
              "g07_proto_change_reflected_no_stale_plane",
              (res_sum(ra) + res_sum(rb)).c_str());
    }

    // ---- g08-g09 nocrop + nms 掩码不叠加 ----
    {
        SegConfig c = base_cfg(0.25f, 0.45f, 0.0f, false);
        Fx fx;
        fx.alloc(c);
        fx.proto_half(c);  // 不裁剪时正区=整幅左半 720x640=460800
        const int bins[4] = {4, 4, 4, 4};
        float coeffs[32];
        ones(coeffs);
        fx.encode(0, 30, 40, bins, 12.0f, 0.9, coeffs);
        fx.bind();
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, c, L, SW, SH, &r, &e) && r.masks.size() == 1 &&
              r.masks[0].positive == 460800, "g08_nocrop_full_460800", res_sum(r).c_str());
    }
    {
        SegConfig c = base_cfg(0.25f, 0.45f, 0.0f, true);
        Fx fx;
        fx.alloc(c);
        fx.proto_half(c);
        const int bins[4] = {4, 4, 4, 4};
        float coeffs[32];
        ones(coeffs);
        fx.encode(0, 30, 40, bins, 12.0f, 0.9, coeffs);
        fx.encode(0, 30, 41, bins, 12.0f, 0.8, coeffs);
        fx.bind();
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, c, L, SW, SH, &r, &e) && r.boxes.size() == 1 &&
              r.masks.size() == 1 && r.masks[0].box_index == 0 && r.masks[0].positive == 7040,
              "g09_nms_mask_follows_kept_box_only", res_sum(r).c_str());
    }

    // ---- g10-g13 NMS 手算 ----
    {
        SegConfig c = base_cfg(0.25f, 0.45f, 0.0f, true);
        Fx fx;
        fx.alloc(c);
        fx.proto_all(c, 1.0);
        const int bins[4] = {4, 4, 4, 4};
        float coeffs[32];
        ones(coeffs);
        // B (30,41): 验证侧独立 IoU(+1) = 0.7808219178082192 > 0.45 抑制
        fx.encode(0, 30, 40, bins, 12.0f, 0.9, coeffs);
        fx.encode(0, 30, 41, bins, 12.0f, 0.8, coeffs);
        fx.bind();
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, c, L, SW, SH, &r, &e) && r.nms_suppressed == 1 &&
              r.boxes.size() == 1, "g10_nms_suppress_0.7808");
        // 恰等阈值不被抑制: nms_iou 精确设为计算出值
        SegConfig c2 = base_cfg(0.25f, 0.79f, 0.0f, true);
        Fx fx2;
        fx2.alloc(c2);
        fx2.proto_all(c2, 1.0);
        fx2.encode(0, 30, 40, bins, 12.0f, 0.9, coeffs);
        fx2.encode(0, 30, 41, bins, 12.0f, 0.8, coeffs);
        fx2.bind();
        SegResult r2;
        check(decode_yolov8_seg(fx2.views, c2, L, SW, SH, &r2, &e) &&
              r2.nms_suppressed == 0 && r2.boxes.size() == 2, "g11_iou_just_below_threshold_keeps");
        // bins dl=5: B=(284,212)-(364,276) IoU=0.8024691358024691 抑制
        const int bins5[4] = {5, 4, 5, 4};
        Fx fx3;
        fx3.alloc(c);
        fx3.proto_half(c);
        fx3.encode(0, 30, 40, bins, 12.0f, 0.9, coeffs);
        fx3.encode(0, 30, 41, bins5, 12.0f, 0.8, coeffs);  // 相邻网格 cell，避免同格双编码覆盖
        fx3.bind();
        SegResult r3;
        check(decode_yolov8_seg(fx3.views, c, L, SW, SH, &r3, &e) && r3.nms_suppressed == 1 &&
              r3.boxes.size() == 1, "g12_wider_bin5_iou_0.8025_suppresses", res_sum(r3).c_str());
        // 三框完全分离三保留
        Fx fx4;
        fx4.alloc(c);
        fx4.proto_half(c);
        // A=s0(30,10): y1=212 不落 pad 丢框区; B=s0(40,40)=主框附近; C=s1(20,20)
        fx4.encode(0, 30, 10, bins, 12.0f, 0.9, coeffs);
        fx4.encode(0, 40, 40, bins, 12.0f, 0.85, coeffs);
        fx4.encode(1, 20, 20, bins, 12.0f, 0.8, coeffs);
        fx4.bind();
        SegResult r4;
        check(decode_yolov8_seg(fx4.views, c, L, SW, SH, &r4, &e) && r4.boxes.size() == 3 &&
              r4.masks.size() == 3 && r4.nms_suppressed == 0, "g13_three_disjoint_kept",
              res_sum(r4).c_str());
    }

    // ---- g14-g15 极端参数 ----
    {
        SegConfig c = base_cfg(0.25f, 0.45f, -1.0f, true);
        Fx fx;
        fx.alloc(c);
        // proto 全 +0.5: 点积 +16 > -1（阈值 -1.0 显式更低）→ crop 内全正
        fx.proto_all(c, 0.5);
        const int bins[4] = {4, 4, 4, 4};
        float coeffs[32];
        ones(coeffs);
        fx.encode(0, 30, 40, bins, 12.0f, 0.9, coeffs);
        fx.bind();
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, c, L, SW, SH, &r, &e) && r.masks.size() == 1 &&
              r.masks[0].positive == 16384, "g14_negative_mask_threshold_-1_keeps",
              res_sum(r).c_str());
        // 同夹具 mask_threshold=0 → 0 positives（「不把软掩码当布尔」阈值语义）
        SegConfig c2 = base_cfg(0.25f, 0.45f, 0.0f, true);
        SegResult r2;
        check(decode_yolov8_seg(fx.views, c2, L, SW, SH, &r2, &e) && r2.masks.size() == 1 &&
              r2.masks[0].positive == 16384, "g15_same_fixture_zero_threshold_fullcrop",
              res_sum(r2).c_str());
    }
    {
        SegConfig c = base_cfg(0.25f, 0.45f, 0.0f, true);
        c.num_classes = 2;
        c.class_whitelist = {0, 1};
        Fx fx;
        fx.alloc(c);
        fx.proto_all(c, 1.0);
        const int bins[4] = {4, 4, 4, 4};
        float coeffs[32];
        ones(coeffs);
        fx.encode(0, 30, 40, bins, 12.0f, 0.9, coeffs, 0);
        fx.encode(0, 50, 60, bins, 12.0f, 0.8, coeffs, 1);
        fx.bind();
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, c, L, SW, SH, &r, &e) && r.boxes.size() == 2 &&
              r.masks.size() == 2 && r.boxes[1].label == "cls_1" && r.boxes[0].label == "rail",
              "g16_two_classes_labels");
    }

    // ---- g17-g19 跨尺度联合 ----
    {
        SegConfig c = base_cfg(0.25f, 0.45f, 0.0f, true);
        Fx fx;
        fx.alloc(c);
        fx.proto_all(c, 1.0);
        const int bins[4] = {4, 4, 4, 4};
        float coeffs[32];
        ones(coeffs);
        // s1 (15,15) bins(2,2,3,3): 验证侧预言 (0.3375,0.2111111,0.125,0.2222222)
        const int b1[4] = {2, 2, 3, 3};
        fx.encode(1, 15, 15, b1, 12.0f, 0.85, coeffs);
        // s2 (12,10) bins(1,1,2,1): 预言 (0.475,0.6333333,0.15,0.1777778)
        const int b2[4] = {1, 1, 2, 1};
        fx.encode(2, 12, 10, b2, 12.0f, 0.8, coeffs);
        fx.bind();
        SegResult r;
        std::string e;
        check(decode_yolov8_seg(fx.views, c, L, SW, SH, &r, &e) && r.boxes.size() == 2,
              "g17_s1_s2_decode", e.c_str());
        if (r.boxes.size() == 2) {
            check_near(r.boxes[0].x, 0.3375, 2e-4, "g18_s1_x");
            check_near(r.boxes[0].y, 0.2111111, 2e-4, "g18b_s1_y");
            check_near(r.boxes[0].w, 0.125, 2e-4, "g18c_s1_w");
            check_near(r.boxes[0].h, 0.2222222, 2e-4, "g18d_s1_h");
            check_near(r.boxes[1].x, 0.475, 2e-4, "g19_s2_x");
            check_near(r.boxes[1].y, 0.6333333, 2e-4, "g19b_s2_y");
            check_near(r.boxes[1].w, 0.15, 2e-4, "g19c_s2_w");
            check_near(r.boxes[1].h, 0.1777778, 2e-4, "g19d_s2_h");
        }
    }

    // ---- g20-g21 畸形张量 ----
    {
        SegConfig c = base_cfg(0.25f, 0.45f, 0.0f, true);
        Fx fx;
        fx.alloc(c);
        fx.bind();
        // proto 非方: 覆写 h/w 不等（proto_channels 恒 32；h=200,w=120）
        fx.views[12].height = 200;
        fx.views[12].width = 120;
        SegResult r;
        std::string e;
        check(!decode_yolov8_seg(fx.views, c, L, SW, SH, &r, &e) &&
              r.reason == seg_reason::kModelMismatch(), "g20_proto_nonsquare_mismatch", e.c_str());
    }
    {
        SegConfig c = base_cfg(0.25f, 0.45f, 0.0f, true);
        Fx fx;
        fx.alloc(c);
        fx.bind();
        fx.planes[11].assign(fx.planes[11].size(), 0.0f);
        fx.views[11].channels = 1;  // s2 分支（base 偏移 8）seg 系数通道残缺
        SegResult r;
        std::string e;
        check(!decode_yolov8_seg(fx.views, c, L, SW, SH, &r, &e) &&
              r.reason == seg_reason::kModelMismatch(), "g21_s2_branch_seg_channels_mismatch", e.c_str());
    }

    // ---- g22-g24 确定性与边界 ----
    {
        SegConfig c = base_cfg(0.25f, 0.45f, 0.0f, true);
        Fx fx;
        fx.alloc(c);
        fx.proto_all(c, 1.0);
        const int bins[4] = {4, 4, 4, 4};
        float coeffs[32];
        ones(coeffs);
        fx.encode(0, 30, 40, bins, 12.0f, 0.9, coeffs);
        fx.bind();
        SegResult r1, r2;
        std::string e;
        decode_yolov8_seg(fx.views, c, L, SW, SH, &r1, &e);
        decode_yolov8_seg(fx.views, c, L, SW, SH, &r2, &e);
        bool same = r1.boxes.size() == r2.boxes.size() && r1.masks.size() == r2.masks.size();
        if (same && !r1.boxes.empty()) {
            same = r1.boxes[0].x == r2.boxes[0].x && r1.boxes[0].y == r2.boxes[0].y &&
                   r1.boxes[0].score == r2.boxes[0].score;
        }
        if (same && !r1.masks.empty()) {
            same = std::memcmp(r1.masks[0].plane.data(), r2.masks[0].plane.data(),
                               r1.masks[0].plane.size()) == 0;
        }
        check(same, "g22_two_runs_bitwise_identical");
        // x+w==1.0 精确边界: 有点到右缘的框（dl 需大于网格外的负面影响）
        // x 右缘裁剪: cell j=79、bins dl=4: x1=(-4+79.5)*8=604 → rx1=1208;
        // x2=(4+79.5)*8=668 → rx2=1336 clamp SW=1280 → x=0.94375, w=0.05625, x+w==1.0 精确
        Fx fx2;
        fx2.alloc(c);
        fx2.proto_half(c);
        const int be[4] = {4, 4, 4, 4};
        fx2.encode(0, 30, 79, be, 12.0f, 0.9, coeffs);
        fx2.bind();
        SegResult re;
        check(decode_yolov8_seg(fx2.views, c, L, SW, SH, &re, &e) && re.boxes.size() == 1,
              "g23_right_clamp_decode", e.c_str());
        if (re.boxes.size() == 1)
            check_near(re.boxes[0].x + re.boxes[0].w, 1.0, 1e-6, "g23b_x_plus_w_equals_1");
        check(r1.reason == seg_reason::kOk() && std::string(seg_reason::kOk()) == "SEG_OK",
              "g24_reason_token_stable");
    }

    std::printf("vext OK (%d failures, %d pass)\n", g_fail, g_pass);
    return g_fail == 0 ? 0 : 1;
}
