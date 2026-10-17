import QtQuick 2.15
import ".."
import "../components"

// P3 历史回放（ITEM-020 / 交互 §5.3、视觉 §7.7·§8.3）。
// D-01 关键帧缩略图条（桥接导出 keyframes + 缩略图引用，按 ts 定位）；D-02 缺口斜纹；
// D-03 播放/暂停/倍速（PlaybackState）；D-04 报警段/标记按实际级别上色；
// D-05 段/缺口/标记可点选；D-06 完整摄像头选择；D-07 录像不可用详情黄条 + 独立备注；
// D-08 非法日期提示 + 控件 44px 可键盘聚焦；D-09 轨道 32px / 关键帧条 64px / 游标顶部等宽时间。
Rectangle {
    id: page
    color: Theme.color.bg_base

    readonly property var replay: (typeof railhmiReplay !== "undefined") ? railhmiReplay : null
    readonly property var allCams: (typeof railhmiCameras !== "undefined") ? railhmiCameras : []
    property string date: "2026-10-16"
    property string dateError: ""
    property string activeCam: "CAM-01"
    property bool syncOn: true
    property var tl: ({})
    property var thumbs: []
    property var detail: ({})
    property var lockedCams: ["CAM-02", "CAM-01", "CAM-03"]
    property double cursor: 0
    property var syncInfo: ({ available: false, inSync: false, prompt: "同步状态不可用" })
    property var playState: ({ playing: false, speed: 1.0 })
    property bool exportPending: false
    property var exportResult: ({})

    function canExport() {
        return (typeof railhmiPermissions !== "undefined" && railhmiPermissions)
                ? railhmiPermissions.canExportRecording() : false
    }
    function askExport() { page.exportPending = true }
    function confirmExport() {
        if (typeof railhmiPermissions !== "undefined" && railhmiPermissions)
            page.exportResult = railhmiPermissions.exportRecording(page.activeCam, page.cursor, page.cursor + 60000, true)
        page.exportPending = false
    }

    function dayStart() { return tl.dayStartMs !== undefined ? tl.dayStartMs : 0 }
    function dayMs() { return tl.durationMs !== undefined ? tl.durationMs : 86400000 }
    function xOf(ts) { return (ts - dayStart()) / dayMs() * track.width }
    function selectedTs() { return replay ? replay.selectedAlarmTs() : 0 }
    function levelColorOf(lvl) {
        if (lvl === 1) return Theme.alarm.level1.accent
        if (lvl === 2) return Theme.alarm.level2.accent
        if (lvl === 3) return Theme.alarm.level3.accent
        return Theme.color.neutral
    }
    function levelTintOf(lvl) {
        if (lvl === 1) return Theme.alarm.level1.tint_bg
        if (lvl === 2) return Theme.alarm.level2.tint_bg
        if (lvl === 3) return Theme.alarm.level3.tint_bg
        return Theme.color.neutral_bg
    }
    function sampleKeyframes(list) {
        var out = []
        if (!list || list.length === 0) return out
        var step = Math.max(1, Math.ceil(list.length / 48))
        for (var i = 0; i < list.length; i += step) out.push(list[i])
        return out
    }
    // F-01：三联回放画面标题（配置驱动：摄像头 id → 名称；未知回退 id，绝不产生 TypeError）
    function chipName(camId) {
        for (var i = 0; i < allCams.length; ++i)
            if (allCams[i].id === camId && allCams[i].name) return allCams[i].name
        return "" + camId
    }
    // F-02：关键帧缩略图由媒体层生成；缺失或非绝对/资源基址时返回空串，避免相对路径被
    // 解析为 qrc:/qml/pages/... 而反复报 Image 打不开（缺失时显示占位方框）。
    function thumbSource(t) {
        if (!t || !t.thumb) return ""
        var s = "" + t.thumb
        if (s.length === 0) return ""
        if (s.indexOf("image://") === 0 || s.indexOf("file://") === 0 || s.indexOf("qrc:/") === 0) return s
        if (s.charAt(0) === "/") return "file://" + s
        return ""
    }
    function reload() {
        if (!replay) { tl = {}; detail = {}; return }
        var cam = replay.selectedCameraId()
        if (cam && cam.length > 0) activeCam = cam
        lockedCams = replay.relatedCameras()
        detail = replay.selectedDetail()
        var res = replay.timeline(activeCam, date)
        if (res.valid === false) { dateError = "日期无效（YYYY-MM-DD）"; return }   // D-08：不静默
        dateError = ""
        tl = res
        thumbs = sampleKeyframes(tl.keyframes)
        cursor = selectedTs() > 0 ? selectedTs() : dayStart()
        refreshSync()
    }
    function reloadCam(camId) {
        activeCam = camId
        var res = replay.timeline(camId, date)
        if (res.valid === false) { dateError = "日期无效（YYYY-MM-DD）"; return }
        dateError = ""
        tl = res
        thumbs = sampleKeyframes(tl.keyframes)
        refreshSync()
    }
    function refreshSync() {
        if (!replay) return
        syncInfo = replay.syncState(cursor)
    }
    function doJump(which) {
        if (!replay) return
        var base = selectedTs() > 0 ? selectedTs() : cursor
        var j = replay.jumpTargets(base)
        cursor = j[which]
        refreshSync()
    }
    function togglePlay() { if (replay) replay.setPlaying(!page.playState.playing); }
    function setSpeed(s) { if (replay) replay.setSpeed(s); }
    function seekTo(ts) { cursor = ts; refreshSync() }
    function selectAlarmAt(ts) { if (replay) { replay.selectAlarm(page.activeCam, ts) } }

    Component.onCompleted: { if (replay) playState = replay.playback(); reload() }
    Connections {
        target: page.replay
        function onSelectionChanged() { page.reload() }
        function onPlaybackChanged() { page.playState = page.replay.playback() }
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacing.s3
        spacing: Theme.spacing.s2

        // ---- 顶部：日期/完整摄像头选择/同步开关 + 来源标注 ----
        Row {
            spacing: Theme.spacing.s2
            Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_secondary
                   font.pixelSize: Theme.typography.scale.caption.size; text: "日期" }
            Rectangle {
                width: 120; height: Theme.size.touch_target_min; color: Theme.color.bg_sunken
                border.color: page.dateError.length > 0 ? Theme.color.error : Theme.color.border
                border.width: Theme.border.width; radius: Theme.radius.chip
                TextInput {
                    id: dateInput
                    anchors.fill: parent; anchors.leftMargin: Theme.spacing.s2
                    verticalAlignment: TextInput.AlignVCenter; color: Theme.color.text_primary
                    font.pixelSize: Theme.typography.scale.caption.size; text: page.date
                    onEditingFinished: { page.date = text; page.reload() }
                }
            }
            Text { visible: page.dateError.length > 0; anchors.verticalCenter: parent.verticalCenter
                   color: Theme.color.error; font.pixelSize: Theme.typography.scale.caption.size
                   text: page.dateError }
            // D-06：完整摄像头选择（6 路）；relatedCameras 仅作进入默认锁定
            Repeater {
                model: page.allCams
                FilterChip {
                    text: modelData.name
                    selected: page.activeCam === modelData.id
                    onClicked: page.reloadCam(modelData.id)
                }
            }
            FilterChip { text: "三联同步"; selected: page.syncOn; onClicked: page.syncOn = !page.syncOn }
            // 受限入口不渲染：司机无导出权限时不出现该控件（ITEM-023）
            FilterChip { objectName: "exportRecording"; visible: page.canExport(); text: "导出录像"
                         onClicked: page.askExport() }
        }
        Row {
            spacing: Theme.spacing.s2
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: page.replay ? !page.replay.sourceStatus().usable || page.replay.sourceStatus().note.length > 0 : false
                color: Theme.color.degraded
                font.pixelSize: Theme.typography.scale.caption.size
                text: page.replay ? ("来源：" + page.replay.sourceStatus().label +
                                (page.replay.sourceStatus().note.length > 0 ? "（" + page.replay.sourceStatus().note + "）" : "")) : ""
            }
            Text {
                objectName: "syncStatusText"
                anchors.verticalCenter: parent.verticalCenter
                visible: page.syncInfo.available === false || page.syncInfo.inSync === false
                color: page.syncInfo.available === false ? Theme.color.text_muted : Theme.color.error
                font.pixelSize: Theme.typography.scale.caption.size
                text: page.syncInfo.available === false ? "同步状态不可用" : page.syncInfo.prompt
            }
        }

        // ---- 中部：回放画面（三联，锁定相关摄像头） ----
        Row {
            id: videoRow
            width: parent.width
            height: parent.height * 0.36
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
                           font.family: Theme.typography.family.mono
                           text: page.replay ? page.replay.formatTime(page.cursor) : "" }
                }
            }
        }

        // ---- 下部：时间轴 + 关键帧 + 播放控制 ----
        Item {
            id: timelineArea
            width: parent.width
            height: parent.height - videoRow.height - Theme.spacing.s4

            // D-09 / v2 D-02：游标顶部等宽时间，随游标 x 移动
            Text {
                id: cursorTime
                objectName: "cursorTime"
                x: Math.max(0, Math.min(parent.width - width, page.xOf(page.cursor) - width / 2))
                anchors.top: parent.top
                color: Theme.color.text_primary
                font.family: Theme.typography.family.mono
                font.pixelSize: Theme.typography.scale.caption.size
                text: "当前 " + (page.replay ? page.replay.formatTime(page.cursor) : "")
            }
            Rectangle {
                id: track
                objectName: "timelineTrack"
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: cursorTime.bottom
                anchors.topMargin: Theme.spacing.s1
                height: Theme.size.button.sm   // §7.7 轨道 32px
                color: Theme.color.surface
                border.color: Theme.color.border; border.width: Theme.border.width

                // 连续/报警区段（报警段带实际级别色 + tint，可点选）
                Repeater {
                    model: page.tl.segments !== undefined ? page.tl.segments : []
                    Rectangle {
                        objectName: modelData.kind === "critical" ? "alarmSegment" : "continuousSegment"
                        x: page.xOf(modelData.t0); width: Math.max(1, page.xOf(modelData.t1) - page.xOf(modelData.t0))
                        height: parent.height
                        color: modelData.kind === "critical" ? page.levelTintOf(modelData.level) : Theme.color.neutral_bg
                        border.color: modelData.kind === "critical" ? page.levelColorOf(modelData.level) : Theme.color.border
                        border.width: modelData.kind === "critical" ? 3 : Theme.border.width
                        MouseArea { anchors.fill: parent; onClicked: page.seekTo(modelData.t0) }
                    }
                }
                // 缺口（D-02 斜纹留白 + “录像缺失”，可点选）
                Repeater {
                    model: page.tl.gaps !== undefined ? page.tl.gaps : []
                    Rectangle {
                        objectName: "timelineGap"
                        x: page.xOf(modelData.t0); width: Math.max(1, page.xOf(modelData.t1) - page.xOf(modelData.t0))
                        height: parent.height
                        color: Theme.color.bg_base
                        border.color: Theme.color.degraded; border.width: Theme.border.width
                        Canvas {
                            anchors.fill: parent
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            onPaint: {
                                var ctx = getContext("2d"); ctx.reset();
                                ctx.strokeStyle = Theme.color.degraded; ctx.lineWidth = 1;
                                for (var x = -height; x < width; x += 8) {
                                    ctx.beginPath(); ctx.moveTo(x, height); ctx.lineTo(x + height, 0); ctx.stroke();
                                }
                            }
                        }
                        Text { anchors.centerIn: parent; color: Theme.color.degraded
                               font.pixelSize: Theme.typography.scale.caption.size; text: "录像缺失" }
                        MouseArea { anchors.fill: parent; acceptedButtons: Qt.LeftButton
                                    onClicked: page.seekTo(modelData.t0) }
                    }
                }
                // 报警标记（实际级别色，可点选 -> 选中该报警）
                Repeater {
                    model: page.tl.alarms !== undefined ? page.tl.alarms : []
                    Rectangle {
                        objectName: "alarmMarker"
                        x: page.xOf(modelData.ts); width: 3; height: parent.height
                        color: page.levelColorOf(modelData.level)
                    }
                }
                Repeater {
                    model: page.tl.alarms !== undefined ? page.tl.alarms : []
                    Item {
                        x: page.xOf(modelData.ts) - 6; width: 12; height: parent.height
                        MouseArea { anchors.fill: parent; onClicked: page.selectAlarmAt(modelData.ts) }
                    }
                }
                // 游标（2px 白线）
                Rectangle {
                    objectName: "timelineCursor"
                    x: page.xOf(page.cursor); width: 2; height: parent.height; color: Theme.color.text_primary
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: page.seekTo(page.dayStart() + mouseX / track.width * page.dayMs())
                }
            }

            // 关键帧缩略图条（D-01：64px，高，按 ts 定位，缩略图引用来自媒体层）
            Rectangle {
                id: keyStrip
                objectName: "keyframeStrip"
                anchors.top: track.bottom; anchors.topMargin: Theme.spacing.s1
                anchors.left: parent.left; anchors.right: parent.right
                height: Theme.size.status_strip_height   // §7.7 关键帧条 64px
                color: Theme.color.bg_sunken
                border.color: Theme.color.border; border.width: Theme.border.width
                Repeater {
                    model: page.thumbs
                    Rectangle {
                        objectName: "keyframeThumb"
                        x: page.xOf(modelData.ts); width: 24; height: parent.height
                        color: Theme.color.surface_hover
                        border.color: Theme.color.border; border.width: 1
                        Image { anchors.fill: parent; source: page.thumbSource(modelData)
                                visible: page.thumbSource(modelData).length > 0
                                fillMode: Image.PreserveAspectFit }
                        Text { anchors.centerIn: parent; visible: page.thumbSource(modelData).length === 0
                               color: Theme.color.text_muted; font.pixelSize: Theme.typography.scale.caption.size
                               text: "—" }
                        MouseArea { anchors.fill: parent; onClicked: page.seekTo(modelData.ts) }
                    }
                }
            }

            // 播放控制（D-03 播放/暂停 + 倍速 + 四跳；触控 ≥44px）
            Row {
                anchors.top: keyStrip.bottom; anchors.topMargin: Theme.spacing.s2; spacing: Theme.spacing.s2
                FilterChip { objectName: "playbackToggle"; text: page.playState.playing ? "暂停" : "播放"
                             onClicked: page.togglePlay() }
                FilterChip { objectName: "speed-0.5"; text: "0.5×"; selected: page.playState.speed === 0.5
                             onClicked: page.setSpeed(0.5) }
                FilterChip { objectName: "speed-1"; text: "1×"; selected: page.playState.speed === 1.0
                             onClicked: page.setSpeed(1.0) }
                FilterChip { objectName: "speed-2"; text: "2×"; selected: page.playState.speed === 2.0
                             onClicked: page.setSpeed(2.0) }
                FilterChip { text: "-30s"; onClicked: page.doJump("minus30") }
                FilterChip { text: "报警时刻"; onClicked: page.doJump("alarm") }
                FilterChip { text: "+30s"; onClicked: page.doJump("plus30") }
                FilterChip { text: "+60s"; onClicked: page.doJump("plus60") }
            }
        }
    }

    // 导出录像二次确认模态（ITEM-023）
    Rectangle {
        objectName: "exportConfirmModal"
        anchors.fill: parent
        visible: page.exportPending
        color: Theme.color.overlay_scrim
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.36; height: 180
            color: Theme.color.surface_elev
            border.color: Theme.color.border_strong; border.width: Theme.border.width
            radius: Theme.radius.modal
            Column {
                anchors.centerIn: parent; spacing: Theme.spacing.s3
                Text { anchors.horizontalCenter: parent.horizontalCenter; color: Theme.color.text_primary
                       font.pixelSize: Theme.typography.scale.body.size
                       text: "确认导出录像？" + page.activeCam }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter; spacing: Theme.spacing.s3
                    FilterChip { objectName: "exportConfirmYes"; text: "确认"; onClicked: page.confirmExport() }
                    FilterChip { objectName: "exportConfirmNo"; text: "取消"; onClicked: page.exportPending = false }
                }
            }
        }
    }

    // ---- 右侧：事件详情栏（按选中报警动态生成；D-07 录像黄条 + 独立备注） ----
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
            // 录像不可用/不完整 -> 显著黄条（PLY-008 / §8.3）
            Rectangle {
                objectName: "detailRecordBar"
                visible: page.detail.recordOk === false && page.detail.recordLabel !== undefined
                width: parent.width
                height: Theme.size.button.sm
                radius: Theme.radius.chip
                color: Theme.color.degraded_bg
                border.color: Theme.color.degraded; border.width: Theme.border.width
                Text { anchors.centerIn: parent; color: Theme.color.degraded
                       font.pixelSize: Theme.typography.scale.caption.size
                       text: page.detail.recordLabel !== undefined ? page.detail.recordLabel : "录像不可用" }
            }
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
                   text: "备注：" + (page.detail.note !== undefined && page.detail.note.length > 0 ? page.detail.note : "—") }
        }
    }
}
