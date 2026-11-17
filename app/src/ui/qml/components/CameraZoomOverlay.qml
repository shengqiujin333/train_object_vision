import QtQuick 2.15
import ".."
import "../components"

// 单路摄像头放大浮层（P1 实时监控 / P4 全摄像头共用，挂在应用外壳上）。
// 点击任一摄像头格 -> 在当前画面弹出本浮层，单独放大该路画面。
// 关闭条件：60 秒自动消失，或点击窗口外的空白/遮罩区域；点击放大窗口本身不会关闭。
// 浮层只是同一 VideoChannel 的第二个渲染面（latest-only 快照），不重新取流、不改槽位顺序。
Item {
    id: overlay
    objectName: "zoomOverlay"
    visible: false
    z: Theme.z_index.modal

    property var channel: null
    property string title: ""
    property string stateText: ""
    property string detail: ""
    property bool live: false

    readonly property real pad: Theme.spacing.s4
    readonly property real gap: Theme.spacing.s2
    readonly property real headerH: Theme.typography.scale.h2.line
    readonly property real hintH: Theme.typography.scale.caption.line
    readonly property real maxW: overlay.width * 0.86
    readonly property real maxH: overlay.height * 0.86
    // 放大画面严格 16:9：Fit 模式下不裁剪也不产生黑边。
    readonly property real videoW: Math.max(1, Math.min(overlay.maxW,
        (overlay.maxH - overlay.headerH - overlay.hintH - overlay.gap * 2 - overlay.pad * 2) * 16 / 9))
    readonly property real videoH: overlay.videoW * 9 / 16
    readonly property real panelW: overlay.videoW + overlay.pad * 2
    readonly property real panelH: overlay.videoH + overlay.headerH + overlay.hintH
                                   + overlay.gap * 2 + overlay.pad * 2

    function open(ch, name, stateLabel, detailText, isLive) {
        overlay.channel = ch
        overlay.title = name
        overlay.stateText = stateLabel
        overlay.detail = detailText
        overlay.live = isLive
        overlay.visible = true
    }
    function close() {
        overlay.visible = false
        overlay.channel = null
    }

    // 1 分钟自动消失；每次重新打开都从 0 计时（running 由 visible 驱动）。
    Timer {
        interval: 60000
        running: overlay.visible
        repeat: false
        onTriggered: overlay.close()
    }

    // 遮罩：点击窗口外任何空白区域立即关闭。
    Rectangle {
        anchors.fill: parent
        color: Theme.color.overlay_scrim
        MouseArea {
            anchors.fill: parent
            onClicked: overlay.close()
        }
    }

    Rectangle {
        id: panel
        anchors.centerIn: parent
        width: overlay.panelW
        height: overlay.panelH
        color: Theme.color.surface
        border.color: Theme.color.border_strong
        border.width: Theme.border.width_emphasis
        radius: Theme.radius.modal

        // 点击放大窗口内部不关闭（只有点外面空白/遮罩才关闭）。
        MouseArea {
            anchors.fill: parent
            onClicked: { }
        }

        Column {
            anchors.fill: parent
            anchors.margins: overlay.pad
            spacing: overlay.gap

            Text {
                width: parent.width
                height: overlay.headerH
                verticalAlignment: Text.AlignVCenter
                color: Theme.color.text_primary
                font.pixelSize: Theme.typography.scale.h2.size
                font.bold: true
                text: overlay.title + "  ·  " + overlay.stateText
            }

            Item {
                width: overlay.videoW
                height: overlay.videoH
                VideoSurface {
                    anchors.fill: parent
                    live: overlay.live
                    channel: overlay.channel
                    stateText: overlay.stateText
                    detail: overlay.detail
                    fillMode: 0
                }
            }

            Text {
                width: parent.width
                height: overlay.hintH
                verticalAlignment: Text.AlignVCenter
                color: Theme.color.text_muted
                font.pixelSize: Theme.typography.scale.caption.size
                text: "点击窗口外空白处关闭 · 60 秒后自动关闭"
            }
        }
    }
}
