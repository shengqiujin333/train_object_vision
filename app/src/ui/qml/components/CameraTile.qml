import QtQuick 2.15
import ".."
import "../components"

// 摄像头格（ITEM-012；ITEM-014 接入渲染面）。显示位置名、连接状态、可选检测职责徽标。
// 视频区由 VideoSurface 承载（渲染路径来自配置；非 LIVE 显示占位层）。
Rectangle {
    id: tile
    property string title: ""
    property string state: ""
    property string badge: ""
    property bool aiEnabled: false
    property bool showBadge: false
    // 实时可展示（= core may_present_as_live）。仅真时渲染面显示实时层。
    property bool live: false
    property string detail: ""

    color: Theme.color.bg_sunken
    border.color: Theme.color.border
    border.width: Theme.border.width
    radius: Theme.radius.card

    VideoSurface {
        id: surface
        anchors.fill: parent
        anchors.margins: Theme.border.width
        live: tile.live
        stateText: tile.state
        detail: tile.detail
    }

    Text {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.margins: Theme.spacing.s2
        color: Theme.color.text_primary
        font.pixelSize: Theme.typography.scale.label.size
        text: tile.title
    }
    Text {
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: Theme.spacing.s2
        color: Theme.color.text_secondary
        font.pixelSize: Theme.typography.scale.caption.size
        text: tile.state
    }
    DetectionBadge {
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacing.s2
        visible: tile.showBadge
        text: tile.badge
        detected: tile.aiEnabled
    }
}
