// railhmi-core navigation contract extension fixtures for ITEM-011 (verifier-owned).
// Independent assertions beyond the implementer's test_navigation.cpp:
//   - full role x page permission matrix
//   - visible_pages order/subset invariants and the exact 3-page driver set
//   - default_page accessibility invariant (default landing is always allowed)
//   - resolve_navigation echo + verdict for all role/page combinations
//   - parse_page full round-trip plus case/empty/prefix rejection negatives
//   - page id/title identity (distinct, non-empty, lowercase snake ids)
//   - mode_display repeat-call stability
#include "railhmi/core/navigation.h"

#include <cstdio>
#include <set>
#include <string>
#include <vector>

namespace {

using namespace railhmi::core;

int passed = 0;
int failed = 0;

void check(bool cond, const std::string& what) {
    if (cond) {
        ++passed;
        std::printf("[PASS] %s\n", what.c_str());
    } else {
        ++failed;
        std::printf("[FAIL] %s\n", what.c_str());
    }
}

std::vector<Page> pages() { return all_pages(); }

const char* role_id(Role r) {
    switch (r) {
        case Role::Driver: return "driver";
        case Role::Maintainer: return "maintainer";
        case Role::Developer: return "developer";
        case Role::Admin: return "admin";
    }
    return "?";
}

bool in(const std::vector<Page>& v, Page p) {
    for (Page x : v)
        if (x == p) return true;
    return false;
}

void test_full_matrix() {
    const Page ps[6] = {Page::Realtime, Page::AlarmCenter, Page::Replay,
                        Page::AllCameras, Page::AiDebug, Page::Maintenance};
    const Role roles[4] = {Role::Driver, Role::Maintainer, Role::Developer, Role::Admin};
    for (Role r : roles) {
        for (Page p : ps) {
            const bool expect = (r == Role::Driver)
                                    ? (p == Page::Realtime || p == Page::AlarmCenter ||
                                       p == Page::Replay)
                                    : true;
            check(role_can_access(r, p) == expect,
                  std::string("matrix ") + role_id(r) + " x " + to_string(p) +
                      (expect ? " allowed" : " denied"));
        }
    }
}

void test_visible_order_and_subset() {
    const std::vector<Page> base = pages();
    const Role roles[4] = {Role::Driver, Role::Maintainer, Role::Developer, Role::Admin};
    for (Role r : roles) {
        auto vp = visible_pages(r);
        bool order = true, subset = true;
        size_t at = 0;
        for (Page p : vp) {
            bool found = false;
            for (size_t i = at; i < base.size(); ++i) {
                if (base[i] == p) {
                    at = i + 1;
                    found = true;
                    break;
                }
            }
            if (!found) order = false;
            if (!in(base, p)) subset = false;
        }
        check(order, std::string("visible order preserved for ") + role_id(r));
        check(subset, std::string("visible subset of all_pages for ") + role_id(r));
        check(!vp.empty(), std::string("visible non-empty for ") + role_id(r));
    }
    // Driver nav is exactly {realtime, alarm_center, replay}
    auto d = visible_pages(Role::Driver);
    check(d.size() == 3, "visible driver size 3");
    check(d[0] == Page::Realtime && d[1] == Page::AlarmCenter && d[2] == Page::Replay,
          "visible driver exact ordered triple");
    // Other roles: full 6-page ordered list
    auto m = visible_pages(Role::Maintainer);
    check(m == base, "visible maintainer equals all_pages order");
}

void test_default_lands_allowed() {
    const Role roles[4] = {Role::Driver, Role::Maintainer, Role::Developer, Role::Admin};
    for (Role r : roles) {
        Page d = default_page(r);
        check(role_can_access(r, d),
              std::string("default_page access invariant for ") + role_id(r));
    }
    // Guarded default split: driver's default is not visible to drivers... i.e. the
    // maintainer default must NOT be a driver-visible page (ensures defaults differ
    // between guarded and unguarded roles).
    Page md = default_page(Role::Maintainer);
    check(!role_can_access(Role::Driver, md),
          "maintainer default is outside the driver set");
}

void test_resolve_echo() {
    const Page ps[6] = {Page::Realtime, Page::AlarmCenter, Page::Replay,
                        Page::AllCameras, Page::AiDebug, Page::Maintenance};
    const Role roles[4] = {Role::Driver, Role::Maintainer, Role::Developer, Role::Admin};
    for (Role r : roles) {
        for (Page p : ps) {
            auto nr = resolve_navigation(r, p);
            check(nr.requested == p,
                  std::string("resolve echoes requested ") + role_id(r) + "/" +
                      to_string(p));
            check(nr.allowed == role_can_access(r, p),
                  std::string("resolve matches matrix ") + role_id(r) + "/" +
                      to_string(p));
        }
    }
}

void test_parse_page_negatives() {
    Page p = Page::Realtime;
    for (const char* id : {"realtime", "alarm_center", "replay", "all_cameras",
                            "ai_debug", "maintenance"}) {
        check(parse_page(id, &p) && to_string(p) == id,
              std::string("parse round-trip ") + id);
    }
    check(!parse_page("REALTIME", &p), "parse rejects uppercase REALTIME");
    check(!parse_page("", &p), "parse rejects empty");
    check(!parse_page(" lead", &p), "parse rejects leading-space");
    check(!parse_page("realtime ", &p), "parse rejects trailing-space");
    check(!parse_page("realtime_x", &p), "parse rejects prefixed id");
    check(!parse_page("alarm", &p), "parse rejects truncated id");
    check(!parse_page("IR", &p), "parse rejects mode-as-page token");
}

void test_identity() {
    std::set<std::string> ids, titles;
    for (Page q : pages()) {
        std::string id = to_string(q);
        std::string title = page_title(q);
        check(!id.empty(), "id non-empty");
        check(!title.empty(), "title non-empty");
        check(ids.insert(id).second, "id distinct " + id);
        check(titles.insert(title).second, "title distinct " + title);
        bool snake = true, lower = true;
        for (char c : id) {
            if (c == '_') continue;
            if (c < 'a' || c > 'z') lower = false;
        }
        for (char c : id)
            if (c != '_' && (c < 'a' || c > 'z')) snake = false;
        check(lower, "id lowercase " + id);
        (void)snake;
    }
}

void test_mode_display_stability() {
    ModeDisplay a = mode_display();
    ModeDisplay b = mode_display();
    check(a.primary == b.primary && a.secondary == b.secondary &&
              a.interactive == b.interactive,
          "mode_display repeat-call stability");
    check(a.primary == "可见光" && a.secondary == "红外未接入" && !a.interactive,
          "mode_display fixed contract");
}

}  // namespace

int main() {
    test_full_matrix();
    test_visible_order_and_subset();
    test_default_lands_allowed();
    test_resolve_echo();
    test_parse_page_negatives();
    test_identity();
    test_mode_display_stability();
    std::printf("== %d passed, %d failed ==\n", passed, failed);
    std::printf("RESULT: %s passed=%d failed=%d\n",
                (failed == 0 ? "PASS" : "FAIL"), passed, failed);
    return failed == 0 ? 0 : 1;
}
