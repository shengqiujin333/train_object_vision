import QtQuick 2.15
import ".."
import "../components"

// P1 实时监控（ITEM-011/012；ITEM-014 三联布局 + P0 元素）。
// 布局比例与高度来自 railhmi-core compute_layout（单一来源）：左-前-右、前方最大。
// P0 元素：顶部状态条（外壳）、三联、当前报警 —— 一屏内不滚动即可见。
// 现场调整：删除与外壳顶部状态条重复的关键状态区（受电弓/轮轨/车门/摄像头/AI/录像/存储）；
// 释放的垂直高度分给主视频区与底部行，避免画面上下出现大片留白。
// 底部两格较小的「受电弓 / 轮轨」实时画面，当前报警收进两格后面的框里，
// 不再独占一整条通栏。五格画面均可点击弹出单路放大浮层。
// 司机页不显示检测职责徽标。
Rectangle {
    id: page
    color: Theme.color.bg_base

    // 摄像头格点击：把该路 channel 交给外壳的单路放大浮层。
    signal zoomRequested(var channel, string name, string stateText, string detailText, bool isLive)

    readonly property var layout: (typeof railhmiLayout !== "undefined") ? railhmiLayout : ({
            leftRatio: 0.26, frontRatio: 0.48, rightRatio: 0.26,
            alarmBarHeight: 64, marginNarrow: 16, marginWide: 24
        })
    readonly property int gap: Theme.spacing.s3
    readonly property int margin: (page.height >= 1080) ? page.layout.marginWide : page.layout.marginNarrow
    // 底部行高度：两格按可用宽度三等分、由 16:9 反推高度，同时不超过屏高 30%。
    // 关键状态行删掉后由底部行吸收多余高度，主三联仍严格 16:9，不产生上下大片留白。
    readonly property real bottomTileW: (page.width - page.margin * 2 - page.gap * 2) / 3
    readonly property real bottomRowHeight: Math.max(page.layout.alarmBarHeight,
                                                      Math.min(page.bottomTileW * 9 / 16,
                                                               page.height * 0.30))

    function camByPosition(pos) {
        for (var i = 0; i < railhmiChannels.length; ++i)
            if (railhmiChannels[i].info.position === pos) return railhmiChannels[i]
        return null
    }
    function camName(pos, fallback) {
        var c = page.camByPosition(pos)
        return c ? c.info.name : fallback
    }
    function camState(pos) {
        var c = page.camByPosition(pos)
        return c ? c.info.stateLabel : "未配置"
    }
    function openZoom(pos, fallback) {
        var c = page.camByPosition(pos)
        if (!c) return
        page.zoomRequested(c, page.camName(pos, fallback), c.info.stateLabel,
                           c.info.detail, c.info.presentable)
    }

    // ---- 底部行：受电弓 / 轮轨 两个较小画面 + 其后的「当前报警」框 ----
    Item {
        id: bottomRow
        objectName: "bottomRow"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: page.margin
        height: page.bottomRowHeight

        // 两格保持 16:9（比前方主画面小），剩余宽度全部给报警框。
        readonly property real tileH: bottomRow.height
        readonly property real tileW: Math.min(bottomRow.tileH * 16 / 9,
                                               (bottomRow.width - page.gap * 2) / 3)
        readonly property real alarmW: Math.max(0, bottomRow.width
                                                  - bottomRow.tileW * 2 - page.gap * 2)

        CameraTile {
            objectName: "tilePantograph"
            x: 0
            y: 0
            width: bottomRow.tileW
            height: bottomRow.tileH
            title: page.camName("roof", "受电弓")
            state: page.camState("roof")
            channel: page.camByPosition("roof")
            live: channel ? channel.info.presentable : false
            detail: channel ? channel.info.detail : ""
            showBadge: false
            fillMode: 1
            onActivated: page.openZoom("roof", "受电弓")
        }
        CameraTile {
            objectName: "tileWheelRail"
            x: bottomRow.tileW + page.gap
            y: 0
            width: bottomRow.tileW
            height: bottomRow.tileH
            title: page.camName("underfloor", "轮轨")
            state: page.camState("underfloor")
            channel: page.camByPosition("underfloor")
            live: channel ? channel.info.presentable : false
            detail: channel ? channel.info.detail : ""
            showBadge: false
            fillMode: 1
            onActivated: page.openZoom("underfloor", "轮轨")
        }

        // 「当前报警」收进受电弓/轮轨后面的框里；门磁未关严为真实 L2 报警来源。
        // 未接入/不可用状态不写进报警条（由顶栏与关键状态区如实呈现）。
        Rectangle {
            id: alarmBar
            objectName: "alarmBar"
            x: bottomRow.tileW * 2 + page.gap * 2
            y: 0
            width: bottomRow.alarmW
            height: bottomRow.tileH
            color: Theme.color.bg_sunken
            border.color: Theme.color.border
            border.width: Theme.border.width
            radius: Theme.radius.card

            readonly property string doorAlarm: (typeof railhmiDoorAlarmText !== "undefined")
                                                ? railhmiDoorAlarmText : ""
            Column {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.margins: Theme.spacing.s4
                spacing: Theme.spacing.s2
                Text {
                    color: Theme.color.text_secondary
                    font.pixelSize: Theme.typography.scale.label.size
                    text: "当前报警"
                }
                Text {
                    objectName: "alarmBarText"
                    width: parent.width
                    wrapMode: Text.Wrap
                    color: alarmBar.doorAlarm.length > 0 ? Theme.color.error : Theme.color.text_primary
                    font.pixelSize: Theme.typography.scale.body.size
                    text: alarmBar.doorAlarm.length > 0 ? alarmBar.doorAlarm : "无"
                }
            }
        }
    }

    // 关键状态区已按现场需求删除（与外壳顶部状态条重复），视频区直接落到底部行之上。

    // ---- 视频区（P0，前方优先；三格严格 16:9，消除上下大片黑边） ----
    // 几何推导（源画面 16:9，g = 间距）：
    //   frontH = 2 * sideH + g；frontW = frontH * 16/9；sideW = sideH * 16/9；
    //   frontW + g + sideW = row.width  => sideH = ((row.width - g) * 9/16 - g) / 3
    // 高度不足时整体等比缩小并居中：任意屏幕比例下三格仍严格 16:9，不产生黑边。
    Item {
        id: row
        objectName: "threePanelRow"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.topMargin: page.margin
        anchors.bottom: bottomRow.top
        anchors.bottomMargin: page.margin

        readonly property real g: page.gap
        readonly property real sideH0: Math.max(0, (((row.width - g) * 9 / 16) - g) / 3)
        readonly property real frontH0: 2 * sideH0 + g
        readonly property real k: Math.min(1, row.height / Math.max(1, frontH0))
        readonly property real sideH: sideH0 * k
        readonly property real frontH: frontH0 * k
        readonly property real frontW: frontH * 16 / 9
        readonly property real sideW: sideH * 16 / 9
        readonly property real contentW: frontW + g + sideW

        Item {
            id: videoBlock
            width: row.contentW
            height: row.frontH
            anchors.centerIn: parent

            CameraTile {
                objectName: "tileFront"
                x: 0
                y: 0
                width: row.frontW
                height: row.frontH
                title: page.camName("primary", "前方")
                state: page.camState("primary")
                channel: page.camByPosition("primary")
                live: channel ? channel.info.presentable : false
                detail: channel ? channel.info.detail : ""
                fpsText: channel ? channel.info.fpsText : ""
                lastFrameText: channel ? channel.info.lastFrameText : ""
                reconnectText: channel ? channel.info.reconnectText : ""
                showBadge: false
                fillMode: 1
                onActivated: page.openZoom("primary", "前方")
            }
            CameraTile {
                objectName: "tileLeft"
                x: row.frontW + row.g
                y: 0
                width: row.sideW
                height: row.sideH
                title: page.camName("left", "左侧")
                state: page.camState("left")
                channel: page.camByPosition("left")
                live: channel ? channel.info.presentable : false
                detail: channel ? channel.info.detail : ""
                fpsText: channel ? channel.info.fpsText : ""
                lastFrameText: channel ? channel.info.lastFrameText : ""
                reconnectText: channel ? channel.info.reconnectText : ""
                showBadge: false
                fillMode: 1
                onActivated: page.openZoom("left", "左侧")
            }
            CameraTile {
                objectName: "tileRight"
                x: row.frontW + row.g
                y: row.sideH + row.g
                width: row.sideW
                height: row.sideH
                title: page.camName("right", "右侧")
                state: page.camState("right")
                channel: page.camByPosition("right")
                live: channel ? channel.info.presentable : false
                detail: channel ? channel.info.detail : ""
                fpsText: channel ? channel.info.fpsText : ""
                lastFrameText: channel ? channel.info.lastFrameText : ""
                reconnectText: channel ? channel.info.reconnectText : ""
                showBadge: false
                fillMode: 1
                onActivated: page.openZoom("right", "右侧")
            }
        }
    }
}
