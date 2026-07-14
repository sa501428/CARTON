import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

ToolButton {
    id: control

    property color idleColor: "transparent"
    property color contentColor: Theme.chromeText
    property bool onLightSurface: false
    property url iconSource: ""
    property int iconSize: 16

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
        Behavior on color { ColorAnimation { duration: Theme.reducedMotion ? 0 : Theme.animationFast } }
    }

    contentItem: RowLayout {
        spacing: control.iconSource.toString().length > 0 && control.text.length > 0 ? 6 : 0
        SvgIcon {
            visible: control.iconSource.toString().length > 0
            source: control.iconSource
            color: control.enabled ? control.contentColor : Theme.textDisabled
            Layout.preferredWidth: visible ? control.iconSize : 0
            Layout.preferredHeight: visible ? control.iconSize : 0
        }
        Text {
            visible: control.text.length > 0
            text: control.text
            font: control.font
            color: control.enabled ? control.contentColor : Theme.textDisabled
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            Layout.alignment: Qt.AlignVCenter
        }
    }
}
