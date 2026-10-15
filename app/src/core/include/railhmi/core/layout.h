#pragma once

// railhmi-core: 实时监控页三联布局与 P0 可见性（ITEM-014）。
// 纯几何计算（像素 rect + 比例），可用单元测试在两档分辨率下核对：
// 左-前-右顺序、前方面积最大、P0 元素不滚动即可见、一级报警横幅不遮挡前方中央可视区。
//
// 仅依赖 C++ 标准库。

namespace railhmi {
namespace core {

struct Rect {
    int x = 0, y = 0, w = 0, h = 0;
};

struct LayoutMetrics {
    int width = 0, height = 0, margin = 0;
    int top_bar_h = 48;
    int status_strip_h = 64;
    int alarm_bar_h = 64;
    double left_ratio = 0.26;   // 视觉规范 §8.1
    double front_ratio = 0.48;
    double right_ratio = 0.26;

    Rect top_bar;
    Rect banner;          // 一级报警横幅（浮层，顶部）
    Rect row;             // 三联行
    Rect left;            // 左 CAM-02
    Rect front;           // 前 CAM-01（最大）
    Rect right;           // 右 CAM-03
    Rect status_strip;    // 关键状态区
    Rect alarm_bar;       // 当前报警条
    Rect front_center;    // 前方画面中央可视区（用于横幅遮挡判据）
};

LayoutMetrics compute_layout(int width, int height);

bool three_panel_ordered(const LayoutMetrics& m);
bool front_is_largest(const LayoutMetrics& m);
bool p0_fits_without_scroll(const LayoutMetrics& m);
bool alarm_banner_obscures_front_center(const LayoutMetrics& m);
bool ratios_preserved(const LayoutMetrics& a, const LayoutMetrics& b);

}  // namespace core
}  // namespace railhmi
