import QtQuick
import QtQuick.Controls
import Carton

ToolButton {
    id: control

    property color idleColor: "transparent"
    property color contentColor: Theme.chromeText
    property bool onLightSurface: false

    implicitHeight: 30
    implicitWidth: Math.max(30, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: 10
    rightPadding: 10
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.textSm
    font.weight: Font.Medium

    background: Rectangle {
        radius: Theme.radiusSm
        color: {
            if (!control.enabled) return "transparent"
            var tint = control.onLightSurface ? Qt.rgba(0, 0, 0, 1) : Qt.rgba(1, 1, 1, 1)
            if (control.pressed) return Qt.rgba(tint.r, tint.g, tint.b, control.onLightSurface ? 0.09 : 0.16)
            if (control.hovered) return Qt.rgba(tint.r, tint.g, tint.b, control.onLightSurface ? 0.05 : 0.09)
            return control.idleColor
        }
        Behavior on color { ColorAnimation { duration: 90 } }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? control.contentColor : Theme.textDisabled
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
