import QtQuick
import QtQuick.Controls
import Carton

TabButton {
    id: control

    implicitHeight: 38
    leftPadding: 16
    rightPadding: 16
    hoverEnabled: true
    focusPolicy: Qt.StrongFocus
    Accessible.name: text
    font.family: Theme.fontFamily
    font.pixelSize: Theme.textBase
    font.weight: control.checked ? Font.DemiBold : Font.Medium

    ToolTip.visible: hovered && text.length > 0
    ToolTip.text: text
    ToolTip.delay: 450

    background: Rectangle {
        border.width: control.activeFocus ? 1 : 0
        border.color: Theme.accent
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
