import QtQuick 2.15
import ".."
import "../components"

// P1 实时监控（ITEM-011/012；ITEM-014 三联布局 + P0 元素）。
// 布局比例与高度来自 railhmi-core compute_layout（单一来源）：左-前-右、前方最大。
// P0 元素：顶部状态条（外壳）、三联、关键状态区、当前报警条 —— 一屏内不滚动即可见。
// 司机页不显示检测职责徽标。
Rectangle {
    id: page
    color: Theme.color.bg_base

    // 关键状态区异常项点击：冒泡给外壳（允许则跳转详情，不允许则显示无权限）。
    signal statusNavigate(string pageId, bool allowed)

    readonly property var L: (typeof railhmiLayout !== "undefined") ? railhmiLayout : ({
            leftRatio: 0.26, frontRatio: 0.48, rightRatio: 0.26,
            statusStripHeight: 64, alarmBarHeight: 64, marginNarrow: 16, marginWide: 24
        })
    readonly property int gap: Theme.spacing.s3
    readonly property int margin: (page.height >= 1080) ? page.L.marginWide : page.L.marginNarrow

    function camByPosition(pos) {
        for (var i = 0; i < railhmiCameras.length; ++i)
            if (railhmiCameras[i].position === pos) return railhmiCameras[i]
        return null
    }
    function camName(pos, fallback) {
        var c = page.camByPosition(pos)
        return c ? c.name : fallback
    }
    function camState(pos) {
        var c = page.camByPosition(pos)
        return c ? c.state : "连接中"
    }

    // ---- 当前报警条（P0，底部固定） ----
    Rectangle {
        id: alarmBar
        objectName: "alarmBar"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: page.L.alarmBarHeight
        color: Theme.color.bg_sunken
        border.color: Theme.color.border
        border.width: Theme.border.width

        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: Theme.spacing.s3
            color: Theme.color.text_secondary
            font.pixelSize: Theme.typography.scale.body.size
            text: "当前报警：无"
        }
    }

    // ---- 关键状态区（P0，报警条之上） ----
    Rectangle {
        id: statusStrip
        objectName: "statusStrip"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: alarmBar.top
        anchors.margins: page.margin
        height: page.L.statusStripHeight
        color: Theme.color.bg_sunken
        border.color: Theme.color.border
        border.width: Theme.border.width

        KeyStatusStrip {
            id: keyStrip
            objectName: "keyStatusStrip"
            anchors.fill: parent
            onNavigateRequested: page.statusNavigate(pageId, allowed)
        }
    }

    // ---- 三联行（P0，左-前-右，前方最大） ----
    Item {
        id: row
        objectName: "threePanelRow"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: page.margin
        anchors.bottom: statusStrip.top
        anchors.bottomMargin: page.margin

        Row {
            anchors.fill: parent
            spacing: page.gap

            CameraTile {
                objectName: "tileLeft"
                width: (row.width - 2 * page.gap) * page.L.leftRatio
                height: row.height
                title: page.camName("left", "左侧")
                state: page.camState("left")
                live: false
                showBadge: false
            }
            CameraTile {
                objectName: "tileFront"
                width: (row.width - 2 * page.gap) * page.L.frontRatio
                height: row.height
                title: page.camName("primary", "前方")
                state: page.camState("primary")
                live: false
                showBadge: false
            }
            CameraTile {
                objectName: "tileRight"
                width: (row.width - 2 * page.gap) * page.L.rightRatio
                height: row.height
                title: page.camName("right", "右侧")
                state: page.camState("right")
                live: false
                showBadge: false
            }
        }
    }
}
