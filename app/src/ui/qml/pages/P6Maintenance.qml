import QtQuick 2.15
import ".."
import "../components"

// P6 设备维护（ITEM-022 / 交互 §5.6、视觉 §8.6）。
// 分区卡片：摄像头（在线/状态/帧率/最近帧/质量）、AI（状态/模型版本/FPS/最近结果）、
// 录像与存储、网络与温度、云台（Pan/Tilt/Zoom 目标 vs 实际 + 容差校验 + 一键归位/重调前方预置位）、
// 日志、NVR（登记/状态/通道映射=未配置，无自动修改入口）。云台操作二次确认并写维护日志。
// 超差 -> 前方 AI NOT_READY。颜色/字号均来自 Theme。
Rectangle {
    id: page
    color: Theme.color.bg_base

    readonly property var M: (typeof railhmiMaintenance !== "undefined") ? railhmiMaintenance : null
    property var ptz: ({})
    property var ptzResult: ({})
    property string pendingKind: ""
    property var rowBg: Theme.color.surface

    function reload() {
        if (M) ptz = M.ptzState()
    }
    function askPtz(kind) { page.pendingKind = kind }
    function confirmPtz() {
        if (M && page.pendingKind.length > 0) page.ptzResult = M.ptzAction(page.pendingKind)
        page.pendingKind = ""
        reload()
    }
    function checkText() {
        return page.ptz.check !== undefined ? page.ptz.check.text : "—"
    }
    function frontAiText() {
        if (page.ptzResult.check !== undefined) return page.ptzResult.check.frontAiReady ? "READY" : "NOT_READY"
        return page.ptz.check !== undefined ? (page.ptz.check.frontAiReady ? "READY" : "NOT_READY") : "—"
    }

    Component.onCompleted: reload()
    Connections { target: page.M; function onChanged() { page.reload() } }

    Grid {
        anchors.fill: parent
        anchors.margins: Theme.spacing.s3
        columns: 2
        spacing: Theme.spacing.s3

        // 摄像头
        Rectangle {
            objectName: "maintCameraCard"
            width: (parent.width - Theme.spacing.s3) / 2; height: 260
            color: Theme.color.surface; border.color: Theme.color.border; border.width: Theme.border.width
            radius: Theme.radius.card
            Column {
                anchors.fill: parent; anchors.margins: Theme.spacing.s3; spacing: Theme.spacing.s1
                Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.label.size; text: "摄像头" }
                Repeater {
                    model: (typeof railhmiMaintenanceCameras !== "undefined") ? railhmiMaintenanceCameras : []
                    Row {
                        objectName: "maintCam-" + modelData.id
                        spacing: Theme.spacing.s2
                        Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.caption.size
                               text: modelData.name + " (" + modelData.id + ")" }
                        Text { color: modelData.online ? Theme.color.ok : Theme.color.error
                               font.pixelSize: Theme.typography.scale.caption.size
                               text: modelData.online ? "在线" : "离线" }
                        Text { color: Theme.color.text_secondary; font.pixelSize: Theme.typography.scale.caption.size
                               text: modelData.stateLabel + " · " + modelData.fpsText + " · " + modelData.lastFrameText +
                                     " · 质量 " + modelData.qualityLabel }
                    }
                }
            }
        }

        // AI
        Rectangle {
            objectName: "maintAiCard"
            width: (parent.width - Theme.spacing.s3) / 2; height: 260
            color: Theme.color.surface; border.color: Theme.color.border; border.width: Theme.border.width
            radius: Theme.radius.card
            Column {
                anchors.fill: parent; anchors.margins: Theme.spacing.s3; spacing: Theme.spacing.s1
                Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.label.size; text: "AI" }
                Repeater {
                    model: (typeof railhmiAiDebug !== "undefined") ? railhmiAiDebug : []
                    Text {
                        objectName: "maintAi-" + modelData.cameraId
                        color: modelData.available ? Theme.color.text_secondary : Theme.color.error
                        font.pixelSize: Theme.typography.scale.caption.size
                        text: modelData.cameraId + " · " + modelData.profileLabel + " · " +
                              (modelData.modelVersion.length > 0 ? modelData.modelVersion : "—") + " · FPS " +
                              modelData.fpsText + " · 延迟 " + modelData.latencyMs + " ms · " + modelData.lastResultText +
                              (modelData.available ? "" : " · " + modelData.unavailableReason)
                    }
                }
            }
        }

        // 录像与存储
        Rectangle {
            objectName: "maintRecordingCard"
            width: (parent.width - Theme.spacing.s3) / 2; height: 120
            color: Theme.color.surface; border.color: Theme.color.border; border.width: Theme.border.width
            radius: Theme.radius.card
            Column {
                anchors.fill: parent; anchors.margins: Theme.spacing.s3; spacing: Theme.spacing.s1
                Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.label.size; text: "录像与存储" }
                Text { objectName: "recordingState"; color: Theme.color.text_secondary
                       font.pixelSize: Theme.typography.scale.caption.size
                       text: page.M ? (page.M.recording().stateText + " · 剩余 " + page.M.recording().freeText +
                                       " · 已用 " + page.M.recording().usedPct + "%") : "—" }
            }
        }

        // 网络与温度
        Rectangle {
            objectName: "maintHealthCard"
            width: (parent.width - Theme.spacing.s3) / 2; height: 120
            color: Theme.color.surface; border.color: Theme.color.border; border.width: Theme.border.width
            radius: Theme.radius.card
            Column {
                anchors.fill: parent; anchors.margins: Theme.spacing.s3; spacing: Theme.spacing.s1
                Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.label.size; text: "网络与温度" }
                Text { objectName: "healthText"; color: Theme.color.text_secondary
                       font.pixelSize: Theme.typography.scale.caption.size
                       text: page.M ? page.M.health().text : "—" }
            }
        }

        // 云台（含二次确认操作）
        Rectangle {
            objectName: "maintPtzCard"
            width: (parent.width - Theme.spacing.s3) / 2; height: 200
            color: Theme.color.surface; border.color: Theme.color.border; border.width: Theme.border.width
            radius: Theme.radius.card
            Column {
                anchors.fill: parent; anchors.margins: Theme.spacing.s3; spacing: Theme.spacing.s1
                Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.label.size; text: "云台" }
                Text { objectName: "ptzValues"; color: Theme.color.text_secondary
                       font.pixelSize: Theme.typography.scale.caption.size
                       text: page.ptz.panActual !== undefined
                             ? ("Pan 实际 " + page.ptz.panActual.toFixed(2) + "° / 目标 " + page.ptz.panTarget.toFixed(2) +
                                "°  Tilt 实际 " + page.ptz.tiltActual.toFixed(2) + "° / 目标 " + page.ptz.tiltTarget.toFixed(2) +
                                "°  Zoom 实际 " + page.ptz.zoomActual.toFixed(2) + " / 目标 " + page.ptz.zoomTarget.toFixed(2))
                             : "—" }
                Text { objectName: "ptzCheckText"; color: (page.ptz.check !== undefined && page.ptz.check.ok)
                                                        ? Theme.color.ok : Theme.color.error
                       font.pixelSize: Theme.typography.scale.caption.size; text: page.checkText() }
                Text { objectName: "ptzFrontAi"; color: Theme.color.text_secondary
                       font.pixelSize: Theme.typography.scale.caption.size; text: "前方 AI：" + page.frontAiText() }
                Row {
                    spacing: Theme.spacing.s2
                    FilterChip { objectName: "ptzHome"; text: "一键归位"; onClicked: page.askPtz("home") }
                    FilterChip { objectName: "ptzPreset"; text: "重调前方预置位"; onClicked: page.askPtz("preset_front") }
                }
            }
        }

        // 日志
        Rectangle {
            objectName: "maintLogCard"
            width: (parent.width - Theme.spacing.s3) / 2; height: 200
            color: Theme.color.surface; border.color: Theme.color.border; border.width: Theme.border.width
            radius: Theme.radius.card
            Column {
                anchors.fill: parent; anchors.margins: Theme.spacing.s3; spacing: Theme.spacing.s1
                Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.label.size; text: "维护日志" }
                Repeater {
                    model: page.M ? page.M.recentLogs(8) : []
                    Text {
                        objectName: "maintLogRow"
                        color: Theme.color.text_secondary; font.pixelSize: Theme.typography.scale.caption.size
                        text: modelData.action + " · " + modelData.user + " · " + modelData.result + " · " + modelData.detail
                    }
                }
            }
        }

        // NVR 设备卡（登记/状态/通道映射=未配置；无自动修改入口）
        Repeater {
            model: (typeof railhmiIntegrations !== "undefined") ? railhmiIntegrations : []
            DeviceCard {
                objectName: "maintNvrCard"
                width: (parent.width - Theme.spacing.s3) / 2; height: 140
                devName: modelData.name
                address: modelData.address
                registered: modelData.registered
                integrationState: modelData.state
                protocol: modelData.protocol
                channelMapping: modelData.channelMapping
            }
        }
    }

    // 二次确认模态
    Rectangle {
        objectName: "ptzConfirmModal"
        anchors.fill: parent
        visible: page.pendingKind.length > 0
        color: Theme.color.overlay_scrim
        Rectangle {
            anchors.centerIn: parent
            width: parent.width * 0.36; height: 180
            color: Theme.color.surface_elev
            border.color: Theme.color.border_strong; border.width: Theme.border.width
            radius: Theme.radius.modal
            Column {
                anchors.centerIn: parent; spacing: Theme.spacing.s3
                Text { anchors.horizontalCenter: parent.horizontalCenter
                       color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.body.size
                       text: page.pendingKind === "preset_front" ? "确认重调前方预置位？" : "确认一键归位？" }
                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.spacing.s3
                    FilterChip { objectName: "ptzConfirmYes"; text: "确认"; onClicked: page.confirmPtz() }
                    FilterChip { objectName: "ptzConfirmNo"; text: "取消"; onClicked: page.pendingKind = "" }
                }
            }
        }
    }
}
