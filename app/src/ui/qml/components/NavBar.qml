import QtQuick 2.15
import ".."

// 固定导航（ITEM-011）。只渲染当前角色可见的页面（entries 由 C++ 按角色过滤后注入）。
Item {
    id: nav
    property var entries: []       // [{ id, title }]，已按角色过滤
    property string activeId: ""
    signal navigate(string pageId)

    Rectangle {
        anchors.fill: parent
        color: Theme.color.surface
        border.color: Theme.color.border
        border.width: Theme.border.width

        Column {
            anchors.fill: parent
            Repeater {
                model: nav.entries
                Rectangle {
                    width: nav.width
                    height: Theme.size.nav_width
                    color: modelData.id === nav.activeId ? Theme.color.surface_hover : Theme.color.surface

                    Text {
                        anchors.centerIn: parent
                        width: parent.width - Theme.spacing.s2
                        horizontalAlignment: Text.AlignHCenter
                        wrapMode: Text.WordWrap
                        color: Theme.color.text_primary
                        font.pixelSize: Theme.typography.scale.label.size
                        text: modelData.title
                    }
                    MouseArea {
                        anchors.fill: parent
                        onClicked: nav.navigate(modelData.id)
                    }
                }
            }
        }
    }
}
