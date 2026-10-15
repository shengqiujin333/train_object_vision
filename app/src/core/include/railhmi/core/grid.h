#pragma once

// railhmi-core: 全摄像头页网格布局（ITEM-015）。
// 支持 3x2 / 2x3 两种形状，槽位由序号固定推导——断流/冻结不改变序号与行列，
// 因此网格不会因状态变化而重排（保留位置）。纯几何 + 稳定槽位，单元测试可覆盖。

#include <vector>

#include "railhmi/core/layout.h"   // reuse core::Rect

namespace railhmi {
namespace core {

// ThreeByTwo = 3 列 x 2 行（宽屏）；TwoByThree = 2 列 x 3 行（竖屏）。
enum class GridShape { ThreeByTwo, TwoByThree };

// 按窗口宽高比选择形状：宽 >= 高 -> 3x2，否则 2x3。
GridShape choose_grid_shape(int width, int height);
const char* to_string(GridShape s);
int grid_columns(GridShape s);
int grid_rows(GridShape s);

struct GridSlot {
    int index = 0;
    int row = 0;
    int col = 0;
};

// 稳定槽位：slot(i) = { row = i / cols, col = i % cols }，只依赖形状与序号，与运行时状态无关。
std::vector<GridSlot> grid_slots(int count, GridShape shape);
bool grid_slots_equal(const std::vector<GridSlot>& a, const std::vector<GridSlot>& b);

// 单元格像素矩形（含 gap；不含外边距）。
Rect grid_cell_rect(GridShape shape, int index, int area_x, int area_y, int area_w, int area_h, int gap);
// 全部单元格互不重叠（网格不跳位的前提）。
bool grid_rects_no_overlap(const std::vector<Rect>& rects);

}  // namespace core
}  // namespace railhmi
