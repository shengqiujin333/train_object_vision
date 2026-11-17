import QtQuick 2.15
import ".."
import "../components"

// P6 设备维护（2026-11-17 现场定稿）。
// 页面只保留两块，并铺满整屏：
//   1) 日志显示：功能操作的过程信息实时写入，可保存/导出（同时进结构化日志）；
//   2) 功能操作：摄像头检测 / 录像与存储 / 蓝牙扫描 / 故障自检 / 系统信息。
// 原来的分区卡片（摄像头、AI、录像与存储、网络与温度、云台、维护日志、网络录像机、
// BLE 门磁、权限矩阵、结构化日志、启动恢复进度、故障自检）按业主要求全部去掉，
// 相应能力改由上面的五个操作 + 日志呈现；云台归位/预置位仍可通过维护操作日志追踪。
Rectangle {
    id: page
    color: Theme.color.bg_base

    property string exportPath: ""

    function ops() { return (typeof railhmiMaintOps !== "undefined") ? railhmiMaintOps : null }

    // ---- 功能操作：五个一键操作，过程信息全部写进日志 ----
    function runCameraCheck() {
        var o = page.ops()
        if (!o) return
        var cams = (typeof railhmiCameras !== "undefined") ? railhmiCameras : []
        var list = []
        for (var i = 0; i < cams.length; ++i)
            list.push({ id: cams[i].id, name: cams[i].name, url: cams[i].url ? cams[i].url : "" })
        o.pingCameras(list)
    }
    function runNvrCheck() {
        var o = page.ops()
        if (!o) return
        o.appendLog("录像与存储", "开始检测 NVR 在线与登录…")
        o.checkNvr()
    }
    function runBleScan() {
        var o = page.ops()
        if (!o) return
        var devs = (typeof railhmiDoorDevices !== "undefined") ? railhmiDoorDevices : []
        var sum = (typeof railhmiDoorSummary !== "undefined") ? railhmiDoorSummary : ({})
        o.appendLog("蓝牙", "扫描结果：范围内门磁设备 " + devs.length + " 个；来源 " +
                            (sum.source !== undefined ? sum.source : "—"))
        for (var i = 0; i < devs.length; ++i) {
            var d = devs[i]
            o.appendLog("蓝牙", " · " + (d.name ? d.name : "未命名") + "  MAC " + d.mac +
                                "  RSSI " + d.rssi + " dBm  门态 " + d.doorLabel +
                                "  电量 " + (d.batteryText ? d.batteryText : "—") +
                                "  最近 " + (d.lastSeenText ? d.lastSeenText : "—"))
        }
        if (devs.length === 0)
            o.appendLog("蓝牙", "范围内未发现门磁：请确认传感器已上电并在板子附近，或检查 HCI/BlueZ 权限")
        else
            o.appendLog("蓝牙", "门磁状态汇总：" + (sum.value !== undefined ? sum.value : "—"))
    }
    function runSelfCheck() {
        var o = page.ops()
        if (!o) return
        o.appendLog("故障自检", "===== 自检开始 =====")
        var cams = (typeof railhmiCameras !== "undefined") ? railhmiCameras : []
        o.appendLog("故障自检", "配置：相机 " + cams.length + " 路")
        var live = 0
        var offline = 0
        for (var i = 0; i < railhmiChannels.length; ++i) {
            var info = railhmiChannels[i].info
            if (info.presentable) live++
            if (info.state === "offline") offline++
            o.appendLog("故障自检", info.id + " " + info.name + "：" + info.stateLabel +
                                    "  fps " + info.fpsText + "  最近帧 " + info.lastFrameText +
                                    (info.presentable ? "  → 正常" : "  → 异常"))
        }
        o.appendLog("故障自检", "视频汇总：" + live + " 路实时 / 离线 " + offline + " 路")
        var nvr = (typeof railhmiIntegrations !== "undefined") ? railhmiIntegrations : []
        for (var j = 0; j < nvr.length; ++j)
            o.appendLog("故障自检", "录像机 " + nvr[j].name + "：" + nvr[j].state +
                                    "（通道映射 " + nvr[j].channelMapping + "）")
        var ds = (typeof railhmiDoorSummary !== "undefined") ? railhmiDoorSummary : ({})
        o.appendLog("故障自检", "门磁：" + (ds.value !== undefined ? ds.value : "未接入") +
                                "（来源 " + (ds.source !== undefined ? ds.source : "—") + "）")
        var aiText = (typeof railhmiAi !== "undefined") ? railhmiAi.statusText : "未接入"
        var aiModel = (typeof railhmiAi !== "undefined") ? railhmiAi.detectModel : "—"
        o.appendLog("故障自检", "AI 模型：" + aiText + "，检测模型 " + aiModel)
        var faults = (typeof railhmiFaults !== "undefined") ? railhmiFaults : []
        o.appendLog("故障自检", "已登记故障场景 " + faults.length + " 项（FI-01..）")
        // WiFi 驱动/链路：明确给出「驱动是否正常」，而不是只报"有网"。
        var wifi = o.wifiCheckSummary()
        var wl = wifi.split("\n")
        for (var k = 0; k < wl.length; ++k)
            if (wl[k].length > 0) o.appendLog("故障自检", wl[k])
        o.appendLog("故障自检", "===== 自检结束 =====")
    }
    // U 盘导出：报警数据（CSV）+ 设备维护日志（txt）+ 清单；无 U 盘时如实提示。
    function exportToUsb() {
        var o = page.ops()
        if (!o) return
        var rows = []
        if (typeof railhmiAlarms !== "undefined" && railhmiAlarms)
            rows = railhmiAlarms.query("", "", "", "", "", "")
        o.appendLog("U盘导出", "开始导出（报警 " + rows.length + " 条 + 维护日志）…")
        var dir = o.exportToUsb(rows)
        if (dir.length === 0)
            o.appendLog("U盘导出", "未检测到可用 U 盘：请插入 U 盘后重试（支持 /media、/run/media、/mnt）")
    }
    function showSystemInfo() {        var o = page.ops()
        if (!o) return
        var info = o.systemInfo()
        o.appendLog("系统信息", "===== 系统信息 =====")
        for (var i = 0; i < info.length; ++i)
            o.appendLog("系统信息", info[i].group + " · " + info[i].key + "：" + info[i].value)
        o.appendLog("系统信息", "===== 结束 =====")
    }

    Column {
        anchors.fill: parent
        anchors.margins: Theme.spacing.s3
        spacing: Theme.spacing.s3

        // ---- 1) 日志显示（占据除操作条以外的全部高度）----
        Rectangle {
            id: logPanel
            objectName: "maintLogPanel"
            width: parent.width
            height: parent.height - opsBar.height - parent.spacing
            color: Theme.color.surface
            border.color: Theme.color.border
            border.width: Theme.border.width
            radius: Theme.radius.card

            readonly property var opsRef: (typeof railhmiMaintOps !== "undefined") ? railhmiMaintOps : null
            readonly property var logModel: opsRef ? opsRef.logLines : []

            Column {
                anchors.fill: parent
                anchors.margins: Theme.spacing.s2
                spacing: Theme.spacing.s1

                Row {
                    width: parent.width
                    spacing: Theme.spacing.s2
                    Text {
                        color: Theme.color.text_primary
                        font.pixelSize: Theme.typography.scale.label.size
                        text: "日志显示"
                    }
                    FilterChip {
                        objectName: "maintLogExport"
                        text: "保存/导出"
                        onClicked: { if (logPanel.opsRef) page.exportPath = logPanel.opsRef.exportLog() }
                    }
                    FilterChip {
                        objectName: "maintLogClear"
                        text: "清空"
                        onClicked: {
                            if (logPanel.opsRef) { logPanel.opsRef.clearLog(); page.exportPath = "" }
                        }
                    }
                }
                Text {
                    objectName: "maintLogExportPath"
                    width: parent.width
                    wrapMode: Text.WrapAnywhere
                    color: Theme.color.text_muted
                    font.pixelSize: Theme.typography.scale.caption.size
                    text: page.exportPath.length > 0
                          ? ("已保存：" + page.exportPath)
                          : "功能操作的过程信息会实时写入日志；点「保存/导出」落盘"
                }

                ListView {
                    objectName: "maintOpsLog"
                    width: parent.width
                    height: parent.height - 52
                    clip: true
                    model: logPanel.logModel
                    boundsBehavior: Flickable.StopAtBounds
                    delegate: Text {
                        width: ListView.view.width
                        wrapMode: Text.WrapAnywhere
                        color: Theme.color.text_secondary
                        font.pixelSize: Theme.typography.scale.caption.size
                        text: modelData
                    }
                    onCountChanged: positionViewAtEnd()
                }
            }
        }

        // ---- 2) 功能操作（固定高度，贴在底部）----
        Rectangle {
            id: opsBar
            objectName: "maintOpsBar"
            width: parent.width
            height: 56
            color: Theme.color.surface
            border.color: Theme.color.border
            border.width: Theme.border.width
            radius: Theme.radius.card

            Row {
                anchors.verticalCenter: parent.verticalCenter
                anchors.left: parent.left
                anchors.margins: Theme.spacing.s2
                spacing: Theme.spacing.s1
                Text {
                    color: Theme.color.text_primary
                    font.pixelSize: Theme.typography.scale.label.size
                    text: "功能操作"
                }
                FilterChip {
                    objectName: "maintOpsCamera"
                    text: "摄像头检测"
                    onClicked: page.runCameraCheck()
                }
                FilterChip {
                    objectName: "maintOpsNvr"
                    text: "录像与存储"
                    onClicked: page.runNvrCheck()
                }
                FilterChip {
                    objectName: "maintOpsBle"
                    text: "蓝牙扫描"
                    onClicked: page.runBleScan()
                }
                FilterChip {
                    objectName: "maintOpsSelfCheck"
                    text: "故障自检"
                    onClicked: page.runSelfCheck()
                }
                FilterChip {
                    objectName: "maintOpsSystem"
                    text: "系统信息"
                    onClicked: page.showSystemInfo()
                }
                FilterChip {
                    objectName: "maintOpsUsb"
                    text: "U盘导出"
                    onClicked: page.exportToUsb()
                }
            }
        }
    }
}
