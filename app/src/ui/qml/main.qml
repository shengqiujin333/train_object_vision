import QtQuick 2.15
import "."

// railhmi 骨架窗口（ITEM-003/ITEM-010）。所有颜色/字号来自生成的 Theme 单例，不硬编码。
Rectangle {
    id: root
    width: 1920
    height: 1080
    color: Theme.color.bg_base

    Column {
        anchors.centerIn: parent
        spacing: Theme.spacing.s4

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.color.text_primary
            font.pixelSize: Theme.typography.scale.display.size
            font.bold: true
            text: "railhmi"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.color.text_secondary
            font.pixelSize: Theme.typography.scale.body_lg.size
            text: railhmiBuildDescription
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.color.text_muted
            font.pixelSize: Theme.typography.scale.body.size
            text: railhmiSimulation ? "simulation input (preview)" : "target skeleton"
        }
    }
}
