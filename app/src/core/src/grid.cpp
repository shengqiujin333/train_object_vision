#include "railhmi/core/grid.h"

namespace railhmi {
namespace core {

namespace {
bool overlap(const Rect& a, const Rect& b) {
    return a.x < b.x + b.w && b.x < a.x + a.w && a.y < b.y + b.h && b.y < a.y + a.h;
}
}  // namespace

GridShape choose_grid_shape(int width, int height) {
    return (width >= height) ? GridShape::ThreeByTwo : GridShape::TwoByThree;
}

const char* to_string(GridShape s) {
    return s == GridShape::ThreeByTwo ? "3x2" : "2x3";
}

int grid_columns(GridShape s) { return s == GridShape::ThreeByTwo ? 3 : 2; }
int grid_rows(GridShape s) { return s == GridShape::ThreeByTwo ? 2 : 3; }

std::vector<GridSlot> grid_slots(int count, GridShape shape) {
    const int cols = grid_columns(shape);
    std::vector<GridSlot> out;
    out.reserve(count > 0 ? count : 0);
    for (int i = 0; i < count; ++i) {
        GridSlot s;
        s.index = i;
        s.row = i / cols;
        s.col = i % cols;
        out.push_back(s);
    }
    return out;
}

bool grid_slots_equal(const std::vector<GridSlot>& a, const std::vector<GridSlot>& b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].index != b[i].index || a[i].row != b[i].row || a[i].col != b[i].col) return false;
    }
    return true;
}

Rect grid_cell_rect(GridShape shape, int index, int area_x, int area_y, int area_w, int area_h, int gap) {
    const int cols = grid_columns(shape);
    const int rows = grid_rows(shape);
    const int cw = (area_w - (cols - 1) * gap) / cols;
    const int ch = (area_h - (rows - 1) * gap) / rows;
    const int r = index / cols;
    const int c = index % cols;
    return Rect{area_x + c * (cw + gap), area_y + r * (ch + gap), cw > 0 ? cw : 0, ch > 0 ? ch : 0};
}

bool grid_rects_no_overlap(const std::vector<Rect>& rects) {
    for (std::size_t i = 0; i < rects.size(); ++i)
        for (std::size_t j = i + 1; j < rects.size(); ++j)
            if (overlap(rects[i], rects[j])) return false;
    return true;
}

}  // namespace core
}  // namespace railhmi
