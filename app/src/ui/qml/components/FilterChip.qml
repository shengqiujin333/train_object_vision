import QtQuick 2.15
import ".."

// 过滤选项 chip（ITEM-018）。选中高亮，点击发出 clicked()。
Rectangle {
    id: chip
    property string text: ""
    property bool selected: false
    signal clicked()

    implicitWidth: label.implicitWidth + Theme.spacing.s3 * 2
    implicitHeight: Theme.size.button.sm
    radius: Theme.radius.chip
    color: chip.selected ? Theme.color.accent : Theme.color.neutral_bg
    border.color: chip.selected ? Theme.color.accent : Theme.color.border
    border.width: Theme.border.width

    Text {
        id: label
        anchors.centerIn: parent
        color: chip.selected ? Theme.color.text_on_solid : Theme.color.text_secondary
        font.pixelSize: Theme.typography.scale.caption.size
        text: chip.text
    }
    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.PointingHandCursor
        onClicked: chip.clicked()
    }
}
