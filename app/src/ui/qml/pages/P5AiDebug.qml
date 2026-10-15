import QtQuick 2.15
import ".."

// P5 AI 调试（ITEM-017 / 交互 §5.5、视觉 §8.5）：检测框 / ROI / 置信度 / 帧率 / 延迟 / 模型版本 /
// 最近结果时间。调试叠加层默认关闭；仅 ai_enabled 相机出现；推理线程停止时每路显式“AI 不可用”。
// 司机角色不渲染本页（外壳按角色过滤）。颜色/字号均来自 Theme。
Rectangle {
    id: page
    color: Theme.color.bg_base

    readonly property var channels: (typeof railhmiAiDebug !== "undefined") ? railhmiAiDebug : []
    property bool overlayVisible: false   // 调试叠加层默认关闭

    Row {
        anchors.fill: parent
        anchors.margins: Theme.spacing.s3
        spacing: Theme.spacing.s3

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

                    // 视频占位（真实像素由媒体层提供）
                    Rectangle {
                        anchors.fill: parent
                        anchors.margins: Theme.spacing.s1
                        color: Theme.color.bg_base
                    }

                    // 非可用时叠加占位提示
                    Text {
                        anchors.centerIn: parent
                        visible: !modelData.available
                        color: Theme.color.text_secondary
                        font.pixelSize: Theme.typography.scale.body.size
                        text: modelData.unavailableReason
                    }

                    // ROI（职责区域）
                    Repeater {
                        model: (page.overlayVisible && modelData.available) ? modelData.roi : []
                        Rectangle {
                            x: modelData.x * parent.width
                            y: modelData.y * parent.height
                            width: modelData.w * parent.width
                            height: modelData.h * parent.height
                            color: "transparent"
                            border.color: Theme.color.accent
                            border.width: 1
                        }
                    }

                    // 检测框 + 置信度（调试叠加层默认关闭）
                    Repeater {
                        model: (page.overlayVisible && modelData.available) ? modelData.boxes : []
                        Rectangle {
                            objectName: "aiBox"
                            x: modelData.x * parent.width
                            y: modelData.y * parent.height
                            width: modelData.w * parent.width
                            height: modelData.h * parent.height
                            color: "transparent"
                            border.color: Theme.color.ok
                            border.width: 2
                            Text {
                                color: Theme.color.text_primary
                                font.pixelSize: Theme.typography.scale.caption.size
                                text: modelData.label + " " + (Math.round(modelData.confidence * 100)) + "%"
                            }
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

        // 右：指标面板
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

                Row {
                    spacing: Theme.spacing.s2
                    Text {
                        color: Theme.color.text_primary
                        font.pixelSize: Theme.typography.scale.label.size
                        text: "调试叠加层"
                    }
                    Rectangle {
                        objectName: "overlayToggle"
                        width: Theme.size.button.sm
                        height: Theme.size.button.sm
                        radius: Theme.radius.chip
                        color: page.overlayVisible ? Theme.color.accent : Theme.color.neutral_bg
                        border.color: Theme.color.border
                        border.width: Theme.border.width
                        MouseArea {
                            anchors.fill: parent
                            onClicked: page.overlayVisible = !page.overlayVisible
                        }
                    }
                    Text {
                        color: Theme.color.text_secondary
                        font.pixelSize: Theme.typography.scale.caption.size
                        text: page.overlayVisible ? "开" : "关（默认）"
                    }
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
                            text: "FPS：" + modelData.fpsText + "  延迟：" + modelData.latencyMs + " ms"
                        }
                        Text {
                            color: Theme.color.text_secondary
                            font.pixelSize: Theme.typography.scale.caption.size
                            text: "最近结果：" + modelData.lastResultText + "  置信度：" + modelData.confidenceText
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
