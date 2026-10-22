pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Rectangle {
    id: root
    property var tabSession: null
    signal loadRegionsRequested(string format)
    readonly property string kind: tabSession ? tabSession.type : "single"
    readonly property bool multiSource: ["multi-map", "map-region", "rotated-45", "bullseye", "virtual-4c", "processing"].indexOf(kind) >= 0
    readonly property string defaultTarget: kind === "map-region" ? "This map across regions" : (multiSource ? "Selected cell" : "All cells in this tab")
    readonly property var scopeOptions: {
        var choices = [
            {
                id: "default",
                label: defaultTarget + " (default)"
            },
            {
                id: "cell",
                label: "Selected cell"
            }
        ];
        if (kind === "map-region") {
            choices.push({
                id: "map",
                label: "This map across regions"
            });
            choices.push({
                id: "region",
                label: "This region across maps"
            });
        } else if (kind === "pairwise")
            choices.push({
                id: "region",
                label: "Selected region row and column"
            });
        choices.push({
            id: "tab",
            label: "All cells in this tab"
        });
        return choices;
    }
    color: Theme.surfaceAlt
    border.color: Theme.borderSubtle
    implicitHeight: tools.implicitHeight + 16
    Flow {
        id: tools
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        spacing: 8
        AppButton {
            visible: root.multiSource
            text: "Add map"
            onClicked: root.tabSession.addMap()
        }
        AppButton {
            visible: ["multi-region", "map-region"].indexOf(root.kind) >= 0
            text: "Load region pairs…"
            onClicked: root.loadRegionsRequested("bedpe")
        }
        AppButton {
            visible: root.kind === "pairwise"
            text: "Load regions…"
            onClicked: regionMenu.popup()
        }
        AppButton {
            text: "Layout…"
            onClicked: layoutMenu.popup()
        }
        AppButton {
            text: "Linking…"
            visible: root.tabSession && root.tabSession.cellCount > 1
            onClicked: linkingMenu.popup()
        }
        Row {
            spacing: 6
            Label {
                id: layerTargetLabel
                text: "Add layers to"
                color: Theme.textSecondary
                anchors.verticalCenter: parent.verticalCenter
            }
            AppComboBox {
                objectName: "layerTarget"
                width: Math.min(250, Math.max(80, tools.width - layerTargetLabel.implicitWidth - 6))
                model: root.scopeOptions
                textRole: "label"
                valueRole: "id"
                currentIndex: {
                    for (var i = 0; i < root.scopeOptions.length; ++i)
                        if (root.tabSession && root.scopeOptions[i].id === root.tabSession.layerScope)
                            return i;
                    return 0;
                }
                onActivated: if (root.tabSession)
                    root.tabSession.layerScope = currentValue
                Accessible.name: "Layer destination"
            }
        }
    }
    Menu {
        id: regionMenu
        MenuItem {
            text: "Load BED regions…"
            onTriggered: root.loadRegionsRequested("bed")
        }
        MenuItem {
            text: "Use BEDPE endpoints as regions…"
            onTriggered: root.loadRegionsRequested("bedpe-as-bed")
        }
    }
    Menu {
        id: layoutMenu
        MenuItem {
            text: "Transpose maps and regions"
            visible: root.kind === "map-region"
            checkable: true
            checked: root.tabSession && root.tabSession.transposed
            onTriggered: root.tabSession.transposed = checked
        }
        Menu {
            title: "Columns"
            enabled: ["multi-map", "multi-region"].indexOf(root.kind) >= 0
            Repeater {
                model: 8
                MenuItem {
                    id: columnOption
                    required property int index
                    text: String(columnOption.index + 1)
                    checkable: true
                    checked: root.tabSession && root.tabSession.layoutColumns === columnOption.index + 1
                    onTriggered: root.tabSession.layoutColumns = columnOption.index + 1
                }
            }
        }
        Menu {
            title: "Diagonal cells"
            enabled: root.kind === "pairwise"
            MenuItem {
                text: "Split primary / control"
                checkable: true
                checked: root.tabSession && root.tabSession.diagonalMode === "split"
                onTriggered: root.tabSession.diagonalMode = "split"
            }
            MenuItem {
                text: "Hide diagonal"
                checkable: true
                checked: root.tabSession && root.tabSession.diagonalMode === "blank"
                onTriggered: root.tabSession.diagonalMode = "blank"
            }
        }
        MenuItem {
            text: "Remove selected map"
            visible: root.multiSource
            enabled: root.tabSession && root.tabSession.mapCount > 1
            onTriggered: {
                var cell = root.tabSession.cellLayout[root.tabSession.activeCellIndex];
                if (cell)
                    root.tabSession.removeMap(cell.mapIndex);
            }
        }
    }
    Menu {
        id: linkingMenu
        MenuItem {
            text: "Link navigation"
            visible: root.multiSource
            checkable: true
            checked: root.tabSession && root.tabSession.linkNavigation
            onTriggered: root.tabSession.linkNavigation = checked
        }
        MenuItem {
            text: "Link cursors"
            checkable: true
            checked: root.tabSession && root.tabSession.linkCrosshair
            onTriggered: root.tabSession.linkCrosshair = checked
        }
        MenuItem {
            text: "Link color ranges"
            checkable: true
            checked: root.tabSession && root.tabSession.linkColorScale
            onTriggered: root.tabSession.linkColorScale = checked
        }
    }
}
