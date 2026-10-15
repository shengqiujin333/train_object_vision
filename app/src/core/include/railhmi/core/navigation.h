#pragma once

// railhmi-core: 应用外壳导航与角色权限（ITEM-011）。
// 六页固定导航（实时监控/报警中心/历史录像/全摄像头/AI调试/设备维护）、
// 按角色（司机/维护/研发/管理员）过滤可见入口、受限页返回“无权限”、
// 顶部模式位（本期仅可见光，红外未接入，无切换控件）。
// 纯逻辑，可用单元测试驱动；QML 外壳消费本模型的可见页集合。
//
// 仅依赖 C++ 标准库。

#include <string>
#include <vector>

#include "railhmi/core/config.h"

namespace railhmi {
namespace core {

enum class Page { Realtime = 0, AlarmCenter, Replay, AllCameras, AiDebug, Maintenance };

const char* to_string(Page p);       // 稳定 id：realtime/alarm_center/replay/all_cameras/ai_debug/maintenance
const char* page_title(Page p);      // 中文标题
bool parse_page(const std::string& s, Page* out);

// 全部页面（固定顺序）。
std::vector<Page> all_pages();

// 权限矩阵（交互方案 v2 §8）：司机可见实时/报警/回放；维护/研发/管理员可见全部。
bool role_can_access(Role role, Page page);

// 按角色过滤后的固定导航项（保持 all_pages 顺序；权限不可见项不出现）。
std::vector<Page> visible_pages(Role role);

// 角色默认落地页。
Page default_page(Role role);

// 导航请求结果：允许则跳转；不允许则由界面显示“无权限”说明 + 返回（不空白崩溃）。
struct NavigationResult {
    Page requested = Page::Realtime;
    bool allowed = false;
};
NavigationResult resolve_navigation(Role role, Page requested);

// 顶部模式位：本期固定可见光；红外未接入；无切换控件。
struct ModeDisplay {
    std::string primary;    // "可见光"
    std::string secondary;  // "红外未接入"
    bool interactive;       // false（不提供模式切换控件）
};
ModeDisplay mode_display();

}  // namespace core
}  // namespace railhmi
