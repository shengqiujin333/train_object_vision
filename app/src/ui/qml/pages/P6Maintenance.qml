import QtQuick 2.15
import ".."
import "../components"

// P6 设备维护（ITEM-012）：摄像头（含检测职责徽标）+ NVR 集成项（通道映射=未配置）。
Rectangle {
    id: page
    color: Theme.color.bg_base

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacing.s4
        spacing: Theme.spacing.s3

        Text {
            color: Theme.color.text_primary
            font.pixelSize: Theme.typography.scale.h1.size
            font.bold: true
            text: "设备维护"
        }

        Grid {
            width: parent.width
            columns: 3
            spacing: Theme.spacing.s3
            Repeater {
                model: railhmiCameras
                CameraTile {
                    width: (parent.width - 2 * Theme.spacing.s3) / 3
                    height: 120
                    title: modelData.name
                    state: modelData.state
                    badge: modelData.badge
                    aiEnabled: modelData.aiEnabled
                    showBadge: true
                }
            }
        }

        Repeater {
            model: railhmiIntegrations
            DeviceCard {
                width: parent.width
                height: 140
                devName: modelData.name
                address: modelData.address
                registered: modelData.registered
                integrationState: modelData.state
                protocol: modelData.protocol
                channelMapping: modelData.channelMapping
            }
        }
    }
}
