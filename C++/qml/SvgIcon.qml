import QtQuick
import QtQuick.Effects

Item {
    id: root
    property url source
    property color color: "white"

    Image {
        id: iconImage
        anchors.fill: parent
        source: root.source
        sourceSize.width: Math.max(1, width * Screen.devicePixelRatio)
        sourceSize.height: Math.max(1, height * Screen.devicePixelRatio)
        fillMode: Image.PreserveAspectFit
        smooth: true
        visible: false
    }

    MultiEffect {
        anchors.fill: parent
        source: iconImage
        colorization: 1
        colorizationColor: root.color
    }
}
