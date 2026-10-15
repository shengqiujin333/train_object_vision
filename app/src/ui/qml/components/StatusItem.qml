import QtQuick 2.15
import ".."

// 状态项（ITEM-016）：颜色 + 形状字形 + 文字三重编码；去色后仍可凭形状与文字区分。
// 正常简洁（无底块、点字形）；异常突出（底块 + 级别色 + 形状 + 文字）。异常项可点击。
Rectangle {
    id: item
    property string label: ""
    property string value: ""
    property string secondary: ""
    property string glyph: ""
    property string colorToken: "neutral"
    property bool abnormal: false
    property bool clickable: false
    property bool showGlyph: true

    signal activated()

    readonly property color fgColor: item.resolveColor(item.colorToken)
    readonly property color bgColor: item.resolveColor(item.colorToken + "_bg")

    function resolveColor(t) {
        if (t === "ok") return Theme.color.ok
        if (t === "ok_bg") return Theme.color.ok_bg
        if (t === "info") return Theme.color.info
        if (t === "info_bg") return Theme.color.info_bg
        if (t === "degraded") return Theme.color.degraded
        if (t === "degraded_bg") return Theme.color.degraded_bg
        if (t === "error") return Theme.color.error
        if (t === "error_bg") return Theme.color.error_bg
        if (t === "neutral") return Theme.color.neutral
        if (t === "neutral_bg") return Theme.color.neutral_bg
        return Theme.color.text_secondary
    }

    implicitWidth: row.implicitWidth + Theme.spacing.s2 * 2
    implicitHeight: Theme.size.button.md
    radius: Theme.radius.chip
    color: item.abnormal ? item.bgColor : "transparent"
    border.width: item.abnormal ? Theme.border.width : 0
    border.color: item.fgColor

    Row {
        id: row
        anchors.centerIn: parent
        spacing: Theme.spacing.s1
        Text {
            visible: item.showGlyph && item.glyph.length > 0
            anchors.verticalCenter: parent.verticalCenter
            color: item.fgColor
            font.pixelSize: Theme.typography.scale.label.size
            text: item.glyph
        }
        Column {
            anchors.verticalCenter: parent.verticalCenter
            spacing: 0
            Text {
                color: item.abnormal ? item.fgColor : Theme.color.text_secondary
                font.pixelSize: Theme.typography.scale.caption.size
                text: item.label
            }
            Text {
                color: item.abnormal ? item.fgColor : Theme.color.text_primary
                font.pixelSize: Theme.typography.scale.label.size
                text: item.secondary.length > 0 ? (item.value + " · " + item.secondary) : item.value
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        enabled: item.clickable
        cursorShape: item.clickable ? Qt.PointingHandCursor : Qt.ArrowCursor
        onClicked: item.activated()
    }
}
