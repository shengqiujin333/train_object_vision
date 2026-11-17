import QtQuick 2.15
import ".."

// 顶栏右侧资源读数：内存占用率 + 每个 CPU 核占用率 + 每个 NPU 核占用率。
// 数值全部来自 SystemMetricsRuntime 对 /proc、/sys 的真实采样；取不到时显示「—」
// 或明确标注「聚合」，绝不把单个值复制成多个核、也绝不填 0 冒充正常。
// 两行紧凑排布（caption 行高 ×2），避免 1280 顶栏里挤掉左侧六个状态项。
Item {
    id: meters
    objectName: "resourceMeters"
    property var m: (typeof railhmiSystemMetrics !== "undefined") ? railhmiSystemMetrics.metrics : ({})
    width: col.implicitWidth
    height: col.implicitHeight

    function coresOf(key) {
        var v = meters.m !== undefined && meters.m !== null ? meters.m[key] : undefined
        return v !== undefined && v !== null ? v : []
    }
    function loadColor(v) {
        if (v >= 90) return Theme.color.error
        if (v >= 70) return Theme.color.degraded
        return Theme.color.text_primary
    }
    function pctText(v) {
        return v !== undefined && v >= 0 ? v + "%" : "—"
    }

    Column {
        id: col
        spacing: 0

        Row {
            spacing: Theme.spacing.s2
            height: Theme.typography.scale.caption.line
            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: meters.loadColor(meters.m.memoryPct)
                font.pixelSize: Theme.typography.scale.caption.size
                font.family: Theme.typography.family.mono
                text: "内存 " + meters.pctText(meters.m.memoryPct)
            }
            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.color.text_secondary
                font.pixelSize: Theme.typography.scale.caption.size
                text: "CPU " + (meters.m.cpuCount !== undefined ? meters.m.cpuCount : 0) + "核"
            }
            Repeater {
                model: meters.coresOf("cpuCores")
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: meters.loadColor(modelData)
                    font.pixelSize: Theme.typography.scale.caption.size
                    font.family: Theme.typography.family.mono
                    text: modelData + "%"
                }
            }
            Text {
                visible: meters.m.cpuCount === 0
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.color.degraded
                font.pixelSize: Theme.typography.scale.caption.size
                text: "未接入"
            }
        }

        Row {
            spacing: Theme.spacing.s2
            height: Theme.typography.scale.caption.line
            Text {
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.color.text_secondary
                font.pixelSize: Theme.typography.scale.caption.size
                text: "NPU " + (meters.m.npuCount !== undefined ? meters.m.npuCount : 0) + "核"
            }
            Repeater {
                model: meters.coresOf("npuCores")
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    color: meters.loadColor(modelData)
                    font.pixelSize: Theme.typography.scale.caption.size
                    font.family: Theme.typography.family.mono
                    text: modelData + "%"
                }
            }
            Text {
                visible: meters.m.npuPerCoreAvailable !== true
                anchors.verticalCenter: parent.verticalCenter
                color: Theme.color.degraded
                font.pixelSize: Theme.typography.scale.caption.size
                font.family: Theme.typography.family.mono
                text: meters.m.npuAvailable === true
                      ? ("聚合 " + meters.pctText(meters.m.npuAggregatePct)) : "未接入"
            }
        }
    }
}
