#include "railhmi/core/layout.h"

#include <cmath>

namespace railhmi {
namespace core {

namespace {
bool overlap(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
}  // namespace

LayoutMetrics compute_layout(int width, int height) {
    LayoutMetrics m;
    m.width = width;
    m.height = height;
    m.margin = (height >= 1080) ? 24 : 16;

    m.top_bar = Rect{0, 0, width, m.top_bar_h};
    m.banner = Rect{0, m.top_bar_h, width, 64};

    m.alarm_bar = Rect{0, height - m.alarm_bar_h, width, m.alarm_bar_h};
    m.status_strip = Rect{0, height - m.alarm_bar_h - m.status_strip_h, width, m.status_strip_h};

    const int row_y = m.top_bar_h + m.margin;
    const int row_h = m.status_strip.y - m.margin - row_y;
    const int row_x = m.margin;
    const int row_w = width - 2 * m.margin;
    m.row = Rect{row_x, row_y, row_w > 0 ? row_w : 0, row_h > 0 ? row_h : 0};

    const int left_w = static_cast<int>(std::lround(m.row.w * m.left_ratio));
    const int right_w = static_cast<int>(std::lround(m.row.w * m.right_ratio));
    const int front_w = m.row.w - left_w - right_w;
    m.left = Rect{row_x, row_y, left_w, m.row.h};
    m.front = Rect{row_x + left_w, row_y, front_w, m.row.h};
    m.right = Rect{row_x + left_w + front_w, row_y, right_w, m.row.h};

    const int cx = m.front.x + static_cast<int>(std::lround(m.front.w * 0.2));
    const int cy = m.front.y + static_cast<int>(std::lround(m.front.h * 0.2));
    m.front_center = Rect{cx, cy,
                          static_cast<int>(std::lround(m.front.w * 0.6)),
                          static_cast<int>(std::lround(m.front.h * 0.6))};
    return m;
}

bool three_panel_ordered(const LayoutMetrics& m) {
    return m.left.x < m.front.x && m.front.x < m.right.x &&
           m.left.w > 0 && m.front.w > 0 && m.right.w > 0;
}

bool front_is_largest(const LayoutMetrics& m) {
    if (m.row.w <= 0) return false;
    const double front_ratio = static_cast<double>(m.front.w) / m.row.w;
    return m.front.w > m.left.w && m.front.w > m.right.w && front_ratio >= 0.34;
}

bool p0_fits_without_scroll(const LayoutMetrics& m) {
    const int used = m.top_bar.h + m.row.h + m.status_strip.h + m.alarm_bar.h + 2 * m.margin;
    return m.row.h >= 200 && used <= m.height;
}

bool alarm_banner_obscures_front_center(const LayoutMetrics& m) {
    return overlap(m.banner, m.front_center);
}

bool ratios_preserved(const LayoutMetrics& a, const LayoutMetrics& b) {
    if (a.row.w <= 0 || b.row.w <= 0) return false;
    const double atol = 0.02;
    const double al = static_cast<double>(a.left.w) / a.row.w;
    const double bl = static_cast<double>(b.left.w) / b.row.w;
    const double af = static_cast<double>(a.front.w) / a.row.w;
    const double bf = static_cast<double>(b.front.w) / b.row.w;
    const double ar = static_cast<double>(a.right.w) / a.row.w;
    const double br = static_cast<double>(b.right.w) / b.row.w;
    return std::fabs(al - bl) <= atol && std::fabs(af - bf) <= atol &&
           std::fabs(ar - br) <= atol;
}

}  // namespace core
}  // namespace railhmi
