import QtQuick 2.15
import ".."
import "../components"

// P4 全摄像头（ITEM-012 槽位/顺序/徽标；ITEM-015 六路并发 3x2 / 2x3 网格）。
// 每格显示：摄像头名、连接状态角标（中文标签）、实时 fps、最近一帧时间、检测职责徽标。
// 槽位由 railhmiChannels（固定注册表顺序）决定：断流/冻结只改变该格内容，不重排、不丢格。
// 形状：宽 >= 高 用 3x2，否则 2x3（railhmiGrid），随窗口尺寸切换。
Rectangle {
    id: page
    color: Theme.color.bg_base

    readonly property var gridCfg: (typeof railhmiGrid !== "undefined") ? railhmiGrid : ({
            wideColumns: 3, wideRows: 2, tallColumns: 2, tallRows: 3
        })
    readonly property int cols: (width >= height) ? page.gridCfg.wideColumns : page.gridCfg.tallColumns
    readonly property int rows: (width >= height) ? page.gridCfg.wideRows : page.gridCfg.tallRows
    readonly property int gap: Theme.spacing.s3

    Grid {
        id: grid
        anchors.fill: parent
        anchors.margins: Theme.spacing.s4
        columns: page.cols
        spacing: page.gap

        Repeater {
            model: railhmiChannels
            CameraTile {
                readonly property var channelInfo: modelData.info
                channel: modelData
                objectName: "gridCell-" + channelInfo.id
                width: (grid.width - (page.cols - 1) * page.gap) / page.cols
                height: (grid.height - (page.rows - 1) * page.gap) / page.rows
                title: channelInfo.name
                state: channelInfo.stateLabel
                badge: channelInfo.badge
                aiEnabled: channelInfo.aiEnabled
                showBadge: true
                live: channelInfo.presentable
                detail: channelInfo.detail
                fpsText: channelInfo.fpsText
                lastFrameText: channelInfo.lastFrameText
                reconnectText: channelInfo.reconnectText
            }
        }
    }
}
