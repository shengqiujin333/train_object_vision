import QtQuick 2.15
import ".."

// 受限页：显示“无权限”说明 + 返回（ITEM-011），不空白崩溃。
Rectangle {
    id: denied
    property string requestedTitle: ""
    signal back()
    color: Theme.color.bg_base

    Column {
        anchors.centerIn: parent
        spacing: Theme.spacing.s4

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.color.error_fg
            font.pixelSize: Theme.typography.scale.h1.size
            font.bold: true
            text: "无权限"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.color.text_secondary
            font.pixelSize: Theme.typography.scale.body.size
            text: "当前角色无权访问：" + denied.requestedTitle
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: Theme.size.button.lg * 2
            height: Theme.size.button.lg
            radius: Theme.radius.button
            color: Theme.color.accent

            Text {
                anchors.centerIn: parent
                color: Theme.color.text_on_solid
                font.pixelSize: Theme.typography.scale.body.size
                text: "返回"
            }
            MouseArea {
                anchors.fill: parent
                onClicked: denied.back()
            }
        }
    }
}
