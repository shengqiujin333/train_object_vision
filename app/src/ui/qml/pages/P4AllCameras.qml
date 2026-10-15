import QtQuick 2.15
import ".."
import "../components"

// P4 全摄像头（ITEM-012）：配置驱动六路槽位与顺序；显示位置名/状态/检测职责徽标。
Rectangle {
    id: page
    color: Theme.color.bg_base

    Grid {
        anchors.fill: parent
        anchors.margins: Theme.spacing.s4
        columns: 3
        spacing: Theme.spacing.s3

        Repeater {
            model: railhmiCameras
            CameraTile {
                width: (parent.width - 2 * Theme.spacing.s3) / 3
                height: (parent.height - Theme.spacing.s3) / 2
                title: modelData.name
                state: modelData.state
                badge: modelData.badge
                aiEnabled: modelData.aiEnabled
                showBadge: true
            }
        }
    }
}
