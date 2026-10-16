import QtQuick 2.15
import ".."
import "../components"

// P5 AI 调试（ITEM-017/021 / 交互 §5.5、视觉 §8.5）。
// 展示：检测框 / ROI / 置信度 / 距离 / 帧率 / 端到端延迟 / 模型版本 / 最近结果时间。
// 调试叠加层逐项开关（框/ROI/文字/掩码），**默认全部关闭**；描边用独立高对比色
// （框 Theme.color.debug_box=#00E5FF，ROI 虚线 Theme.color.debug_roi=#FFD400），避免与报警红混淆。
// 隔离：司机角色不渲染本页与任何叠加（外壳按角色过滤；此处再加防御性无权限占位）。
Rectangle {
    id: page
    color: Theme.color.bg_base

    readonly property var channels: (typeof railhmiAiDebug !== "undefined") ? railhmiAiDebug : []
    readonly property bool allowed: (typeof railhmiRole !== "undefined") ? (railhmiRole !== "driver") : false

    // 调试叠加层：逐项开关，默认全部关闭（交互 §5.5 隔离要求）
    property bool overlayBoxes: false
    property bool overlayRoi: false
    property bool overlayLabels: false
    property bool overlayMasks: false
    readonly property bool overlayAny: overlayBoxes || overlayRoi || overlayLabels || overlayMasks
    function allOff() { overlayBoxes = false; overlayRoi = false; overlayLabels = false; overlayMasks = false }

    // 司机直达：无权限占位（不渲染任何检测框/参数）
    Text {
        objectName: "aiDebugDenied"
        anchors.centerIn: parent
        visible: !page.allowed
        color: Theme.color.text_muted
        font.pixelSize: Theme.typography.scale.body.size
        text: "无权限：AI 调试仅研发/维护/管理员可见"
    }

    Row {
        anchors.fill: parent
        anchors.margins: Theme.spacing.s3
        spacing: Theme.spacing.s3
        visible: page.allowed

        // 左：每路画面 + 叠加层
        Grid {
            id: grid
            width: parent.width - metrics.width - Theme.spacing.s3
            height: parent.height
            columns: 2
            spacing: Theme.spacing.s3

            Repeater {
                model: page.channels
                Rectangle {
                    objectName: "aiPanel-" + modelData.cameraId
                    width: (grid.width - Theme.spacing.s3) / 2
                    height: (grid.height - Theme.spacing.s3) / 2
                    color: Theme.color.bg_sunken
                    border.color: Theme.color.border
                    border.width: Theme.border.width
                    radius: Theme.radius.card

                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: Theme.spacing.s1
                        color: Theme.color.bg_base
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: !modelData.available
                        color: Theme.color.text_secondary
                        font.pixelSize: Theme.typography.scale.body.size
                        text: modelData.unavailableReason
                    }

                    // ROI 掩码层（默认关闭）：半透明填充示意掩码
                    Repeater {
                        model: (page.overlayMasks && modelData.available) ? modelData.roi : []
                        Rectangle {
                            objectName: "aiMask"
                            x: modelData.x * parent.width
                            y: modelData.y * parent.height
                            width: modelData.w * parent.width
                            height: modelData.h * parent.height
                            color: Theme.color.debug_roi
                            opacity: 0.15
                        }
                    }

                    // ROI 虚线描边（默认关闭）：独立高对比黄，Canvas 虚线
                    Repeater {
                        model: (page.overlayRoi && modelData.available) ? modelData.roi : []
                        Rectangle {
                            objectName: "aiRoi"
                            x: modelData.x * parent.width
                            y: modelData.y * parent.height
                            width: modelData.w * parent.width
                            height: modelData.h * parent.height
                            color: "transparent"
                            Canvas {
                                anchors.fill: parent
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()
                                onPaint: {
                                    var ctx = getContext("2d"); ctx.reset();
                                    ctx.strokeStyle = Theme.color.debug_roi; ctx.lineWidth = 2;
                                    if (ctx.setLineDash) ctx.setLineDash([6, 4]);
                                    ctx.strokeRect(1, 1, width - 2, height - 2);
                                }
                            }
                        }
                    }

                    // 检测框（默认关闭）：独立高对比青，避免与报警红混淆
                    Repeater {
                        model: (page.overlayBoxes && modelData.available) ? modelData.boxes : []
                        Rectangle {
                            objectName: "aiBox"
                            x: modelData.x * parent.width
                            y: modelData.y * parent.height
                            width: modelData.w * parent.width
                            height: modelData.h * parent.height
                            color: "transparent"
                            border.color: Theme.color.debug_box
                            border.width: 2
                        }
                    }

                    // 文字叠加（默认关闭）：标签 + 置信度 + 距离
                    Repeater {
                        model: (page.overlayLabels && modelData.available) ? modelData.boxes : []
                        Text {
                            objectName: "aiLabel"
                            x: modelData.x * parent.width
                            y: Math.max(0, modelData.y * parent.height - Theme.size.button.sm)
                            color: Theme.color.debug_box
                            font.pixelSize: Theme.typography.scale.caption.size
                            text: modelData.label + " " + (Math.round(modelData.confidence * 100)) + "% " +
                                  (modelData.distanceM !== undefined && modelData.distanceM >= 0
                                   ? modelData.distanceM.toFixed(1) + "m" : "")
                        }
                    }

                    Text {
                        anchors.top: parent.top
                        anchors.left: parent.left
                        anchors.margins: Theme.spacing.s2
                        color: Theme.color.text_primary
                        font.pixelSize: Theme.typography.scale.label.size
                        text: modelData.name + " · " + modelData.profileLabel
                    }
                }
            }
        }

        // 右：指标面板 + 叠加层开关
        Rectangle {
            id: metrics
            objectName: "aiMetrics"
            width: parent.width * 0.30
            height: parent.height
            color: Theme.color.surface
            border.color: Theme.color.border
            border.width: Theme.border.width
            radius: Theme.radius.card

            Column {
                anchors.fill: parent
                anchors.margins: Theme.spacing.s3
                spacing: Theme.spacing.s2

                Text {
                    color: Theme.color.text_primary
                    font.pixelSize: Theme.typography.scale.label.size
                    text: "调试叠加层（默认关闭）"
                }
                Row {
                    spacing: Theme.spacing.s2
                    FilterChip { objectName: "overlayBoxes"; text: "检测框"; selected: page.overlayBoxes
                                 onClicked: page.overlayBoxes = !page.overlayBoxes }
                    FilterChip { objectName: "overlayRoi"; text: "ROI"; selected: page.overlayRoi
                                 onClicked: page.overlayRoi = !page.overlayRoi }
                }
                Row {
                    spacing: Theme.spacing.s2
                    FilterChip { objectName: "overlayLabels"; text: "文字"; selected: page.overlayLabels
                                 onClicked: page.overlayLabels = !page.overlayLabels }
                    FilterChip { objectName: "overlayMasks"; text: "掩码"; selected: page.overlayMasks
                                 onClicked: page.overlayMasks = !page.overlayMasks }
                    FilterChip { objectName: "overlayAllOff"; text: "全部关闭"; selected: !page.overlayAny
                                 onClicked: page.allOff() }
                }

                Repeater {
                    model: page.channels
                    Column {
                        objectName: "aiMetrics-" + modelData.cameraId
                        spacing: 0
                        Text {
                            color: Theme.color.text_primary
                            font.pixelSize: Theme.typography.scale.label.size
                            text: modelData.cameraId + " · " + modelData.profileLabel
                        }
                        Text {
                            color: Theme.color.text_secondary
                            font.pixelSize: Theme.typography.scale.caption.size
                            text: "模型：" + (modelData.modelVersion.length > 0 ? modelData.modelVersion : "—")
                        }
                        Text {
                            color: Theme.color.text_secondary
                            font.pixelSize: Theme.typography.scale.caption.size
                            text: "FPS：" + modelData.fpsText + "  端到端延迟：" + modelData.latencyMs + " ms"
                        }
                        Text {
                            color: Theme.color.text_secondary
                            font.pixelSize: Theme.typography.scale.caption.size
                            text: "最近结果：" + modelData.lastResultText + "  置信度：" + modelData.confidenceText +
                                  "  距离：" + (modelData.distanceText !== undefined ? modelData.distanceText : "—")
                        }
                        Text {
                            visible: !modelData.available
                            color: Theme.color.error
                            font.pixelSize: Theme.typography.scale.caption.size
                            text: modelData.unavailableReason
                        }
                    }
                }
            }
        }
    }
}
