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

    readonly property var maint: (typeof railhmiMaintenance !== "undefined") ? railhmiMaintenance : null
    property var ptz: ({})
    property var ptzResult: ({})
    property string pendingKind: ""
    property string reconnectNotice: ""
    property var rowBg: Theme.color.surface
    // 健康/日志/恢复（ITEM-024，连接 changed() 以便刷新）
    property var healthSt: ({ completed: true, steps: [] })
    property var logRows: []

    function reloadHealth() {
        healthSt = (typeof railhmiHealth !== "undefined" && railhmiHealth)
                   ? railhmiHealth.recovery() : ({ completed: true, steps: [] })
    }
    function reloadLogs() {
        logRows = (typeof railhmiHealth !== "undefined" && railhmiHealth)
                  ? railhmiHealth.logs(logCard.fLevel, logCard.fSource, logCard.fKeyword) : []
    }

    function doReconnect(cameraId) {
        if (!maint) return
        var r = maint.reconnect(cameraId)
        page.reconnectNotice = cameraId + " 重连中（已重连 " + r.reconnectCount + " 次）"
    }

    function reload() {
        if (maint) ptz = maint.ptzState()
        reloadHealth()
        reloadLogs()
    }
    function askPtz(kind) { page.pendingKind = kind }
    function confirmPtz() {
        if (maint && page.pendingKind.length > 0) page.ptzResult = maint.ptzAction(page.pendingKind)
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
    Connections { target: page.maint; function onChanged() { page.reload() } }
    // 健康/日志/恢复变更刷新（ITEM-024 D-02）
    Connections {
        target: (typeof railhmiHealth !== "undefined") ? railhmiHealth : null
        function onChanged() { page.reloadHealth(); page.reloadLogs() }
    }

    // 卡片区：可滚动，避免固定高卡片在 1920x1080 下溢出裁切（ITEM-024 D-01）
    Flickable {
        id: flick
        anchors.fill: parent
        clip: true
        contentWidth: width
        contentHeight: grid.height + 2 * Theme.spacing.s3

    Grid {
        id: grid
        x: Theme.spacing.s3
        y: Theme.spacing.s3
        width: flick.width - 2 * Theme.spacing.s3
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
                                     " · 质量 " + modelData.qualityLabel +
                                     (modelData.reconnectText.length > 0 ? " · " + modelData.reconnectText : "") }
                        FilterChip {
                            objectName: "reconnect-" + modelData.id
                            text: "重连"
                            onClicked: page.doReconnect(modelData.id)
                        }
                    }
                }
                Text { objectName: "reconnectNotice"; visible: page.reconnectNotice.length > 0
                       color: Theme.color.info; font.pixelSize: Theme.typography.scale.caption.size
                       text: page.reconnectNotice }
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
                       text: page.maint ? (page.maint.recording().stateText + " · 剩余 " + page.maint.recording().freeText +
                                       " · 已用 " + page.maint.recording().usedPct + "%") : "—" }
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
                       text: page.maint ? page.maint.health().text : "—" }
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
                Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.label.size; text: "维护日志（按日滚动）" }
                Repeater {
                    model: page.maint ? page.maint.logsByDay(7) : []
                    Column {
                        Text { objectName: "maintLogDay"
                               color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.caption.size
                               text: modelData.day + "（" + modelData.count + "）" }
                        Repeater {
                            model: modelData.entries
                            Text {
                                objectName: "maintLogRow"
                                color: Theme.color.text_secondary; font.pixelSize: Theme.typography.scale.caption.size
                                text: "  " + modelData.action + " · " + modelData.user + " · " + modelData.result + " · " + modelData.detail
                            }
                        }
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

        // 权限矩阵卡（ITEM-023）：页面/导出/配置权限矩阵；受限入口不渲染由权限查询驱动
        Rectangle {
            objectName: "maintPermMatrixCard"
            width: (parent.width - Theme.spacing.s3) / 2; height: 320
            color: Theme.color.surface; border.color: Theme.color.border; border.width: Theme.border.width
            radius: Theme.radius.card
            Column {
                anchors.fill: parent; anchors.margins: Theme.spacing.s3; spacing: Theme.spacing.s1
                Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.label.size
                       text: "权限矩阵（当前角色：" + (typeof railhmiPermissions !== "undefined" && railhmiPermissions
                                                        ? railhmiPermissions.roleName() : "—") + "）" }
                Row {
                    spacing: Theme.spacing.s3
                    Text { color: Theme.color.text_muted; font.pixelSize: Theme.typography.scale.caption.size; text: "项目" }
                    Text { color: Theme.color.text_muted; font.pixelSize: Theme.typography.scale.caption.size; text: "司机" }
                    Text { color: Theme.color.text_muted; font.pixelSize: Theme.typography.scale.caption.size; text: "维护" }
                    Text { color: Theme.color.text_muted; font.pixelSize: Theme.typography.scale.caption.size; text: "研发" }
                    Text { color: Theme.color.text_muted; font.pixelSize: Theme.typography.scale.caption.size; text: "管理员" }
                }
                Repeater {
                    model: (typeof railhmiPermissions !== "undefined" && railhmiPermissions)
                           ? railhmiPermissions.matrix() : []
                    Row {
                        objectName: "permRow-" + modelData.permission
                        spacing: Theme.spacing.s3
                        Text { color: Theme.color.text_secondary; font.pixelSize: Theme.typography.scale.caption.size
                               text: modelData.label }
                        Text { color: modelData.driver ? Theme.color.ok : Theme.color.text_disabled
                               font.pixelSize: Theme.typography.scale.caption.size; text: modelData.driver ? "Y" : "-" }
                        Text { color: modelData.maintainer ? Theme.color.ok : Theme.color.text_disabled
                               font.pixelSize: Theme.typography.scale.caption.size; text: modelData.maintainer ? "Y" : "-" }
                        Text { color: modelData.developer ? Theme.color.ok : Theme.color.text_disabled
                               font.pixelSize: Theme.typography.scale.caption.size; text: modelData.developer ? "Y" : "-" }
                        Text { color: modelData.admin ? Theme.color.ok : Theme.color.text_disabled
                               font.pixelSize: Theme.typography.scale.caption.size; text: modelData.admin ? "Y" : "-" }
                    }
                }
            }
        }

        // 结构化日志卡（ITEM-024）：按级别/来源/关键字结构化查询，按日滚动
        Rectangle {
            objectName: "maintLogQueryCard"
            id: logCard
            width: (parent.width - Theme.spacing.s3) / 2; height: 320
            color: Theme.color.surface; border.color: Theme.color.border; border.width: Theme.border.width
            radius: Theme.radius.card
            property string fLevel: ""
            property string fSource: ""
            property string fKeyword: ""
            Column {
                anchors.fill: parent; anchors.margins: Theme.spacing.s3; spacing: Theme.spacing.s1
                Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.label.size
                       text: "结构化日志（按日滚动）" }
                Row {
                    spacing: Theme.spacing.s2
                    FilterChip { objectName: "logFilterAll"; text: "全部"; selected: logCard.fLevel === ""
                                 onClicked: { logCard.fLevel = ""; page.reloadLogs() } }
                    FilterChip { objectName: "logFilterWarn"; text: "警告+"; selected: logCard.fLevel === "warn"
                                 onClicked: { logCard.fLevel = "warn"; page.reloadLogs() } }
                    FilterChip { objectName: "logFilterError"; text: "错误"; selected: logCard.fLevel === "error"
                                 onClicked: { logCard.fLevel = "error"; page.reloadLogs() } }
                }
                // 来源/关键字结构化查询输入（D-03）
                Row {
                    spacing: Theme.spacing.s2
                    Rectangle {
                        width: 110; height: Theme.size.touch_target_min; color: Theme.color.bg_sunken
                        border.color: Theme.color.border; border.width: Theme.border.width; radius: Theme.radius.chip
                        TextInput {
                            objectName: "logSourceInput"
                            anchors.fill: parent; anchors.leftMargin: Theme.spacing.s2
                            verticalAlignment: TextInput.AlignVCenter; color: Theme.color.text_primary
                            font.pixelSize: Theme.typography.scale.caption.size
                            text: logCard.fSource
                            onEditingFinished: { logCard.fSource = text; page.reloadLogs() }
                        }
                    }
                    Rectangle {
                        width: 130; height: Theme.size.touch_target_min; color: Theme.color.bg_sunken
                        border.color: Theme.color.border; border.width: Theme.border.width; radius: Theme.radius.chip
                        TextInput {
                            objectName: "logKeywordInput"
                            anchors.fill: parent; anchors.leftMargin: Theme.spacing.s2
                            verticalAlignment: TextInput.AlignVCenter; color: Theme.color.text_primary
                            font.pixelSize: Theme.typography.scale.caption.size
                            text: logCard.fKeyword
                            onEditingFinished: { logCard.fKeyword = text; page.reloadLogs() }
                        }
                    }
                }
                Repeater {
                    model: page.logRows
                    Text {
                        objectName: "structuredLogRow"
                        color: modelData.level === "error" ? Theme.color.error
                               : (modelData.level === "warn" ? Theme.color.degraded : Theme.color.text_secondary)
                        font.pixelSize: Theme.typography.scale.caption.size
                        text: modelData.levelLabel + " [" + modelData.source + "] " + modelData.message + " " + modelData.fields
                    }
                }
            }
        }

        // 恢复进度卡（ITEM-024）
        Rectangle {
            objectName: "maintRecoveryCard"
            width: (parent.width - Theme.spacing.s3) / 2; height: 260
            color: Theme.color.surface; border.color: Theme.color.border; border.width: Theme.border.width
            radius: Theme.radius.card
            Column {
                anchors.fill: parent; anchors.margins: Theme.spacing.s3; spacing: Theme.spacing.s1
                Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.label.size
                       text: "启动恢复进度" }
                Row {
                    spacing: Theme.spacing.s2
                    Text { objectName: "recoveryProgressText"; color: Theme.color.text_secondary
                           font.pixelSize: Theme.typography.scale.caption.size
                           text: page.healthSt.text !== undefined
                                 ? (page.healthSt.text + " · " + page.healthSt.progress + "%") : "—" }
                    FilterChip { objectName: "recoveryDemo"; text: "演示恢复"
                                 onClicked: { if (typeof railhmiHealth !== "undefined" && railhmiHealth) railhmiHealth.resetRecovery() } }
                }
                Repeater {
                    model: page.healthSt.steps !== undefined ? page.healthSt.steps : []
                    Text {
                        objectName: "recoveryStepRow-" + modelData.step
                        color: modelData.status === "ok" ? Theme.color.ok
                               : (modelData.status === "failed" ? Theme.color.error
                               : (modelData.status === "degraded" ? Theme.color.degraded : Theme.color.text_muted))
                        font.pixelSize: Theme.typography.scale.caption.size
                        text: modelData.statusLabel + " · " + modelData.label + " " + modelData.detail
                    }
                }
            }
        }

        // 故障自检卡（ITEM-025）：FI-01..10 显式提示 + 恢复路径
        Rectangle {
            objectName: "maintSelfCheckCard"
            width: (parent.width - Theme.spacing.s3) / 2; height: 320
            color: Theme.color.surface; border.color: Theme.color.border; border.width: Theme.border.width
            radius: Theme.radius.card
            Column {
                anchors.fill: parent; anchors.margins: Theme.spacing.s3; spacing: Theme.spacing.s1
                Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.label.size
                       text: "故障自检（模拟注入 → 显式提示 → 恢复路径）" }
                Repeater {
                    model: (typeof railhmiFaults !== "undefined") ? railhmiFaults : []
                    Column {
                        objectName: "faultRow-" + modelData.id
                        Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.caption.size
                               text: modelData.id + " · " + modelData.label }
                        Text { color: Theme.color.text_secondary; font.pixelSize: Theme.typography.scale.caption.size
                               text: "提示：" + modelData.prompt }
                        Text { color: Theme.color.degraded; font.pixelSize: Theme.typography.scale.caption.size
                               text: "恢复：" + modelData.recovery }
                    }
                }
            }
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
