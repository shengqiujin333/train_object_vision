import QtQuick 2.15
import RailHmi.Spike 1.0

// CPU 回退路径候选: appsink(NV12) -> librga NV12->BGR -> QImage -> FrameItem
// FrameItem 由 C++ 注册; 图像经排队信号投递到 UI 线程绘制。
Item {
    id: root
    Rectangle { anchors.fill: parent; color: "black" }

    FrameItem {
        objectName: "frameItem"
        anchors.fill: parent
    }

    Text {
        anchors.centerIn: parent
        color: "#E6EDF3"
        font.pixelSize: 20
        text: "railhmi render spike — path=cpu (RGA + Qt 绘制)"
    }
}
