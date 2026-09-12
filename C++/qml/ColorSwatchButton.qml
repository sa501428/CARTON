import QtQuick
import QtQuick.Controls
import Carton

// A colour picker entry that shows the colour it will edit. Plain text buttons
// gave no clue what "Low color" was currently set to, which made a custom map
// that happened to match a preset look like the setting did nothing.
AbstractButton {
    id: control

    property string label: ""
    property color swatch: "#000000"
    // Drawn muted when the colour is not currently in use, without disabling
    // the button: clicking it is what switches the map to Custom.
    property bool dimmed: false

    implicitHeight: Theme.controlHeight
    implicitWidth: Math.max(72, labelText.implicitWidth + 38)
    hoverEnabled: true
    Accessible.name: control.label + " color"

    background: Rectangle {
        radius: Theme.radiusSm
        color: control.pressed ? Theme.surfacePressed
                               : (control.hovered ? Theme.surfaceHover : Theme.surface)
        border.width: 1
        border.color: control.hovered ? Theme.borderStrong : Theme.border
    }

    contentItem: Row {
        spacing: 8
        leftPadding: 8
        rightPadding: 8
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: 16
            height: 16
            radius: 3
            color: control.swatch
            opacity: control.dimmed ? 0.4 : 1
            border.width: 1
            border.color: Theme.borderStrong
        }
        Label {
            id: labelText
            anchors.verticalCenter: parent.verticalCenter
            text: control.label
            color: control.dimmed ? Theme.textMuted : Theme.textPrimary
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textSm
        }
    }
}
