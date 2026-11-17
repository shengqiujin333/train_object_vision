import QtQuick 2.15
import Railhmi.Video 1.0
import ".."
import "../components"

// P3 历史回放（ITEM-020 / 交互 §5.3、视觉 §7.7·§8.3）。
// 现场定稿：账号/密码/通道固定并自动登录；日期后提供可见「查询」按钮；
// 去掉未实现的倍速与导出，保留已实现的四跳转。
// 现场二轮：右侧「时间/类型/级别/距离/置信度/处理状态/备注」等无用字段栏整栏删除，
// 画面改成可用区域内尽可能大的严格 16:9 + Cover（不再有巨大黑边），
// 关键帧条只在真有缩略图时出现，底部大片空白压缩掉。
// D-01 关键帧缩略图条；D-02 缺口斜纹；D-04 报警段/标记按实际级别上色；
// D-05 段/缺口/标记可点选；D-06 完整摄像头选择；D-07 录像不可用详情黄条；
// D-08 非法日期提示 + 控件 44px 触控；D-09 轨道 32px / 关键帧条 64px / 游标顶部等宽时间。
Rectangle {
    id: page
    color: Theme.color.bg_base

    readonly property var replay: (typeof railhmiReplay !== "undefined") ? railhmiReplay : null
    readonly property var allCams: (typeof railhmiCameras !== "undefined") ? railhmiCameras : []
    property string date: Qt.formatDate(new Date(), "yyyy-MM-dd")
    readonly property bool nvrMode: replay ? replay.nvrEnabled() : false
    property var nvrInfo: ({ busy: false, message: "", channels: [], decoderState: "stopped" })
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
    // 三联回放画面标题（配置驱动：摄像头 id → 名称；未知回退 id，绝不产生 TypeError）
    function chipName(camId) {
        for (var i = 0; i < allCams.length; ++i)
            if (allCams[i].id === camId && allCams[i].name) return allCams[i].name
        return "" + camId
    }
    // 关键帧缩略图由媒体层生成；缺失或非绝对/资源基址时返回空串。
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
        if (page.nvrMode) {
            nvrInfo = replay.nvrState()
            queryHistory()
            return
        }
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
        if (page.nvrMode) {
            queryHistory()
            return
        }
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
        if (page.nvrMode) replay.seekNvr(activeCam, cursor, playState.playing)
        refreshSync()
    }
    function togglePlay() { if (replay) replay.setPlaying(!page.playState.playing); }
    function seekTo(ts) {
        cursor = ts
        if (page.nvrMode) replay.seekNvr(activeCam, ts, playState.playing)
        refreshSync()
    }
    // 通道号由 nvr.yaml 的固定六路映射自动带出（传 0 即回退到映射），页面不再填通道。
    function queryHistory() {
        if (!replay || !page.nvrMode) return
        replay.queryNvr(page.activeCam, page.date, 0)
        tl = replay.timeline(page.activeCam, page.date)
        thumbs = []
    }
    // 现场无物理键盘：日期由虚拟键盘输入，「查询」按钮 / 键盘「完成」都走这一个入口。
    function applyDate() {
        page.date = dateInput.text
        keyboard.close()
        page.reload()
    }
    function updateNvr() {
        if (!replay || !page.nvrMode) return
        nvrInfo = replay.nvrState()
        tl = replay.timeline(page.activeCam, page.date)
    }
    function selectAlarmAt(ts) { if (replay) { replay.selectAlarm(page.activeCam, ts) } }

    Component.onCompleted: {
        if (replay) playState = replay.playback()
        if (page.nvrMode) { syncOn = false; nvrInfo = replay.nvrState() }
        reload()
    }
    Component.onDestruction: { if (replay && page.nvrMode) replay.stopHistory() }
    Connections {
        target: page.replay
        function onSelectionChanged() { page.reload() }
        function onPlaybackChanged() {
            page.playState = page.replay.playback()
            if (page.nvrMode && page.playState.posMs > 0) page.cursor = page.playState.posMs
        }
        function onNvrChanged() { page.updateNvr() }
    }

    // 「点空白处收起键盘」：放在最底层；键盘本体的全页 scrim（z=modal）负责真正收起。
    MouseArea {
        objectName: "replayDismissKeyboard"
        anchors.fill: parent
        enabled: keyboard.visible
        onClicked: keyboard.close()
    }

    Item {
        id: content
        anchors.fill: parent
        anchors.margins: Theme.spacing.s3

        // ---- 顶部：日期 + 查询 + 六路摄像头 + 同步开关 ----
        Row {
            id: toolRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: Theme.size.touch_target_min
            spacing: Theme.spacing.s2
            Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_secondary
                   font.pixelSize: Theme.typography.scale.caption.size; text: "日期" }
            Rectangle {
                objectName: "replayDateInput"
                width: 120; height: Theme.size.touch_target_min; color: Theme.color.bg_sunken
                border.color: keyboard.target === dateInput ? Theme.color.focus_ring
                            : (page.dateError.length > 0 ? Theme.color.error : Theme.color.border)
                border.width: Theme.border.width; radius: Theme.radius.chip
                TextInput {
                    id: dateInput
                    anchors.fill: parent; anchors.leftMargin: Theme.spacing.s2
                    verticalAlignment: TextInput.AlignVCenter; color: Theme.color.text_primary
                    font.pixelSize: Theme.typography.scale.caption.size; text: page.date
                }
                MouseArea {
                    anchors.fill: parent
                    cursorShape: Qt.PointingHandCursor
                    onClicked: keyboard.open(dateInput)
                }
            }
            FilterChip { objectName: "queryHistoryButton"; text: "查询"
                         enabled: !page.nvrInfo.busy
                         onClicked: page.applyDate() }
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
            FilterChip { text: page.nvrMode ? "三联同步未接入" : "三联同步"; enabled: !page.nvrMode
                         selected: page.syncOn; onClicked: page.syncOn = !page.syncOn }
        }

        // ---- 来源 / 同步状态 / 录像不可用黄条（右侧事件详情栏已整栏删除） ----
        Row {
            id: sourceRow
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: toolRow.bottom
            anchors.topMargin: Theme.spacing.s2
            height: Theme.size.button.sm
            spacing: Theme.spacing.s2
            Text {
                anchors.verticalCenter: parent.verticalCenter
                visible: page.replay ? !page.replay.sourceStatus().usable || page.replay.sourceStatus().note.length > 0 : false
                color: Theme.color.degraded
                textFormat: Text.PlainText
                font.pixelSize: Theme.typography.scale.caption.size
                text: page.nvrMode ? ("来源：NVR · " + page.nvrInfo.message) : page.replay ? ("来源：" + page.replay.sourceStatus().label +
                                (page.replay.sourceStatus().note.length > 0 ? "（" + page.replay.sourceStatus().note + "）" : "")) : ""
            }
            Text {
                objectName: "syncStatusText"
                anchors.verticalCenter: parent.verticalCenter
                visible: !page.nvrMode && (page.syncInfo.available === false || page.syncInfo.inSync === false)
                color: page.syncInfo.available === false ? Theme.color.text_muted : Theme.color.error
                font.pixelSize: Theme.typography.scale.caption.size
                text: page.syncInfo.available === false ? "同步状态不可用" : page.syncInfo.prompt
            }
            // D-07：录像不可用/不完整仍显著提示，但不再占一整条右侧详情栏。
            Rectangle {
                objectName: "detailRecordBar"
                visible: page.detail.recordOk === false && page.detail.recordLabel !== undefined
                width: recordText.width + Theme.spacing.s2 * 2
                height: Theme.size.button.sm
                radius: Theme.radius.chip
                color: Theme.color.degraded_bg
                border.color: Theme.color.degraded; border.width: Theme.border.width
                Text { id: recordText; anchors.centerIn: parent; color: Theme.color.degraded
                       font.pixelSize: Theme.typography.scale.caption.size
                       text: page.detail.recordLabel !== undefined ? page.detail.recordLabel : "录像不可用" }
            }
        }

        // ---- 回放画面：严格 16:9，按可用高度反推最大宽度，居中；Cover 消除黑边 ----
        Item {
            id: videoArea
            objectName: "replayVideoArea"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: sourceRow.bottom
            anchors.topMargin: Theme.spacing.s2
            anchors.bottom: bottomBar.top
            anchors.bottomMargin: Theme.spacing.s2

            Row {
                id: videoRow
                anchors.centerIn: parent
                spacing: Theme.spacing.s2
                readonly property int panelCount: page.syncOn ? page.lockedCams.length : 1
                readonly property real slotW:
                    (videoArea.width - (panelCount - 1) * Theme.spacing.s2) / panelCount
                readonly property real panelH: Math.min(videoArea.height, slotW * 9 / 16)
                readonly property real panelW: panelH * 16 / 9

                Repeater {
                    model: page.syncOn ? page.lockedCams : [page.activeCam]
                    Rectangle {
                        objectName: "replayPanel-" + modelData
                        width: videoRow.panelW
                        height: videoRow.panelH
                        color: Theme.color.bg_sunken
                        border.color: Theme.color.border; border.width: Theme.border.width
                        radius: Theme.radius.card
                        clip: true
                        VideoFrame {
                            readonly property var history: page.replay && page.nvrMode && page.nvrInfo.playingCamera === modelData
                                ? page.replay.historyChannel(modelData) : null
                            anchors.fill: parent
                            channel: history
                            fillMode: 1                 // 与实时页一致：Cover，16:9 面板内无黑边
                            visible: history && history.info.presentable === true
                        }
                        Text { anchors.top: parent.top; anchors.left: parent.left; anchors.margins: Theme.spacing.s2
                               color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.label.size
                               text: page.chipName(modelData) + " (" + modelData + ")" }
                        Text { anchors.centerIn: parent; color: Theme.color.text_muted
                               font.pixelSize: Theme.typography.scale.caption.size
                               visible: !page.nvrMode || page.nvrInfo.decoderState !== "playing" && page.nvrInfo.decoderState !== "paused" && page.nvrInfo.decoderState !== "ended"
                               text: page.nvrMode ? page.nvrInfo.message : "回放画面（媒体层提供）" }
                        Text { anchors.bottom: parent.bottom; anchors.left: parent.left; anchors.margins: Theme.spacing.s2
                               color: Theme.color.text_secondary; font.pixelSize: Theme.typography.scale.caption.size
                               font.family: Theme.typography.family.mono
                               text: page.replay ? page.replay.formatTime(page.cursor) : "" }
                    }
                }
            }
        }

        // ---- 底部：游标时间 + 时间轴 + 可点击播放控制（空白区域全部让给画面） ----
        Item {
            id: bottomBar
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            readonly property real keyH: page.thumbs.length > 0 ? Theme.size.status_strip_height : 0
            height: Theme.typography.scale.caption.line + Theme.spacing.s1
                    + Theme.size.button.sm
                    + (page.thumbs.length > 0 ? Theme.spacing.s1 + keyH : 0)
                    + Theme.spacing.s2 + Theme.size.touch_target_min

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
                // 缺口（D-02 斜纹留白 + "录像缺失"，可点选）
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

            // D-01 关键帧条：NVR 模式下媒体层不产出缩略图，恒为空 -> 直接不占位；
            // 只有真有缩略图时才显示，避免「播放按钮上方一整条没用的空框」。
            Rectangle {
                id: keyStrip
                objectName: "keyframeStrip"
                anchors.top: track.bottom
                anchors.topMargin: page.thumbs.length > 0 ? Theme.spacing.s1 : 0
                anchors.left: parent.left; anchors.right: parent.right
                height: bottomBar.keyH
                visible: page.thumbs.length > 0
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

            // 播放控制：已实现的播放/暂停与四跳转；未实现的倍速按钮已移除（触控 ≥44px）。
            Row {
                id: controls
                anchors.top: keyStrip.bottom
                anchors.topMargin: Theme.spacing.s2
                spacing: Theme.spacing.s2
                FilterChip { objectName: "playbackToggle"; text: page.playState.playing ? "暂停" : "播放"
                             onClicked: page.togglePlay() }
                FilterChip { objectName: "jump-minus30"; text: "-30s"; onClicked: page.doJump("minus30") }
                FilterChip { objectName: "jump-alarm"; text: "报警时刻"; onClicked: page.doJump("alarm") }
                FilterChip { objectName: "jump-plus30"; text: "+30s"; onClicked: page.doJump("plus30") }
                FilterChip { objectName: "jump-plus60"; text: "+60s"; onClicked: page.doJump("plus60") }
            }
        }
    }

    // 右侧事件详情栏已删除：时间/类型/级别/距离/置信度/处理状态/备注等字段现场无用，
    // 只读通道列表也由 nvr.yaml 固定映射 + 顶部摄像头选择完全替代。
    // 导出录像入口/二次确认已移除：录像导出由 NVR 侧完成。

    // 日期字段专用虚拟键盘：点日期框弹出，点页面空白处收起，「完成」等价于按「查询」。
    VirtualKeyboard {
        id: keyboard
        objectName: "replayKeyboard"
        onAccepted: page.applyDate()
    }
}
