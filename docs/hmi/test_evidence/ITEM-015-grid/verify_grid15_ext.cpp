// ITEM-015 验证自有扩展夹具（software_verification, verifier-owned）。
// 断言范围在实现者 test_grid.cpp 之外独立设计：
//   g0    3x2 网格单位格像素几何的手算精确值身份（area/gap 全字段 == grid_cell_rect）
//   gb    退化输入（0 尺寸、空槽位、越界 count、count>6）与函数健壮性
//   s0/s1 判据鉴别力：伪造重叠矩形必须判 false、伪造乱序槽位必须判 false
//         （防"永远返回 true"的空判据）；邻接共享边不算重叠
//   v0    shape 边界：宽==高 → 3x2（>= 语义）；slot(5)/slot(7) 行列映射显式值
//   m0..  文本格式化边界：format_fps / format_last_frame 全区间断言
//   c0..  全六态中文标签（channel_state_label）逐 token 断言
//   r0..  唯序稳健性：部分运行时（中间缺流）不改变其余格 id/slot/不崩溃；
//         runtime 长于注册表被忽略；同输入两次构建逐字段相等（确定性）
//   st0.. 陈旧阈值边界（>2000 严格大于）与未来时间戳（age<0 → "—" 不崩溃）
//   uc0.. 配置驱动（真实 cameras.yaml 基线）：build_registry → build_channel_views
//         六路名/徽标/顺序；真实注册表上断流第 4 路（CAM-04 车顶受电弓）占位+重连+最近帧；
//         删除 CAM-02 后注册表/视图顺序跟随配置（5 路，不重排不丢改名）
// 纯模型层（railhmi-core），不依赖 Qt/GStreamer。
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "railhmi/core/camera_registry.h"
#include "railhmi/core/channel_view.h"
#include "railhmi/core/config.h"
#include "railhmi/core/grid.h"
#include "railhmi/core/stream_state.h"

using namespace railhmi::core;

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

std::string read_file(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return std::string();
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool erase_block(std::string* s, const std::string& start_marker, const std::string& end_marker) {
    std::istringstream in(*s);
    std::ostringstream out;
    std::string line;
    bool dropping = false, removed = false, first = true;
    while (std::getline(in, line)) {
        if (!dropping && line.find(start_marker) != std::string::npos) {
            dropping = true; removed = true; continue;
        }
        if (dropping && line.find(end_marker) != std::string::npos) dropping = false;
        if (dropping) continue;
        if (!first) out << "\n";
        out << line;
        first = false;
    }
    if (!first) out << "\n";
    *s = out.str();
    return removed;
}

bool views_fields_equal(const ChannelView& a, const ChannelView& b) {
    return a.id == b.id && a.name == b.name && a.state == b.state &&
           a.state_label == b.state_label && a.fps_text == b.fps_text &&
           a.last_frame_text == b.last_frame_text &&
           a.reconnect_text == b.reconnect_text && a.presentable == b.presentable &&
           a.placeholder == b.placeholder && a.stale == b.stale && a.slot == b.slot;
}

// ---- g0/gb：网格几何 ----
void test_grid_geometry() {
    // 手算：area 632x352, gap=2, 3x2 → cw=(632-2*2)/3=209, ch=(352-2)/2=175
    // x = 0/211/422（步进 cw+gap=211），y = 0/177
    const Rect e[6] = {
        Rect{0, 0, 209, 175},   Rect{211, 0, 209, 175},  Rect{422, 0, 209, 175},
        Rect{0, 177, 209, 175}, Rect{211, 177, 209, 175}, Rect{422, 177, 209, 175},
    };
    std::vector<Rect> got;
    for (int i = 0; i < 6; ++i)
        got.push_back(grid_cell_rect(GridShape::ThreeByTwo, i, 0, 0, 632, 352, 2));
    for (int i = 0; i < 6; ++i)
        check(rect_eq(got[i], e[i]), "g0 cell " + std::to_string(i) + " exact hand-computed rect",
              "got(" + std::to_string(got[i].x) + "," + std::to_string(got[i].y) + "," +
              std::to_string(got[i].w) + "," + std::to_string(got[i].h) + ")");

    check(grid_rects_no_overlap(got), "g0b six real cells pairwise no overlap");
    bool within = true;
    for (const Rect& r : got)
        if (r.x < 0 || r.y < 0 || r.x + r.w > 632 || r.y + r.h > 352) within = false;
    check(within, "g0c all cells within area");
    // 手算核对末格余量：422+209=631 ≤ 632（整除余 1px 区域），177+175=352 恰好填满
    check(got[2].x + got[2].w == 631 && got[5].y + got[5].h == 352,
          "g0d remainder/right-bottom edge identity");

    // gap=0：无间隙铺满（cw 只受整除余数影响）
    const Rect a0 = grid_cell_rect(GridShape::ThreeByTwo, 1, 10, 20, 628, 350, 0);
    check(rect_eq(a0, Rect{219, 20, 209, 175}), "g0e gap=0 contiguous cell (10+209, 20, 209,175)");

    // 2x3 形状（2 列 3 行）同一公式：area 320x480 gap 8 → cw=156, ch=(480-16)/3=154
    // idx3: row=3/2=1, col=3%2=1 → x=1*(156+8)=164, y=1*(154+8)=162
    const Rect t0 = grid_cell_rect(GridShape::TwoByThree, 3, 0, 0, 320, 480, 8);
    check(rect_eq(t0, Rect{164, 162, 156, 154}), "g0f 2x3 cell idx3 exact (row1 col1)");

    // 退化：0 尺寸 → w/h 归零不产生负值
    const Rect z = grid_cell_rect(GridShape::ThreeByTwo, 0, 0, 0, 0, 0, 4);
    check(z.w == 0 && z.h == 0, "gb zero-size area clamps to 0 (negative cw/ch dropped)");
}

void test_grid_discriminators_and_bounds() {
    // 判据鉴别力：重叠矩形必须 false
    std::vector<Rect> bad = {Rect{0, 0, 10, 10}, Rect{5, 5, 10, 10}};
    check(!grid_rects_no_overlap(bad), "s0 overlapping rects rejected (not always-true)");
    // 邻接共享边不算重叠
    std::vector<Rect> adj = {Rect{0, 0, 10, 10}, Rect{10, 0, 10, 10}};
    check(grid_rects_no_overlap(adj), "s0b edge-adjacent rects accepted");
    // 同位置完全相等矩形必须 false
    std::vector<Rect> dup = {Rect{3, 3, 7, 7}, Rect{3, 3, 7, 7}};
    check(!grid_rects_no_overlap(dup), "s0c identical rects rejected");

    // 槽位乱序必须判 false
    std::vector<GridSlot> a = grid_slots(6, GridShape::ThreeByTwo);
    std::vector<GridSlot> b = a;
    std::swap(b[0], b[1]);
    check(grid_slots_equal(a, b) == false, "s1 swapped slots detected unequal");
    check(grid_slots_equal(a, a), "s1b identical slots equal");

    // 边界：宽==高 → 3x2（>= 语义）
    check(choose_grid_shape(1000, 1000) == GridShape::ThreeByTwo, "v0 square window -> 3x2");
    check(choose_grid_shape(999, 1000) == GridShape::TwoByThree, "v0b just-narrow -> 2x3");

    // 槽位显式映射（3 列/2 列）与 count 扩展
    std::vector<GridSlot> s6 = grid_slots(6, GridShape::ThreeByTwo);
    check(s6[3].row == 1 && s6[3].col == 0 && s6[5].row == 1 && s6[5].col == 2,
          "v0c 3x2 explicit mapping idx3=(1,0) idx5=(1,2)");
    std::vector<GridSlot> t6 = grid_slots(6, GridShape::TwoByThree);
    check(t6[3].row == 1 && t6[3].col == 1 && t6[5].row == 2 && t6[5].col == 1,
          "v0d 2x3 explicit mapping idx3=(1,1) idx5=(2,1)");
    std::vector<GridSlot> s5 = grid_slots(5, GridShape::ThreeByTwo);
    check(s5.size() == 5 && s5[4].row == 1 && s5[4].col == 1,
          "v0e count=5 valid, idx4=(1,1)");
    std::vector<GridSlot> s7 = grid_slots(7, GridShape::ThreeByTwo);
    check(s7.size() == 7 && s7[6].row == 2 && s7[6].col == 0,
          "v0f count=7 valid, idx6=(2,0)");
    check(grid_slots(0, GridShape::ThreeByTwo).empty(), "gb0 count=0 -> empty, no crash");
}

struct Baseline {
    std::string cameras, app, thresholds, roi;
    bool loaded = false;
};

Baseline baseline() {
    Baseline b;
    b.cameras = read_file("config/cameras.yaml");
    b.app = read_file("config/app.yaml");
    b.thresholds = read_file("config/thresholds.yaml");
    b.roi = read_file("config/roi.yaml");
    b.loaded = !b.cameras.empty() && !b.app.empty() && !b.thresholds.empty() && !b.roi.empty();
    return b;
}

std::vector<CameraSlot> real_registry(const Baseline& b) {
    ConfigLoadResult r = load_config_from_strings(b.cameras, b.app, b.thresholds, b.roi);
    return r.ok ? build_registry(r.settings) : std::vector<CameraSlot>();
}

std::vector<ChannelRuntime> all_live(long long now) {
    std::vector<ChannelRuntime> v(6);
    for (int i = 0; i < 6; ++i) {
        v[i].has_stream = true;
        v[i].state = StreamState::Live;
        v[i].fps = 25.0;
        v[i].last_frame_ms = now - 300;
        v[i].presentable = true;
    }
    return v;
}

// ---- r0..：顺序稳健性（部分/超长 runtime、确定性） ----
void test_views_robustness() {
    const std::vector<CameraSlot> reg = real_registry(baseline());
    if (reg.size() != 6) {
        check(false, "r0 prerequisite: baseline registry 6 slots");
        return;
    }
    const long long now = 500000;

    // 中间缺流：只有第 0、5 路启动，中间四格仍占位不丢格，id/slot 不动
    std::vector<ChannelRuntime> partial = all_live(now);
    for (int i = 1; i <= 4; ++i) partial[i].has_stream = false;
    std::vector<ChannelView> vp = build_channel_views(reg, partial, now);
    check(vp.size() == 6, "r0 partial runtime: still 6 views (no cell dropped)");
    bool shape_ok = true;
    for (int i = 0; i < 6; ++i)
        if (vp[i].id != reg[i].id || vp[i].slot != i) shape_ok = false;
    check(shape_ok, "r0b partial runtime: id/slot unchanged for all cells");
    check(vp[1].state == "unstarted" && vp[4].state == "unstarted" &&
              vp[1].placeholder && vp[4].placeholder,
          "r0c middle unstarted cells placeholder");
    check(vp[0].state == "live" && vp[5].state == "live" && vp[0].presentable,
          "r0d started ends stay live");

    // runtime 长于注册表：多出的项被忽略，不崩溃
    std::vector<ChannelRuntime> long_rt = all_live(now);
    ChannelRuntime extra;
    extra.has_stream = true;
    extra.state = StreamState::Offline;
    long_rt.push_back(extra);
    long_rt.push_back(extra);
    std::vector<ChannelView> vl = build_channel_views(reg, long_rt, now);
    check(vl.size() == 6, "r1 runtime longer than registry: extra ignored");

    // 确定性：同输入两次构建逐字段相等
    std::vector<ChannelView> v1 = build_channel_views(reg, all_live(now), now);
    std::vector<ChannelView> v2 = build_channel_views(reg, all_live(now), now);
    bool same = (v1.size() == v2.size());
    for (std::size_t i = 0; i < v1.size() && same; ++i)
        same = views_fields_equal(v1[i], v2[i]);
    check(same, "r2 determinism: field-by-field identical across calls");
}

// ---- st0/m0/c0：展示口径边界 ----
void test_view_field_boundaries() {
    const std::vector<CameraSlot> reg = real_registry(baseline());
    if (reg.size() != 6) {
        check(false, "st0 prerequisite: baseline registry 6 slots");
        return;
    }
    const long long now = 100000;

    // 陈旧阈值：默认 2000ms，严格大于（==2000 不陈旧，2501 陈旧）
    std::vector<ChannelRuntime> rt = all_live(now);
    rt[2].last_frame_ms = now - 2000;
    std::vector<ChannelView> v_eq = build_channel_views(reg, rt, now);
    check(!v_eq[2].stale, "st0 stale boundary: age==threshold not stale");
    rt[2].last_frame_ms = now - 2501;
    std::vector<ChannelView> v_gt = build_channel_views(reg, rt, now);
    check(v_gt[2].stale && v_gt[2].last_frame_text == "2 秒前",
          "st0b stale over threshold flagged, text '2 秒前'");

    // 未来时间戳（时钟回拨）：age<0 → 最近帧显示 "—" 且不陈旧，不崩溃
    rt[2].last_frame_ms = now + 9999;
    std::vector<ChannelView> v_past = build_channel_views(reg, rt, now);
    check(v_past[2].last_frame_text == "—" && !v_past[2].stale,
          "st1 negative age -> '—' no crash");

    // 尚无帧：last_frame_ms=-1 → "—" 且不下陈旧结论
    rt[2].last_frame_ms = -1;
    std::vector<ChannelView> v_nofr = build_channel_views(reg, rt, now);
    check(v_nofr[2].last_frame_text == "—" && !v_nofr[2].stale,
          "st2 no frame yet -> '—'");

    // fps 文本边界
    check(format_fps(25.04) == "25.0 fps", "m0 fps 25.04 -> '25.0 fps'");
    check(format_fps(25.06) == "25.1 fps", "m0b fps 25.06 -> '25.1 fps'");
    check(format_fps(0.06) == "0.1 fps", "m0c fps just over epsilon -> text");
    check(format_fps(0.04) == "—", "m0d fps under epsilon -> dash");
    check(format_fps(-2.0) == "—", "m0e negative fps -> dash");

    // 最近帧文本边界
    check(format_last_frame(0) == "<1 秒前", "m1 age 0 -> '<1 秒前'");
    check(format_last_frame(999) == "<1 秒前", "m1b age 999 -> '<1 秒前'");
    check(format_last_frame(1000) == "1 秒前", "m1c age 1000 -> '1 秒前'");
    check(format_last_frame(59999) == "59 秒前", "m1d age 59999 -> '59 秒前'");
    check(format_last_frame(60000) == "1 分 0 秒前", "m1e age 60000 -> '1 分 0 秒前'");
    check(format_last_frame(3661000) == "61 分 1 秒前", "m1f over an hour keeps 分 format");
}

void test_state_labels() {
    check(channel_state_label("live") == "实时", "c0 live label");
    check(channel_state_label("connecting") == "连接中", "c0b connecting label");
    check(channel_state_label("reconnecting") == "重连中", "c0c reconnecting label");
    check(channel_state_label("offline") == "离线", "c0d offline label");
    check(channel_state_label("degraded") == "降级", "c0e degraded label");
    check(channel_state_label("frozen") == "画面冻结", "c0f frozen label");
    check(channel_state_label("unstarted") == "未启动", "c0g unstarted label");
    check(channel_state_label("").empty() || channel_state_label("") == "未启动",
          "c0h empty token -> unstarted/empty without crash");
    // 中文三重编码文字部分齐全：六个状态 + 未启动共七种文字，互不重复
    std::vector<std::string> toks = {"live", "connecting", "reconnecting", "offline",
                                     "degraded", "frozen", "unstarted"};
    bool distinct = true;
    for (std::size_t i = 0; i < toks.size(); ++i)
        for (std::size_t j = i + 1; j < toks.size(); ++j)
            if (channel_state_label(toks[i]) == channel_state_label(toks[j])) distinct = false;
    check(distinct, "c0i state labels pairwise distinct (三重编码文字部分无歧义)");
}

// ---- 全六态逐格视图语义（在真实注册表上断流/冻结/降级） ----
void test_views_all_states() {
    const std::vector<CameraSlot> reg = real_registry(baseline());
    if (reg.size() != 6) {
        check(false, "c1 prerequisite: baseline registry 6 slots");
        return;
    }
    const long long now = 300000;

    // 断流格：CAM-04（车顶受电弓）重连第 3 次，最近帧停 7 秒前；其余五路 live
    std::vector<ChannelRuntime> rt = all_live(now);
    rt[3].state = StreamState::Reconnecting;
    rt[3].presentable = false;
    rt[3].fps = 0.0;
    rt[3].reconnect_count = 3;
    rt[3].last_frame_ms = now - 7000;
    std::vector<ChannelView> v = build_channel_views(reg, rt, now);
    check(v[3].id == "CAM-04" && v[3].name == "车顶受电弓", "c1a cut cell keeps config identity");
    check(v[3].state == "reconnecting" && v[3].state_label == "重连中", "c1b cut token/label");
    check(v[3].placeholder && !v[3].presentable, "c1c cut cell placeholder");
    check(v[3].reconnect_text == "重连 3 次", "c1d reconnect count 3 text");
    check(v[3].last_frame_text == "7 秒前" && v[3].stale, "c1e frozen last frame '7 秒前' + stale");
    check(v[3].fps_text == "—", "c1f cut fps dash");
    bool others = true;
    for (int i : {0, 1, 2, 4, 5})
        if (!(v[i].state == "live" && v[i].presentable && !v[i].placeholder &&
              v[i].reconnect_text.empty()))
            others = false;
    check(others, "c1g other five cells live/presentable/no reconnect text");

    // 冻结格：CAM-02 冻结但仍有 1 秒前最近帧，占位（presentable=false 来自流层）
    std::vector<ChannelRuntime> fz = all_live(now);
    fz[1].state = StreamState::Frozen;
    fz[1].presentable = false;
    fz[1].last_frame_ms = now - 1000;
    std::vector<ChannelView> vf = build_channel_views(reg, fz, now);
    check(vf[1].state == "frozen" && vf[1].state_label == "画面冻结", "c2 frozen token/label");
    check(vf[1].placeholder && vf[1].last_frame_text == "1 秒前" && !vf[1].stale,
          "c2b frozen keeps last-frame time, fresh, placeholder");

    // 降级可用格：DEGRADED 仍 presentable（渲染画面），标签降级
    std::vector<ChannelRuntime> dg = all_live(now);
    dg[5].state = StreamState::Degraded;
    dg[5].fps = 12.5;
    std::vector<ChannelView> vd = build_channel_views(reg, dg, now);
    check(vd[5].state == "degraded" && vd[5].state_label == "降级", "c3 degraded token/label");
    check(vd[5].presentable && !vd[5].placeholder, "c3b degraded still presentable");
    check(vd[5].fps_text == "12.5 fps", "c3c degraded fps text preserved");
}

// ---- uc0..：配置驱动（真实 cameras.yaml 基线 → 视图） ----
void test_views_config_driven() {
    Baseline b = baseline();
    if (!b.loaded) {
        check(false, "uc0 prerequisite: baseline config readable");
        return;
    }
    const std::vector<CameraSlot> reg = real_registry(b);
    if (reg.size() != 6) {
        check(false, "uc0 baseline registry 6 slots", std::to_string(reg.size()));
        return;
    }
    const long long now = 400000;
    std::vector<ChannelView> v = build_channel_views(reg, all_live(now), now);
    check(v.size() == 6, "uc0 six views from baseline config");
    check(v[0].id == "CAM-01" && v[0].name == "车头前方" && v[0].position == "primary",
          "uc0a CAM-01 车头前方/primary");
    check(v[1].name == "车辆左侧" && v[2].name == "车辆右侧" && v[3].name == "车顶受电弓" &&
              v[4].name == "车底轮轨" && v[5].name == "司机室车门",
          "uc0b view names follow cameras.yaml order");
    check(v[0].badge == "前方障碍/人员" && v[1].badge == "广角·无检测" &&
              v[3].badge == "受电弓三状态" && v[4].badge == "轮轨" && v[5].badge == "车门",
          "uc0c detection badges from ai_profile");
    check(v[0].ai_enabled && !v[1].ai_enabled && v[3].ai_enabled && v[4].ai_enabled &&
              v[5].ai_enabled,
          "uc0d ai_enabled flags carried");
    bool order_ok = true;
    for (int i = 0; i < 6; ++i)
        if (v[i].slot != i || v[i].id != reg[i].id) order_ok = false;
    check(order_ok, "uc0e view order == registry order == cameras.yaml order");

    // 配置变更：删除 CAM-02 → 5 路视图，顺序跟随配置，不重排不丢格
    std::string cams = b.cameras;
    if (!erase_block(&cams, "  - id: CAM-02", "  - id: CAM-03")) {
        check(false, "uc1 prerequisite: CAM-02 block removable");
        return;
    }
    ConfigLoadResult r5 = load_config_from_strings(cams, b.app, b.thresholds, b.roi);
    check(r5.ok, "uc1 5-camera config loads");
    std::vector<CameraSlot> reg5 = build_registry(r5.settings);
    check(reg5.size() == 5, "uc1b 5 registry slots", std::to_string(reg5.size()));
    std::vector<ChannelRuntime> rt5(5);
    for (int i = 0; i < 5; ++i) {
        rt5[i].has_stream = true;
        rt5[i].state = StreamState::Live;
        rt5[i].fps = 25.0;
        rt5[i].last_frame_ms = now - 300;
        rt5[i].presentable = true;
    }
    std::vector<ChannelView> v5 = build_channel_views(reg5, rt5, now);
    check(v5.size() == 5, "uc1c 5 views");
    bool id_ok = (v5.size() == 5);
    for (std::size_t i = 0; i < v5.size() && id_ok; ++i)
        id_ok = (v5[i].id == reg5[i].id && v5[i].slot == static_cast<int>(i) &&
                 v5[i].presentable);
    check(id_ok, "uc1d ids/slot/presentable follow config order after removal");
    bool no_cam02 = true;
    for (std::size_t i = 0; i < v5.size(); ++i)
        if (v5[i].id == "CAM-02") no_cam02 = false;
    check(no_cam02, "uc1e removed camera absent from grid views");
    check(grid_slots(5, GridShape::ThreeByTwo).size() == 5,
          "uc1f grid slots valid for 5 cells (removed-middle grid still geometrically valid)");

    // 配置变更：改名 CAM-06 → 视图名跟随（配置即视图）
    std::string cams2 = b.cameras;
    const std::string from = "name: 司机室车门";
    const std::string to = "name: 驾驶室门";
    const std::size_t p = cams2.find(from);
    check(p != std::string::npos, "uc2 prerequisite: CAM-06 name line found");
    if (p != std::string::npos) {
        cams2.replace(p, from.size(), to);
        ConfigLoadResult r2 = load_config_from_strings(cams2, b.app, b.thresholds, b.roi);
        std::vector<CameraSlot> reg2 = r2.ok ? build_registry(r2.settings)
                                            : std::vector<CameraSlot>();
        check(r2.ok && reg2.size() == 6, "uc2 renamed config loads 6 slots");
        if (!r2.ok || reg2.size() != 6) return;
        std::vector<ChannelView> v2 = build_channel_views(reg2, std::vector<ChannelRuntime>(), now);
        check(v2.size() == 6 && v2.size() == reg2.size() && v2[5].name == "驾驶室门" &&
                  v2[0].name == "车头前方",
              "uc2b renamed camera reflected in views, others unchanged");
    }
}

}  // namespace

int main() {
    test_grid_geometry();
    test_grid_discriminators_and_bounds();
    test_state_labels();
    test_view_field_boundaries();
    test_views_robustness();
    test_views_all_states();
    test_views_config_driven();

    const bool ok = g_pass == g_total;
    std::printf("== %d passed, %d failed ==\n", g_pass, g_total - g_pass);
    std::printf("RESULT: %s passed=%d failed=%d\n", ok ? "PASS" : "FAIL", g_pass,
                g_total - g_pass);
    return ok ? 0 : 1;
}
