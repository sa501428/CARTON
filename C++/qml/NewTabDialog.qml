pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Dialog {
    id: root
    objectName: "newTabChooser"
    property var applicationWindow: null
    signal tabChosen(string type)
    property var choices: [
        {
            group: "Explore",
            type: "single",
            title: "Explore a map",
            detail: "One contact map with an optional control and layer overlays."
        },
        {
            group: "Compare",
            type: "multi-map",
            title: "Compare maps",
            detail: "Several maps side by side with linked navigation."
        },
        {
            group: "Explore regions",
            type: "multi-region",
            title: "Explore region pairs",
            detail: "One window for each pair in a BEDPE file."
        },
        {
            group: "Explore regions",
            type: "map-region",
            title: "Maps × regions",
            detail: "Compare the same regions across several maps."
        },
        {
            group: "Explore regions",
            type: "pairwise",
            title: "Pairwise region matrix",
            detail: "Inspect every combination of regions from a BED file."
        },
        {
            group: "Analyze a locus",
            type: "rotated-45",
            title: "45° diagonal heatmaps",
            detail: "Follow short-range contacts along the chromosome."
        },
        {
            group: "Analyze a locus",
            type: "bullseye",
            title: "SIP bullseye",
            detail: "Inspect the local contact pattern around an interaction."
        },
        {
            group: "Analyze a locus",
            type: "virtual-4c",
            title: "Virtual 4C",
            detail: "Plot contacts from an anchor and create a reusable track."
        },
        {
            group: "Analyze a locus",
            type: "processing",
            title: "Experimental processing",
            detail: "Explore filters and matrix transformations alongside the input."
        }
    ]
    title: "Create a tab"
    modal: true
    width: Math.min(620, applicationWindow ? applicationWindow.width - 32 : 620)
    height: Math.min(700, applicationWindow ? applicationWindow.height - 64 : 700)
    standardButtons: Dialog.Cancel
    background: DialogFrame {}
    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true
        ColumnLayout {
            width: parent.width
            spacing: 8
            Repeater {
                model: root.choices
                ColumnLayout {
                    id: choice
                    required property var modelData
                    required property int index
                    Layout.fillWidth: true
                    Label {
                        visible: choice.index === 0 || root.choices[choice.index - 1].group !== choice.modelData.group
                        text: choice.modelData.group
                        color: Theme.textSecondary
                        font.weight: Font.DemiBold
                        Layout.topMargin: 8
                    }
                    ItemDelegate {
                        id: choiceButton
                        Layout.fillWidth: true
                        implicitHeight: choiceText.implicitHeight + 24
                        Accessible.name: choice.modelData.title + ". " + choice.modelData.detail
                        background: Rectangle {
                            color: choiceButton.hovered || choiceButton.activeFocus ? Theme.selectedSurface : Theme.surface
                            border.color: choiceButton.activeFocus ? Theme.accent : Theme.border
                            radius: Theme.radiusSm
                        }
                        contentItem: ColumnLayout {
                            id: choiceText
                            Label {
                                text: choice.modelData.title
                                color: Theme.textPrimary
                                font.weight: Font.DemiBold
                            }
                            Label {
                                Layout.fillWidth: true
                                text: choice.modelData.detail
                                color: Theme.textSecondary
                                wrapMode: Text.WordWrap
                            }
                        }
                        onClicked: {
                            root.tabChosen(choice.modelData.type);
                            root.close();
                        }
                    }
                }
            }
        }
    }
}
