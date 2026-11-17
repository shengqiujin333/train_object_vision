import QtQuick 2.15
import ".."

// 顶部全局状态条：时间 / 摄像头 / 录像 / 系统健康 / 车辆状态 / 门磁（现场需求精简）。
// 每项含颜色 + 形状字形 + 文字三重编码；已删除模式位与 AI 位。
// 「车辆状态」是唯一可人工置位的项（无车速/GPS 数据源，界面标注「人工置位」）。
// 右侧为真实资源读数：内存占用率 + 每个 CPU 核 + 每个 NPU 核（SystemMetricsRuntime 采样）。
Rectangle {
    id: bar
    property var items: (typeof railhmiTopBar !== "undefined") ? railhmiTopBar : []
    property string appName: "railhmi"
    property string clockText: ""

    signal navigateRequested(string pageId, bool allowed)

    color: Theme.color.surface
    border.color: Theme.color.border
    border.width: Theme.border.width

    Timer {
        interval: 1000
        running: true
        repeat: true
        triggeredOnStart: true
        onTriggered: bar.clockText = Qt.formatDateTime(new Date(), "HH:mm:ss")
    }

    // 资源读数占右侧；左侧状态项止于读数左边，避免 1280 宽下互相重叠。
    ResourceMeters {
        id: resourceMeters
        objectName: "topResourceMeters"
        anchors.verticalCenter: parent.verticalCenter
        anchors.right: parent.right
        anchors.rightMargin: Theme.spacing.s4
    }

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.s4
        anchors.right: resourceMeters.left
        anchors.rightMargin: Theme.spacing.s3
        spacing: Theme.spacing.s4

        Text {
            anchors.verticalCenter: parent.verticalCenter
            color: Theme.color.text_primary
            font.pixelSize: Theme.typography.scale.label.size
            font.bold: true
            text: bar.appName
        }

        Repeater {
            model: bar.items
            StatusItem {
                anchors.verticalCenter: parent.verticalCenter
                objectName: "top-" + modelData.key
                label: modelData.label
                value: (modelData.key === "time") ? bar.clockText : modelData.value
                secondary: modelData.secondary
                glyph: modelData.glyph
                colorToken: modelData.color
                abnormal: modelData.abnormal !== undefined ? modelData.abnormal : (modelData.level === "warning" || modelData.level === "alarm")
                clickable: modelData.clickable
                onActivated: {
                    // 「车辆状态」没有车速/GPS 数据源，点击是人工在 未接入→静止→运动 间循环，
                    // 不跳页面；C++ 侧 VehicleStateRuntime::changed 会重建顶栏并标注「人工置位」。
                    if (modelData.key === "vehicle_state" && typeof railhmiVehicleState !== "undefined")
                        railhmiVehicleState.cycle()
                    else
                        bar.navigateRequested(modelData.target, modelData.allowed)
                }
            }
        }
    }
}
