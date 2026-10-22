import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

ToolButton {
    id: control

    property color idleColor: "transparent"
    property color contentColor: Theme.textPrimary
    property bool onLightSurface: !Theme.dark
    property url iconSource: ""
    property int iconSize: 16

    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(Theme.controlHeight, implicitContentWidth + leftPadding + rightPadding)
    leftPadding: 10
    rightPadding: 10
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    font.family: Theme.fontFamily
    font.pixelSize: Theme.textSm
    font.weight: Font.Medium

    ToolTip.visible: hovered && Accessible.name.length > 0
    ToolTip.text: Accessible.name
    ToolTip.delay: 450

    background: Rectangle {
        radius: Theme.radiusSm
        border.width: control.activeFocus ? 2 : 0
        border.color: Theme.accent
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
