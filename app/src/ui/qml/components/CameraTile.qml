import QtQuick 2.15
import ".."
import "../components"

// 摄像头格（ITEM-012；ITEM-014 渲染面；ITEM-015 fps/最近帧/重连/占位）。
// 显示位置名、连接状态角标、实时 fps、最近一帧时间、可选检测职责徽标。
// 非 LIVE（presentable=false）时 VideoSurface 显示占位层，旧帧不作为实时画面。
Rectangle {
    id: tile
    property string title: ""
    property string state: ""
    property string badge: ""
    property bool aiEnabled: false
    property bool showBadge: false
    // 实时可展示（= core may_present_as_live）。仅真时渲染面显示实时层。
    property bool live: false
    property var channel: null
    property string detail: ""
    // 运行时展示字段（ITEM-015）
    property string fpsText: ""
    property string lastFrameText: ""
    property string reconnectText: ""

    color: Theme.color.bg_sunken
    border.color: Theme.color.border
    border.width: Theme.border.width
    radius: Theme.radius.card

    VideoSurface {
        id: surface
        anchors.fill: parent
        anchors.margins: Theme.border.width
        live: tile.live
        channel: tile.channel
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
    // 右下：实时 fps 与最近一帧时间
    Column {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacing.s2
        spacing: Theme.spacing.s1
        Text {
            anchors.right: parent.right
            visible: tile.fpsText.length > 0
            color: Theme.color.text_secondary
            font.pixelSize: Theme.typography.scale.caption.size
            text: tile.fpsText
        }
        Text {
            anchors.right: parent.right
            visible: tile.lastFrameText.length > 0
            color: Theme.color.text_muted
            font.pixelSize: Theme.typography.scale.caption.size
            text: tile.lastFrameText
        }
    }
    // 顶部居中：重连次数（断流后可见）
    Text {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.margins: Theme.spacing.s2
        visible: tile.reconnectText.length > 0
        color: Theme.color.text_secondary
        font.pixelSize: Theme.typography.scale.caption.size
        text: tile.reconnectText
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
