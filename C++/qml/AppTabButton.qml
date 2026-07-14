import QtQuick
import QtQuick.Controls
import Carton

TabButton {
    id: control

    implicitHeight: 38
    leftPadding: 16
    rightPadding: 16
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.textBase
    font.weight: control.checked ? Font.DemiBold : Font.Medium

    background: Rectangle {
        color: control.hovered && !control.checked ? Theme.surfaceHover : "transparent"

        Rectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 2
            color: control.checked ? Theme.accent : "transparent"
        }
    }

    contentItem: Text {
        text: control.text
        font: control.font
        color: control.checked ? Theme.textPrimary : Theme.textSecondary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
}
