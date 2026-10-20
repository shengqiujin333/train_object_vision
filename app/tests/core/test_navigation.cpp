// railhmi-core 导航与角色权限测试（ITEM-011 / 交互方案 v2 §8、§11#10、§11#14）。
// 所有角色可见六页，默认六画面；模式位固定可见光、红外未接入。

#include "test_util.h"

#include <string>
#include <vector>

#include "railhmi/core/navigation.h"

namespace {

using namespace railhmi::core;

bool contains(const std::vector<Page>& v, Page p) {
    for (Page x : v) if (x == p) return true;
    return false;
}

void test_ids() {
    railhmi_test::check(std::string(to_string(Page::AiDebug)) == "ai_debug", "to_string(AiDebug)");
    railhmi_test::check(std::string(page_title(Page::Maintenance)) == "设备维护", "page_title(Maintenance)");
    Page p;
    railhmi_test::check(parse_page("all_cameras", &p) && p == Page::AllCameras, "parse_page(all_cameras)");
    railhmi_test::check(!parse_page("nope", &p), "parse_page rejects unknown");
    railhmi_test::check(all_pages().size() == 6, "six fixed pages");
    railhmi_test::check(all_pages()[0] == Page::Realtime && all_pages()[5] == Page::Maintenance,
                        "fixed page order");
}

void test_permission_matrix() {
    railhmi_test::check(role_can_access(Role::Driver, Page::Realtime), "driver can realtime");
    railhmi_test::check(role_can_access(Role::Driver, Page::AlarmCenter), "driver can alarm center");
    railhmi_test::check(role_can_access(Role::Driver, Page::Replay), "driver can replay");
    railhmi_test::check(role_can_access(Role::Driver, Page::AllCameras), "driver can all cameras");
    railhmi_test::check(role_can_access(Role::Driver, Page::AiDebug), "driver can AI debug");
    railhmi_test::check(role_can_access(Role::Driver, Page::Maintenance), "driver can maintenance");
    for (Role r : {Role::Maintainer, Role::Developer, Role::Admin}) {
        for (Page pg : all_pages()) {
            railhmi_test::check(role_can_access(r, pg), "maintainer/developer/admin can all pages");
        }
    }
}

void test_visible_pages() {
    auto d = visible_pages(Role::Driver);
    railhmi_test::check(d.size() == 6, "driver sees all 6 nav entries", std::to_string(d.size()));
    railhmi_test::check(contains(d, Page::Realtime) && contains(d, Page::AlarmCenter) &&
                            contains(d, Page::Replay),
                        "driver nav = realtime/alarm/replay");
    railhmi_test::check(contains(d, Page::AiDebug) && contains(d, Page::Maintenance) &&
                            contains(d, Page::AllCameras),
                        "driver nav includes AI debug/maintenance/all cameras");
    auto m = visible_pages(Role::Maintainer);
    railhmi_test::check(m.size() == 6, "maintainer sees all 6 nav entries", std::to_string(m.size()));
    railhmi_test::check(visible_pages(Role::Developer).size() == 6 &&
                            visible_pages(Role::Admin).size() == 6,
                        "developer/admin see all 6 nav entries");
}

void test_default_page() {
    railhmi_test::check(default_page(Role::Driver) == Page::AllCameras, "driver lands on all cameras");
    railhmi_test::check(default_page(Role::Maintainer) == Page::AllCameras, "maintainer lands on all cameras");
    railhmi_test::check(default_page(Role::Developer) == Page::AllCameras, "developer lands on all cameras");
    railhmi_test::check(default_page(Role::Admin) == Page::AllCameras, "admin lands on all cameras");
}

void test_navigation_resolution() {
    auto a = resolve_navigation(Role::Driver, Page::Realtime);
    railhmi_test::check(a.allowed && a.requested == Page::Realtime, "driver realtime allowed");
    auto b = resolve_navigation(Role::Driver, Page::AiDebug);
    railhmi_test::check(b.allowed && b.requested == Page::AiDebug,
                        "driver direct AI debug allowed");
    auto c = resolve_navigation(Role::Maintainer, Page::Maintenance);
    railhmi_test::check(c.allowed, "maintainer maintenance allowed");
}

void test_mode_display() {
    ModeDisplay m = mode_display();
    railhmi_test::check(m.primary == "可见光", "mode primary = visible");
    railhmi_test::check(m.secondary == "红外未接入", "mode secondary = IR not connected");
    railhmi_test::check(!m.interactive, "mode has no interactive toggle");
}

}  // namespace

void run_navigation_tests() {
    test_ids();
    test_permission_matrix();
    test_visible_pages();
    test_default_page();
    test_navigation_resolution();
    test_mode_display();
}
