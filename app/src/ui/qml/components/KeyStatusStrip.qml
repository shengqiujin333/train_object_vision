import QtQuick 2.15
import ".."

// 关键状态区（ITEM-016）：横向七项 受电弓/轮轨/车门/摄像头/AI/录像/存储。
// 每项=图标(形状字形)+名称+值；异常突出且可点击（按角色），正常简洁。
// 点击异常项：允许则跳转详情，不允许则由外壳显示“无权限 + 返回”。
Rectangle {
    id: strip
    property var items: (typeof railhmiKeyStatus !== "undefined") ? railhmiKeyStatus : []
    color: Theme.color.bg_sunken
    border.color: Theme.color.border
    border.width: Theme.border.width

    signal navigateRequested(string pageId, bool allowed)

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: Theme.spacing.s3
        spacing: Theme.spacing.s4

        Repeater {
            model: strip.items
            StatusItem {
                objectName: "status-" + modelData.key
                label: modelData.label
                value: modelData.value
                glyph: modelData.glyph
                colorToken: modelData.color
                abnormal: modelData.abnormal
                clickable: modelData.clickable
                onActivated: strip.navigateRequested(modelData.target, modelData.allowed)
            }
        }
    }
}
