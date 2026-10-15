import QtQuick 2.15
import ".."

// 摄像头格（ITEM-012）。显示位置名、连接状态占位、可选检测职责徽标。
Rectangle {
    id: tile
    property string title: ""
    property string state: ""
    property string badge: ""
    property bool aiEnabled: false
    property bool showBadge: false

    color: Theme.color.bg_sunken
    border.color: Theme.color.border
    border.width: Theme.border.width
    radius: Theme.radius.card

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
