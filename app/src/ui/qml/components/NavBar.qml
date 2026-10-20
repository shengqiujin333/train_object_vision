import QtQuick 2.15
import ".."

// 六个功能页始终显示，entries 由 C++ 注入。
Item {
    id: nav
    objectName: "navigation"
    property var entries: []       // [{ id, title }]
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
                    objectName: "nav-" + modelData.id
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
