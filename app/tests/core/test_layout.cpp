// railhmi-core 实时监控页三联布局测试（ITEM-014 / 视觉 §8.1、交互 §7.1、TEST_DESIGN TC-LAYOUT）。
// 两档分辨率（1920x1080 / 1280x720）下核对：三联顺序左-前-右、前方面积最大且 >=0.34、
// P0 元素不滚动即可见、一级报警横幅不遮挡前方中央可视区、缩放比例保持。

#include "test_util.h"

#include "railhmi/core/layout.h"

namespace {

using namespace railhmi::core;

void check_resolution(int w, int h) {
    const LayoutMetrics m = compute_layout(w, h);
    const std::string tag = std::to_string(w) + "x" + std::to_string(h);

    railhmi_test::check(three_panel_ordered(m), tag + " three-panel order left-front-right");
    railhmi_test::check(front_is_largest(m), tag + " front largest (>=0.34 width)");
    railhmi_test::check(m.front.w > m.left.w && m.front.w > m.right.w,
                        tag + " front wider than both sides");
    railhmi_test::check(p0_fits_without_scroll(m), tag + " P0 elements fit without scroll");
    railhmi_test::check(!alarm_banner_obscures_front_center(m),
                        tag + " L1 alarm banner does not cover front center");

    // 三联铺满行宽（无重叠/无空隙）
    railhmi_test::check(m.left.x + m.left.w == m.front.x,
                        tag + " left/front adjacent");
    railhmi_test::check(m.front.x + m.front.w == m.right.x,
                        tag + " front/right adjacent");
    railhmi_test::check(m.right.x + m.right.w == m.row.x + m.row.w,
                        tag + " right flush to row edge");

    // 前方中央可视区落在前方面板内部
    railhmi_test::check(m.front_center.x >= m.front.x &&
                            m.front_center.y >= m.front.y &&
                            m.front_center.x + m.front_center.w <= m.front.x + m.front.w &&
                            m.front_center.y + m.front_center.h <= m.front.y + m.front.h,
                        tag + " front center inside front panel");

    // P0 顶部状态条与底部关键状态区/报警条都在屏内且不重叠三联行
    railhmi_test::check(m.top_bar.y == 0 && m.top_bar.h > 0, tag + " top bar at top");
    railhmi_test::check(m.status_strip.y >= m.row.y + m.row.h, tag + " status strip below row");
    railhmi_test::check(m.alarm_bar.y + m.alarm_bar.h == h, tag + " alarm bar flush bottom");
}

void test_two_resolutions() {
    check_resolution(1920, 1080);
    check_resolution(1280, 720);
}

void test_ratios_preserved_on_resize() {
    const LayoutMetrics a = compute_layout(1920, 1080);
    const LayoutMetrics b = compute_layout(1280, 720);   // 同 16:9
    const LayoutMetrics c = compute_layout(960, 540);    // 同 16:9 缩小
    railhmi_test::check(ratios_preserved(a, b), "16:9 1920x1080 vs 1280x720 ratios preserved");
    railhmi_test::check(ratios_preserved(a, c), "16:9 1920x1080 vs 960x540 ratios preserved");
    railhmi_test::check(three_panel_ordered(b) && three_panel_ordered(c),
                        "resized layouts keep left-front-right order");
}

void test_front_min_area_ratio() {
    const LayoutMetrics m = compute_layout(1920, 1080);
    const double front = static_cast<double>(m.front.w) / m.row.w;
    const double side = static_cast<double>(m.left.w) / m.row.w;
    railhmi_test::check(front >= 0.34, "front area ratio >= 0.34");
    railhmi_test::check(side >= 0.22 && side <= 0.30, "side ratio within [0.22,0.30]");
}

}  // namespace

void run_layout_tests() {
    test_two_resolutions();
    test_ratios_preserved_on_resize();
    test_front_min_area_ratio();
}
