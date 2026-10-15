import QtQuick 2.15
import ".."

// 顶部全局状态条（ITEM-011）。模式位固定显示可见光/红外未接入，无切换控件。
Rectangle {
    id: bar
    property string appName: "railhmi"
    property string modePrimary: "可见光"
    property string modeSecondary: "红外未接入"
    property bool modeInteractive: false
    property string clockText: ""
    property string recordingText: "录像 —"
    property string aiText: "AI —"
    property string healthText: "系统 —"

    color: Theme.color.surface
    border.color: Theme.color.border
    border.width: Theme.border.width

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.s4
        spacing: Theme.spacing.s4

        Text {
            color: Theme.color.text_primary
            font.pixelSize: Theme.typography.scale.label.size
            font.bold: true
            text: bar.appName
        }
        Text {
            color: Theme.color.text_secondary
            font.pixelSize: Theme.typography.scale.label.size
            text: bar.clockText
        }

        // 模式位：可见光 chip + 红外未接入（灰显）。modeInteractive=false -> 仅展示，无切换控件。
        Rectangle {
            height: Theme.size.button.sm
            width: modePrimaryLabel.width + Theme.spacing.s4
            radius: Theme.radius.chip
            color: Theme.color.neutral_bg
            Text {
                id: modePrimaryLabel
                anchors.centerIn: parent
                color: Theme.color.text_secondary
                font.pixelSize: Theme.typography.scale.label.size
                text: bar.modePrimary
            }
        }
        Text {
            color: Theme.color.text_disabled
            font.pixelSize: Theme.typography.scale.label.size
            text: bar.modeSecondary
        }

        Text {
            color: Theme.color.text_secondary
            font.pixelSize: Theme.typography.scale.label.size
            text: bar.recordingText
        }
        Text {
            color: Theme.color.text_secondary
            font.pixelSize: Theme.typography.scale.label.size
            text: bar.aiText
        }
        Text {
            color: Theme.color.text_secondary
            font.pixelSize: Theme.typography.scale.label.size
            text: bar.healthText
        }
    }
}
