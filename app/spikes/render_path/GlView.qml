import QtQuick 2.15
import org.freedesktop.gstreamer.GstGL 1.0

// GL 路径候选: decodebin ! glupload ! glcolorconvert ! qmlglsink
// 由 C++ 侧把 qmlglsink 的 widget 属性指向本 GstGLVideoItem。
Item {
    id: root
    Rectangle { anchors.fill: parent; color: "black" }

    GstGLVideoItem {
        objectName: "videoItem"
        anchors.fill: parent
    }

    // 仅在无帧时可见的提示 (视频帧会覆盖在其上)
    Text {
        anchors.centerIn: parent
        color: "#E6EDF3"
        font.pixelSize: 20
        text: "railhmi render spike — path=gl (qmlglsink)"
    }
}
