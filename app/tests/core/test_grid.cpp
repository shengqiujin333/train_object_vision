// railhmi-core 全摄像头网格与每格视图测试（ITEM-015 / 交互 §7.3、视觉 §7.5、TEST_DESIGN）。
// 覆盖：3x2/2x3 形状与稳定槽位、单元格不重叠、每格显示字段（名/状态标签/fps/最近帧/重连/徽标）、
// 断流或冻结只改该格内容而不改变顺序（网格不重排/不丢格）。

#include "test_util.h"

#include <string>
#include <vector>

#include "railhmi/core/channel_view.h"
#include "railhmi/core/grid.h"

namespace {

using namespace railhmi::core;

std::vector<CameraSlot> six_slots() {
    const CameraPosition pos[6] = {CameraPosition::Primary, CameraPosition::Left,
                                   CameraPosition::Right,   CameraPosition::Roof,
                                   CameraPosition::Underfloor, CameraPosition::Cab};
    const char* names[6] = {"前方", "左侧", "右侧", "车顶", "车底", "司机室"};
    std::vector<CameraSlot> v;
    for (int i = 0; i < 6; ++i) {
        CameraSlot s;
        s.id = "CAM-0" + std::to_string(i + 1);
        s.name = names[i];
        s.position = pos[i];
        s.order = i;
        s.ai_enabled = (i == 0);
        s.ai_profile = (i == 0) ? AiProfile::Front : AiProfile::None;
        s.detection_badge = detection_badge_for(s.ai_profile);
        v.push_back(s);
    }
    return v;
}

void test_grid_shape() {
    railhmi_test::check(choose_grid_shape(1920, 1080) == GridShape::ThreeByTwo, "landsacpe -> 3x2");
    railhmi_test::check(choose_grid_shape(1280, 720) == GridShape::ThreeByTwo, "1280x720 -> 3x2");
    railhmi_test::check(choose_grid_shape(1080, 1920) == GridShape::TwoByThree, "portrait -> 2x3");
    railhmi_test::check(grid_columns(GridShape::ThreeByTwo) == 3 && grid_rows(GridShape::ThreeByTwo) == 2,
                        "3x2 dims");
    railhmi_test::check(grid_columns(GridShape::TwoByThree) == 2 && grid_rows(GridShape::TwoByThree) == 3,
                        "2x3 dims");
}

void test_grid_slots_stable() {
    const std::vector<GridSlot> a = grid_slots(6, GridShape::ThreeByTwo);
    const std::vector<GridSlot> b = grid_slots(6, GridShape::ThreeByTwo);
    railhmi_test::check(a.size() == 6, "6 slots");
    railhmi_test::check(grid_slots_equal(a, b), "slots identical across calls (state-independent)");
    railhmi_test::check(a[0].row == 0 && a[0].col == 0 && a[3].row == 1 && a[3].col == 0 && a[5].row == 1 && a[5].col == 2,
                        "3x2 slot mapping (row = i/3, col = i%3)");
    const std::vector<GridSlot> tall = grid_slots(6, GridShape::TwoByThree);
    railhmi_test::check(tall[5].row == 2 && tall[5].col == 1, "2x3 slot mapping (row = i/2, col = i%2)");
}

void test_grid_rects() {
    const GridShape shape = GridShape::ThreeByTwo;
    std::vector<Rect> rects;
    for (int i = 0; i < 6; ++i) rects.push_back(grid_cell_rect(shape, i, 16, 16, 1888, 1000, 12));
    railhmi_test::check(grid_rects_no_overlap(rects), "3x2 cells do not overlap");
    bool inside = true;
    for (const Rect& r : rects) {
        if (r.x < 16 || r.y < 16 || r.x + r.w > 16 + 1888 || r.y + r.h > 16 + 1000 || r.w <= 0 || r.h <= 0)
            inside = false;
    }
    railhmi_test::check(inside, "3x2 cells within area");
}

std::vector<ChannelRuntime> all_live(long long now) {
    std::vector<ChannelRuntime> r(6);
    for (int i = 0; i < 6; ++i) {
        r[i].has_stream = true;
        r[i].state = StreamState::Live;
        r[i].fps = 25.0;
        r[i].last_frame_ms = now - 400;
        r[i].presentable = true;
    }
    return r;
}

void test_channel_views_unstarted() {
    const std::vector<CameraSlot> reg = six_slots();
    const std::vector<ChannelView> v = build_channel_views(reg, {}, 1000);
    railhmi_test::check(v.size() == 6, "unstarted: still 6 cells (no cell dropped)");
    bool order_ok = true;
    for (std::size_t i = 0; i < v.size(); ++i)
        if (v[i].id != reg[i].id || v[i].slot != static_cast<int>(i)) order_ok = false;
    railhmi_test::check(order_ok, "unstarted: order equals registry order");
    railhmi_test::check(v[0].state == "unstarted" && v[0].state_label == "未启动", "unstarted token/label");
    railhmi_test::check(v[0].fps_text == "—" && v[0].last_frame_text == "—", "unstarted: fps/last = —");
    railhmi_test::check(v[0].placeholder && !v[0].presentable, "unstarted: placeholder");
    railhmi_test::check(v[0].badge == detection_badge_for(AiProfile::Front) && v[0].ai_enabled,
                        "badge carried from registry");
}

void test_channel_views_live() {
    const std::vector<CameraSlot> reg = six_slots();
    const long long now = 100000;
    const std::vector<ChannelView> v = build_channel_views(reg, all_live(now), now);
    railhmi_test::check(v[0].state == "live" && v[0].state_label == "实时", "live label");
    railhmi_test::check(v[0].fps_text == "25.0 fps", "live fps text");
    railhmi_test::check(v[0].last_frame_text == "<1 秒前", "live last-frame text");
    railhmi_test::check(v[0].presentable && !v[0].placeholder, "live presentable, no placeholder");
    railhmi_test::check(v[0].reconnect_text.empty(), "live no reconnect text");
}

void test_cut_one_keeps_order() {
    const std::vector<CameraSlot> reg = six_slots();
    const long long now = 200000;
    std::vector<ChannelRuntime> rt = all_live(now);
    // 断开第 4 路（索引 3，车顶）
    rt[3].state = StreamState::Reconnecting;
    rt[3].presentable = false;
    rt[3].reconnect_count = 1;
    rt[3].fps = 0.0;
    rt[3].last_frame_ms = now - 5000;   // 冻结在断流前最后帧

    const std::vector<ChannelView> live = build_channel_views(reg, all_live(now), now);
    const std::vector<ChannelView> mixed = build_channel_views(reg, rt, now);

    railhmi_test::check(mixed.size() == live.size() && mixed.size() == 6, "cut: grid size unchanged");
    bool same_order = true;
    for (std::size_t i = 0; i < mixed.size(); ++i)
        if (mixed[i].id != live[i].id || mixed[i].slot != live[i].slot) same_order = false;
    railhmi_test::check(same_order, "cut: slots do not reorder (grid does not jump)");

    railhmi_test::check(mixed[3].state == "reconnecting" && mixed[3].state_label == "重连中",
                        "cut: channel 4 reconnecting");
    railhmi_test::check(mixed[3].placeholder && !mixed[3].presentable, "cut: channel 4 placeholder");
    railhmi_test::check(mixed[3].reconnect_text == "重连 1 次", "cut: reconnect count text");
    railhmi_test::check(mixed[3].last_frame_text == "5 秒前", "cut: frozen last-frame time shown");

    bool others_live = true;
    for (int i : {0, 1, 2, 4, 5})
        if (!(mixed[i].state == "live" && mixed[i].presentable && mixed[i].fps_text == "25.0 fps"))
            others_live = false;
    railhmi_test::check(others_live, "cut: the other five cells keep refreshing");
}

void test_helpers() {
    railhmi_test::check(channel_state_label("frozen") == "画面冻结", "frozen label");
    railhmi_test::check(channel_state_label("weird-token") == "weird-token", "unknown token passthrough");
    railhmi_test::check(format_fps(0.0) == "—", "fps 0 -> —");
    railhmi_test::check(format_last_frame(-1).size() > 0, "no-frame text set");
    railhmi_test::check(format_last_frame(65000) == "1 分 5 秒前", "minutes format");
}

}  // namespace

void run_grid_tests() {
    test_grid_shape();
    test_grid_slots_stable();
    test_grid_rects();
    test_channel_views_unstarted();
    test_channel_views_live();
    test_cut_one_keeps_order();
    test_helpers();
}
