import QtQuick
import QtQuick.Controls
import Carton

ComboBox {
    id: control

    implicitHeight: 32
    leftPadding: 12
    rightPadding: 30
    hoverEnabled: true
    font.family: Theme.fontFamily
    font.pixelSize: Theme.textBase

    background: Rectangle {
        radius: Theme.radiusSm
        color: !control.enabled
               ? Theme.surfaceDisabled
               : control.pressed ? Theme.surfacePressed : (control.hovered ? Theme.surfaceHover : Theme.surface)
        border.width: 1
        border.color: control.activeFocus ? Theme.accent : Theme.border
        Behavior on border.color { ColorAnimation { duration: 90 } }
    }

    contentItem: Text {
        text: control.displayText
        font: control.font
        color: control.enabled ? Theme.textPrimary : Theme.textDisabled
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }

    indicator: Text {
        x: control.width - width - 10
        y: (control.height - height) / 2
        text: "⌄"
        font.pixelSize: Theme.textMd
        color: control.enabled ? Theme.textSecondary : Theme.textDisabled
    }

    delegate: ItemDelegate {
        required property var modelData
        required property int index
        width: control.width
        height: 30
        highlighted: control.highlightedIndex === index
        background: Rectangle {
            color: parent.highlighted ? Theme.accentSoft : "transparent"
            radius: Theme.radiusSm
        }
        contentItem: Text {
            text: control.textRole ? modelData[control.textRole] : modelData
            font.family: Theme.fontFamily
            font.pixelSize: Theme.textBase
            color: Theme.textPrimary
            verticalAlignment: Text.AlignVCenter
            leftPadding: 8
        }
    }

    popup: Popup {
        y: control.height + 4
        width: control.width
        implicitHeight: Math.min(320, contentItem.implicitHeight + 8)
        padding: 4
        background: Rectangle {
            color: Theme.surface
            radius: Theme.radiusMd
            border.width: 1
            border.color: Theme.border
        }
        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            ScrollIndicator.vertical: ScrollIndicator {}
        }
    }
}
