import QtQuick
import QtQuick.Controls
import Carton

TextField {
    id: control

    implicitHeight: Theme.controlHeight
    leftPadding: 12
    rightPadding: 12
    selectByMouse: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.textBase
    color: Theme.textPrimary
    placeholderTextColor: Theme.textMuted
    selectionColor: Theme.accentSoft
    selectedTextColor: Theme.textPrimary

    background: Rectangle {
        radius: Theme.radiusSm
        color: !control.enabled ? Theme.surfaceDisabled : Theme.surface
        border.width: control.activeFocus ? 2 : 1
        border.color: control.activeFocus ? Theme.accent : Theme.border
        Behavior on border.color { ColorAnimation { duration: Theme.reducedMotion ? 0 : Theme.animationFast } }
    }
}
