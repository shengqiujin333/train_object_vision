import QtQuick 2.15
import ".."

// 设备卡片（ITEM-012 / 视觉 §7.11）。NVR 登记项：地址/已登记/状态/通道映射（未配置）。
Rectangle {
    id: card
    property string devName: ""
    property string address: ""
    property bool registered: false
    property string integrationState: ""
    property string protocol: ""
    property string channelMapping: ""

    color: Theme.color.surface
    border.color: Theme.color.border
    border.width: Theme.border.width
    radius: Theme.radius.card

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacing.s3
        spacing: Theme.spacing.s2

        Text {
            color: Theme.color.text_primary
            font.pixelSize: Theme.typography.scale.h2.size
            text: card.devName + (card.registered ? "" : "（未登记）")
        }
        Text {
            color: Theme.color.text_secondary
            font.pixelSize: Theme.typography.scale.body.size
            text: "地址：" + card.address
        }
        Text {
            color: Theme.color.text_secondary
            font.pixelSize: Theme.typography.scale.body.size
            text: "状态：" + card.integrationState + " · 协议：" + card.protocol
        }
        Text {
            color: Theme.color.text_secondary
            font.pixelSize: Theme.typography.scale.body.size
            text: "通道映射：" + card.channelMapping
        }
    }
}
