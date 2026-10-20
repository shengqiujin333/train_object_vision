import QtQuick 2.15
import Railhmi.Video 1.0
import ".."

// 视频渲染面：VideoFrame 绘制媒体层经 RGA 转换后的独立像素快照。
// 断流/冻结时清空画面并展示状态；GL 尚未接入时由运行时明确报不可用。
Item {
    id: surface
    // 实时可展示标志（= core may_present_as_live：仅 LIVE 且质量可用才为真）
    property bool live: false
    property var channel: null
    property string stateText: ""
    property string detail: ""

    readonly property string renderPath: (typeof railhmiRenderPath !== "undefined")
                                         ? railhmiRenderPath : "cpu"

    Rectangle {
        anchors.fill: parent
        color: Theme.color.bg_sunken
    }

    // 实时画面层：保持宽高比；仅 live 时可见。
    VideoFrame {
        id: videoLayer
        objectName: "videoLayer"
        anchors.fill: parent
        visible: surface.live
        channel: surface.channel
    }

    // 非 LIVE 占位层：断连/重连/陈旧时覆盖，避免旧帧冒充实时
    Rectangle {
        id: placeholderLayer
        objectName: "placeholderLayer"
        anchors.fill: parent
        visible: !surface.live
        color: Theme.color.bg_base
        opacity: 0.72

        Column {
            anchors.centerIn: parent
            spacing: Theme.spacing.s2
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color: Theme.color.text_secondary
                font.pixelSize: Theme.typography.scale.body.size
                text: surface.stateText
            }
            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                color: Theme.color.text_muted
                font.pixelSize: Theme.typography.scale.caption.size
                text: surface.detail
            }
        }
    }

}
