import QtQuick
import QtQuick.Controls
import Carton

CheckBox {
    id: control

    implicitHeight: 24
    spacing: 8
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.textBase

    indicator: Rectangle {
        x: control.leftPadding
        y: (control.height - height) / 2
        width: 18
        height: 18
        radius: 5
        color: control.checked ? Theme.accent : (control.enabled ? Theme.surface : Theme.surfaceDisabled)
        border.width: control.checked ? 0 : 1
        border.color: control.hovered ? Theme.borderStrong : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.reducedMotion ? 0 : Theme.animationFast } }

        Text {
            anchors.centerIn: parent
            visible: control.checked
            text: "✓"
            color: Theme.accentForeground
            font.pixelSize: 12
            font.bold: true
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        leftPadding: control.indicator.width + control.spacing
    }
}
