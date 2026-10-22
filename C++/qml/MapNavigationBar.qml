pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Rectangle {
    id: root
    property var controller: null
    property var tabSession: null
    property var chromosomes: []
    property var resolutions: []
    property string resolutionContext: ""
    property bool separateAxes: false
    readonly property bool loaded: controller && controller.filePath.length > 0
    readonly property string editingLabel: {
        if (!tabSession || !tabSession.cellLayout.length)
            return "No map selected";
        var cell = tabSession.cellLayout[Math.max(0, tabSession.activeCellIndex)];
        return cell ? "Editing: " + cell.label : "No map selected";
    }
    signal inspectRequested
    signal colorsRequested
    function syncModels() {
        chromosomes = controller ? controller.chromosomeNames() : [];
        resolutions = controller ? controller.resolutions() : [];
        resolutionContext = controller ? controller.chrX + "\n" + controller.chrY : "";
        syncLoci(true);
    }
    function syncLoci(reset) {
        if (controller && loaded && (controller.chrX !== controller.chrY || controller.x0 !== controller.y0 || controller.x1 !== controller.y1))
            separateAxes = true;
        if (reset || !locusX.activeFocus)
            locusX.text = controller && loaded ? controller.chrX + ":" + controller.x0 + "-" + controller.x1 : "";
        if (reset || !locusY.activeFocus)
            locusY.text = controller && loaded ? controller.chrY + ":" + controller.y0 + "-" + controller.y1 : "";
    }
    function focusLocus() {
        locusX.forceActiveFocus();
        locusX.selectAll();
    }
    function go() {
        if (controller && locusX.text.trim().length)
            controller.goTo(locusX.text, separateAxes ? locusY.text : locusX.text);
    }
    onControllerChanged: Qt.callLater(root.syncModels)
    Component.onCompleted: syncModels()
    Connections {
        target: root.controller
        function onMetadataChanged() {
            root.syncModels();
        }
        function onViewChanged() {
            if (root.controller && root.resolutionContext !== root.controller.chrX + "\n" + root.controller.chrY)
                root.syncModels();
            else
                root.syncLoci();
        }
    }
    implicitHeight: contents.implicitHeight + 16
    color: Theme.surface
    border.color: Theme.borderSubtle
    ColumnLayout {
        id: contents
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        spacing: 6
        RowLayout {
            Layout.fillWidth: true
            AppToolButton {
                text: "←"
                Accessible.name: "Back in navigation history"
                enabled: root.controller && root.controller.canUndoView
                onClicked: root.controller.undoView()
            }
            AppToolButton {
                text: "→"
                Accessible.name: "Forward in navigation history"
                enabled: root.controller && root.controller.canRedoView
                onClicked: root.controller.redoView()
            }
            GridLayout {
                Layout.fillWidth: true
                columns: root.width < 760 ? 1 : 2
                AppTextField {
                    id: locusX
                    objectName: "navigationLocus"
                    Layout.fillWidth: true
                    Layout.minimumWidth: 90
                    placeholderText: "Go to locus, e.g. chr22:10000000-14000000"
                    Accessible.name: "Horizontal locus"
                    enabled: root.loaded
                    onAccepted: root.go()
                }
                AppTextField {
                    id: locusY
                    Layout.fillWidth: true
                    visible: root.separateAxes
                    placeholderText: "Vertical locus"
                    Accessible.name: "Vertical locus"
                    enabled: root.loaded
                    onAccepted: root.go()
                }
            }
            AppButton {
                text: "Go"
                highlighted: true
                enabled: root.loaded && locusX.text.trim().length > 0
                onClicked: root.go()
            }
            AppButton {
                text: "Fit chromosome"
                visible: root.width > 700
                enabled: root.loaded
                onClicked: root.controller.resetView()
            }
            AppToolButton {
                text: "⋯"
                Accessible.name: "More navigation options"
                onClicked: navigationMenu.popup()
            }
        }
        Flow {
            Layout.fillWidth: true
            spacing: 8
            Row {
                spacing: 6
                Label {
                    text: "X"
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.textSecondary
                }
                AppComboBox {
                    width: 96
                    model: root.chromosomes
                    currentIndex: root.controller ? model.indexOf(root.controller.chrX) : -1
                    enabled: root.loaded
                    Accessible.name: "Horizontal chromosome"
                    onActivated: root.controller.chrX = currentText
                }
                Label {
                    text: "Y"
                    anchors.verticalCenter: parent.verticalCenter
                    color: Theme.textSecondary
                }
                AppComboBox {
                    width: 96
                    model: root.chromosomes
                    currentIndex: root.controller ? model.indexOf(root.controller.chrY) : -1
                    enabled: root.loaded
                    Accessible.name: "Vertical chromosome"
                    onActivated: root.controller.chrY = currentText
                }
            }
            Row {
                spacing: 6
                Label {
                    text: "Resolution"
                    color: Theme.textSecondary
                    anchors.verticalCenter: parent.verticalCenter
                }
                AppComboBox {
                    width: 100
                    model: root.resolutions
                    currentIndex: root.controller ? model.indexOf(root.controller.resolution) : -1
                    displayText: currentIndex >= 0 ? Number(currentText).toLocaleString(Qt.locale(), 'f', 0) + " bp" : "—"
                    enabled: root.loaded && !root.controller.resolutionLocked
                    Accessible.name: "Hi-C resolution"
                    onActivated: root.controller.resolution = Number(currentText)
                }
                AppCheckBox {
                    text: "Fixed"
                    checked: root.controller && root.controller.resolutionLocked
                    enabled: root.loaded
                    onToggled: if (root.controller)
                        root.controller.resolutionLocked = checked
                }
            }
            Repeater {
                model: [
                    {
                        key: "pan",
                        label: "Pan"
                    },
                    {
                        key: "zoom",
                        label: "Zoom region"
                    },
                    {
                        key: "annotate",
                        label: "Annotate"
                    }
                ]
                AppButton {
                    id: toolButton
                    required property var modelData
                    text: toolButton.modelData.label
                    checkable: true
                    autoExclusive: true
                    checked: InteractionState.tool === toolButton.modelData.key
                    tonal: checked
                    enabled: root.loaded
                    onClicked: InteractionState.tool = toolButton.modelData.key
                    Accessible.name: toolButton.modelData.label + " tool"
                }
            }
            AppButton {
                width: Math.min(implicitWidth, Math.max(160, root.width - 16))
                text: "Calculation: " + (root.controller ? root.controller.matrixType : "observed") + " · " + (root.controller ? root.controller.norm : "NONE")
                onClicked: root.inspectRequested()
                Accessible.name: "Inspect calculation and normalization"
            }
            Row {
                spacing: 6
                Label {
                    text: "Palette"
                    color: Theme.textSecondary
                    anchors.verticalCenter: parent.verticalCenter
                }
                AppComboBox {
                    objectName: "quickHeatmapPalette"
                    width: 156
                    model: ["White-Red", "Viridis", "Blue-White-Red", "Grayscale", "Custom"]
                    currentIndex: root.controller ? Math.max(0, model.indexOf(root.controller.colorMap)) : 0
                    enabled: root.loaded
                    Accessible.name: "Heatmap color palette"
                    onActivated: root.controller.colorMap = currentText
                }
            }
            Row {
                spacing: 4
                Label {
                    text: "Color"
                    color: Theme.textSecondary
                    anchors.verticalCenter: parent.verticalCenter
                }
                AppToolButton {
                    objectName: "decreaseHeatmapColor"
                    text: "−"
                    enabled: root.loaded
                    Accessible.name: "Less color intensity"
                    ToolTip.text: "Less color: double the color-range maximum"
                    onClicked: root.controller.scaleColorRange(2.0)
                }
                AppToolButton {
                    objectName: "increaseHeatmapColor"
                    text: "+"
                    enabled: root.loaded
                    Accessible.name: "More color intensity"
                    ToolTip.text: "More color: halve the color-range maximum"
                    onClicked: root.controller.scaleColorRange(0.5)
                }
                AppButton {
                    text: "Auto"
                    enabled: root.loaded && !root.controller.colorMaxAuto
                    onClicked: root.controller.resetColorScale()
                    Accessible.name: "Restore automatic heatmap color range"
                }
                AppButton {
                    text: "Colors…"
                    onClicked: root.colorsRequested()
                    Accessible.name: "Heatmap colors and range settings"
                }
            }
        }
        Label {
            Layout.fillWidth: true
            text: root.editingLabel
            color: Theme.accent
            font.pixelSize: Theme.textSm
            elide: Text.ElideRight
            Accessible.name: text
        }
    }
    Menu {
        id: navigationMenu
        MenuItem {
            text: "Fit chromosome"
            enabled: root.loaded
            onTriggered: root.controller.resetView()
        }
        MenuItem {
            text: "Whole genome (All)"
            enabled: root.loaded && root.chromosomes.some(function (c) {
                return String(c).toLowerCase() === "all";
            })
            onTriggered: root.controller.setWholeGenomeView()
        }
        MenuItem {
            text: "Separate horizontal and vertical loci"
            checkable: true
            checked: root.separateAxes
            onTriggered: root.separateAxes = checked
        }
        MenuItem {
            text: "Lock horizontal locus"
            checkable: true
            checked: root.controller && root.controller.xLocusLocked
            onTriggered: if (root.controller)
                root.controller.xLocusLocked = checked
        }
        MenuItem {
            text: "Lock vertical locus"
            checkable: true
            checked: root.controller && root.controller.yLocusLocked
            onTriggered: if (root.controller)
                root.controller.yLocusLocked = checked
        }
        MenuSeparator {}
        MenuItem {
            text: "Wheel: zoom · Alt-drag: zoom region · Shift-drag: annotate"
            enabled: false
        }
    }
}
