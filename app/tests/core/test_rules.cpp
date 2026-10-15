// railhmi-core 检测职责与报警规则/事件层测试（ITEM-006 / TC-CORE-04、TC-CORE-05）。
// 注入每类事件断言级别与 PRD §8.2 一致；同 track_id 窗口内不新增事件；确认不删除事件。

#include "test_util.h"

#include <string>
#include <vector>

#include "railhmi/core/rules.h"

namespace {

using namespace railhmi::core;

RuleConfig cfg() {
    RuleConfig c;
    c.confirm_frames = 3;
    c.confirm_window_ms = 100;
    c.confidence_min = 0.5;
    c.suppress_window_ms = 1000;
    return c;
}

Detection front_det(const std::string& cam, long long t, int track, bool in_roi,
                    double conf, const std::string& label) {
    Detection d;
    d.camera_id = cam;
    d.profile = AiProfile::Front;
    d.t_ms = t;
    d.track_id = track;
    d.in_roi = in_roi;
    d.confidence = conf;
    d.label = label;
    d.distance_m = 8.0;
    return d;
}

void test_level_mapping() {
    railhmi_test::check(map_level(AlarmType::PersonOnRail) == AlarmLevel::L1, "person_on_rail=L1");
    railhmi_test::check(map_level(AlarmType::FrontObstacle) == AlarmLevel::L1, "front_obstacle=L1");
    railhmi_test::check(map_level(AlarmType::WheelDerail) == AlarmLevel::L1, "wheel_derail=L1");
    railhmi_test::check(map_level(AlarmType::PantographAbnormal) == AlarmLevel::L2,
                        "pantograph_abnormal=L2");
    railhmi_test::check(map_level(AlarmType::DoorAjar) == AlarmLevel::L2, "door_ajar=L2");
    railhmi_test::check(map_level(AlarmType::FrontCameraDevice) == AlarmLevel::L2,
                        "front_camera_device=L2");
    railhmi_test::check(map_level(AlarmType::SideCameraDevice) == AlarmLevel::L3,
                        "side_camera_device=L3");
    railhmi_test::check(map_device_level(DeviceCategory::FrontPrimary) == AlarmLevel::L2,
                        "device front primary=L2");
    railhmi_test::check(map_device_level(DeviceCategory::Side) == AlarmLevel::L3,
                        "device side=L3");
}

void test_front_confirm_and_level() {
    RuleEngine e(cfg());
    std::vector<AlarmEvent> created;
    for (int i = 0; i < 3; ++i) {
        auto v = e.on_detection(front_det("CAM-01", 10 + i * 20, 7, true, 0.9, "person"));
        for (auto& x : v) created.push_back(x);
    }
    railhmi_test::check(created.size() == 1, "front person: event after confirm_frames",
                        std::to_string(created.size()));
    if (!created.empty()) {
        railhmi_test::check(created[0].level == AlarmLevel::L1, "front person level = L1");
        railhmi_test::check(created[0].type == AlarmType::PersonOnRail, "front person type");
        railhmi_test::check(created[0].ack_state == AckState::Unacked, "new event unacked");
    }
    // 障碍物
    RuleEngine e2(cfg());
    created.clear();
    for (int i = 0; i < 3; ++i) {
        auto v = e2.on_detection(front_det("CAM-01", 10 + i * 20, 1, true, 0.8, "obstacle"));
        for (auto& x : v) created.push_back(x);
    }
    railhmi_test::check(created.size() == 1 && created[0].type == AlarmType::FrontObstacle &&
                            created[0].level == AlarmLevel::L1,
                        "front obstacle -> L1 FrontObstacle");
}

void test_front_gating() {
    {   // ROI 外不报警
        RuleEngine e(cfg());
        int n = 0;
        for (int i = 0; i < 5; ++i)
            n += static_cast<int>(e.on_detection(front_det("CAM-01", 10 + i * 20, 1, false, 0.9, "person")).size());
        railhmi_test::check(n == 0, "front outside ROI -> no event");
    }
    {   // 置信度不足不报警
        RuleEngine e(cfg());
        int n = 0;
        for (int i = 0; i < 5; ++i)
            n += static_cast<int>(e.on_detection(front_det("CAM-01", 10 + i * 20, 1, true, 0.3, "person")).size());
        railhmi_test::check(n == 0, "front low confidence -> no event");
    }
    {   // 帧间隔超确认窗口 -> 计数重置，不报警
        RuleEngine e(cfg());
        int n = 0;
        for (int i = 0; i < 3; ++i)
            n += static_cast<int>(e.on_detection(front_det("CAM-01", 10 + i * 500, 1, true, 0.9, "person")).size());
        railhmi_test::check(n == 0, "front frames beyond confirm window -> no event");
    }
}

void test_suppression_and_track() {
    RuleConfig c = cfg();
    c.confirm_frames = 2;
    RuleEngine e(c);
    // 建立事件（track 1）
    std::vector<AlarmEvent> created;
    for (int i = 0; i < 2; ++i) {
        auto v = e.on_detection(front_det("CAM-01", 10 + i * 10, 1, true, 0.9, "person"));
        for (auto& x : v) created.push_back(x);
    }
    railhmi_test::check(created.size() == 1, "suppression: first event created");
    const long long id1 = created.empty() ? -1 : created[0].id;

    // 同一 track 在抑制窗口内 -> 不新增，更新同一事件
    Detection upd = front_det("CAM-01", 40, 1, true, 0.95, "person");
    upd.distance_m = 5.5;
    auto more = e.on_detection(upd);
    railhmi_test::check(more.empty(), "same track within suppress window -> no new event");
    railhmi_test::check(e.events().size() == 1, "events size still 1 under suppression");
    const AlarmEvent* ev = e.find(id1);
    railhmi_test::check(ev && ev->distance_m == 5.5, "suppressed event distance updated");

    // 不同 track -> 独立事件
    std::vector<AlarmEvent> t2;
    for (int i = 0; i < 2; ++i) {
        auto v = e.on_detection(front_det("CAM-01", 50 + i * 10, 2, true, 0.9, "person"));
        for (auto& x : v) t2.push_back(x);
    }
    railhmi_test::check(t2.size() == 1, "different track -> new event");
    railhmi_test::check(e.events().size() == 2, "events size 2 after second track");

    // 超出抑制窗口后同一 track 可再产生新事件
    std::vector<AlarmEvent> t3;
    for (int i = 0; i < 2; ++i) {
        auto v = e.on_detection(front_det("CAM-01", 3000 + i * 10, 1, true, 0.9, "person"));
        for (auto& x : v) t3.push_back(x);
    }
    railhmi_test::check(t3.size() == 1, "same track after suppress window -> new event");
    railhmi_test::check(e.events().size() == 3, "events size 3 after suppress window");
}

void test_profiles() {
    {   // pantograph 三态：仅异常报警，持续确认 -> L2
        RuleEngine e(cfg());
        Detection d;
        d.camera_id = "CAM-04";
        d.profile = AiProfile::Pantograph;
        d.confidence = 0.9;
        // 正常态不报警
        d.pantograph = PantographState::Normal;
        int n = 0;
        for (int i = 0; i < 4; ++i) { d.t_ms = 10 + i * 20; n += (int)e.on_detection(d).size(); }
        railhmi_test::check(n == 0, "pantograph normal -> no event");
        // 异常态持续确认 -> L2
        d.pantograph = PantographState::Abnormal;
        std::vector<AlarmEvent> created;
        for (int i = 0; i < 3; ++i) { d.t_ms = 200 + i * 20; for (auto& x : e.on_detection(d)) created.push_back(x); }
        railhmi_test::check(created.size() == 1 &&
                                created[0].level == AlarmLevel::L2 &&
                                created[0].type == AlarmType::PantographAbnormal,
                            "pantograph abnormal -> L2");
    }
    {   // wheel_rail -> L1
        RuleEngine e(cfg());
        Detection d;
        d.camera_id = "CAM-05";
        d.profile = AiProfile::WheelRail;
        d.confidence = 0.9;
        d.wheel_abnormal = true;
        std::vector<AlarmEvent> created;
        for (int i = 0; i < 3; ++i) { d.t_ms = 10 + i * 20; for (auto& x : e.on_detection(d)) created.push_back(x); }
        railhmi_test::check(created.size() == 1 && created[0].level == AlarmLevel::L1 &&
                                created[0].type == AlarmType::WheelDerail,
                            "wheel_rail abnormal -> L1");
    }
    {   // cab_door -> L2
        RuleEngine e(cfg());
        Detection d;
        d.camera_id = "CAM-06";
        d.profile = AiProfile::CabDoor;
        d.confidence = 0.9;
        d.door = DoorState::Ajar;
        std::vector<AlarmEvent> created;
        for (int i = 0; i < 3; ++i) { d.t_ms = 10 + i * 20; for (auto& x : e.on_detection(d)) created.push_back(x); }
        railhmi_test::check(created.size() == 1 && created[0].level == AlarmLevel::L2 &&
                                created[0].type == AlarmType::DoorAjar,
                            "cab_door ajar -> L2");
    }
    {   // none 不产生事件
        RuleEngine e(cfg());
        Detection d;
        d.camera_id = "CAM-02";
        d.profile = AiProfile::None;
        d.in_roi = true;
        d.confidence = 0.99;
        int n = 0;
        for (int i = 0; i < 5; ++i) { d.t_ms = 10 + i * 20; n += (int)e.on_detection(d).size(); }
        railhmi_test::check(n == 0, "ai_profile=none -> no event");
    }
}

void test_device_events() {
    RuleEngine e(cfg());
    auto f = e.on_device_event("CAM-01", DeviceCategory::FrontPrimary, 100, "front offline");
    railhmi_test::check(f.size() == 1 && f[0].level == AlarmLevel::L2 &&
                            f[0].type == AlarmType::FrontCameraDevice,
                        "front primary device event -> L2");
    auto s = e.on_device_event("CAM-02", DeviceCategory::Side, 200, "side offline");
    railhmi_test::check(s.size() == 1 && s[0].level == AlarmLevel::L3 &&
                            s[0].type == AlarmType::SideCameraDevice,
                        "side device event -> L3");
    // 抑制窗口内重复设备事件不新增
    auto again = e.on_device_event("CAM-01", DeviceCategory::FrontPrimary, 300, "front offline");
    railhmi_test::check(again.empty(), "duplicate device event suppressed");
}

void test_ack_keeps_event() {
    RuleConfig c = cfg();
    c.confirm_frames = 1;
    RuleEngine e(c);
    auto v = e.on_detection(front_det("CAM-01", 10, 1, true, 0.9, "person"));
    railhmi_test::check(v.size() == 1, "single-frame confirm event created");
    const long long id = v.empty() ? -1 : v[0].id;
    railhmi_test::check(e.unacked_count() == 1, "unacked count 1");
    railhmi_test::check(e.ack(id), "ack returns true for existing event");
    const AlarmEvent* ev = e.find(id);
    railhmi_test::check(ev != nullptr, "ack does NOT delete event");
    railhmi_test::check(ev && ev->ack_state == AckState::Acked, "ack_state becomes acked");
    railhmi_test::check(e.unacked_count() == 0, "unacked count 0 after ack");
    railhmi_test::check(!e.ack(9999), "ack unknown id returns false");
}

void test_roi_helpers() {
    RoiConfig r;
    r.camera = "CAM-01";
    r.name = "danger_zone";
    r.x = 0.1; r.y = 0.8; r.w = 0.8; r.h = 0.15;
    railhmi_test::check(roi_contains(r, 0.5, 0.85), "roi contains center point");
    railhmi_test::check(!roi_contains(r, 0.5, 0.5), "roi rejects outside point");
    railhmi_test::check(!roi_contains(r, 0.95, 0.85), "roi rejects beyond x+w");
    std::vector<RoiConfig> rois{r};
    railhmi_test::check(find_roi(rois, "CAM-01", "danger_zone") != nullptr, "find_roi finds entry");
    railhmi_test::check(find_roi(rois, "CAM-09", "danger_zone") == nullptr, "find_roi miss");
}

void test_to_string_identity() {
    railhmi_test::check(std::string(to_string(AlarmType::Device)) == "device", "to_string(Device)");
    railhmi_test::check(std::string(to_string(AlarmType::SideCameraDevice)) == "side_camera_device",
                        "to_string(SideCameraDevice)");
    railhmi_test::check(std::string(to_string(AckState::Acked)) == "acked", "to_string(Acked)");
    railhmi_test::check(std::string(to_string(AckState::Unacked)) == "unacked", "to_string(Unacked)");
    railhmi_test::check(std::string(to_string(PantographState::Abnormal)) == "abnormal",
                        "to_string(PantographState::Abnormal)");
    railhmi_test::check(std::string(to_string(DoorState::Ajar)) == "ajar", "to_string(DoorState::Ajar)");
    railhmi_test::check(std::string(to_string(DeviceCategory::Side)) == "side",
                        "to_string(DeviceCategory::Side)");
    railhmi_test::check(map_level(AlarmType::Device) == AlarmLevel::L2, "map_level(Device)=L2");
}

void test_device_category_from_position() {
    railhmi_test::check(device_category_from_position(CameraPosition::Primary) ==
                            DeviceCategory::FrontPrimary,
                        "position primary -> FrontPrimary");
    railhmi_test::check(device_category_from_position(CameraPosition::Left) == DeviceCategory::Side &&
                            device_category_from_position(CameraPosition::Right) == DeviceCategory::Side,
                        "position left/right -> Side");
    railhmi_test::check(device_category_from_position(CameraPosition::Roof) == DeviceCategory::Other &&
                            device_category_from_position(CameraPosition::Underfloor) == DeviceCategory::Other &&
                            device_category_from_position(CameraPosition::Cab) == DeviceCategory::Other,
                        "position roof/underfloor/cab -> Other");
    railhmi_test::check(map_device_level(DeviceCategory::Other) == AlarmLevel::L2,
                        "device other -> L2 (explicit)");
}

void test_roi_corner_inclusive() {
    RoiConfig r;
    r.camera = "CAM-01";
    r.name = "zone";
    r.x = 0.2; r.y = 0.3; r.w = 0.5; r.h = 0.4;
    railhmi_test::check(roi_contains(r, 0.2, 0.3), "roi includes top-left corner");
    railhmi_test::check(roi_contains(r, 0.7, 0.7), "roi includes bottom-right corner");
    railhmi_test::check(!roi_contains(r, 0.19, 0.3), "roi rejects x-epsilon");
    railhmi_test::check(!roi_contains(r, 0.5, 0.71), "roi rejects y+h+epsilon");
}

void test_cross_type_key_isolation() {
    RuleEngine e(cfg());
    Detection w;
    w.camera_id = "CAM-05";
    w.profile = AiProfile::WheelRail;
    w.confidence = 0.9;
    w.wheel_abnormal = true;
    std::vector<AlarmEvent> wev;
    for (int i = 0; i < 3; ++i) { w.t_ms = 10 + i * 20; for (auto& x : e.on_detection(w)) wev.push_back(x); }
    railhmi_test::check(wev.size() == 1 && wev[0].type == AlarmType::WheelDerail &&
                            wev[0].level == AlarmLevel::L1,
                        "wheel event independent");
    Detection f;
    f.camera_id = "CAM-05";
    f.profile = AiProfile::Front;
    f.in_roi = true;
    f.confidence = 0.9;
    f.label = "person";
    f.track_id = 0;
    std::vector<AlarmEvent> fev;
    for (int i = 0; i < 3; ++i) { f.t_ms = 200 + i * 20; for (auto& x : e.on_detection(f)) fev.push_back(x); }
    railhmi_test::check(fev.size() == 1 && fev[0].type == AlarmType::PersonOnRail,
                        "front key isolated from wheel (same cam/track, different type)");
    railhmi_test::check(e.events().size() == 2, "cross-type events independent");
}

void test_device_vs_detection_independence() {
    RuleEngine e(cfg());
    auto d1 = e.on_device_event("CAM-01", DeviceCategory::FrontPrimary, 10, "offline");
    railhmi_test::check(d1.size() == 1, "device channel event 1");
    int dn = 0;
    for (int i = 0; i < 3; ++i) dn += (int)e.on_detection(front_det("CAM-01", 20 + i * 20, 1, true, 0.9, "person")).size();
    railhmi_test::check(dn == 1, "detection channel independent of device suppression");
    auto d2 = e.on_device_event("CAM-01", DeviceCategory::FrontPrimary, 1011, "offline");
    railhmi_test::check(d2.size() == 1, "device event after window");
    auto d3 = e.on_device_event("CAM-01", DeviceCategory::FrontPrimary, 2200, "offline");
    railhmi_test::check(d3.size() == 1, "device event after second window");
    railhmi_test::check(e.events().size() == 4, "device(3)+detection(1)=4");
}

void test_non_candidate_full_reset() {
    RuleEngine e(cfg());
    Detection w;
    w.camera_id = "CAM-05";
    w.profile = AiProfile::WheelRail;
    w.confidence = 0.9;
    w.wheel_abnormal = true;
    w.t_ms = 10;
    e.on_detection(w);
    int fn = (int)e.on_detection(front_det("CAM-05", 20, 0, true, 0.9, "person")).size();
    railhmi_test::check(fn == 0, "no event with two single-frame counts");
    Detection nc = w;
    nc.wheel_abnormal = false;  // 非候选
    nc.t_ms = 30;
    railhmi_test::check(e.on_detection(nc).empty() && e.events().empty(),
                        "non-candidate frame resets all camera confirms");
    // 重置后需连续 3 帧才确认
    int n40 = (int)e.on_detection(front_det("CAM-05", 40, 0, true, 0.9, "person")).size();
    int n50 = (int)e.on_detection(front_det("CAM-05", 50, 0, true, 0.9, "person")).size();
    railhmi_test::check(n40 == 0 && n50 == 0 && e.events().empty(),
                        "reset cleared cross-type confirms (need full cycle)");
    int n60 = (int)e.on_detection(front_det("CAM-05", 60, 0, true, 0.9, "person")).size();
    railhmi_test::check(n60 == 1 && e.events().size() == 1, "full cycle after reset emits");
}

void test_emit_resets_confirm_cycle() {
    RuleEngine e(cfg());
    int n = 0;
    for (int i = 0; i < 3; ++i) n += (int)e.on_detection(front_det("CAM-01", 10 + i * 10, 1, true, 0.9, "person")).size();
    railhmi_test::check(n == 1 && e.events().size() == 1, "emit resets cycle: first event");
    railhmi_test::check(e.on_detection(front_det("CAM-01", 40, 1, true, 0.9, "person")).empty() &&
                            e.on_detection(front_det("CAM-01", 50, 1, true, 0.9, "person")).empty(),
                        "suppressed updates do not emit");
    // t=1030 恰跨出抑制窗口(30+1000)，但确认计数已重置 -> 需完整 3 帧
    railhmi_test::check(e.on_detection(front_det("CAM-01", 1030, 1, true, 0.9, "person")).empty(),
                        "boundary frame alone not enough");
    railhmi_test::check(e.on_detection(front_det("CAM-01", 1040, 1, true, 0.9, "person")).empty(),
                        "second frame not enough");
    railhmi_test::check(
        e.on_detection(front_det("CAM-01", 1050, 1, true, 0.9, "person")).size() == 1,
        "full new cycle emits");
    railhmi_test::check(e.events().size() == 2, "emit resets cycle: second event");
}

void test_suppress_boundary_strict() {
    RuleConfig c = cfg();
    c.confirm_frames = 1;
    RuleEngine e(c);
    railhmi_test::check(e.on_detection(front_det("CAM-01", 0, 1, true, 0.9, "person")).size() == 1,
                        "boundary: first event");
    railhmi_test::check(e.on_detection(front_det("CAM-01", 999, 1, true, 0.9, "person")).empty(),
                        "999ms still suppressed");
    railhmi_test::check(e.on_detection(front_det("CAM-01", 1000, 1, true, 0.9, "person")).size() == 1,
                        "exactly suppress_window not suppressed (new event)");
    railhmi_test::check(e.on_detection(front_det("CAM-01", 1999, 1, true, 0.9, "person")).empty(),
                        "1999ms still suppressed");
    railhmi_test::check(e.on_detection(front_det("CAM-01", 2000, 1, true, 0.9, "person")).size() == 1,
                        "2000ms not suppressed");
    railhmi_test::check(e.events().size() == 3, "boundary events count 3");
}

void test_suppress_update_refreshes() {
    RuleConfig c = cfg();
    c.confirm_frames = 1;
    RuleEngine e(c);
    auto a = e.on_detection(front_det("CAM-01", 0, 1, true, 0.9, "person"));
    railhmi_test::check(a.size() == 1, "update test: first event");
    const long long id = a.empty() ? -1 : a[0].id;
    Detection u = front_det("CAM-01", 800, 1, true, 0.95, "person");
    u.distance_m = 10.0;
    railhmi_test::check(e.on_detection(u).empty() && e.events().size() == 1,
                        "update within window no new event");
    const AlarmEvent* ev = e.find(id);
    railhmi_test::check(ev && ev->last_update_ms == 800, "update refreshes last_update_ms");
    railhmi_test::check(ev && ev->distance_m == 10.0, "update refreshes distance");
    railhmi_test::check(e.on_detection(front_det("CAM-01", 1600, 1, true, 0.9, "person")).size() == 1,
                        "new event after window from original emit");
    railhmi_test::check(e.events().size() == 2, "update test: two events");
}

void test_ack_idempotent_isolated() {
    RuleConfig c = cfg();
    c.confirm_frames = 1;
    RuleEngine e(c);
    auto a = e.on_detection(front_det("CAM-01", 0, 1, true, 0.9, "person"));
    auto b = e.on_detection(front_det("CAM-01", 10, 2, true, 0.9, "person"));
    railhmi_test::check(a.size() == 1 && b.size() == 1 && e.events().size() == 2,
                        "two tracks -> two events");
    const long long id1 = a[0].id, id2 = b[0].id;
    railhmi_test::check(e.ack(id1), "ack id1");
    railhmi_test::check(e.ack(id1), "ack id1 is idempotent");
    railhmi_test::check(e.find(id1)->ack_state == AckState::Acked, "id1 acked");
    railhmi_test::check(e.find(id2)->ack_state == AckState::Unacked, "id2 unaffected");
    railhmi_test::check(e.unacked_count() == 1, "ack isolation: unacked 1");
}

}  // namespace

void run_rule_tests() {
    test_level_mapping();
    test_front_confirm_and_level();
    test_front_gating();
    test_suppression_and_track();
    test_profiles();
    test_device_events();
    test_ack_keeps_event();
    test_roi_helpers();
    // v2 修复：吸收验证侧边界夹具与设备类别映射（O-1）为第一方回归
    test_to_string_identity();
    test_device_category_from_position();
    test_roi_corner_inclusive();
    test_cross_type_key_isolation();
    test_device_vs_detection_independence();
    test_non_candidate_full_reset();
    test_emit_resets_confirm_cycle();
    test_suppress_boundary_strict();
    test_suppress_update_refreshes();
    test_ack_idempotent_isolated();
}
