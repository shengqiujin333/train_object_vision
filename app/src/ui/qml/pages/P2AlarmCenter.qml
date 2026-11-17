import QtQuick 2.15
import ".."
import "../components"

// P2 报警中心（ITEM-018 / 交互 §5.2、视觉 §8.2）。
// 列表：级别徽标(颜色+形状+文字)/类型/时间/来源摄像头/描述/处理状态/录像可用性；
// 默认 级别降序→时间降序；筛选 日期/时间段/摄像头/类型/级别/确认状态；
// 确认与批量确认（只改 ack_state 并写维护日志）；录像不可用行内显著标注；空态 + 清除筛选。
// 过滤/排序/确认逻辑在 railhmiAlarms（C++，委托 core），QML 只做展示与交互。
// 现场定稿：第二行固定写死「全部类型 / 摄像头 / 门磁 / NVR / 主机」五个类别，
// 互斥单选，一次只高亮一个；不再从当前报警行动态生成（无报警时 chip 也必须可见）。
// 日期给可见默认值（最近 30 天窗口，避免默认只查当天把已有历史事件滤成空列表），
// 由「查询」按钮提交（不依赖键盘回车），点日期弹应用内虚拟键盘，点空白收起。
Rectangle {
    id: page
    color: Theme.color.bg_base

    property var model: (typeof railhmiAlarms !== "undefined") ? railhmiAlarms : null
    property var rows: []
    property string fLevel: ""
    property string fType: ""
    property string fCamera: ""
    property string fAck: ""
    // 默认「最近 30 天 → 今天」：现场已有事件多为前几天产生，只给当天会显示空态。
    property string fFrom: Qt.formatDate(new Date(new Date().getTime() - 30 * 86400000), "yyyy-MM-dd")
    property string fTo: Qt.formatDate(new Date(), "yyyy-MM-dd")
    // 固定类别单选：all / camera / door / nvr / host。
    // 这四个 token 与 core::alarm_category 一致；NVR/主机当前没有事件生产者，
    // 选中后如实显示空态，绝不伪造事件凑数。
    property string fCategory: "all"

    signal replayRequested(string cameraId, double tsWallMs)
    signal snapshotRequested(string cameraId, string snapshotRef)

    function today() { return Qt.formatDate(new Date(), "yyyy-MM-dd") }
    function daysAgo(days) {
        return Qt.formatDate(new Date(new Date().getTime() - days * 86400000), "yyyy-MM-dd")
    }
    function refresh() {
        if (!model) { rows = []; return }
        rows = model.query(fLevel, fType, fCamera, fAck, fFrom, fTo, page.categoryToken())
    }
    // 「全部类型」交给 core 时不加类别条件；其余四个是固定 token。
    function categoryToken() {
        return page.fCategory === "all" ? "" : page.fCategory
    }
    // 单选：任何一次选择都先清掉上一项的类型/摄像头维度，界面上永远只高亮一个 chip。
    function selectCategory(key) {
        page.fCategory = key
        page.fType = ""
        page.fCamera = ""
        page.refresh()
    }
    // 日期不随输入实时查询：只有「查询」按钮 / 键盘「完成」提交，现场无键盘也能操作。
    function applyDates() {
        page.fFrom = fromInput.text
        page.fTo = toInput.text
        keyboard.close()
        page.refresh()
    }
    function clearFilters() {
        fLevel = ""; fType = ""; fCamera = ""; fAck = ""
        fFrom = page.daysAgo(30); fTo = page.today(); fCategory = "all"
        fromInput.text = fFrom; toInput.text = fTo
        keyboard.close()
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
        if (l === "L1") return Theme.alarm.level1.accent
        if (l === "L2") return Theme.alarm.level2.accent
        return Theme.alarm.level3.accent
    }
    function levelGlyph(l) {
        if (l === "L1") return "\u25B2"
        if (l === "L2") return "\u25CF"
        return "\u25C6"
    }
    function levelTint(l) {
        if (l === "L1") return Theme.alarm.level1.tint_bg
        if (l === "L2") return Theme.alarm.level2.tint_bg
        return Theme.alarm.level3.tint_bg
    }

    Component.onCompleted: {
        fromInput.text = fFrom
        toInput.text = fTo
        refresh()
    }
    Connections {
        target: page.model
        function onChanged() { page.refresh() }
    }

    // 「点空白处收起键盘」：底层 MouseArea 只接管没有任何控件处理的空白点击。
    MouseArea {
        objectName: "alarmDismissKeyboard"
        anchors.fill: parent
        enabled: keyboard.visible
        onClicked: keyboard.close()
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
        // 固定类别单选组：无论当前有没有报警行，这五个 chip 都稳定存在。
        // 类别由 core::alarm_category 从事件本身派生，不依赖历史事件集合。
        Row {
            objectName: "alarmCategoryRow"
            spacing: Theme.spacing.s2
            FilterChip { objectName: "alarmFilter-allType"; text: "全部类型"
                         selected: page.fCategory === "all"
                         onClicked: page.selectCategory("all") }
            FilterChip { objectName: "alarmFilter-camera"; text: "摄像头"
                         selected: page.fCategory === "camera"
                         onClicked: page.selectCategory("camera") }
            FilterChip { objectName: "alarmFilter-door"; text: "门磁"
                         selected: page.fCategory === "door"
                         onClicked: page.selectCategory("door") }
            FilterChip { objectName: "alarmFilter-nvr"; text: "NVR"
                         selected: page.fCategory === "nvr"
                         onClicked: page.selectCategory("nvr") }
            FilterChip { objectName: "alarmFilter-host"; text: "主机"
                         selected: page.fCategory === "host"
                         onClicked: page.selectCategory("host") }
        }
        Row {
            spacing: Theme.spacing.s2
            Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_secondary
                   font.pixelSize: Theme.typography.scale.caption.size; text: "日期从" }
            Rectangle {
                objectName: "alarmFromDateInput"
                width: 120; height: Theme.size.button.sm; color: Theme.color.bg_sunken
                border.color: keyboard.target === fromInput ? Theme.color.focus_ring : Theme.color.border
                border.width: Theme.border.width; radius: Theme.radius.chip
                TextInput {
                    id: fromInput
                    anchors.fill: parent; anchors.leftMargin: Theme.spacing.s2
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.color.text_primary
                    font.pixelSize: Theme.typography.scale.caption.size
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: keyboard.open(fromInput) }
            }
            Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_secondary
                   font.pixelSize: Theme.typography.scale.caption.size; text: "至" }
            Rectangle {
                objectName: "alarmToDateInput"
                width: 120; height: Theme.size.button.sm; color: Theme.color.bg_sunken
                border.color: keyboard.target === toInput ? Theme.color.focus_ring : Theme.color.border
                border.width: Theme.border.width; radius: Theme.radius.chip
                TextInput {
                    id: toInput
                    anchors.fill: parent; anchors.leftMargin: Theme.spacing.s2
                    verticalAlignment: TextInput.AlignVCenter
                    color: Theme.color.text_primary
                    font.pixelSize: Theme.typography.scale.caption.size
                }
                MouseArea { anchors.fill: parent; cursorShape: Qt.PointingHandCursor
                            onClicked: keyboard.open(toInput) }
            }
            FilterChip { objectName: "alarmQueryButton"; text: "查询"
                         onClicked: page.applyDates() }
            Text { anchors.verticalCenter: parent.verticalCenter; color: Theme.color.text_muted
                   font.pixelSize: Theme.typography.scale.caption.size
                   text: "（YYYY-MM-DD，默认最近 30 天，点日期弹键盘，点空白收起）" }
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

    // 「日期从 / 至」共用同一个虚拟键盘；「完成」等价于按「查询」。
    VirtualKeyboard {
        id: keyboard
        objectName: "alarmKeyboard"
        onAccepted: page.applyDates()
    }
}
