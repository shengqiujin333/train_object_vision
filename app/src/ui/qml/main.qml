import QtQuick 2.15
import "."
import "components"
import "pages"

// 应用外壳：单窗口 + 顶部全局状态条 + 全部六个功能入口 + 页面宿主。
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
        if (id === "alarm_center") return "pages/P2AlarmCenter.qml"
        if (id === "replay") return "pages/P3Replay.qml"
        if (id === "all_cameras") return "pages/P4AllCameras.qml"
        if (id === "ai_debug") return "pages/P5AiDebug.qml"
        if (id === "maintenance") return "pages/P6Maintenance.qml"
        return "pages/PageView.qml"
    }
    // 报警中心“查看录像” -> 锁定相关摄像头并定位报警时刻（ITEM-020 D-01 修复）。
    function onReplayRequest(cameraId, tsWallMs) {
        if (typeof railhmiReplay !== "undefined" && railhmiReplay)
            railhmiReplay.selectAlarm(cameraId, tsWallMs)
        shell.currentPageId = "replay"
        shell.denied = !shell.allowed("replay")
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
                    objectName: "pageLoader"
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
                        if (item && item.replayRequested)
                            item.replayRequested.connect(shell.onReplayRequest)
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

    // 启动自恢复进度浮层（ITEM-024）
    RecoveryOverlay {
        anchors.fill: parent
    }
}
