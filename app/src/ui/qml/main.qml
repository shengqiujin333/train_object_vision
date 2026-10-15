import QtQuick 2.15

// railhmi 骨架窗口（ITEM-003）。仅使用 QtQuick 基础模块，避免对 Controls2 的硬依赖。
Rectangle {
    id: root
    width: 1920
    height: 1080
    color: "#0E1116"   // design_tokens bg_base（后续由令牌生成层接管，ITEM-004）

    Column {
        anchors.centerIn: parent
        spacing: 12

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#E6EDF3"
            font.pixelSize: 32
            font.bold: true
            text: "railhmi"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#9BA7B4"
            font.pixelSize: 18
            text: railhmiBuildDescription
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#6E7A88"
            font.pixelSize: 16
            text: railhmiSimulation ? "simulation input (preview)" : "target skeleton"
        }
    }
}
