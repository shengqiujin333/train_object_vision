import QtQuick 2.15
import ".."
import "../components"

// P2 报警中心（ITEM-018 / 交互 §5.2、视觉 §8.2）。
// 列表：级别徽标(颜色+形状+文字)/类型/时间/来源摄像头/描述/处理状态/录像可用性；
// 默认 级别降序→时间降序；筛选 日期/时间段/摄像头/类型/级别/确认状态；
// 确认与批量确认（只改 ack_state 并写维护日志）；录像不可用行内显著标注；空态 + 清除筛选。
// 过滤/排序/确认逻辑在 railhmiAlarms（C++，委托 core），QML 只做展示与交互。
Rectangle {
    id: page
    color: Theme.color.bg_base

    property var model: (typeof railhmiAlarms !== "undefined") ? railhmiAlarms : null
    property var rows: []
    property var opts: ({})
    property string fLevel: ""
    property string fType: ""
    property string fCamera: ""
    property string fAck: ""
    property string fFrom: ""
    property string fTo: ""

    signal replayRequested(string cameraId, double tsWallMs)
    signal snapshotRequested(string cameraId, string snapshotRef)

    function refresh() {
        if (!model) { rows = []; return }
        rows = model.query(fLevel, fType, fCamera, fAck, fFrom, fTo)
    }
    function clearFilters() {
        fLevel = ""; fType = ""; fCamera = ""; fAck = ""; fFrom = ""; fTo = ""
        refresh()
    }
    function ackOne(id) { if (model) { model.ack([id]); refresh() } }
    function ackAllUnacked() {
        if (!model) return
        var ids = []
        for (var i = 0; i < rows.length; ++i)
            if (rows[i].ack === "unacked") ids.push(rows[i].id)
        if (ids.length > 0) { model.ack(ids); refresh() }
    }
    function levelColor(l) {
        if (l === "L1") return Theme.alarm.L1.accent
        if (l === "L2") return Theme.alarm.L2.accent
        return Theme.alarm.L3.accent
    }
    function levelGlyph(l) {
        if (l === "L1") return "\u25B2"
        if (l === "L2") return "\u25CF"
        return "\u25C6"
    }
    function levelTint(l) {
        if (l === "L1") return Theme.alarm.L1.tint_bg
        if (l === "L2") return Theme.alarm.L2.tint_bg
        return Theme.alarm.L3.tint_bg
    }

    Component.onCompleted: { if (model) opts = model.options(); refresh() }
    Connections {
        target: page.model
        function onChanged() { page.refresh() }
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacing.s3
        spacing: Theme.spacing.s2

        // ---- 筛选栏 ----
        Row {
            spacing: Theme.spacing.s2
            FilterChip { text: "全部级别"; selected: page.fLevel === ""; onClicked: { page.fLevel = ""; page.refresh() } }
            FilterChip { text: "一级"; selected: page.fLevel === "L1"; onClicked: { page.fLevel = "L1"; page.refresh() } }
            FilterChip { text: "二级"; selected: page.fLevel === "L2"; onClicked: { page.fLevel = "L2"; page.refresh() } }
            FilterChip { text: "三级"; selected: page.fLevel === "L3"; onClicked: { page.fLevel = "L3"; page.refresh() } }
            FilterChip { text: "全部状态"; selected: page.fAck === ""; onClicked: { page.fAck = ""; page.refresh() } }
            FilterChip { text: "未确认"; selected: page.fAck === "unacked"; onClicked: { page.fAck = "unacked"; page.refresh() } }
            FilterChip { text: "已确认"; selected: page.fAck === "acked"; onClicked: { page.fAck = "acked"; page.refresh() } }
            FilterChip { text: "清除筛选"; selected: false; onClicked: page.clearFilters() }
            FilterChip { text: "批量确认"; selected: false; onClicked: page.ackAllUnacked() }
        }
        Row {
            spacing: Theme.spacing.s2
            FilterChip { text: "全部类型"; selected: page.fType === ""; onClicked: { page.fType = ""; page.refresh() } }
            Repeater {
                model: page.opts.types !== undefined ? page.opts.types : []
                FilterChip {
                    text: modelData.label
                    selected: page.fType === modelData.token
                    onClicked: { page.fType = modelData.token; page.refresh() }
                }
            }
            FilterChip { text: "全部摄像头"; selected: page.fCamera === ""; onClicked: { page.fCamera = ""; page.refresh() } }
            Repeater {
                model: page.opts.cameras !== undefined ? page.opts.cameras : []
                FilterChip {
                    text: modelData.name
                    selected: page.fCamera === modelData.id
                    onClicked: { page.fCamera = modelData.id; page.refresh() }
                }
            }
        }
        Row {
            spacing: Theme.spacing.s2
            Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_secondary
                   font.pixelSize: Theme.typography.scale.caption.size; text: "日期从" }
            Rectangle {
                width: 120; height: Theme.size.button.sm; color: Theme.color.bg_sunken
                border.color: Theme.color.border; border.width: Theme.border.width; radius: Theme.radius.chip
                TextInput {
                    id: fromInput
                    anchors.fill: parent; anchors.leftMargin: Theme.spacing.s2
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.color.text_primary
                    font.pixelSize: Theme.typography.scale.caption.size
                    text: page.fFrom
                    onEditingFinished: { page.fFrom = text; page.refresh() }
                }
            }
            Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_secondary
                   font.pixelSize: Theme.typography.scale.caption.size; text: "至" }
            Rectangle {
                width: 120; height: Theme.size.button.sm; color: Theme.color.bg_sunken
                border.color: Theme.color.border; border.width: Theme.border.width; radius: Theme.radius.chip
                TextInput {
                    id: toInput
                    anchors.fill: parent; anchors.leftMargin: Theme.spacing.s2
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.color.text_primary
                    font.pixelSize: Theme.typography.scale.caption.size
                    text: page.fTo
                    onEditingFinished: { page.fTo = text; page.refresh() }
                }
            }
            Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_muted
                   font.pixelSize: Theme.typography.scale.caption.size; text: "（YYYY-MM-DD，时间段按当日窗口）" }
        }

        // ---- 列表 / 空态 ----
        Item {
            width: parent.width
            height: parent.height - y

            ListView {
                id: list
                anchors.fill: parent
                visible: page.rows.length > 0
                model: page.rows
                clip: true
                spacing: Theme.border.width

                delegate: Rectangle {
                    objectName: "alarmRow-" + modelData.id
                    width: list.width
                    height: Theme.size.button.md + Theme.spacing.s4
                    color: Theme.color.surface
                    border.color: modelData.ack === "acked" ? Theme.color.border : page.levelColor(modelData.level)
                    border.width: Theme.border.width

                    // 左缘级别色
                    Rectangle {
                        width: 3; height: parent.height
                        color: page.levelColor(modelData.level)
                    }
                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: Theme.spacing.s2
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacing.s2

                        Rectangle {
                            width: levelRow.width + Theme.spacing.s2 * 2
                            height: Theme.size.button.sm
                            radius: Theme.radius.chip
                            color: page.levelTint(modelData.level)
                            border.color: page.levelColor(modelData.level)
                            border.width: Theme.border.width
                            Row {
                                id: levelRow
                                anchors.centerIn: parent
                                spacing: Theme.spacing.s1
                                Text { color: page.levelColor(modelData.level); font.pixelSize: Theme.typography.scale.caption.size
                                       text: page.levelGlyph(modelData.level) }
                                Text { color: page.levelColor(modelData.level); font.pixelSize: Theme.typography.scale.caption.size
                                       text: modelData.levelLabel }
                            }
                        }
                        Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_primary
                               font.pixelSize: Theme.typography.scale.label.size; text: modelData.typeLabel }
                        Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_secondary
                               font.pixelSize: Theme.typography.scale.caption.size; text: modelData.time }
                        Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_secondary
                               font.pixelSize: Theme.typography.scale.caption.size; text: modelData.cameraName + " (" + modelData.cameraId + ")" }
                        Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_primary
                               font.pixelSize: Theme.typography.scale.caption.size; text: modelData.description }
                        Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_secondary
                               font.pixelSize: Theme.typography.scale.caption.size; text: modelData.ackLabel }
                        // 录像可用性：不可用/不完整 -> 行内显著标注（PLY-008）
                        Rectangle {
                            visible: !modelData.recordOk
                            width: recText.width + Theme.spacing.s2 * 2
                            height: Theme.size.button.sm
                            radius: Theme.radius.chip
                            color: Theme.color.degraded_bg
                            border.color: Theme.color.degraded
                            border.width: Theme.border.width
                            Text { id: recText; anchors.centerIn: parent; color: Theme.color.degraded
                                   font.pixelSize: Theme.typography.scale.caption.size
                                   text: modelData.recordLabel }
                        }
                    }

                    Row {
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.spacing.s2
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.spacing.s2
                        FilterChip { visible: modelData.ack === "unacked"; text: "确认"; onClicked: page.ackOne(modelData.id) }
                        FilterChip { text: "查看录像"; onClicked: page.replayRequested(modelData.cameraId, modelData.tsWallMs) }
                        FilterChip { visible: modelData.snapshot; text: "查看快照"
                                     onClicked: page.snapshotRequested(modelData.cameraId, modelData.snapshotRef) }
                    }
                }
            }

            // 空态（无结果）
            Column {
                anchors.centerIn: parent
                visible: page.rows.length === 0
                spacing: Theme.spacing.s3
                Text { anchors.horizontalCenter: parent.horizontalCenter; color: Theme.color.text_muted
                       font.pixelSize: Theme.typography.scale.body.size; text: "暂无符合条件报警" }
                FilterChip { anchors.horizontalCenter: parent.horizontalCenter; text: "清除筛选"
                             onClicked: page.clearFilters() }
            }
        }
    }
}
