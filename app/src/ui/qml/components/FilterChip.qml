import QtQuick 2.15
import ".."

// 过滤选项 chip（ITEM-018；ITEM-020 触控/键盘可达）。选中高亮，点击发出 clicked()。
// 触控目标 ≥ touch_target_min；可 Tab 聚焦（activeFocusOnTab）并用 Enter/Space 触发。
Rectangle {
    id: chip
    property string text: ""
    property bool selected: false
    signal clicked()

    implicitWidth: label.implicitWidth + Theme.spacing.s3 * 2
    implicitHeight: Theme.size.touch_target_min
    radius: Theme.radius.chip
    color: chip.selected ? Theme.color.accent : Theme.color.neutral_bg
    border.color: chip.activeFocus ? Theme.color.focus_ring
                 : (chip.selected ? Theme.color.accent : Theme.color.border)
    border.width: chip.activeFocus ? 2 : Theme.border.width
    activeFocusOnTab: true
    focus: false

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
    Keys.onReturnPressed: chip.clicked()
    Keys.onEnterPressed: chip.clicked()
    Keys.onSpacePressed: chip.clicked()
}
