import QtQuick 2.15
import ".."

// 视频渲染面（ITEM-014）。渲染路径由配置选定并经 railhmiRenderPath 注入（默认 cpu，gl 可开关）。
// 逐帧纹理由 media 层（ITEM-013 CameraStream + librga / GstGLVideoItem）提供；
// 本组件职责：按所选路径选择渲染实现、非 LIVE 显示占位层、绝不把陈旧帧当实时画面。
Item {
    id: surface
    // 实时可展示标志（= core may_present_as_live：仅 LIVE 且质量可用才为真）
    property bool live: false
    property string stateText: ""
    property string detail: ""

    readonly property string renderPath: (typeof railhmiRenderPath !== "undefined")
                                         ? railhmiRenderPath : "cpu"

    Rectangle {
        anchors.fill: parent
        color: Theme.color.bg_sunken
    }

    // 实时画面层：仅 live 时可见。cpu 路径由 librga 输出 BGR 图元；gl 路径由 GstGLVideoItem 承载。
    Item {
        id: videoLayer
        objectName: "videoLayer"
        anchors.fill: parent
        visible: surface.live
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

    Text {
        objectName: "renderPathLabel"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: Theme.spacing.s2
        color: Theme.color.text_muted
        font.pixelSize: Theme.typography.scale.caption.size
        text: "render:" + surface.renderPath
    }
}
