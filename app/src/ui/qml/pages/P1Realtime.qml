import QtQuick 2.15
import ".."
import "../components"

// P1 实时监控（ITEM-011/012）：左-前-右三联；司机页不显示检测职责徽标。
Rectangle {
    id: page
    color: Theme.color.bg_base

    function camByPosition(pos) {
        for (var i = 0; i < railhmiCameras.length; ++i)
            if (railhmiCameras[i].position === pos) return railhmiCameras[i]
        return null
    }

    Row {
        anchors.fill: parent
        anchors.margins: Theme.spacing.s4
        spacing: Theme.spacing.s3

        CameraTile {
            objectName: "tileLeft"
            width: (parent.width - 2 * Theme.spacing.s3) * 0.26
            height: parent.height
            title: page.camByPosition("left") ? page.camByPosition("left").name : "左侧"
            state: page.camByPosition("left") ? page.camByPosition("left").state : "连接中"
            showBadge: false
        }
        CameraTile {
            objectName: "tileFront"
            width: (parent.width - 2 * Theme.spacing.s3) * 0.48
            height: parent.height
            title: page.camByPosition("primary") ? page.camByPosition("primary").name : "前方"
            state: page.camByPosition("primary") ? page.camByPosition("primary").state : "连接中"
            showBadge: false
        }
        CameraTile {
            objectName: "tileRight"
            width: (parent.width - 2 * Theme.spacing.s3) * 0.26
            height: parent.height
            title: page.camByPosition("right") ? page.camByPosition("right").name : "右侧"
            state: page.camByPosition("right") ? page.camByPosition("right").state : "连接中"
            showBadge: false
        }
    }
}
