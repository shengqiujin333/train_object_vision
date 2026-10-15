// ITEM-014 验证自有扩展夹具（software_verification, verifier-owned）。
// 断言范围在实现者 test_layout.cpp 之外独立设计：
//   g0/g1  两档验收分辨率（1920x1080 / 1280x720）逐像素精确值身份
//          （手算常量逐字段 == compute_layout 输出，防判据函数掩盖几何漂移）
//   s0..s5 判据函数鉴别力：伪造坏布局必须判 false / 伪造遮挡必须判 true
//          （防"永远返回 true"的空判据）
//   r0..r5 常规窗口尺寸响应矩阵（800x480..3840x2160）：顺序+邻接+前方最大+
//          前方中央在面板内+与 1920x1080 比例保持（验收 resize 不变量全尺寸域）
//   w0/w1  高度扫描 720..2160（步进 8，宽 1920）：row.h 恒等式/P0 fit/横幅不遮前方中央；
//          宽度扫描 640..3840（步进 8，h=720 与 1080）：顺序/邻接/前方最大/中央/比例
//   c0     两档分辨率横幅底部与前方中央顶部的实测裕量（>0 且记录具体值）
//   d0/d1  计算确定性（逐字段相等）与 0 尺寸无崩溃语义
// 纯模型层（railhmi-core），不依赖 Qt。
#include <cstdio>
#include <string>

#include "railhmi/core/layout.h"

using railhmi::core::LayoutMetrics;
using railhmi::core::Rect;
using railhmi::core::alarm_banner_obscures_front_center;
using railhmi::core::compute_layout;
using railhmi::core::front_is_largest;
using railhmi::core::p0_fits_without_scroll;
using railhmi::core::ratios_preserved;
using railhmi::core::three_panel_ordered;

namespace {
int g_total = 0;
int g_pass = 0;

void check(bool ok, const std::string& name, const std::string& detail = "") {
    ++g_total;
    if (ok) {
        ++g_pass;
        std::printf("[PASS] %s\n", name.c_str());
    } else {
        std::printf("[FAIL] %s%s%s\n", name.c_str(), detail.empty() ? "" : " | ",
                    detail.c_str());
        std::fflush(stdout);
    }
}

bool rect_eq(const Rect& a, const Rect& b) {
    return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

bool tiles_adjacent(const LayoutMetrics& m) {
    return m.left.x + m.left.w == m.front.x &&
           m.front.x + m.front.w == m.right.x &&
           m.right.x + m.right.w == m.row.x + m.row.w;
}

bool center_inside_front(const LayoutMetrics& m) {
    return m.front_center.x >= m.front.x && m.front_center.y >= m.front.y &&
           m.front_center.x + m.front_center.w <= m.front.x + m.front.w &&
           m.front_center.y + m.front_center.h <= m.front.y + m.front.h;
}

void test_exact_tiers() {
    // ---- g0: 1920x1080 手算精确值 ----
    // margin=24; row=(24,72,1872,856); left=lround(1872*0.26)=487;
    // front=1872-487-487=898; right=(1409,72,487,856);
    // center=(511+180, 72+171, 539, 514)
    {
        const LayoutMetrics m = compute_layout(1920, 1080);
        check(m.width == 1920 && m.height == 1080 && m.margin == 24,
              "g0a 1920x1080 margin=24");
        check(rect_eq(m.top_bar, Rect{0, 0, 1920, 48}), "g0b top bar exact (0,0,1920,48)");
        check(rect_eq(m.banner, Rect{0, 48, 1920, 64}),
              "g0c L1 banner strip exact (0,48,1920,64)");
        check(rect_eq(m.alarm_bar, Rect{0, 1016, 1920, 64}),
              "g0d alarm bar exact flush bottom (0,1016,1920,64)");
        check(rect_eq(m.status_strip, Rect{0, 952, 1920, 64}),
              "g0e status strip exact (0,952,1920,64)");
        check(rect_eq(m.row, Rect{24, 72, 1872, 856}), "g0f row exact (24,72,1872,856)");
        check(rect_eq(m.left, Rect{24, 72, 487, 856}), "g0g left exact (24,72,487,856)");
        check(rect_eq(m.front, Rect{511, 72, 898, 856}), "g0h front exact (511,72,898,856)");
        check(rect_eq(m.right, Rect{1409, 72, 487, 856}), "g0i right exact (1409,72,487,856)");
        check(rect_eq(m.front_center, Rect{691, 243, 539, 514}),
              "g0j front center exact (691,243,539,514)");
        check(tiles_adjacent(m) && center_inside_front(m),
              "g0k 1920 panels adjacent, center inside front");
        check(alarm_banner_obscures_front_center(m) == false,
              "g0l 1920 banner does not obscure front center");
    }
    // ---- g1: 1280x720 手算精确值 ----
    // margin=16; row=(16,64,1248,512); left=lround(1248*0.26)=324;
    // front=600; right=(940,64,324,512); center=(340+120,64+102,360,307)
    {
        const LayoutMetrics m = compute_layout(1280, 720);
        check(m.width == 1280 && m.height == 720 && m.margin == 16,
              "g1a 1280x720 margin=16");
        check(rect_eq(m.top_bar, Rect{0, 0, 1280, 48}), "g1b top bar exact (0,0,1280,48)");
        check(rect_eq(m.banner, Rect{0, 48, 1280, 64}),
              "g1c L1 banner strip exact (0,48,1280,64)");
        check(rect_eq(m.alarm_bar, Rect{0, 656, 1280, 64}),
              "g1d alarm bar exact flush bottom (0,656,1280,64)");
        check(rect_eq(m.status_strip, Rect{0, 592, 1280, 64}),
              "g1e status strip exact (0,592,1280,64)");
        check(rect_eq(m.row, Rect{16, 64, 1248, 512}), "g1f row exact (16,64,1248,512)");
        check(rect_eq(m.left, Rect{16, 64, 324, 512}), "g1g left exact (16,64,324,512)");
        check(rect_eq(m.front, Rect{340, 64, 600, 512}), "g1h front exact (340,64,600,512)");
        check(rect_eq(m.right, Rect{940, 64, 324, 512}), "g1i right exact (940,64,324,512)");
        check(rect_eq(m.front_center, Rect{460, 166, 360, 307}),
              "g1j front center exact (460,166,360,307)");
        check(tiles_adjacent(m) && center_inside_front(m),
              "g1k 720 panels adjacent, center inside front");
        check(alarm_banner_obscures_front_center(m) == false,
              "g1l 720 banner does not obscure front center");
    }
}

void test_criterion_discrimination() {
    // ---- s 系: 判据函数必须能拒绝坏布局（防恒真判据） ----
    {
        LayoutMetrics bad;
        bad.row = Rect{24, 72, 1000, 400};
        bad.left = Rect{924, 72, 100, 400};
        bad.front = Rect{24, 72, 600, 400};
        bad.right = Rect{124, 72, 300, 400};
        check(!three_panel_ordered(bad),
              "s0a wrong panel order (front leftmost) -> three_panel_ordered=false");
    }
    {
        LayoutMetrics bad;
        bad.row = Rect{24, 72, 1000, 400};
        bad.left = Rect{24, 72, 600, 400};
        bad.front = Rect{624, 72, 300, 400};
        bad.right = Rect{924, 72, 76, 400};
        check(!front_is_largest(bad),
              "s0b front narrower than left -> front_is_largest=false");
    }
    {
        LayoutMetrics bad;
        bad.height = 1080;
        bad.margin = 24;
        bad.top_bar = Rect{0, 0, 1920, 48};
        bad.row = Rect{24, 72, 1872, 2000};
        check(!p0_fits_without_scroll(bad),
              "s0c row too tall (P0 sum 2224 > 1080) -> p0_fits=false");
    }
    {
        LayoutMetrics m = compute_layout(1280, 720);
        m.banner = Rect{0, 48, 1280, 256};  // 下探到 304，覆盖中央区
        check(alarm_banner_obscures_front_center(m),
              "s0d banner covering front center -> obscures=true");
    }
    {
        LayoutMetrics a;
        a.row = Rect{24, 72, 1000, 400};
        a.left = Rect{24, 72, 260, 400};
        a.front = Rect{284, 72, 480, 400};
        a.right = Rect{764, 72, 260, 400};
        LayoutMetrics b = a;
        b.left = Rect{24, 72, 100, 400};
        b.right = Rect{644, 72, 260, 400};
        check(!ratios_preserved(a, b), "s0e left ratio 0.26 vs 0.10 -> ratios_preserved=false");
        b.left = Rect{24, 72, 261, 400};
        b.right = Rect{644, 72, 260, 400};
        b.front = Rect{285, 72, 479, 400};
        check(ratios_preserved(a, b), "s0f 0.001 drift -> ratios_preserved=true");
        a.row = Rect{24, 72, 0, 400};
        check(!ratios_preserved(a, b), "s0g zero row width -> ratios_preserved=false guard");
    }
    {
        // 顺序/最大/fit 判据在真实两档输出上必须为 true（与鉴别力互为反例）
        const LayoutMetrics m = compute_layout(1920, 1080);
        check(three_panel_ordered(m) && front_is_largest(m) && p0_fits_without_scroll(m),
              "s1 real 1920x1080 passes all three criteria");
    }
}

bool matrix_ok_for(const LayoutMetrics& m) {
    return three_panel_ordered(m) && front_is_largest(m) && tiles_adjacent(m) &&
           center_inside_front(m) && !alarm_banner_obscures_front_center(m);
}

void test_resize_matrix() {
    // ---- r 系: 常规窗口尺寸（验收 resize 不变量全尺寸域），与 1920x1080 比例保持 ----
    const LayoutMetrics base = compute_layout(1920, 1080);
    const int sizes[][2] = {{800, 480},  {1024, 600},  {1366, 768}, {1600, 900},
                            {1920, 1080}, {2560, 1440}, {3840, 2160}};
    for (const auto& s : sizes) {
        const LayoutMetrics m = compute_layout(s[0], s[1]);
        check(matrix_ok_for(m) && ratios_preserved(base, m),
              "r resize " + std::to_string(s[0]) + "x" + std::to_string(s[1]) +
                  " order/largest/adjacent/center/banner/fit-in-acceptance");
    }
}

void test_continuous_scans() {
    // ---- w0: 高度扫描（宽 1920）: row.h 恒等式 + P0 一定 fit(sum==h) + 横幅不遮中心 ----
    bool okh = true;
    for (int h = 720; h <= 2160; h += 8) {
        const LayoutMetrics m = compute_layout(1920, h);
        const int margin = (h >= 1080) ? 24 : 16;
        const int expect_row_h = h - 176 - 2 * margin;
        okh &= (m.row.h == expect_row_h);
        okh &= (m.top_bar.h + m.row.h + m.status_strip.h + m.alarm_bar.h +
                2 * m.margin) == h;  // used == height 恒等式
        okh &= p0_fits_without_scroll(m);
        okh &= !alarm_banner_obscures_front_center(m);
    }
    check(okh, "w0 height scan 720..2160 step8: row-h identity + used==h + p0 + banner clear");

    // ---- w1: 宽度扫描（h=720 与 h=1080 各一遍）----
    const LayoutMetrics base = compute_layout(1920, 1080);
    const int heights[2] = {720, 1080};
    for (int hi = 0; hi < 2; ++hi) {
        bool okw = true;
        for (int w = 640; w <= 3840; w += 8) {
            const LayoutMetrics m = compute_layout(w, heights[hi]);
            okw &= matrix_ok_for(m) && ratios_preserved(base, m);
        }
        check(okw, "w1 width scan 640..3840 step8 @" +
                       std::to_string(heights[hi]) +
                       ": order/largest/adjacent/center/banner clear + ratios vs 1920x1080");
    }
}

void test_clearance_and_determinism() {
    // ---- c0: 两档分辨率横幅底部(top=112) 与 前方中央顶部的实测裕量 ----
    {
        const LayoutMetrics a = compute_layout(1920, 1080);
        const int margin_a = a.front_center.y - (a.banner.y + a.banner.h);
        check(margin_a > 0, "c0a 1920x1080 banner-to-center clearance >0",
              std::to_string(margin_a));
        const LayoutMetrics b = compute_layout(1280, 720);
        const int margin_b = b.front_center.y - (b.banner.y + b.banner.h);
        check(margin_b > 0, "c0b 1280x720 banner-to-center clearance >0",
              std::to_string(margin_b));
    }
    // ---- d0: 同尺寸两次计算逐字段确定性 ----
    {
        const LayoutMetrics a = compute_layout(1366, 768);
        const LayoutMetrics b = compute_layout(1366, 768);
        check(a.margin == b.margin && rect_eq(a.top_bar, b.top_bar) &&
                  rect_eq(a.banner, b.banner) && rect_eq(a.row, b.row) &&
                  rect_eq(a.left, b.left) && rect_eq(a.front, b.front) &&
                  rect_eq(a.right, b.right) && rect_eq(a.status_strip, b.status_strip) &&
                  rect_eq(a.alarm_bar, b.alarm_bar) &&
                  rect_eq(a.front_center, b.front_center),
              "d0 compute_layout deterministic field-by-field (1366x768)");
    }
    // ---- d1: 0 尺寸无崩溃且不产生可见面板 ----
    {
        const LayoutMetrics z = compute_layout(0, 0);
        check(!three_panel_ordered(z) && !front_is_largest(z) && !p0_fits_without_scroll(z),
              "d1 zero-size layout: no crash, no visible panels claimed");
    }
}

}  // namespace

int main() {
    test_exact_tiers();
    test_criterion_discrimination();
    test_resize_matrix();
    test_continuous_scans();
    test_clearance_and_determinism();
    std::printf("== extension fixtures ==\n== %d passed, %d failed ==\n", g_pass,
                g_total - g_pass);
    const bool ok = (g_pass == g_total);
    std::printf("RESULT: %s passed=%d failed=%d\n", ok ? "PASS" : "FAIL", g_pass,
                g_total - g_pass);
    return ok ? 0 : 1;
}
