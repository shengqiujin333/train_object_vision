import QtQuick 2.15
import ".."

// 应用内虚拟键盘（现场无物理键盘、板端未安装 Qt VirtualKeyboard QML 模块）。
// 全键盘：数字行含 `-`，另有字母三行与「清空/空格/退格/完成」功能行。
// 键盘只操作传入的 TextInput 文本与光标，不依赖系统输入法、不需要回车键确认；
// 页面在字段点击时 open(input)，点空白处 close()，按「完成」发 accepted() 再收起。
// 根节点覆盖整页：P2 有报警行时 ListView(Flickable) 会吞掉页面底层的空白点击，
// 因此「点空白收起」必须上移到 z=modal 的全页 scrim，不能依赖页面底层 MouseArea。
Item {
    id: kb
    objectName: "virtualKeyboard"
    visible: false
    z: Theme.z_index.modal
    anchors.fill: parent

    readonly property real keyW: Theme.size.touch_target_min
    readonly property real keyH: Theme.size.touch_target_min
    readonly property real keyGap: Theme.spacing.s1
    property var target: null
    property var keyRows: [
        ["1", "2", "3", "4", "5", "6", "7", "8", "9", "0", "-"],
        ["q", "w", "e", "r", "t", "y", "u", "i", "o", "p"],
        ["a", "s", "d", "f", "g", "h", "j", "k", "l"],
        ["z", "x", "c", "v", "b", "n", "m"]
    ]
    property var actionKeys: [
        { label: "清空", action: "clear" },
        { label: "空格", action: "space" },
        { label: "退格", action: "back" },
        { label: "完成", action: "done" }
    ]

    signal accepted()

    function open(input) {
        kb.target = input
        kb.visible = true
    }
    function close() {
        kb.visible = false
        kb.target = null
    }
    function press(text) {
        if (!kb.target) return
        var field = kb.target
        var pos = field.cursorPosition
        var old = field.text
        field.text = old.slice(0, pos) + text + old.slice(pos)
        field.cursorPosition = pos + text.length
    }
    function backspace() {
        if (!kb.target) return
        var field = kb.target
        var pos = field.cursorPosition
        if (pos <= 0) return
        var old = field.text
        field.text = old.slice(0, pos - 1) + old.slice(pos)
        field.cursorPosition = pos - 1
    }
    function clearAll() {
        if (!kb.target) return
        kb.target.text = ""
        kb.target.cursorPosition = 0
    }
    function commit() {
        kb.accepted()
        kb.close()
    }

    // 全页 scrim：点键盘面板以外的任何位置都收起；propagateComposedEvents 让
    // 下层的筛选/查询按钮仍然能收到点击，不需要「先点一下关键盘、再点按钮」。
    MouseArea {
        id: kbScrim
        objectName: "virtualKeyboardScrim"
        anchors.fill: parent
        enabled: kb.visible
        propagateComposedEvents: true
        onClicked: {
            mouse.accepted = false
            kb.close()
        }
    }

    Rectangle {
        id: kbPanel
        width: kb.keyW * 11 + kb.keyGap * 10 + Theme.spacing.s3 * 2
        height: header.height + kb.keyH * 5 + kb.keyGap * 4 + Theme.spacing.s3 * 3
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: Theme.spacing.s4
        color: Theme.color.surface_elev
        border.color: Theme.color.border_strong
        border.width: Theme.border.width
        radius: Theme.radius.modal
        // 面板自身的空白点击不能穿透到页面的「点空白收起」区。
        MouseArea { anchors.fill: parent }

        Item {
            id: header
            x: Theme.spacing.s3
            y: Theme.spacing.s3
            width: parent.width - Theme.spacing.s3 * 2
            height: Theme.size.button.sm
            Text {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.color.text_secondary
                font.pixelSize: Theme.typography.scale.caption.size
                text: "点击按键输入（含数字与 -）· 点空白处收起"
            }
            Rectangle {
                id: closeKey
                anchors.right: parent.right
                width: Theme.size.nav_width
                height: parent.height
                radius: Theme.radius.chip
                color: Theme.color.neutral_bg
                border.color: Theme.color.border
                border.width: Theme.border.width
                Text {
                    anchors.centerIn: parent
                    color: Theme.color.text_primary
                    font.pixelSize: Theme.typography.scale.caption.size
                    text: "关闭"
                }
                MouseArea { anchors.fill: parent; onClicked: kb.close() }
            }
        }

        Column {
            id: rowsArea
            x: Theme.spacing.s3
            y: header.y + header.height + Theme.spacing.s3
            width: kbPanel.width - Theme.spacing.s3 * 2
            spacing: kb.keyGap

            Repeater {
                model: kb.keyRows
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: kb.keyGap
                    Repeater {
                        model: modelData
                        Rectangle {
                            objectName: "virtualKey-" + modelData
                            width: kb.keyW
                            height: kb.keyH
                            radius: Theme.radius.chip
                            color: keyArea.pressed ? Theme.color.accent_pressed : Theme.color.neutral_bg
                            border.color: Theme.color.border
                            border.width: Theme.border.width
                            Text {
                                anchors.centerIn: parent
                                color: Theme.color.text_primary
                                font.pixelSize: Theme.typography.scale.label.size
                                text: modelData
                            }
                            MouseArea {
                                id: keyArea
                                anchors.fill: parent
                                cursorShape: Qt.PointingHandCursor
                                onClicked: kb.press(modelData)
                            }
                        }
                    }
                }
            }

            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: kb.keyGap
                Repeater {
                    model: kb.actionKeys
                    Rectangle {
                        objectName: "virtualKey-" + modelData.action
                        width: (kb.keyW * 11 + kb.keyGap * 10 - kb.keyGap * 3) / 4
                        height: kb.keyH
                        radius: Theme.radius.chip
                        color: actionArea.pressed ? Theme.color.accent_pressed
                             : (modelData.action === "done" ? Theme.color.accent : Theme.color.surface_hover)
                        border.color: modelData.action === "done" ? Theme.color.accent : Theme.color.border
                        border.width: Theme.border.width
                        Text {
                            anchors.centerIn: parent
                            color: modelData.action === "done" ? Theme.color.text_on_solid : Theme.color.text_primary
                            font.pixelSize: Theme.typography.scale.label.size
                            text: modelData.label
                        }
                        MouseArea {
                            id: actionArea
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                if (modelData.action === "clear") kb.clearAll()
                                else if (modelData.action === "space") kb.press(" ")
                                else if (modelData.action === "back") kb.backspace()
                                else kb.commit()
                            }
                        }
                    }
                }
            }
        }
    }
}
