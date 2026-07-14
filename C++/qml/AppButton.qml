import QtQuick
import QtQuick.Controls
import Carton

Button {
    id: control

    property bool tonal: false

    implicitHeight: Theme.controlHeight
    leftPadding: 14
    rightPadding: 14
    topPadding: 0
    bottomPadding: 0
    font.family: Theme.fontFamily
    font.pixelSize: Theme.textBase
    font.weight: Font.Medium
    hoverEnabled: true

    background: Rectangle {
        radius: Theme.radiusSm
        color: !control.enabled
               ? Theme.surfaceDisabled
               : control.highlighted
                 ? (control.pressed ? Theme.accentPressed : (control.hovered ? Theme.accentHover : Theme.accent))
                 : control.tonal
                   ? (control.pressed ? Theme.accentSoftHover : (control.hovered ? Theme.accentSoftHover : Theme.accentSoft))
                   : (control.pressed ? Theme.surfacePressed : (control.hovered ? Theme.surfaceHover : Theme.surface))
        border.width: control.highlighted ? 0 : 1
        border.color: control.tonal ? "transparent" : Theme.border
        Behavior on color { ColorAnimation { duration: Theme.reducedMotion ? 0 : Theme.animationFast } }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: !control.enabled
               ? Theme.textDisabled
               : control.highlighted
                 ? Theme.accentForeground
                 : control.tonal ? Theme.accent : Theme.textPrimary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
