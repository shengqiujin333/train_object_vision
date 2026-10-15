import QtQuick 2.15
import "."
import "components"
import "pages"

// 应用外壳（ITEM-011）：单窗口 + 顶部全局状态条 + 角色化固定导航 + 页面宿主。
// 可见导航项（railhmiNav）由 C++ 按角色过滤；受限直达页由 DeniedView 显示“无权限 + 返回”。
// 所有颜色/字号来自生成的 Theme 单例（ITEM-010），不硬编码。
Rectangle {
    id: shell
    width: 1920
    height: 1080
    color: Theme.color.bg_base

    property string currentPageId: railhmiDefaultPage
    property bool denied: false

    function entryFor(id, list) {
        for (var i = 0; i < list.length; ++i)
            if (list[i].id === id) return list[i]
        return null
    }
    function titleFor(id) {
        var e = entryFor(id, railhmiAllPages)
        return e ? e.title : ""
    }
    function allowed(id) {
        return entryFor(id, railhmiNav) !== null
    }

    Column {
        anchors.fill: parent

        TopStatusBar {
            width: parent.width
            height: Theme.size.top_bar_height
            modePrimary: railhmiModePrimary
            modeSecondary: railhmiModeSecondary
            modeInteractive: railhmiModeInteractive
        }

        Row {
            width: parent.width
            height: parent.height - Theme.size.top_bar_height

            NavBar {
                width: Theme.size.nav_width
                height: parent.height
                entries: railhmiNav
                activeId: shell.currentPageId
                onNavigate: function (pageId) {
                    shell.currentPageId = pageId
                    shell.denied = !shell.allowed(pageId)
                }
            }

            Item {
                width: parent.width - Theme.size.nav_width
                height: parent.height

                PageView {
                    anchors.fill: parent
                    visible: !shell.denied
                    pageTitle: shell.titleFor(shell.currentPageId)
                    roleName: railhmiRole
                    modePrimary: railhmiModePrimary
                    modeSecondary: railhmiModeSecondary
                }
                DeniedView {
                    anchors.fill: parent
                    visible: shell.denied
                    requestedTitle: shell.titleFor(shell.currentPageId)
                    onBack: shell.denied = false
                }
            }
        }
    }
}
