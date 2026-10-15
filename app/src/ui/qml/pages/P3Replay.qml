import QtQuick 2.15
import ".."
import "../components"

// P3 历史回放（ITEM-020 / 交互 §5.3、视觉 §8.3）。
// 修复 D-01：从报警卡进入 -> selectAlarm(cameraId, ts) 锁定相关摄像头并定位报警时刻，详情动态生成。
// 修复 D-02：三联同步由各路真实回放时钟偏移构建；无时钟时显示“同步状态不可用”，不显示恒真在同步。
// 上：日期/摄像头 + 三联同步开关；中：回放画面（前方报警默认左/前/右）；下：时间轴
// （连续段/报警区段显著色标/缺口/游标）+ 关键帧 + 播放控制（-30s/报警时刻/+30s/+60s）；
// 右：事件详情栏。NVR 不可用时显式标注并回放本机片段。
Rectangle {
    id: page
    color: Theme.color.bg_base

    readonly property var R: (typeof railhmiReplay !== "undefined") ? railhmiReplay : null
    property string date: "2026-10-16"
    property string activeCam: "CAM-01"
    property bool syncOn: true
    property var tl: ({})
    property var detail: ({})
    property var lockedCams: ["CAM-02", "CAM-01", "CAM-03"]
    property double cursor: 0
    property var syncInfo: ({ available: false, inSync: false, prompt: "同步状态不可用" })

    function dayStart() { return tl.dayStartMs !== undefined ? tl.dayStartMs : 0 }
    function dayMs() { return tl.durationMs !== undefined ? tl.durationMs : 86400000 }
    function xOf(ts) { return (ts - dayStart()) / dayMs() * track.width }
    function selectedTs() { return R ? R.selectedAlarmTs() : 0 }
    function reload() {
        if (!R) { tl = {}; detail = {}; return }
        var cam = R.selectedCameraId()
        if (cam && cam.length > 0) activeCam = cam
        lockedCams = R.relatedCameras()
        detail = R.selectedDetail()
        tl = R.timeline(activeCam, date)
        cursor = selectedTs() > 0 ? selectedTs() : dayStart()
        refreshSync()
    }
    function refreshSync() {
        if (!R) return
        // 由各路真实回放时钟偏移构建；无时钟 -> available=false（显示“同步状态不可用”）
        syncInfo = R.syncState(cursor)
    }
    function doJump(which) {
        if (!R) return
        var base = selectedTs() > 0 ? selectedTs() : cursor
        var j = R.jumpTargets(base)
        cursor = j[which]
        refreshSync()
    }
    function levelColor(l) {
        if (l === "L1") return Theme.alarm.L1.accent
        if (l === "L2") return Theme.alarm.L2.accent
        return Theme.alarm.L3.accent
    }
    function chipName(camId) {
        if (camId === "CAM-01") return "前方"
        if (camId === "CAM-02") return "左侧"
        if (camId === "CAM-03") return "右侧"
        return camId
    }

    Component.onCompleted: reload()
    Connections {
        target: page.R
        function onSelectionChanged() { page.reload() }
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacing.s3
        spacing: Theme.spacing.s2

        // ---- 顶部：日期/摄像头/同步开关 + 来源标注 ----
        Row {
            spacing: Theme.spacing.s2
            Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_secondary
                   font.pixelSize: Theme.typography.scale.caption.size; text: "日期" }
            Rectangle {
                width: 120; height: Theme.size.button.sm; color: Theme.color.bg_sunken
                border.color: Theme.color.border; border.width: Theme.border.width; radius: Theme.radius.chip
                TextInput {
                    id: dateInput
                    anchors.fill: parent; anchors.leftMargin: Theme.spacing.s2
                    verticalAlignment: TextInput.AlignVCenter; color: Theme.color.text_primary
                    font.pixelSize: Theme.typography.scale.caption.size; text: page.date
                    onEditingFinished: { page.date = text; page.reload() }
                }
            }
            Repeater {
                model: page.lockedCams
                FilterChip {
                    text: page.chipName(modelData)
                    selected: page.activeCam === modelData
                    onClicked: { page.activeCam = modelData; page.tl = page.R.timeline(modelData, page.date); page.refreshSync() }
                }
            }
            FilterChip { text: "三联同步"; selected: page.syncOn; onClicked: page.syncOn = !page.syncOn }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: page.R ? !page.R.sourceStatus().usable || page.R.sourceStatus().note.length > 0 : false
                color: Theme.color.degraded
                font.pixelSize: Theme.typography.scale.caption.size
                text: page.R ? ("来源：" + page.R.sourceStatus().label +
                                (page.R.sourceStatus().note.length > 0 ? "（" + page.R.sourceStatus().note + "）" : "")) : ""
            }
        }
        // D-02：同步状态不可用 或 超阈值偏差提示（均非恒真在同步）
        Text {
            objectName: "syncStatusText"
            visible: page.syncInfo.available === false || page.syncInfo.inSync === false
            color: page.syncInfo.available === false ? Theme.color.text_muted : Theme.color.error
            font.pixelSize: Theme.typography.scale.caption.size
            text: page.syncInfo.available === false ? "同步状态不可用" : page.syncInfo.prompt
        }

        // ---- 中部：回放画面（三联，锁定相关摄像头） ----
        Row {
            id: videoRow
            width: parent.width
            height: parent.height * 0.42
            spacing: Theme.spacing.s2
            Repeater {
                model: page.syncOn ? page.lockedCams : [page.activeCam]
                Rectangle {
                    objectName: "replayPanel-" + modelData
                    width: (videoRow.width - 2 * Theme.spacing.s2) / 3
                    height: videoRow.height
                    color: Theme.color.bg_sunken
                    border.color: Theme.color.border; border.width: Theme.border.width; radius: Theme.radius.card
                    Text { anchors.top: parent.top; anchors.left: parent.left; anchors.margins: Theme.spacing.s2
                           color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.label.size
                           text: page.chipName(modelData) + " (" + modelData + ")" }
                    Text { anchors.centerIn: parent; color: Theme.color.text_muted
                           font.pixelSize: Theme.typography.scale.caption.size
                           text: "回放画面（媒体层提供）" }
                    Text { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: Theme.spacing.s2
                           color: Theme.color.text_secondary; font.pixelSize: Theme.typography.scale.caption.size
                           text: page.R ? page.R.formatTime(page.cursor) : "" }
                }
            }
        }

        // ---- 下部：时间轴 + 关键帧 + 播放控制 ----
        Item {
            id: timelineArea
            width: parent.width
            height: parent.height - videoRow.height - Theme.spacing.s4

            Rectangle {
                id: track
                objectName: "timelineTrack"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                height: Theme.size.button.md
                color: Theme.color.surface
                border.color: Theme.color.border; border.width: Theme.border.width

                Repeater {
                    model: page.tl.segments !== undefined ? page.tl.segments : []
                    Rectangle {
                        objectName: modelData.kind === "critical" ? "alarmSegment" : "continuousSegment"
                        x: page.xOf(modelData.t0); width: Math.max(1, page.xOf(modelData.t1) - page.xOf(modelData.t0))
                        height: parent.height
                        color: modelData.kind === "critical" ? Theme.color.degraded_bg : Theme.color.neutral_bg
                        border.color: modelData.kind === "critical" ? page.levelColor("L2") : Theme.color.border
                        border.width: modelData.kind === "critical" ? 3 : Theme.border.width
                    }
                }
                Repeater {
                    model: page.tl.gaps !== undefined ? page.tl.gaps : []
                    Rectangle {
                        objectName: "timelineGap"
                        x: page.xOf(modelData.t0); width: Math.max(1, page.xOf(modelData.t1) - page.xOf(modelData.t0))
                        height: parent.height
                        color: Theme.color.bg_base
                        border.color: Theme.color.degraded; border.width: Theme.border.width
                        Text { anchors.centerIn: parent; color: Theme.color.degraded
                               font.pixelSize: Theme.typography.scale.caption.size; text: "录像缺失" }
                    }
                }
                Repeater {
                    model: page.tl.alarms !== undefined ? page.tl.alarms : []
                    Rectangle {
                        objectName: "alarmMarker"
                        x: page.xOf(modelData); width: 3; height: parent.height; color: page.levelColor("L1")
                    }
                }
                Rectangle {
                    objectName: "timelineCursor"
                    x: page.xOf(page.cursor); width: 2; height: parent.height; color: Theme.color.text_primary
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: { page.cursor = page.dayStart() + mouseX / track.width * page.dayMs(); page.refreshSync() }
                }
            }

            Rectangle {
                id: keyStrip
                objectName: "keyframeStrip"
                anchors.top: track.bottom; anchors.left: parent.left; anchors.right: parent.right
                height: Theme.size.button.md
                color: Theme.color.bg_sunken
                border.color: Theme.color.border; border.width: Theme.border.width
                Repeater {
                    model: page.tl.keyframes !== undefined ? Math.min(48, page.tl.keyframes.length) : 0
                    Rectangle {
                        x: index * (keyStrip.width / 48); width: 1; height: parent.height
                        color: Theme.color.border
                    }
                }
            }

            Row {
                anchors.top: keyStrip.bottom; anchors.topMargin: Theme.spacing.s2; spacing: Theme.spacing.s2
                FilterChip { text: "-30s"; onClicked: page.doJump("minus30") }
                FilterChip { text: "报警时刻"; onClicked: page.doJump("alarm") }
                FilterChip { text: "+30s"; onClicked: page.doJump("plus30") }
                FilterChip { text: "+60s"; onClicked: page.doJump("plus60") }
                Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_secondary
                       font.pixelSize: Theme.typography.scale.caption.size
                       text: page.R ? ("游标 " + page.R.formatTime(page.cursor)) : "" }
            }
        }
    }

    // ---- 右侧：事件详情栏（按选中报警动态生成） ----
    Rectangle {
        objectName: "replayDetail"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: parent.bottom
        width: parent.width * 0.22
        color: Theme.color.surface
        border.color: Theme.color.border; border.width: Theme.border.width
        Column {
            anchors.fill: parent; anchors.margins: Theme.spacing.s3; spacing: Theme.spacing.s1
            Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.label.size; text: "事件详情" }
            Text { color: Theme.color.text_secondary; font.pixelSize: Theme.typography.scale.caption.size
                   text: "时间：" + (page.detail.timeText !== undefined ? page.detail.timeText : "—") }
            Text { color: Theme.color.text_secondary; font.pixelSize: Theme.typography.scale.caption.size
                   text: "类型：" + (page.detail.typeLabel !== undefined ? page.detail.typeLabel : "—") }
            Text { color: Theme.color.text_secondary; font.pixelSize: Theme.typography.scale.caption.size
                   text: "级别：" + (page.detail.levelLabel !== undefined ? page.detail.levelLabel : "—") }
            Text { color: Theme.color.text_secondary; font.pixelSize: Theme.typography.scale.caption.size
                   text: "距离：" + (page.detail.distanceText !== undefined ? page.detail.distanceText : "—") }
            Text { color: Theme.color.text_secondary; font.pixelSize: Theme.typography.scale.caption.size
                   text: "置信度：" + (page.detail.confidenceText !== undefined ? page.detail.confidenceText : "—") }
            Text { color: Theme.color.text_secondary; font.pixelSize: Theme.typography.scale.caption.size
                   text: "处理状态：" + (page.detail.ackLabel !== undefined ? page.detail.ackLabel : "—") }
            Text { color: Theme.color.text_muted; font.pixelSize: Theme.typography.scale.caption.size
                   text: "备注：" + (page.detail.note !== undefined ? page.detail.note : "—") }
        }
    }
}
