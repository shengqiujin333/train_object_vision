import QtQuick 2.15
import ".."

// 顶部全局状态条（ITEM-011 模式位；ITEM-016 时间/录像/AI/系统健康/未接入项）。
// 每项含颜色 + 形状字形 + 文字三重编码；模式位固定“可见光 · 红外未接入”，无切换控件。
Rectangle {
    id: bar
    property var items: (typeof railhmiTopBar !== "undefined") ? railhmiTopBar : []
    property string appName: "railhmi"
    property string clockText: ""

    signal navigateRequested(string pageId, bool allowed)

    color: Theme.color.surface
    border.color: Theme.color.border
    border.width: Theme.border.width

    Timer {
        interval: 1000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: bar.clockText = Qt.formatDateTime(new Date(), "HH:mm:ss")
    }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.s4
        spacing: Theme.spacing.s4

        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.color.text_primary
            font.pixelSize: Theme.typography.scale.label.size
            font.bold: true
            text: bar.appName
        }

        Repeater {
            model: bar.items
            StatusItem {
                anchors.verticalCenter: parent.verticalCenter
                objectName: "top-" + modelData.key
                label: modelData.label
                value: (modelData.key === "time") ? bar.clockText : modelData.value
                secondary: modelData.secondary
                glyph: modelData.glyph
                colorToken: modelData.color
                abnormal: modelData.abnormal !== undefined ? modelData.abnormal : (modelData.level === "warning" || modelData.level === "alarm")
                clickable: modelData.clickable
                onActivated: bar.navigateRequested(modelData.target, modelData.allowed)
            }
        }
    }
}
