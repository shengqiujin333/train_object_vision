import QtQuick 2.15
import "."
import "components"

// 应用外壳（ITEM-011/012）：单窗口 + 顶部全局状态条 + 角色化固定导航 + 页面宿主。
// 可见导航项（railhmiNav）由 C++ 按角色过滤；受限直达页由 DeniedView 显示“无权限 + 返回”。
// 页面内容按 id 选择；司机 P1 不显示检测职责徽标（P4/P6 才显示）。颜色/字号均来自 Theme。
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
    function pageSource(id) {
        if (id === "realtime") return "pages/P1Realtime.qml"
        if (id === "all_cameras") return "pages/P4AllCameras.qml"
        if (id === "maintenance") return "pages/P6Maintenance.qml"
        return "pages/PageView.qml"
    }
    // 关键状态区/顶部条异常项点击（ITEM-016）：允许则跳详情，不允许则显示无权限+返回。
    function onStatusNavigate(pageId, allowed) {
        shell.currentPageId = pageId
        shell.denied = !allowed
    }

    Column {
        anchors.fill: parent

        TopStatusBar {
            width: parent.width
            height: Theme.size.top_bar_height
            items: railhmiTopBar
            onNavigateRequested: shell.onStatusNavigate(pageId, allowed)
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

                Loader {
                    id: pageLoader
                    anchors.fill: parent
                    visible: !shell.denied
                    source: shell.pageSource(shell.currentPageId)
                    onLoaded: {
                        if (item && item.hasOwnProperty("pageTitle")) {
                            item.pageTitle = shell.titleFor(shell.currentPageId)
                            item.roleName = railhmiRole
                            item.modePrimary = railhmiModePrimary
                            item.modeSecondary = railhmiModeSecondary
                        }
                        if (item && item.statusNavigate)
                            item.statusNavigate.connect(shell.onStatusNavigate)
                    }
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
