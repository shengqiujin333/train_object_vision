// software_tester.software_verification verifier-owned extended fixture (ITEM-006).
// Covers rules.cpp edges beyond implementer test_rules.cpp:
//   to_string identity / map_level Device=L2
//   ROI 归一化矩形边界（x+w/y+h 含端点，外部不命中），find_roi camera/name 缺失
//   on_device_event 空 detail 回退类型名、自定义 detail 保留、设备事件纳入 unacked/ack
//   同相机跨 AlarmType 键隔离（确认与抑制互不干扰）
//   设备事件与检测确认/抑制互不干扰
//   非候选帧重置整套本相机确认计数（跨类型/跨 track 一并重置）
//   emit 后确认计数重置 -> 下一事件需完整 confirm_frames
//   抑制窗口边界（t_emit + suppress 恰好不再抑制；< 才抑制）、抑制更新不解锁窗口
//   抑制更新后的再发新事件元数据为新帧（ts/conf/dist）
//   ack 幂等、跨事件隔离
#include "railhmi/core/rules.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace railhmi::core;

namespace {

int g_pass = 0, g_fail = 0;
void ok(bool cond, const std::string& name, const std::string& detail = "") {
    if (cond) { ++g_pass; std::printf("[PASS] %s\n", name.c_str()); }
    else { ++g_fail; std::printf("[FAIL] %s%s%s\n", name.c_str(),
             detail.empty() ? "" : " -- ", detail.c_str()); }
}

Detection front(long long t, int track, double conf, const std::string& label) {
    Detection d;
    d.camera_id = "CAM-01"; d.profile = AiProfile::Front; d.t_ms = t;
    d.track_id = track; d.in_roi = true; d.confidence = conf;
    d.label = label; d.distance_m = 10.0;
    return d;
}

Detection wheel(long long t, int track) {
    Detection d;
    d.camera_id = "CAM-01"; d.profile = AiProfile::WheelRail; d.t_ms = t;
    d.track_id = track; d.confidence = 0.9; d.wheel_abnormal = true;
    return d;
}

struct Bin { std::vector<AlarmEvent> v; };
static void feed(RuleEngine& e, const Detection& d, Bin& b) {
    for (auto& x : e.on_detection(d)) b.v.push_back(x);
}

RuleConfig cfg_basic() {
    RuleConfig c;  // confirm 3 / window 100 / conf_min 0.5 / suppress 1000 (tests)
    c.confirm_frames = 3; c.confirm_window_ms = 100;
    c.confidence_min = 0.5; c.suppress_window_ms = 1000;
    return c;
}

// ---------------------------------------------------------------------------
void test_strings_and_level_fallback() {
    ok(std::string(to_string(AlarmType::Device)) == "device", "to_string Device='device'");
    ok(std::string(to_string(AlarmType::SideCameraDevice)) == "side_camera_device",
       "to_string SideCameraDevice");
    ok(std::string(to_string(AckState::Unacked)) == "unacked", "to_string unacked");
    ok(std::string(to_string(AckState::Acked)) == "acked", "to_string acked");
    ok(map_level(AlarmType::Device) == AlarmLevel::L2, "map_level generic Device=L2");
}

// ROI 归一化矩形含右/下边界：nx==x+w、ny==y+h 命中；x+w 之外不命中。
void test_roi_edges() {
    RoiConfig r;
    r.camera = "CAM-01"; r.name = "danger_zone";
    r.x = 0.1; r.y = 0.8; r.w = 0.8; r.h = 0.15;
    ok(roi_contains(r, 0.1, 0.8), "roi boundary (x,y) corner inside");
    ok(roi_contains(r, 0.9, 0.95), "roi boundary (x+w,y+h) corner inside");
    ok(!roi_contains(r, 0.95, 0.85), "roi rejects x beyond x+w");
    ok(!roi_contains(r, 0.5, 0.96), "roi rejects y beyond y+h");
    ok(!roi_contains(r, 0.05, 0.85), "roi rejects x < x");
    std::vector<RoiConfig> rois{r};
    ok(find_roi(rois, "CAM-01", "danger_zone") != nullptr, "find_roi hit");
    ok(find_roi(rois, "CAM-01", "other_zone") == nullptr, "find_roi name miss");
    ok(find_roi(rois, "CAM-02", "danger_zone") == nullptr, "find_roi camera miss");
}

// on_device_event：空 detail 回退为类型名；detail 保留；设备事件纳入 unacked 统计与 ack。
void test_device_event_details() {
    RuleEngine e(cfg_basic());
    auto x = e.on_device_event("CAM-01", DeviceCategory::FrontPrimary, 100, "");
    ok(x.size() == 1 && std::string(x[0].description) == "front_camera_device",
       "device empty detail falls back to type string",
       "desc=" + x[0].description);
    auto y = e.on_device_event("CAM-02", DeviceCategory::Side, 200, "");
    ok(y.size() == 1 && std::string(y[0].description) == "side_camera_device",
       "side empty detail falls back to type string", "desc=" + y[0].description);
    auto z = e.on_device_event("CAM-03", DeviceCategory::Other, 300, "nvr unreachable");
    ok(z.size() == 1 && std::string(z[0].description) == "nvr unreachable",
       "device custom detail preserved");
    ok(z[0].ts_ms == 300 && z[0].last_update_ms == 300, "device event ts fields set");
    ok(e.unacked_count() == 3, "device events counted in unacked_count");

    auto f = e.find(x[0].id);
    ok(f != nullptr && f->type == AlarmType::FrontCameraDevice, "find device event by id");
    ok(e.ack(z[0].id) && e.find(z[0].id)->ack_state == AckState::Acked,
       "ack device event keeps it");
    ok(e.unacked_count() == 2, "unacked after ack device = 2");
}

// 同相机不同 AlarmType 各自独立确认与抑制。
void test_key_isolation_cross_type() {
    RuleEngine e(cfg_basic());
    Bin w, f;
    // wheel: 2 帧
    feed(e, wheel(10, 1), w);
    feed(e, wheel(20, 1), w);
    // front person: 2 帧（独立 key，未达 3）
    feed(e, front(30, 1, 0.9, "person"), f);
    feed(e, front(40, 1, 0.9, "person"), f);
    ok(w.v.empty() && f.v.empty(), "cross-type keys count independently (no event yet)");

    feed(e, wheel(50, 1), w);
    ok(w.v.size() == 1 && w.v[0].type == AlarmType::WheelDerail &&
       w.v[0].level == AlarmLevel::L1, "wheel event after 3 wheel frames");
    ok(f.v.empty(), "front confirm unaffected by wheel emit");

    // wheel 进入抑制：立刻再喂 wheel 不新增
    auto v = e.on_detection(wheel(60, 1));
    ok(v.empty(), "wheel suppress window holds right after emit");
    ok(e.events().size() == 1, "only one event so far");

    // front 第 3 帧 -> front 事件（不受 wheel 抑制干扰）
    feed(e, front(70, 1, 0.9, "person"), f);
    ok(f.v.size() == 1 && f.v[0].type == AlarmType::PersonOnRail &&
       f.v[0].level == AlarmLevel::L1, "front emits unaffected by wheel suppression");
}

// 设备事件与检测确认/抑制互不干扰（键不同：设备 track 固定 0）。
void test_device_vs_detection_independence() {
    RuleEngine e(cfg_basic());
    auto d0 = e.on_device_event("CAM-01", DeviceCategory::FrontPrimary, 10, "offline");
    ok(d0.size() == 1, "device event 1 emitted");
    Bin f;
    feed(e, front(20, 7, 0.9, "person"), f);
    feed(e, front(30, 7, 0.9, "person"), f);
    ok(f.v.empty(), "detection confirm unaffected by device event");
    feed(e, front(40, 7, 0.9, "person"), f);
    ok(f.v.size() == 1 && f.v[0].type == AlarmType::PersonOnRail,
       "front detection emits despite device suppression");

    // 设备抑制窗口独立于检测事件
    auto d1 = e.on_device_event("CAM-01", DeviceCategory::FrontPrimary, 50, "offline");
    ok(d1.empty(), "device repeat suppressed (within 1000ms of emit 10)");
    auto d2 = e.on_device_event("CAM-01", DeviceCategory::FrontPrimary, 1011, "offline");
    ok(d2.size() == 1, "device repeat after window emits");
    auto d3 = e.on_device_event("CAM-01", DeviceCategory::FrontPrimary, 1100, "offline");
    ok(d3.empty(), "device repeat suppressed again (within window)");

    // 检测抑制也不影响设备通道
    auto dup = e.on_detection(front(1010, 7, 0.9, "person"));  // 距事件 970 < 1000
    ok(dup.empty(), "detection suppress still holds near device emits");
    auto dv = e.on_device_event("CAM-01", DeviceCategory::FrontPrimary, 2200, "offline");
    ok(dv.size() == 1, "device channel unaffected by detection state");
    ok(e.events().size() == 4, "events total = device d0 + detection + device d2 + device dv",
       "count=" + std::to_string(e.events().size()));
}

// 非候选帧重置整套本相机确认计数（跨类型/跨 track 一并重置）。
void test_non_candidate_resets_whole_camera() {
    RuleEngine e(cfg_basic());
    Bin f;
    feed(e, front(10, 1, 0.9, "person"), f);
    feed(e, front(20, 1, 0.9, "person"), f);
    // 1 帧低置信度（非候选）-> 同相机所有确认计数清零
    auto v = e.on_detection(front(30, 1, 0.3, "person"));
    ok(v.empty(), "low-conf frame emits nothing");
    // 再 2 帧正常 -> 不达 3 帧，因为计数被重置过
    feed(e, front(40, 1, 0.9, "person"), f);
    feed(e, front(50, 1, 0.9, "person"), f);
    ok(f.v.empty(), "whole-camera confirm reset by non-candidate frame");

    // 对照：2 帧正常 + 1 帧 ROI 外非候选 + 再 1 帧 -> 仍不达
    Bin g;
    feed(e, front(100, 2, 0.9, "person"), g);
    feed(e, front(110, 2, 0.9, "person"), g);
    Detection out = front(120, 2, 0.9, "person");
    out.in_roi = false;
    auto vo = e.on_detection(out);
    ok(vo.empty() && g.v.empty(), "out-of-ROI frame also resets confirm");
    feed(e, front(130, 2, 0.9, "person"), g);
    ok(g.v.empty(), "1 more frame after reset does not confirm");
    feed(e, front(140, 2, 0.9, "person"), g);
    feed(e, front(150, 2, 0.9, "person"), g);
    ok(g.v.size() == 1, "3 consecutive candidate frames confirm");
}

// emit 后确认计数重置 -> 下一事件需完整 confirm_frames 个新命中帧。
void test_emit_resets_confirm_cycle() {
    RuleEngine e(cfg_basic());
    Bin f;
    feed(e, front(10, 1, 0.9, "person"), f);
    feed(e, front(20, 1, 0.9, "person"), f);
    feed(e, front(30, 1, 0.9, "person"), f);   // emit #1
    ok(f.v.size() == 1, "first emit after 3 frames");
    // 抑制窗口内 3 帧 -> 不新增
    feed(e, front(40, 1, 0.9, "person"), f);
    feed(e, front(50, 1, 0.9, "person"), f);
    feed(e, front(60, 1, 0.9, "person"), f);
    ok(f.v.size() == 1, "no new event within suppress window");

    // 超窗口前最后两帧：980/990 < 1000 -> 抑制
    feed(e, front(1010, 1, 0.9, "person"), f);
    feed(e, front(1020, 1, 0.9, "person"), f);
    ok(f.v.size() == 1, "inside suppress window still no new event");
    // t=1030：差 1000 不抑制，但确认计数已重置 -> 单帧不达
    auto v30 = e.on_detection(front(1030, 1, 0.9, "person"));
    ok(v30.empty() && f.v.size() == 1, "post-window single frame does not emit (confirm reset)");
    feed(e, front(1040, 1, 0.9, "person"), f);
    ok(f.v.size() == 1, "second post-window frame still confirms");
    feed(e, front(1050, 1, 0.9, "person"), f);
    ok(f.v.size() == 2, "new event needs full confirm_frames after window");
}

// 抑制窗口边界：t_emit + suppress_ms 恰好不再抑制（严格小于判断）。
void test_suppress_boundary() {
    RuleConfig c = cfg_basic();
    c.confirm_frames = 1;
    RuleEngine e(c);
    auto v1 = e.on_detection(front(100, 1, 0.9, "person"));
    ok(v1.size() == 1, "boundary test first event");

    // t = 1100：差 = 1000 == suppress 不抑制 -> 但需要 1 帧，emit 新事件
    auto vb = e.on_detection(front(1100, 1, 0.9, "person"));
    ok(vb.size() == 1, "t_emit + suppress_ms exactly -> no longer suppressed");
    // t = 1099：差 = 999 < 1000 -> 抑制（改回 track 3 独立事件会更清晰，仍用 track1）
    auto vs = e.on_detection(front(1099, 1, 0.9, "person"));
    ok(vs.empty(), "(t_emit + suppress_ms) - 1 -> still suppressed");
    ok(e.events().size() == 2, "boundary yields exactly 2 events");
}

// 抑制更新不解锁窗口：连续更新持续刷新 last_update_ms，但窗口起点仍是 emit 时间。
void test_updates_do_not_extend_or_unlock() {
    RuleConfig c = cfg_basic();
    c.confirm_frames = 1;
    RuleEngine e(c);
    Bin f;
    feed(e, front(10, 1, 0.9, "person"), f);
    ok(f.v.size() == 1, "emit at t=10");
    // 窗口内更新几帧不新增事件
    for (long long t : {200LL, 400LL, 600LL, 800LL}) {
        auto v = e.on_detection(front(t, 1, 0.9, "person"));
        if (!v.empty()) { f.v.push_back(v[0]); }
    }
    ok(f.v.size() == 1, "updates inside window never emit");
    const AlarmEvent* ev = e.find(f.v[0].id);
    ok(ev && ev->last_update_ms == 800, "last_update_ms reflects last update");
    // 1050 超窗口（10+1000=1010 之外）-> 新事件
    feed(e, front(1050, 1, 0.9, "person"), f);
    ok(f.v.size() == 2, "new emit after window");
    // 新事件元数据为新帧
    const AlarmEvent* e2 = e.find(f.v[1].id);
    ok(e2 && e2->ts_ms == 1050 && e2->confidence == 0.9 && e2->distance_m == 10.0,
       "new event metadata from fresh frame");
    ok(e2 && e2->id != f.v[0].id, "new event has new id");
}

// ack 幂等与跨事件隔离。
void test_ack_idempotent() {
    RuleConfig c = cfg_basic();
    c.confirm_frames = 1;
    RuleEngine e(c);
    Bin f;
    feed(e, front(10, 1, 0.9, "person"), f);
    feed(e, front(1200, 1, 0.9, "person"), f);
    ok(f.v.size() == 2, "two events for idempotence test");
    ok(e.ack(f.v[0].id), "ack first");
    ok(e.ack(f.v[0].id), "ack first twice (idempotent)");
    ok(e.find(f.v[0].id)->ack_state == AckState::Acked, "first stays acked");
    ok(e.find(f.v[1].id)->ack_state == AckState::Unacked, "second stays unacked");
    ok(e.unacked_count() == 1, "unacked count = 1");
    // 抑制窗口内对已 ack 事件的更新不改变 ack_state
    auto v = e.on_detection(front(1250, 1, 0.9, "person"));
    ok(v.empty(), "second event suppressed by window");
    ok(e.find(f.v[1].id)->ack_state == AckState::Unacked, "ack state unchanged by update");
}

// v2 修复（r2, commit e79e2ae）独立核验：device_category_from_position 全 6 位置映射
// 与 to_string(PantographState/DoorState/DeviceCategory) 逐值身份。
void test_v2_position_mapping_and_strings() {
    ok(device_category_from_position(CameraPosition::Primary) == DeviceCategory::FrontPrimary,
       "v2 position primary -> FrontPrimary");
    ok(device_category_from_position(CameraPosition::Left) == DeviceCategory::Side,
       "v2 position left -> Side");
    ok(device_category_from_position(CameraPosition::Right) == DeviceCategory::Side,
       "v2 position right -> Side");
    ok(device_category_from_position(CameraPosition::Roof) == DeviceCategory::Other,
       "v2 position roof -> Other");
    ok(device_category_from_position(CameraPosition::Underfloor) == DeviceCategory::Other,
       "v2 position underfloor -> Other");
    ok(device_category_from_position(CameraPosition::Cab) == DeviceCategory::Other,
       "v2 position cab -> Other");
    ok(map_device_level(DeviceCategory::Other) == AlarmLevel::L2, "v2 device other -> L2");

    ok(std::string(to_string(PantographState::Unknown)) == "unknown", "v2 to_string pantograph unknown");
    ok(std::string(to_string(PantographState::NotRaised)) == "not_raised", "v2 to_string pantograph not_raised");
    ok(std::string(to_string(PantographState::Normal)) == "normal", "v2 to_string pantograph normal");
    ok(std::string(to_string(PantographState::Abnormal)) == "abnormal", "v2 to_string pantograph abnormal");
    ok(std::string(to_string(DoorState::Unknown)) == "unknown", "v2 to_string door unknown");
    ok(std::string(to_string(DoorState::Closed)) == "closed", "v2 to_string door closed");
    ok(std::string(to_string(DoorState::Ajar)) == "ajar", "v2 to_string door ajar");
    ok(std::string(to_string(DeviceCategory::FrontPrimary)) == "front_primary", "v2 to_string devcat front_primary");
    ok(std::string(to_string(DeviceCategory::Side)) == "side", "v2 to_string devcat side");
    ok(std::string(to_string(DeviceCategory::Other)) == "other", "v2 to_string devcat other");
}

}  // namespace

int main() {
    test_strings_and_level_fallback();
    test_roi_edges();
    test_device_event_details();
    test_key_isolation_cross_type();
    test_device_vs_detection_independence();
    test_non_candidate_resets_whole_camera();
    test_emit_resets_confirm_cycle();
    test_suppress_boundary();
    test_updates_do_not_extend_or_unlock();
    test_ack_idempotent();
    test_v2_position_mapping_and_strings();
    std::printf("== %d passed, %d failed ==\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
