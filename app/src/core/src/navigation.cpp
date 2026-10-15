#include "railhmi/core/navigation.h"

namespace railhmi {
namespace core {

const char* to_string(Page p) {
    switch (p) {
        case Page::Realtime: return "realtime";
        case Page::AlarmCenter: return "alarm_center";
        case Page::Replay: return "replay";
        case Page::AllCameras: return "all_cameras";
        case Page::AiDebug: return "ai_debug";
        case Page::Maintenance: return "maintenance";
    }
    return "?";
}

const char* page_title(Page p) {
    switch (p) {
        case Page::Realtime: return "实时监控";
        case Page::AlarmCenter: return "报警中心";
        case Page::Replay: return "历史录像";
        case Page::AllCameras: return "全摄像头";
        case Page::AiDebug: return "AI 调试";
        case Page::Maintenance: return "设备维护";
    }
    return "?";
}

bool parse_page(const std::string& s, Page* out) {
    if (s == "realtime") { *out = Page::Realtime; return true; }
    if (s == "alarm_center") { *out = Page::AlarmCenter; return true; }
    if (s == "replay") { *out = Page::Replay; return true; }
    if (s == "all_cameras") { *out = Page::AllCameras; return true; }
    if (s == "ai_debug") { *out = Page::AiDebug; return true; }
    if (s == "maintenance") { *out = Page::Maintenance; return true; }
    return false;
}

std::vector<Page> all_pages() {
    return {Page::Realtime, Page::AlarmCenter, Page::Replay,
            Page::AllCameras, Page::AiDebug, Page::Maintenance};
}

bool role_can_access(Role role, Page page) {
    switch (role) {
        case Role::Driver:
            return page == Page::Realtime || page == Page::AlarmCenter || page == Page::Replay;
        case Role::Maintainer:
        case Role::Developer:
        case Role::Admin:
            return true;
    }
    return false;
}

std::vector<Page> visible_pages(Role role) {
    std::vector<Page> out;
    for (Page p : all_pages()) {
        if (role_can_access(role, p)) out.push_back(p);
    }
    return out;
}

Page default_page(Role role) {
    switch (role) {
        case Role::Driver: return Page::Realtime;
        case Role::Maintainer: return Page::AllCameras;
        case Role::Developer: return Page::AiDebug;
        case Role::Admin: return Page::Maintenance;
    }
    return Page::Realtime;
}

NavigationResult resolve_navigation(Role role, Page requested) {
    NavigationResult r;
    r.requested = requested;
    r.allowed = role_can_access(role, requested);
    return r;
}

ModeDisplay mode_display() {
    ModeDisplay m;
    m.primary = "可见光";
    m.secondary = "红外未接入";
    m.interactive = false;  // 本期不提供模式切换控件
    return m;
}

}  // namespace core
}  // namespace railhmi
