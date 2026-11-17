import QtQuick 2.15
import ".."
import "../components"

// P5 AI 调试（ITEM-017/021 / 交互 §5.5、视觉 §8.5）。
// 展示：检测框 / ROI / 置信度 / 距离 / 帧率 / 端到端延迟 / 模型版本 / 最近结果时间。
// 调试叠加层逐项开关（框/ROI/文字/掩码），**默认全部关闭**；描边用独立高对比色
// （框 Theme.color.debug_box=#00E5FF，ROI 虚线 Theme.color.debug_roi=#FFD400），避免与报警红混淆。
// 所有角色均可查看本页；调试叠加仍由显式开关控制。
Rectangle {
    id: page
    color: Theme.color.bg_base

    readonly property var channels: (typeof railhmiAiDebug !== "undefined") ? railhmiAiDebug : []
    readonly property bool allowed: true

    // ---- AI 模型接入与现场替换（2026-11-17）----
    // 车前方/受电弓/轮轨当前共用同一个检测模型；车前方另用分割模型做两阶段判定。
    // 候选来自配置 app.ai.catalog；点选后 C++ 侧重建检测器与推理线程（立即生效）。
    readonly property var catalog: (typeof railhmiAiCatalog !== "undefined") ? railhmiAiCatalog : []
    readonly property string detectModel: (typeof railhmiAi !== "undefined") ? railhmiAi.detectModel : ""
    readonly property string segmentModel: (typeof railhmiAi !== "undefined") ? railhmiAi.segmentModel : ""
    readonly property string modelsDir: (typeof railhmiAi !== "undefined") ? railhmiAi.modelsDir : ""
    function modelsOf(kind) {
        var out = []
        for (var i = 0; i < page.catalog.length; ++i)
            if (page.catalog[i].kind === kind) out.push(page.catalog[i])
        return out
    }
    function selectModel(kind, name) {
        if (typeof railhmiAi === "undefined") return
        railhmiAi.selectModel(kind, name)
    }

    // 调试叠加层：逐项开关，默认全部关闭（交互 §5.5 隔离要求）
    property bool overlayBoxes: false
    property bool overlayRoi: false
    property bool overlayLabels: false
    property bool overlayMasks: false
    readonly property bool overlayAny: overlayBoxes || overlayRoi || overlayLabels || overlayMasks
    function allOff() { overlayBoxes = false; overlayRoi = false; overlayLabels = false; overlayMasks = false }

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

                // ---- AI 模型（当前接入 + 现场替换）----
                Rectangle {
                    width: parent.width
                    height: 1
                    color: Theme.color.border
                }
                Text {
                    color: Theme.color.text_primary
                    font.pixelSize: Theme.typography.scale.label.size
                    text: "AI 模型（点击即替换并重载）"
                }
                Text {
                    objectName: "aiDetectModel"
                    color: Theme.color.text_secondary
                    font.pixelSize: Theme.typography.scale.caption.size
                    text: "检测模型：" + (page.detectModel.length > 0 ? page.detectModel : "—") +
                          "（车前方/受电弓/轮轨共用）"
                }
                Flow {
                    objectName: "aiDetectModelChips"
                    width: parent.width
                    spacing: Theme.spacing.s1
                    Repeater {
                        model: page.modelsOf("detect")
                        FilterChip {
                            text: modelData.name
                            selected: modelData.name === page.detectModel
                            onClicked: page.selectModel("detect", modelData.name)
                        }
                    }
                }
                Text {
                    objectName: "aiSegmentModel"
                    color: Theme.color.text_secondary
                    font.pixelSize: Theme.typography.scale.caption.size
                    text: "分割模型：" + (page.segmentModel.length > 0 ? page.segmentModel : "—") +
                          "（仅车前方两阶段）"
                }
                Flow {
                    objectName: "aiSegmentModelChips"
                    width: parent.width
                    spacing: Theme.spacing.s1
                    Repeater {
                        model: page.modelsOf("segment")
                        FilterChip {
                            text: modelData.name
                            selected: modelData.name === page.segmentModel
                            onClicked: page.selectModel("segment", modelData.name)
                        }
                    }
                }
                Text {
                    color: Theme.color.text_muted
                    font.pixelSize: Theme.typography.scale.caption.size
                    width: parent.width
                    wrapMode: Text.Wrap
                    text: "模型目录：" + (page.modelsDir.length > 0 ? page.modelsDir : "—")
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
                            text: "模型：" + (modelData.modelText !== undefined && modelData.modelText.length > 0
                                             ? modelData.modelText : "—")
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
