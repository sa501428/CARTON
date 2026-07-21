pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Rectangle {
    id: root

    property var controller: null
    property var tabSession: null
    property bool landscapeMode: false
    property bool hoverTextVisible: true
    property bool trackPanelsOpen: false
    property bool minimapVisible: true
    property string hoverText: ""
    property real interfaceScale: 1.0
    property real applicationFontScale: 1.0
    property bool reducedMotion: false

    property bool dataExpanded: true
    property bool viewControlsExpanded: true
    property bool displayExpanded: false
    property bool tracksExpanded: true
    property bool annotationsExpanded: true
    property bool chromosomesExpanded: false
    property bool bookmarksExpanded: false
    property bool resultsExpanded: false
    property bool searchResultsExpanded: false
    property bool statusExpanded: false

    signal toggleRequested()
    signal openDatasetRequested()
    signal loadControlRequested()
    signal loadTrackRequested()
    signal loadTrackUrlRequested()
    signal loadAnnotationsRequested()
    signal pooledResourceRequested(string resourceId, string kind)
    signal pooledControlRequested(string resourceId)
    signal exportAnnotationRequested(int index)
    signal annotationColorRequested(int index, color currentColor)
    signal trackMenuRequested(int index)
    signal trackBinEditorRequested(int index)
    signal landscapeModeToggled(bool enabled)
    signal hoverTextToggled(bool enabled)
    signal trackPanelsToggled(bool enabled)
    signal minimapToggled(bool enabled)
    signal interfaceScaleRequested(real value)
    signal fontScaleRequested(real value)
    signal reducedMotionToggled(bool enabled)
    signal lowColorRequested()
    signal highColorRequested()
    signal missingColorRequested()

    color: Theme.panelBg
    border.color: Theme.borderSubtle
    implicitWidth: 340

    function includesSearch(value) {
        return searchField.text.length === 0 || String(value).toLowerCase().indexOf(searchField.text.toLowerCase()) >= 0
    }

    function syncControllerSelections() {
        if (!root.controller) return
        chromosomeX.currentIndex = Math.max(0, chromosomeX.find(root.controller.chrX))
        chromosomeY.currentIndex = Math.max(0, chromosomeY.find(root.controller.chrY))
        resolutionBox.currentIndex = Math.max(0, resolutionBox.find(String(root.controller.resolution)))
        matrixBox.currentIndex = Math.max(0, matrixBox.find(root.controller.matrixType))
        normBox.currentIndex = Math.max(0, normBox.find(root.controller.norm))
        colorMapBox.currentIndex = Math.max(0, colorMapBox.find(root.controller.colorMap))
    }

    function syncControllerModels() {
        if (!root.controller) {
            chromosomeX.model = []
            chromosomeY.model = []
            resolutionBox.model = []
            matrixBox.model = []
            normBox.model = []
            return
        }
        chromosomeX.model = root.controller.chromosomeNames()
        chromosomeY.model = root.controller.chromosomeNames()
        resolutionBox.model = root.controller.resolutions()
        matrixBox.model = root.controller.matrixTypes()
        normBox.model = root.controller.normalizations()
        syncControllerSelections()
    }

    onControllerChanged: Qt.callLater(root.syncControllerModels)
    Component.onCompleted: syncControllerModels()

    Connections {
        target: root.controller
        function onMetadataChanged() { root.syncControllerModels() }
        function onViewChanged() { root.syncControllerSelections() }
        function onColorMapChanged() { root.syncControllerSelections() }
    }

    component SectionHeader: Rectangle {
        required property string title
        required property bool expanded
        property string detail: ""
        signal toggled()
        Layout.fillWidth: true
        implicitHeight: 40
        color: headerHover.hovered ? Theme.hoverSurface : "transparent"
        border.color: Theme.borderSubtle
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 10
            spacing: 8
            Label {
                text: parent.parent.expanded ? "⌄" : "›"
                color: Theme.textSecondary
                font.pixelSize: Theme.textMd
                Layout.preferredWidth: 14
            }
            Label {
                text: parent.parent.title
                color: Theme.textPrimary
                font.pixelSize: Theme.textSm
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            Label {
                visible: parent.parent.detail.length > 0
                text: parent.parent.detail
                color: Theme.textMuted
                font.pixelSize: Theme.textXs
            }
        }
        HoverHandler { id: headerHover }
        TapHandler { onTapped: parent.toggled() }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Layout.leftMargin: 14
            Layout.rightMargin: 6
            Label {
                text: "Workspace"
                color: Theme.textPrimary
                font.pixelSize: Theme.textLg
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            BusyIndicator {
                running: root.controller && root.controller.busy
                visible: running
                Layout.preferredWidth: 18
                Layout.preferredHeight: 18
            }
            AppToolButton {
                text: "‹"
                onLightSurface: true
                contentColor: Theme.textSecondary
                Accessible.name: "Collapse workspace"
                onClicked: root.toggleRequested()
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderSubtle }

        AppTextField {
            id: searchField
            Layout.fillWidth: true
            Layout.margins: 10
            placeholderText: "Search workspace"
            Accessible.name: "Search datasets, chromosomes, bookmarks, tracks, and annotations"
            onTextChanged: if (root.controller) {
                root.controller.workspaceSearch = text
                if (text.length > 0) root.searchResultsExpanded = true
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 0

                SectionHeader {
                    title: "Data"
                    expanded: root.dataExpanded
                    detail: root.controller && root.controller.filePath.length > 0 ? "loaded" : ""
                    onToggled: root.dataExpanded = !root.dataExpanded
                }
                ColumnLayout {
                    visible: root.dataExpanded
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.topMargin: 8
                    Layout.bottomMargin: 10
                    spacing: 7
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton { text: "Open Hi-C"; highlighted: true; Layout.fillWidth: true; onClicked: root.openDatasetRequested() }
                        AppButton {
                            text: root.controller && root.controller.controlReady ? "Control ✓" : "Control"
                            tonal: true
                            Layout.fillWidth: true
                            enabled: !!root.controller
                            onClicked: root.loadControlRequested()
                        }
                    }
                    Label { text: "RECENT DATASETS"; color: Theme.textMuted; font.pixelSize: Theme.textXs; font.weight: Font.DemiBold }
                    Repeater {
                        model: root.controller ? root.controller.datasetsModel : null
                        ItemDelegate {
                            id: datasetDelegate
                            required property var entry
                            Layout.fillWidth: true
                            height: 42
                            hoverEnabled: true
                            background: Rectangle { color: datasetDelegate.hovered ? Theme.hoverSurface : "transparent" }
                            contentItem: Column {
                                leftPadding: 6
                                Text { text: datasetDelegate.entry.name; color: Theme.textPrimary; font.pixelSize: Theme.textSm; elide: Text.ElideRight; width: parent.width - 12 }
                                Text { text: datasetDelegate.entry.path; color: Theme.textMuted; font.pixelSize: Theme.textXs; elide: Text.ElideMiddle; width: parent.width - 12 }
                            }
                            onClicked: {
                                if (root.tabSession) root.tabSession.setPrimaryFile(root.tabSession.activeCellIndex, datasetDelegate.entry.path)
                                else if (root.controller) root.controller.openRecentMap(datasetDelegate.entry.path)
                            }
                        }
                    }
                    Label {
                        visible: DatasetRegistry.resourceCount > 0
                        text: "SESSION DATASET POOL"
                        color: Theme.textMuted
                        font.pixelSize: Theme.textXs
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: DatasetRegistry.resourcesModel
                        ItemDelegate {
                            id: pooledDelegate
                            required property var entry
                            Layout.fillWidth: true
                            height: 42
                            hoverEnabled: true
                            background: Rectangle { color: pooledDelegate.hovered ? Theme.hoverSurface : "transparent" }
                            contentItem: RowLayout {
                                Label {
                                    text: pooledDelegate.entry.kind.toUpperCase()
                                    color: Theme.accent
                                    font.pixelSize: Theme.textXs
                                    font.weight: Font.Bold
                                    Layout.preferredWidth: 72
                                }
                                ColumnLayout {
                                    spacing: 0
                                    Layout.fillWidth: true
                                    Label { text: pooledDelegate.entry.name; color: Theme.textPrimary; font.pixelSize: Theme.textSm; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { text: pooledDelegate.entry.count + (pooledDelegate.entry.custom ? " custom" : " items"); color: Theme.textMuted; font.pixelSize: Theme.textXs }
                                }
                                AppToolButton {
                                    visible: pooledDelegate.entry.kind === "hic"
                                    text: "C"
                                    onLightSurface: true
                                    Accessible.name: "Use as control map"
                                    ToolTip.visible: hovered
                                    ToolTip.text: Accessible.name
                                    onClicked: root.pooledControlRequested(pooledDelegate.entry.id)
                                }
                            }
                            onClicked: root.pooledResourceRequested(pooledDelegate.entry.id, pooledDelegate.entry.kind)
                        }
                    }
                }

                SectionHeader {
                    title: "View & navigation"
                    expanded: root.viewControlsExpanded
                    detail: root.controller && root.controller.resolution > 0 ? root.controller.resolution + " bp" : ""
                    onToggled: root.viewControlsExpanded = !root.viewControlsExpanded
                }
                ColumnLayout {
                    visible: root.viewControlsExpanded
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.topMargin: 8
                    Layout.bottomMargin: 10
                    spacing: 8
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 2
                        columnSpacing: 8
                        rowSpacing: 5
                        Label { text: "Horizontal"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                        Label { text: "Vertical"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                        AppComboBox {
                            id: chromosomeX
                            Layout.fillWidth: true
                            enabled: root.controller && model.length > 0
                            onActivated: root.controller.chrX = currentText
                        }
                        AppComboBox {
                            id: chromosomeY
                            Layout.fillWidth: true
                            enabled: root.controller && model.length > 0
                            onActivated: root.controller.chrY = currentText
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Hi-C bin"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                        AppComboBox {
                            id: resolutionBox
                            Layout.fillWidth: true
                            enabled: root.controller && model.length > 0 && !root.controller.resolutionLocked
                            onActivated: root.controller.resolution = Number(currentText)
                        }
                        AppToolButton {
                            text: root.controller && root.controller.resolutionLocked ? "🔒" : "🔓"
                            onLightSurface: true
                            enabled: root.controller && root.controller.resolution > 0
                            onClicked: root.controller.resolutionLocked = !root.controller.resolutionLocked
                        }
                    }
                    Label { text: "Matrix display"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                    AppComboBox { id: matrixBox; Layout.fillWidth: true; enabled: !!root.controller; onActivated: root.controller.matrixType = currentText }
                    Label { text: "Normalization"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                    AppComboBox { id: normBox; Layout.fillWidth: true; enabled: root.controller && model.length > 0; onActivated: root.controller.norm = currentText }
                    AppTextField { id: topLocationField; Layout.fillWidth: true; placeholderText: "Horizontal chr:start-end"; enabled: root.controller && root.controller.filePath.length > 0; onAccepted: root.controller.goTo(text, leftLocationField.text.length > 0 ? leftLocationField.text : text) }
                    AppTextField { id: leftLocationField; Layout.fillWidth: true; placeholderText: "Vertical locus (optional)"; enabled: root.controller && root.controller.filePath.length > 0; onAccepted: root.controller.goTo(topLocationField.text, text.length > 0 ? text : topLocationField.text) }
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton { text: "Go"; tonal: true; Layout.fillWidth: true; enabled: root.controller && topLocationField.text.length > 0; onClicked: root.controller.goTo(topLocationField.text, leftLocationField.text.length > 0 ? leftLocationField.text : topLocationField.text) }
                        AppButton { text: "Reset"; tonal: true; Layout.fillWidth: true; enabled: root.controller && root.controller.filePath.length > 0; onClicked: root.controller.resetView() }
                        AppButton { text: "All"; tonal: true; Layout.fillWidth: true; enabled: root.controller && root.controller.filePath.length > 0; onClicked: root.controller.setWholeGenomeView() }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton { text: "− Zoom"; tonal: true; Layout.fillWidth: true; enabled: root.controller && root.controller.filePath.length > 0; onClicked: root.controller.zoom(0.5, 0.5, 0.5) }
                        AppButton { text: "+ Zoom"; tonal: true; Layout.fillWidth: true; enabled: root.controller && root.controller.filePath.length > 0; onClicked: root.controller.zoom(2.0, 0.5, 0.5) }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton { text: "Undo"; tonal: true; Layout.fillWidth: true; enabled: root.controller && root.controller.canUndoView; onClicked: root.controller.undoView() }
                        AppButton { text: "Redo"; tonal: true; Layout.fillWidth: true; enabled: root.controller && root.controller.canRedoView; onClicked: root.controller.redoView() }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        AppCheckBox { text: "Lock X"; checked: root.controller && root.controller.xLocusLocked; onToggled: if (root.controller) root.controller.xLocusLocked = checked }
                        AppCheckBox { text: "Lock Y"; checked: root.controller && root.controller.yLocusLocked; onToggled: if (root.controller) root.controller.yLocusLocked = checked }
                    }
                    AppCheckBox { text: "Show 1D plotting panels"; checked: root.trackPanelsOpen; onToggled: root.trackPanelsToggled(checked) }
                    AppCheckBox { text: "Landscape viewport"; checked: root.landscapeMode; onToggled: root.landscapeModeToggled(checked) }
                }

                SectionHeader {
                    title: "Display & performance"
                    expanded: root.displayExpanded
                    onToggled: root.displayExpanded = !root.displayExpanded
                }
                ColumnLayout {
                    visible: root.displayExpanded
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.topMargin: 8
                    Layout.bottomMargin: 10
                    spacing: 8
                    AppCheckBox { text: "Show minimap"; checked: root.minimapVisible; onToggled: root.minimapToggled(checked) }
                    AppCheckBox { text: "Show hover text"; checked: root.hoverTextVisible; onToggled: root.hoverTextToggled(checked) }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        AppCheckBox { text: "Gridlines"; checked: root.controller && root.controller.showGridlines; onToggled: if (root.controller) root.controller.showGridlines = checked }
                        AppCheckBox { text: "Chr context"; checked: root.controller && root.controller.showChromosomeContext; onToggled: if (root.controller) root.controller.showChromosomeContext = checked }
                        AppCheckBox { text: "Endpoints"; checked: root.controller && root.controller.axisEndpointsOnly; onToggled: if (root.controller) root.controller.axisEndpointsOnly = checked }
                        AppCheckBox { text: "Tile debug"; checked: root.controller && root.controller.showTilesDebug; onToggled: if (root.controller) root.controller.showTilesDebug = checked }
                    }
                    Label { text: "Heatmap color scale"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                    AppComboBox {
                        id: colorMapBox
                        Layout.fillWidth: true
                        model: ["White-Red", "Viridis", "Blue-White-Red", "Grayscale", "Custom"]
                        onActivated: if (root.controller) root.controller.colorMap = currentText
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        AppCheckBox { text: "Symmetric"; checked: root.controller && root.controller.symmetricColorScale; onToggled: if (root.controller) root.controller.symmetricColorScale = checked }
                        AppCheckBox { text: "Transparent zero"; checked: root.controller && root.controller.zeroTransparent; onToggled: if (root.controller) root.controller.zeroTransparent = checked }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Min"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                        AppTextField { Layout.fillWidth: true; text: root.controller ? String(root.controller.colorMin) : ""; onAccepted: if (root.controller && isFinite(Number(text))) root.controller.colorMin = Number(text) }
                        Label { text: "Max"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                        AppTextField { Layout.fillWidth: true; text: root.controller ? String(root.controller.colorMax) : ""; onAccepted: if (root.controller && isFinite(Number(text))) root.controller.colorMax = Number(text) }
                        AppButton { text: "Auto"; tonal: true; enabled: root.controller && !root.controller.colorMaxAuto; onClicked: root.controller.resetColorScale() }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Clip"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                        Slider { Layout.fillWidth: true; from: 50; to: 100; stepSize: 0.5; value: root.controller ? root.controller.colorPercentile : 95; onMoved: if (root.controller) root.controller.colorPercentile = value }
                        Label { text: root.controller ? root.controller.colorPercentile.toFixed(1) + "%" : ""; color: Theme.textMuted; font.pixelSize: Theme.textXs }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton { text: "Low color"; tonal: true; Layout.fillWidth: true; onClicked: root.lowColorRequested() }
                        AppButton { text: "High color"; tonal: true; Layout.fillWidth: true; onClicked: root.highColorRequested() }
                        AppButton { text: "Missing"; tonal: true; Layout.fillWidth: true; onClicked: root.missingColorRequested() }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Cache"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                        SpinBox { from: 16; to: 4096; stepSize: 16; editable: true; value: root.controller ? root.controller.cacheLimitMB : 128; onValueModified: if (root.controller) root.controller.cacheLimitMB = value }
                        Label { text: "MB"; color: Theme.textMuted; font.pixelSize: Theme.textSm }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "UI"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                        Slider { Layout.fillWidth: true; from: 0.85; to: 1.35; stepSize: 0.05; value: root.interfaceScale; onMoved: root.interfaceScaleRequested(value) }
                        Label { text: Math.round(root.interfaceScale * 100) + "%"; color: Theme.textMuted; font.pixelSize: Theme.textXs }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Font"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                        Slider { Layout.fillWidth: true; from: 0.85; to: 1.4; stepSize: 0.05; value: root.applicationFontScale; onMoved: root.fontScaleRequested(value) }
                        Label { text: Math.round(root.applicationFontScale * 100) + "%"; color: Theme.textMuted; font.pixelSize: Theme.textXs }
                    }
                    AppCheckBox { text: "Reduce motion"; checked: root.reducedMotion; onToggled: root.reducedMotionToggled(checked) }
                }

                SectionHeader {
                    title: "1D tracks"
                    expanded: root.tracksExpanded
                    detail: root.controller ? String(root.controller.trackCount) : ""
                    onToggled: root.tracksExpanded = !root.tracksExpanded
                }
                ColumnLayout {
                    visible: root.tracksExpanded
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    Layout.topMargin: 8
                    Layout.bottomMargin: 10
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton { text: "Load"; highlighted: true; Layout.fillWidth: true; onClicked: root.loadTrackRequested() }
                        AppButton { text: "URL"; tonal: true; Layout.fillWidth: true; onClicked: root.loadTrackUrlRequested() }
                        AppButton { text: "Clear"; tonal: true; Layout.fillWidth: true; enabled: root.controller && root.controller.trackCount > 0; onClicked: root.controller.clearTracks() }
                    }
                    Repeater {
                        model: root.controller ? root.controller.tracksModel : null
                        Rectangle {
                            id: trackCard
                            required property var entry
                            Layout.fillWidth: true
                            implicitHeight: trackCardColumn.implicitHeight + 16
                            radius: Theme.radiusMd
                            color: Theme.surface
                            border.color: Theme.border
                            ColumnLayout {
                                id: trackCardColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 8
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    AppCheckBox { checked: trackCard.entry.visible; onToggled: root.controller.setTrackVisible(trackCard.entry.index, checked) }
                                    AppToolButton { text: trackCard.entry.collapsed ? "›" : "⌄"; onLightSurface: true; onClicked: root.controller.setTrackCollapsed(trackCard.entry.index, !trackCard.entry.collapsed) }
                                    AppTextField { Layout.fillWidth: true; text: trackCard.entry.name; onAccepted: root.controller.setTrackName(trackCard.entry.index, text) }
                                    AppToolButton { text: "⋯"; onLightSurface: true; onClicked: root.trackMenuRequested(trackCard.entry.index) }
                                }
                                Label { visible: !trackCard.entry.collapsed; text: trackCard.entry.featureCount + " intervals · " + trackCard.entry.format; color: Theme.textMuted; font.pixelSize: Theme.textXs }
                                RowLayout {
                                    visible: !trackCard.entry.collapsed && trackCard.entry.renderMode === "signal"
                                    Layout.fillWidth: true
                                    AppCheckBox { text: "Auto"; checked: trackCard.entry.autoscale; onToggled: root.controller.setTrackAutoscale(trackCard.entry.index, checked) }
                                    AppCheckBox { text: "Log"; checked: trackCard.entry.logScale; onToggled: root.controller.setTrackRange(trackCard.entry.index, trackCard.entry.min, trackCard.entry.max, checked) }
                                    AppComboBox { Layout.fillWidth: true; model: ["min", "mean", "max", "none"]; currentIndex: Math.max(0, model.indexOf(trackCard.entry.reduction)); onActivated: root.controller.setTrackReduction(trackCard.entry.index, currentText) }
                                }
                                RowLayout {
                                    visible: !trackCard.entry.collapsed && trackCard.entry.renderMode === "signal"
                                    Layout.fillWidth: true
                                    Label { text: trackCard.entry.binSize === 0 ? "Bin: Hi-C · " + root.controller.resolution : "Bin: " + trackCard.entry.effectiveBinSize; color: Theme.textSecondary; font.pixelSize: Theme.textXs; Layout.fillWidth: true }
                                    AppButton { text: "Match"; tonal: true; onClicked: root.controller.setTrackBinSize(trackCard.entry.index, 0) }
                                    AppButton { text: "Set…"; tonal: true; onClicked: root.trackBinEditorRequested(trackCard.entry.index) }
                                }
                                RowLayout {
                                    visible: !trackCard.entry.collapsed && trackCard.entry.renderMode === "signal"
                                    Layout.fillWidth: true
                                    Label { text: "Min"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                                    AppTextField { Layout.fillWidth: true; text: String(trackCard.entry.min); enabled: !trackCard.entry.autoscale; onAccepted: root.controller.setTrackRange(trackCard.entry.index, Number(text), trackCard.entry.max, trackCard.entry.logScale) }
                                    Label { text: "Max"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                                    AppTextField { Layout.fillWidth: true; text: String(trackCard.entry.max); enabled: !trackCard.entry.autoscale; onAccepted: root.controller.setTrackRange(trackCard.entry.index, trackCard.entry.min, Number(text), trackCard.entry.logScale) }
                                }
                                RowLayout {
                                    visible: !trackCard.entry.collapsed
                                    Layout.fillWidth: true
                                    Label { text: "Height"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                                    AppTextField {
                                        Layout.fillWidth: true
                                        text: String(trackCard.entry.height)
                                        inputMethodHints: Qt.ImhDigitsOnly
                                        validator: IntValidator { bottom: 20 }
                                        onAccepted: if (acceptableInput) root.controller.setTrackHeight(trackCard.entry.index, Math.round(Number(text)))
                                        onEditingFinished: if (acceptableInput) root.controller.setTrackHeight(trackCard.entry.index, Math.round(Number(text)))
                                    }
                                    Label { text: "px"; color: Theme.textMuted; font.pixelSize: Theme.textXs }
                                }
                            }
                        }
                    }
                }

                SectionHeader {
                    title: "2D annotations"
                    expanded: root.annotationsExpanded
                    detail: root.controller ? String(root.controller.annotationCount) : ""
                    onToggled: root.annotationsExpanded = !root.annotationsExpanded
                }
                ColumnLayout {
                    visible: root.annotationsExpanded
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    Layout.topMargin: 8
                    Layout.bottomMargin: 10
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton { text: "Load"; highlighted: true; Layout.fillWidth: true; onClicked: root.loadAnnotationsRequested() }
                        AppButton { text: "New"; tonal: true; Layout.fillWidth: true; onClicked: if (root.controller) root.controller.addAnnotationLayer("Layer") }
                        AppButton { text: "Merge"; tonal: true; Layout.fillWidth: true; onClicked: if (root.controller) root.controller.mergeVisibleAnnotationLayers("Merged") }
                        AppButton { text: "Clear"; tonal: true; Layout.fillWidth: true; enabled: root.controller && root.controller.annotationCount > 0; onClicked: root.controller.clearAnnotations() }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Sparse limit"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                        SpinBox { Layout.fillWidth: true; from: 1; to: 1000000; editable: true; value: root.controller ? root.controller.sparseFeatureLimit : 10000; onValueModified: if (root.controller) root.controller.sparseFeatureLimit = value }
                    }
                    Repeater {
                        model: root.controller ? root.controller.annotationsModel : null
                        Rectangle {
                            id: annotationCard
                            required property var entry
                            Layout.fillWidth: true
                            implicitHeight: annotationColumn.implicitHeight + 16
                            radius: Theme.radiusMd
                            color: annotationCard.entry.active ? Theme.accentSoft : Theme.surface
                            border.color: annotationCard.entry.active ? Theme.accent : Theme.border
                            ColumnLayout {
                                id: annotationColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 8
                                spacing: 5
                                RowLayout {
                                    Layout.fillWidth: true
                                    AppCheckBox { checked: annotationCard.entry.visible; onToggled: root.controller.setAnnotationLayerVisible(annotationCard.entry.index, checked) }
                                    Label { text: annotationCard.entry.name + " · " + annotationCard.entry.count; color: Theme.textPrimary; Layout.fillWidth: true; elide: Text.ElideRight }
                                    AppButton { text: "Active"; tonal: true; enabled: !annotationCard.entry.active; onClicked: root.controller.setActiveAnnotationLayer(annotationCard.entry.index) }
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 3
                                    AppCheckBox { text: "Transparent"; checked: annotationCard.entry.transparent; onToggled: root.controller.setAnnotationLayerTransparent(annotationCard.entry.index, checked) }
                                    AppCheckBox { text: "Sparse"; checked: annotationCard.entry.sparse; onToggled: root.controller.setAnnotationLayerSparse(annotationCard.entry.index, checked) }
                                    AppCheckBox { text: "Large"; checked: annotationCard.entry.enlarged; onToggled: root.controller.setAnnotationLayerEnlarged(annotationCard.entry.index, checked) }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    Rectangle {
                                        Layout.preferredWidth: 18
                                        Layout.preferredHeight: 18
                                        radius: 4
                                        color: annotationCard.entry.color
                                        border.color: Theme.borderStrong
                                    }
                                    AppButton {
                                        text: annotationCard.entry.colorOverride ? "Recolor" : "Override color"
                                        tonal: true
                                        onClicked: root.annotationColorRequested(annotationCard.entry.index, annotationCard.entry.color)
                                    }
                                    AppButton {
                                        visible: annotationCard.entry.colorOverride
                                        text: "Original colors"
                                        tonal: true
                                        onClicked: root.controller.clearAnnotationLayerColorOverride(annotationCard.entry.index)
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 5
                                    Label {
                                        text: "Diagonal placement"
                                        color: Theme.textSecondary
                                        font.pixelSize: Theme.textXs
                                    }
                                    AppComboBox {
                                        model: ["Both sides", "Above only", "Below only"]
                                        currentIndex: annotationCard.entry.placement === "above" ? 1 :
                                                      (annotationCard.entry.placement === "below" ? 2 : 0)
                                        onActivated: root.controller.setAnnotationLayerPlacement(
                                            annotationCard.entry.index,
                                            currentIndex === 1 ? "above" : (currentIndex === 2 ? "below" : "both"))
                                        Layout.fillWidth: true
                                    }
                                }
                                RowLayout {
                                    Layout.fillWidth: true
                                    AppButton { text: "Duplicate"; tonal: true; Layout.fillWidth: true; onClicked: root.controller.duplicateAnnotationLayer(annotationCard.entry.index) }
                                    AppButton { text: "Export"; tonal: true; Layout.fillWidth: true; onClicked: root.exportAnnotationRequested(annotationCard.entry.index) }
                                    AppButton { text: "Clear"; tonal: true; Layout.fillWidth: true; onClicked: root.controller.clearAnnotationLayer(annotationCard.entry.index) }
                                    AppButton { text: "Delete"; tonal: true; Layout.fillWidth: true; enabled: root.controller.annotationCount > 0; onClicked: root.controller.removeAnnotationLayer(annotationCard.entry.index) }
                                }
                            }
                        }
                    }
                }

                SectionHeader { title: "Chromosomes"; expanded: root.chromosomesExpanded; detail: root.controller ? String(root.controller.chromosomeNames().length) : ""; onToggled: root.chromosomesExpanded = !root.chromosomesExpanded }
                ColumnLayout {
                    visible: root.chromosomesExpanded
                    Layout.fillWidth: true
                    spacing: 2
                    Repeater {
                        model: root.controller ? root.controller.chromosomeNames() : []
                        RowLayout {
                            id: chromosomeRow
                            required property var modelData
                            visible: root.includesSearch(chromosomeRow.modelData)
                            Layout.fillWidth: true; Layout.leftMargin: 12; Layout.rightMargin: 10
                            Label { text: chromosomeRow.modelData; color: Theme.textPrimary; Layout.fillWidth: true; font.pixelSize: Theme.textSm }
                            AppToolButton { text: "X"; onLightSurface: true; onClicked: root.controller.chrX = chromosomeRow.modelData }
                            AppToolButton { text: "Y"; onLightSurface: true; onClicked: root.controller.chrY = chromosomeRow.modelData }
                        }
                    }
                }

                SectionHeader { title: "Bookmarks"; expanded: root.bookmarksExpanded; onToggled: root.bookmarksExpanded = !root.bookmarksExpanded }
                ColumnLayout {
                    visible: root.bookmarksExpanded
                    Layout.fillWidth: true
                    spacing: 2
                    AppButton { text: "+ Save current location"; tonal: true; Layout.fillWidth: true; Layout.margins: 10; onClicked: if (root.controller) root.controller.saveCurrentLocation("") }
                    Repeater {
                        model: root.controller ? root.controller.bookmarksModel : null
                        Rectangle {
                            id: bookmarkCard
                            required property var entry
                            required property int index
                            Layout.fillWidth: true; Layout.leftMargin: 8; Layout.rightMargin: 8
                            height: 54
                            color: bookmarkHover.hovered ? Theme.hoverSurface : "transparent"
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 4
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 1
                                    Label { text: bookmarkCard.entry.name || "Saved locus"; color: Theme.textPrimary; font.pixelSize: Theme.textSm; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { text: bookmarkCard.entry.chrX + ":" + bookmarkCard.entry.x0 + "–" + bookmarkCard.entry.x1; color: Theme.textMuted; font.pixelSize: Theme.textXs; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                AppToolButton { text: "×"; onLightSurface: true; onClicked: root.controller.removeSavedLocation(bookmarkCard.index) }
                            }
                            HoverHandler { id: bookmarkHover }
                            TapHandler { onTapped: root.controller.restoreSavedLocation(bookmarkCard.index) }
                        }
                    }
                }

                SectionHeader { title: "Results"; expanded: root.resultsExpanded; onToggled: root.resultsExpanded = !root.resultsExpanded }
                ColumnLayout {
                    visible: root.resultsExpanded
                    Layout.fillWidth: true; Layout.margins: 14
                    Label { text: "No analysis results yet"; color: Theme.textPrimary; font.pixelSize: Theme.textSm; font.weight: Font.DemiBold }
                    Label { text: "Derived tracks and analysis outputs will appear here."; color: Theme.textMuted; font.pixelSize: Theme.textXs; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }

                SectionHeader { title: "Search results"; expanded: root.searchResultsExpanded; onToggled: root.searchResultsExpanded = !root.searchResultsExpanded }
                ColumnLayout {
                    visible: root.searchResultsExpanded
                    Layout.fillWidth: true
                    Repeater {
                        model: root.controller ? root.controller.searchResultsModel : null
                        ItemDelegate {
                            id: searchDelegate
                            required property var entry
                            Layout.fillWidth: true; height: 48; hoverEnabled: true
                            background: Rectangle { color: searchDelegate.hovered ? Theme.hoverSurface : "transparent" }
                            contentItem: Column {
                                leftPadding: 12
                                Text { text: searchDelegate.entry.label; color: Theme.textPrimary; font.pixelSize: Theme.textSm; elide: Text.ElideRight; width: parent.width - 20 }
                                Text { text: searchDelegate.entry.kind.toUpperCase() + " · " + searchDelegate.entry.detail; color: Theme.textMuted; font.pixelSize: Theme.textXs; elide: Text.ElideMiddle; width: parent.width - 20 }
                            }
                            onClicked: {
                                if (searchDelegate.entry.kind === "dataset") root.controller.openRecentMap(searchDelegate.entry.path)
                                else if (searchDelegate.entry.kind === "bookmark") root.controller.restoreSavedLocation(searchDelegate.entry.index)
                                else if (searchDelegate.entry.kind === "track") root.controller.setTrackVisible(searchDelegate.entry.index, true)
                                else if (searchDelegate.entry.kind === "annotation") root.controller.setActiveAnnotationLayer(searchDelegate.entry.index)
                            }
                        }
                    }
                }

                SectionHeader { title: "Status"; expanded: root.statusExpanded; detail: root.controller ? root.controller.recordCount + " records" : ""; onToggled: root.statusExpanded = !root.statusExpanded }
                ColumnLayout {
                    visible: root.statusExpanded
                    Layout.fillWidth: true
                    Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.topMargin: 8; Layout.bottomMargin: 14
                    spacing: 5
                    Label { Layout.fillWidth: true; text: root.controller && root.controller.filePath.length > 0 ? root.controller.filePath : "No file loaded"; color: Theme.textMuted; font.pixelSize: Theme.textXs; elide: Text.ElideMiddle }
                    Label { Layout.fillWidth: true; text: root.controller ? root.controller.chrX + ":" + root.controller.x0 + "–" + root.controller.x1 : ""; color: Theme.textPrimary; font.pixelSize: Theme.textSm; wrapMode: Text.WrapAnywhere }
                    Label { Layout.fillWidth: true; text: root.controller ? root.controller.chrY + ":" + root.controller.y0 + "–" + root.controller.y1 : ""; color: Theme.textPrimary; font.pixelSize: Theme.textSm; wrapMode: Text.WrapAnywhere }
                    Label { Layout.fillWidth: true; text: root.controller ? root.controller.genomeId + " · " + root.controller.matrixDimensions + " · " + root.controller.resolution + " bp" : ""; color: Theme.textSecondary; font.pixelSize: Theme.textXs; wrapMode: Text.WordWrap }
                    Label { Layout.fillWidth: true; text: root.controller ? "Cache " + root.controller.cacheMemoryMB.toFixed(1) + "/" + root.controller.cacheLimitMB + " MB · " + root.controller.renderingBackend : ""; color: Theme.textSecondary; font.pixelSize: Theme.textXs; wrapMode: Text.WordWrap }
                    Label { Layout.fillWidth: true; text: root.controller ? root.controller.status : ""; color: Theme.textSecondary; font.pixelSize: Theme.textSm; wrapMode: Text.WordWrap }
                    Rectangle {
                        visible: root.hoverTextVisible && root.hoverText.length > 0
                        Layout.fillWidth: true
                        implicitHeight: statusHoverText.implicitHeight + 16
                        radius: Theme.radiusSm; color: Theme.surfaceSunken; border.color: Theme.borderSubtle
                        Label { id: statusHoverText; anchors.fill: parent; anchors.margins: 8; text: root.hoverText; color: Theme.textPrimary; font.pixelSize: Theme.textXs; wrapMode: Text.WordWrap }
                    }
                }
            }
        }
    }
}
