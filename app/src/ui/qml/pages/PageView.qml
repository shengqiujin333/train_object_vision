import QtQuick 2.15
import ".."

// 通用页面骨架（ITEM-011）。具体页面内容由后续工作项实现。
Rectangle {
    id: page
    property string pageTitle: ""
    property string roleName: ""
    property string modePrimary: ""
    property string modeSecondary: ""
    color: Theme.color.bg_base

    Column {
        anchors.centerIn: parent
        spacing: Theme.spacing.s3

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.color.text_primary
            font.pixelSize: Theme.typography.scale.h1.size
            font.bold: true
            text: page.pageTitle
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.color.text_secondary
            font.pixelSize: Theme.typography.scale.body.size
            text: "角色：" + page.roleName + " · 模式：" + page.modePrimary + " / " + page.modeSecondary
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: Theme.color.text_muted
            font.pixelSize: Theme.typography.scale.caption.size
            text: "页面内容由后续工作项实现"
        }
    }
}
