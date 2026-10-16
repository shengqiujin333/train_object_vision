import QtQuick 2.15
import ".."

// 启动自恢复进度浮层（ITEM-024 / DESKTOP §15.4）。
// 恢复未完成时显示顺序步骤与进度；完成（或部分降级）后隐藏。任一关键步失败 -> alive=false 显式提示。
Rectangle {
    id: overlay
    readonly property var health: (typeof railhmiHealth !== "undefined") ? railhmiHealth : null
    property var st: ({ completed: true, steps: [] })
    anchors.fill: parent
    visible: st.completed === false
    color: Theme.color.overlay_scrim
    objectName: "recoveryOverlay"

    function reload() {
        overlay.st = overlay.health ? overlay.health.recovery() : ({ completed: true, steps: [] })
    }
    Component.onCompleted: reload()
    // 订阅模型变更：恢复进度/步骤/完成后隐藏（D-02）
    Connections {
        target: overlay.health
        function onChanged() { overlay.reload() }
    }
    // 演示模式：未完成时逐步推进（配合 D-04）
    Timer {
        interval: 600
        running: overlay.visible && overlay.health
        repeat: true
        onTriggered: overlay.health.advanceRecovery()
    }

    Rectangle {
        anchors.centerIn: parent
        width: parent.width * 0.44
        height: parent.height * 0.62
        color: Theme.color.surface_elev
        border.color: Theme.color.border_strong
        border.width: Theme.border.width
        radius: Theme.radius.modal
        Column {
            anchors.fill: parent
            anchors.margins: Theme.spacing.s4
            spacing: Theme.spacing.s2
            Text {
                color: Theme.color.text_primary
                font.pixelSize: Theme.typography.scale.h2.size
                text: "启动恢复 " + (overlay.st.progress !== undefined ? overlay.st.progress : 0) + "%"
            }
            Text {
                objectName: "recoveryText"
                color: Theme.color.text_secondary
                font.pixelSize: Theme.typography.scale.body.size
                text: overlay.st.text !== undefined ? overlay.st.text : ""
            }
            Text {
                objectName: "recoveryAlive"
                visible: overlay.st.alive === false
                color: Theme.color.error
                font.pixelSize: Theme.typography.scale.label.size
                text: "关键步骤失败：应用不可用，请检查配置/数据库"
            }
            Repeater {
                model: overlay.st.steps !== undefined ? overlay.st.steps : []
                Row {
                    objectName: "recoveryStep-" + modelData.step
                    spacing: Theme.spacing.s2
                    Text { color: modelData.status === "ok" ? Theme.color.ok
                                 : (modelData.status === "failed" ? Theme.color.error
                                 : (modelData.status === "degraded" ? Theme.color.degraded : Theme.color.text_muted))
                           font.pixelSize: Theme.typography.scale.caption.size
                           text: modelData.statusLabel }
                    Text { color: Theme.color.text_primary; font.pixelSize: Theme.typography.scale.caption.size
                           text: modelData.label }
                    Text { color: Theme.color.text_muted; font.pixelSize: Theme.typography.scale.caption.size
                           text: modelData.detail }
                }
            }
        }
    }
}
