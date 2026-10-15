import QtQuick 2.15
import ".."

// 检测职责徽标（ITEM-012 / 视觉 §7.12）。取值来自注册表 ai_profile。
// 作用域：仅 P4/P6；司机 P1 不显示（由承载页决定是否可见）。
Rectangle {
    id: badge
    property string text: ""
    property bool detected: true    // ai_enabled=false（广角·无检测）用更低对比度
    height: Theme.size.button.sm
    width: label.width + Theme.spacing.s4
    radius: Theme.radius.pill
    color: Theme.color.neutral_bg

    Text {
        id: label
        anchors.centerIn: parent
        color: badge.detected ? Theme.color.text_secondary : Theme.color.text_muted
        font.pixelSize: Theme.typography.scale.caption.size
        text: badge.text
    }
}
