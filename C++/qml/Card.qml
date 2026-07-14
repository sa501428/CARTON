import QtQuick
import QtQuick.Effects
import Carton

Item {
    id: root

    property alias color: surface.color
    property alias radius: surface.radius
    property alias border: surface.border
    property bool elevated: false

    implicitWidth: surface.implicitWidth
    implicitHeight: surface.implicitHeight

    Rectangle {
        id: shadowSource
        anchors.fill: surface
        radius: surface.radius
        color: "black"
        visible: root.elevated
    }

    MultiEffect {
        anchors.fill: shadowSource
        source: shadowSource
        visible: root.elevated
        shadowEnabled: true
        shadowColor: Theme.shadow
        shadowVerticalOffset: 2
        shadowBlur: 0.55
        shadowOpacity: 0.5
        blurMax: 24
    }

    Rectangle {
        id: surface
        anchors.fill: parent
        color: Theme.surface
        radius: Theme.radiusMd
        border.width: 1
        border.color: Theme.border
    }
}
