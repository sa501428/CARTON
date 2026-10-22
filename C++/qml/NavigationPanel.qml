pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Rectangle {
    id: root
    objectName: "workspaceSidebar"

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

    property int currentPage: 0
    property int selectedTrackIndex: -1
    property int selectedAnnotationIndex: -1
    signal colorSettingsRequested
    signal settingsRequested
    signal clearTracksRequested
    signal clearAnnotationsRequested
    function showPage(page) {
        currentPage = page;
        searchField.clear();
    }
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

    signal toggleRequested
    signal openDatasetRequested
    signal loadControlRequested
    signal loadTrackRequested
    signal loadTrackUrlRequested
    signal loadAnnotationsRequested
    signal pooledResourceRequested(string resourceId, string kind)
    signal pooledControlRequested(string resourceId)
    signal exportAnnotationRequested(int index)
    signal annotationColorRequested(int index, color currentColor)
    signal trackMenuRequested(int index)
    signal trackNameEditorRequested(int index)
    signal trackBinEditorRequested(int index)
    signal landscapeModeToggled(bool enabled)
    signal hoverTextToggled(bool enabled)
    signal trackPanelsToggled(bool enabled)
    signal minimapToggled(bool enabled)
    signal interfaceScaleRequested(real value)
    signal fontScaleRequested(real value)
    signal reducedMotionToggled(bool enabled)
    signal lowColorRequested
    signal highColorRequested
    signal missingColorRequested

    color: Theme.panelBg
    border.color: Theme.borderSubtle
    implicitWidth: 340

    function includesSearch(value) {
        return searchField.text.length === 0 || String(value).toLowerCase().indexOf(searchField.text.toLowerCase()) >= 0;
    }

    function syncControllerModels() {
        calculationControls.syncModels();
    }
    onControllerChanged: {
        selectedTrackIndex = -1;
        selectedAnnotationIndex = -1;
    }

    component SectionHeader: Rectangle {
        required property string title
        required property bool expanded
        property string detail: ""
        signal toggled
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
        HoverHandler {
            id: headerHover
        }
        TapHandler {
            onTapped: parent.toggled()
        }
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

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 1
            color: Theme.borderSubtle
        }

        TabBar {
            id: sidebarTabs
            implicitWidth: 300
            Layout.fillWidth: true
            currentIndex: root.currentPage
            onCurrentIndexChanged: root.currentPage = currentIndex
            Repeater {
                model: ["Data", "Layers", "Bookmarks", "Inspect"]
                AppTabButton {
                    required property string modelData
                    text: modelData
                    width: sidebarTabs.width / 4
                    font.pixelSize: Theme.textSm
                }
            }
        }
        AppButton {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.topMargin: 8
            text: "Application settings…"
            onClicked: root.settingsRequested()
        }

        AppTextField {
            id: searchField
            Layout.fillWidth: true
            Layout.margins: 10
            placeholderText: "Search workspace"
            Accessible.name: "Search datasets, chromosomes, bookmarks, tracks, and annotations"
            onTextChanged: if (root.controller) {
                root.controller.workspaceSearch = text;
                if (text.length > 0)
                    root.searchResultsExpanded = true;
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
                    visible: root.currentPage === 0 && searchField.text.length === 0
                    title: "Data"
                    expanded: root.dataExpanded
                    detail: root.controller && root.controller.filePath.length > 0 ? "loaded" : ""
                    onToggled: root.dataExpanded = !root.dataExpanded
                }
                ColumnLayout {
                    visible: root.dataExpanded && root.currentPage === 0 && searchField.text.length === 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.topMargin: 8
                    Layout.bottomMargin: 10
                    spacing: 7
                    RowLayout {
                        Layout.fillWidth: true
                        AppButton {
                            text: "Open Hi-C"
                            highlighted: true
                            Layout.fillWidth: true
                            onClicked: root.openDatasetRequested()
                        }
                        AppButton {
                            text: root.controller && root.controller.controlReady ? "Control ✓" : "Control"
                            tonal: true
                            Layout.fillWidth: true
                            enabled: !!root.controller
                            onClicked: root.loadControlRequested()
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "RECENT DATASETS"
                            color: Theme.textMuted
                            font.pixelSize: Theme.textXs
                            font.weight: Font.DemiBold
                        }
                        Item {
                            Layout.fillWidth: true
                        }
                        AppButton {
                            text: "Clear"
                            tonal: true
                            implicitHeight: Theme.controlHeight
                            leftPadding: 8
                            rightPadding: 8
                            font.pixelSize: Theme.textXs
                            enabled: root.controller && root.controller.recentMapCount > 0
                            onClicked: root.controller.clearRecents()
                        }
                    }
                    Label {
                        visible: !root.controller || root.controller.recentMapCount === 0
                        text: "No recent datasets."
                        color: Theme.textMuted
                        font.pixelSize: Theme.textXs
                    }
                    Repeater {
                        model: root.controller ? root.controller.datasetsModel : null
                        ItemDelegate {
                            id: datasetDelegate
                            required property var entry
                            Layout.fillWidth: true
                            height: 42
                            hoverEnabled: true
                            background: Rectangle {
                                color: datasetDelegate.hovered ? Theme.hoverSurface : "transparent"
                            }
                            contentItem: Column {
                                leftPadding: 6
                                Text {
                                    text: datasetDelegate.entry.name
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.textSm
                                    elide: Text.ElideRight
                                    width: parent.width - 12
                                }
                                Text {
                                    text: datasetDelegate.entry.path
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.textXs
                                    elide: Text.ElideMiddle
                                    width: parent.width - 12
                                }
                            }
                            onClicked: {
                                if (root.tabSession)
                                    root.tabSession.setPrimaryFile(root.tabSession.activeCellIndex, datasetDelegate.entry.path);
                                else if (root.controller)
                                    root.controller.openRecentMap(datasetDelegate.entry.path);
                            }
                        }
                    }
                    Label {
                        visible: DatasetRegistry.resourceCount > 0
                        text: "AVAILABLE RESOURCES"
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
                            background: Rectangle {
                                color: pooledDelegate.hovered ? Theme.hoverSurface : "transparent"
                            }
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
                                    Label {
                                        text: pooledDelegate.entry.name
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.textSm
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: pooledDelegate.entry.count + (pooledDelegate.entry.custom ? " custom" : " items")
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.textXs
                                    }
                                }
                                AppToolButton {
                                    visible: pooledDelegate.entry.kind === "hic"
                                    text: "Control"
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

                MatrixDisplayControls {
                    id: calculationControls
                    Layout.fillWidth: true
                    Layout.margins: 10
                    visible: root.currentPage === 3 && searchField.text.length === 0
                    controller: root.controller
                }

                SectionHeader {
                    visible: root.currentPage === 3 && searchField.text.length === 0
                    title: "Plot appearance"
                    expanded: root.displayExpanded
                    onToggled: root.displayExpanded = !root.displayExpanded
                }
                ColumnLayout {
                    visible: root.displayExpanded && root.currentPage === 3 && searchField.text.length === 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 10
                    Layout.rightMargin: 10
                    Layout.topMargin: 8
                    Layout.bottomMargin: 10
                    spacing: 8
                    AppCheckBox {
                        text: "Show minimap"
                        checked: root.minimapVisible
                        onToggled: root.minimapToggled(checked)
                    }
                    AppCheckBox {
                        text: "Show hover text"
                        checked: root.hoverTextVisible
                        onToggled: root.hoverTextToggled(checked)
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 4
                        AppCheckBox {
                            text: "Gridlines"
                            checked: root.controller && root.controller.showGridlines
                            onToggled: if (root.controller)
                                root.controller.showGridlines = checked
                        }
                        AppCheckBox {
                            text: "Chromosome context"
                            checked: root.controller && root.controller.showChromosomeContext
                            onToggled: if (root.controller)
                                root.controller.showChromosomeContext = checked
                        }
                        AppCheckBox {
                            text: "Axis endpoints"
                            checked: root.controller && root.controller.axisEndpointsOnly
                            onToggled: if (root.controller)
                                root.controller.axisEndpointsOnly = checked
                        }
                    }
                    AppButton {
                        Layout.fillWidth: true
                        text: "Heatmap colors and range…"
                        onClicked: root.colorSettingsRequested()
                    }
                    AppCheckBox {
                        text: "Show 1D plotting panels"
                        checked: root.trackPanelsOpen
                        onToggled: root.trackPanelsToggled(checked)
                    }
                    AppCheckBox {
                        text: "Landscape viewport"
                        checked: root.landscapeMode
                        onToggled: root.landscapeModeToggled(checked)
                    }
                }

                SectionHeader {
                    visible: root.currentPage === 1 && searchField.text.length === 0
                    title: "1D tracks"
                    expanded: root.tracksExpanded
                    detail: root.controller ? String(root.controller.trackCount) : ""
                    onToggled: root.tracksExpanded = !root.tracksExpanded
                }
                ColumnLayout {
                    visible: root.tracksExpanded && root.currentPage === 1 && searchField.text.length === 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    Layout.topMargin: 8
                    Layout.bottomMargin: 10
                    spacing: 8
                    Flow {
                        spacing: 6
                        Layout.fillWidth: true
                        AppButton {
                            text: "Load"
                            highlighted: true
                            Layout.fillWidth: true
                            onClicked: root.loadTrackRequested()
                        }
                        AppButton {
                            text: "URL"
                            tonal: true
                            Layout.fillWidth: true
                            onClicked: root.loadTrackUrlRequested()
                        }
                        AppButton {
                            text: "Clear tracks"
                            tonal: true
                            Layout.fillWidth: true
                            enabled: root.controller && root.controller.trackCount > 0
                            onClicked: root.clearTracksRequested()
                        }
                    }
                    Repeater {
                        model: root.controller ? root.controller.tracksModel : null
                        Rectangle {
                            id: trackCard
                            required property var entry
                            Layout.fillWidth: true
                            implicitHeight: trackCardColumn.implicitHeight + 16
                            radius: Theme.radiusMd
                            color: root.selectedTrackIndex === trackCard.entry.index ? Theme.selectedSurface : Theme.surface
                            border.color: root.selectedTrackIndex === trackCard.entry.index ? Theme.accent : Theme.border
                            ColumnLayout {
                                id: trackCardColumn
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.top: parent.top
                                anchors.margins: 8
                                spacing: 6
                                RowLayout {
                                    Layout.fillWidth: true
                                    AppCheckBox {
                                        Accessible.name: "Show " + trackCard.entry.name
                                        checked: trackCard.entry.visible
                                        onToggled: root.controller.setTrackVisible(trackCard.entry.index, checked)
                                    }
                                    AppToolButton {
                                        text: root.selectedTrackIndex === trackCard.entry.index ? "⌄" : "›"
                                        onLightSurface: true
                                        Accessible.name: "Track details"
                                        onClicked: root.selectedTrackIndex = root.selectedTrackIndex === trackCard.entry.index ? -1 : trackCard.entry.index
                                    }
                                    Rectangle {
                                        Layout.preferredWidth: 14
                                        Layout.preferredHeight: 14
                                        radius: 3
                                        color: trackCard.entry.positiveColor
                                        border.color: Theme.borderStrong
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 60
                                        text: trackCard.entry.name
                                        color: Theme.textPrimary
                                        elide: Text.ElideRight
                                        activeFocusOnTab: true
                                        Accessible.name: trackCard.entry.name + ". Select for track details; double-click to rename."
                                        Keys.onReturnPressed: root.selectedTrackIndex = trackCard.entry.index
                                        TapHandler {
                                            onTapped: root.selectedTrackIndex = root.selectedTrackIndex === trackCard.entry.index ? -1 : trackCard.entry.index
                                            onDoubleTapped: root.trackNameEditorRequested(trackCard.entry.index)
                                        }
                                        HoverHandler {
                                            id: trackNameHover
                                        }
                                        ToolTip.visible: trackNameHover.hovered
                                        ToolTip.text: trackCard.entry.name + "\n" + trackCard.entry.source + (trackCard.entry.warning ? "\n" + trackCard.entry.warning : "")
                                    }
                                    AppToolButton {
                                        text: "⋯"
                                        onLightSurface: true
                                        Accessible.name: "Track actions"
                                        onClicked: root.trackMenuRequested(trackCard.entry.index)
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: root.selectedTrackIndex === trackCard.entry.index

                                    Label {
                                        visible: root.selectedTrackIndex === trackCard.entry.index
                                        text: trackCard.entry.featureCount + " intervals · " + trackCard.entry.format
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.textXs
                                    }
                                    Label {
                                        visible: root.selectedTrackIndex === trackCard.entry.index && trackCard.entry.warning !== ""
                                        Layout.fillWidth: true
                                        text: "⚠ " + trackCard.entry.warning
                                        color: Theme.danger
                                        font.pixelSize: Theme.textXs
                                        wrapMode: Text.WordWrap
                                    }
                                    RowLayout {
                                        visible: root.selectedTrackIndex === trackCard.entry.index && trackCard.entry.renderMode === "signal"
                                        Layout.fillWidth: true
                                        AppCheckBox {
                                            text: "Auto"
                                            checked: trackCard.entry.autoscale
                                            onToggled: root.controller.setTrackAutoscale(trackCard.entry.index, checked)
                                        }
                                        AppCheckBox {
                                            text: "Log"
                                            checked: trackCard.entry.logScale
                                            onToggled: root.controller.setTrackRange(trackCard.entry.index, trackCard.entry.min, trackCard.entry.max, checked)
                                        }
                                        AppComboBox {
                                            Layout.fillWidth: true
                                            model: ["min", "mean", "max", "none"]
                                            currentIndex: Math.max(0, model.indexOf(trackCard.entry.reduction))
                                            onActivated: root.controller.setTrackReduction(trackCard.entry.index, currentText)
                                        }
                                    }
                                    RowLayout {
                                        visible: root.selectedTrackIndex === trackCard.entry.index && trackCard.entry.renderMode === "signal"
                                        Layout.fillWidth: true
                                        Label {
                                            text: trackCard.entry.binSize === 0 ? "Bin: Hi-C · " + root.controller.resolution : "Bin: " + trackCard.entry.effectiveBinSize
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.textXs
                                            Layout.fillWidth: true
                                        }
                                        AppButton {
                                            text: "Match"
                                            tonal: true
                                            onClicked: root.controller.setTrackBinSize(trackCard.entry.index, 0)
                                        }
                                        AppButton {
                                            text: "Set…"
                                            tonal: true
                                            onClicked: root.trackBinEditorRequested(trackCard.entry.index)
                                        }
                                    }
                                    RowLayout {
                                        visible: root.selectedTrackIndex === trackCard.entry.index && trackCard.entry.renderMode === "signal"
                                        Layout.fillWidth: true
                                        Label {
                                            text: "Min"
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.textXs
                                        }
                                        AppTextField {
                                            Layout.fillWidth: true
                                            text: String(trackCard.entry.min)
                                            enabled: !trackCard.entry.autoscale
                                            onAccepted: root.controller.setTrackRange(trackCard.entry.index, Number(text), trackCard.entry.max, trackCard.entry.logScale)
                                        }
                                        Label {
                                            text: "Max"
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.textXs
                                        }
                                        AppTextField {
                                            Layout.fillWidth: true
                                            text: String(trackCard.entry.max)
                                            enabled: !trackCard.entry.autoscale
                                            onAccepted: root.controller.setTrackRange(trackCard.entry.index, trackCard.entry.min, Number(text), trackCard.entry.logScale)
                                        }
                                    }
                                    RowLayout {
                                        visible: root.selectedTrackIndex === trackCard.entry.index
                                        Layout.fillWidth: true
                                        Label {
                                            text: "Height"
                                            color: Theme.textSecondary
                                            font.pixelSize: Theme.textSm
                                        }
                                        AppTextField {
                                            Layout.fillWidth: true
                                            text: String(trackCard.entry.height)
                                            inputMethodHints: Qt.ImhDigitsOnly
                                            validator: IntValidator {
                                                bottom: 20
                                            }
                                            onAccepted: if (acceptableInput)
                                                root.controller.setTrackHeight(trackCard.entry.index, Math.round(Number(text)))
                                            onEditingFinished: if (acceptableInput)
                                                root.controller.setTrackHeight(trackCard.entry.index, Math.round(Number(text)))
                                        }
                                        Label {
                                            text: "px"
                                            color: Theme.textMuted
                                            font.pixelSize: Theme.textXs
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                SectionHeader {
                    visible: root.currentPage === 1 && searchField.text.length === 0
                    title: "2D annotations"
                    expanded: root.annotationsExpanded
                    detail: root.controller ? String(root.controller.annotationCount) : ""
                    onToggled: root.annotationsExpanded = !root.annotationsExpanded
                }
                ColumnLayout {
                    visible: root.annotationsExpanded && root.currentPage === 1 && searchField.text.length === 0
                    Layout.fillWidth: true
                    Layout.leftMargin: 8
                    Layout.rightMargin: 8
                    Layout.topMargin: 8
                    Layout.bottomMargin: 10
                    spacing: 8
                    Flow {
                        spacing: 6
                        Layout.fillWidth: true
                        AppButton {
                            text: "Load"
                            highlighted: true
                            Layout.fillWidth: true
                            onClicked: root.loadAnnotationsRequested()
                        }
                        AppButton {
                            text: "New"
                            tonal: true
                            Layout.fillWidth: true
                            onClicked: if (root.controller)
                                root.controller.addAnnotationLayer("Layer")
                        }
                        AppButton {
                            text: "Merge"
                            tonal: true
                            Layout.fillWidth: true
                            onClicked: if (root.controller)
                                root.controller.mergeVisibleAnnotationLayers("Merged")
                        }
                        AppButton {
                            text: "Clear features"
                            tonal: true
                            Layout.fillWidth: true
                            enabled: root.controller && root.controller.annotationCount > 0
                            onClicked: root.clearAnnotationsRequested()
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Label {
                            text: "Sparse limit"
                            color: Theme.textSecondary
                            font.pixelSize: Theme.textSm
                        }
                        SpinBox {
                            Layout.fillWidth: true
                            from: 1
                            to: 1000000
                            editable: true
                            value: root.controller ? root.controller.sparseFeatureLimit : 10000
                            onValueModified: if (root.controller)
                                root.controller.sparseFeatureLimit = value
                        }
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
                                    AppCheckBox {
                                        Accessible.name: "Show " + annotationCard.entry.name
                                        checked: annotationCard.entry.visible
                                        onToggled: root.controller.setAnnotationLayerVisible(annotationCard.entry.index, checked)
                                    }
                                    Label {
                                        text: annotationCard.entry.name + " · " + annotationCard.entry.count
                                        color: Theme.textPrimary
                                        Layout.fillWidth: true
                                        elide: Text.ElideRight
                                    }
                                    AppButton {
                                        text: root.selectedAnnotationIndex === annotationCard.entry.index ? "Done" : "Details"
                                        tonal: true
                                        onClicked: {
                                            root.selectedAnnotationIndex = root.selectedAnnotationIndex === annotationCard.entry.index ? -1 : annotationCard.entry.index;
                                            root.controller.setActiveAnnotationLayer(annotationCard.entry.index);
                                        }
                                    }
                                }
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    visible: root.selectedAnnotationIndex === annotationCard.entry.index

                                    Flow {
                                        Layout.fillWidth: true
                                        spacing: 3
                                        AppCheckBox {
                                            text: "Transparent"
                                            checked: annotationCard.entry.transparent
                                            onToggled: root.controller.setAnnotationLayerTransparent(annotationCard.entry.index, checked)
                                        }
                                        AppCheckBox {
                                            text: "Sparse"
                                            checked: annotationCard.entry.sparse
                                            onToggled: root.controller.setAnnotationLayerSparse(annotationCard.entry.index, checked)
                                        }
                                        AppCheckBox {
                                            text: "Large"
                                            checked: annotationCard.entry.enlarged
                                            onToggled: root.controller.setAnnotationLayerEnlarged(annotationCard.entry.index, checked)
                                        }
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
                                        Item {
                                            Layout.fillWidth: true
                                        }
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
                                            currentIndex: annotationCard.entry.placement === "above" ? 1 : (annotationCard.entry.placement === "below" ? 2 : 0)
                                            onActivated: root.controller.setAnnotationLayerPlacement(annotationCard.entry.index, currentIndex === 1 ? "above" : (currentIndex === 2 ? "below" : "both"))
                                            Layout.fillWidth: true
                                        }
                                    }
                                    Flow {
                                        spacing: 6
                                        Layout.fillWidth: true
                                        AppButton {
                                            text: "Duplicate"
                                            tonal: true
                                            Layout.fillWidth: true
                                            onClicked: root.controller.duplicateAnnotationLayer(annotationCard.entry.index)
                                        }
                                        AppButton {
                                            text: "Export"
                                            tonal: true
                                            Layout.fillWidth: true
                                            onClicked: root.exportAnnotationRequested(annotationCard.entry.index)
                                        }
                                        AppButton {
                                            text: "Clear features"
                                            tonal: true
                                            Layout.fillWidth: true
                                            onClicked: root.controller.clearAnnotationLayer(annotationCard.entry.index)
                                        }
                                        AppButton {
                                            text: "Remove layer"
                                            tonal: true
                                            Layout.fillWidth: true
                                            enabled: root.controller.annotationCount > 0
                                            onClicked: root.controller.removeAnnotationLayer(annotationCard.entry.index)
                                        }
                                    }
                                }
                            }
                        }
                    }
                }

                SectionHeader {
                    visible: root.currentPage === 0 && searchField.text.length === 0
                    title: "Chromosomes"
                    expanded: root.chromosomesExpanded
                    detail: root.controller ? String(root.controller.chromosomeNames().length) : ""
                    onToggled: root.chromosomesExpanded = !root.chromosomesExpanded
                }
                ColumnLayout {
                    visible: root.chromosomesExpanded && root.currentPage === 0 && searchField.text.length === 0
                    Layout.fillWidth: true
                    spacing: 2
                    Repeater {
                        model: root.controller ? root.controller.chromosomeNames() : []
                        RowLayout {
                            id: chromosomeRow
                            required property var modelData
                            visible: root.includesSearch(chromosomeRow.modelData)
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 10
                            Label {
                                text: chromosomeRow.modelData
                                color: Theme.textPrimary
                                Layout.fillWidth: true
                                font.pixelSize: Theme.textSm
                            }
                            AppToolButton {
                                text: "X"
                                onLightSurface: true
                                onClicked: root.controller.chrX = chromosomeRow.modelData
                            }
                            AppToolButton {
                                text: "Y"
                                onLightSurface: true
                                onClicked: root.controller.chrY = chromosomeRow.modelData
                            }
                        }
                    }
                }

                SectionHeader {
                    visible: root.currentPage === 2 && searchField.text.length === 0
                    title: "Bookmarks"
                    expanded: root.bookmarksExpanded
                    onToggled: root.bookmarksExpanded = !root.bookmarksExpanded
                }
                ColumnLayout {
                    visible: root.bookmarksExpanded && root.currentPage === 2 && searchField.text.length === 0
                    Layout.fillWidth: true
                    spacing: 2
                    AppButton {
                        text: "+ Save current location"
                        tonal: true
                        Layout.fillWidth: true
                        Layout.margins: 10
                        onClicked: if (root.controller)
                            root.controller.saveCurrentLocation("")
                    }
                    Repeater {
                        model: root.controller ? root.controller.bookmarksModel : null
                        Rectangle {
                            id: bookmarkCard
                            required property var entry
                            required property int index
                            Layout.fillWidth: true
                            Layout.leftMargin: 8
                            Layout.rightMargin: 8
                            height: 54
                            color: bookmarkHover.hovered ? Theme.hoverSurface : "transparent"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 4
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 1
                                    Label {
                                        text: bookmarkCard.entry.name || "Saved locus"
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.textSm
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        text: bookmarkCard.entry.chrX + ":" + bookmarkCard.entry.x0 + "–" + bookmarkCard.entry.x1
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.textXs
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                                }
                                AppToolButton {
                                    text: "×"
                                    onLightSurface: true
                                    onClicked: root.controller.removeSavedLocation(bookmarkCard.index)
                                }
                            }
                            HoverHandler {
                                id: bookmarkHover
                            }
                            TapHandler {
                                onTapped: root.controller.restoreSavedLocation(bookmarkCard.index)
                            }
                        }
                    }
                }

                SectionHeader {
                    visible: searchField.text.length > 0
                    title: "Search results"
                    expanded: root.searchResultsExpanded
                    onToggled: root.searchResultsExpanded = !root.searchResultsExpanded
                }
                ColumnLayout {
                    visible: root.searchResultsExpanded && searchField.text.length > 0
                    Layout.fillWidth: true
                    Repeater {
                        model: root.controller ? root.controller.searchResultsModel : null
                        ItemDelegate {
                            id: searchDelegate
                            required property var entry
                            Layout.fillWidth: true
                            height: 48
                            hoverEnabled: true
                            background: Rectangle {
                                color: searchDelegate.hovered ? Theme.hoverSurface : "transparent"
                            }
                            contentItem: Column {
                                leftPadding: 12
                                Text {
                                    text: searchDelegate.entry.label
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.textSm
                                    elide: Text.ElideRight
                                    width: parent.width - 20
                                }
                                Text {
                                    text: searchDelegate.entry.kind.toUpperCase() + " · " + searchDelegate.entry.detail
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.textXs
                                    elide: Text.ElideMiddle
                                    width: parent.width - 20
                                }
                            }
                            onClicked: {
                                if (searchDelegate.entry.kind === "dataset")
                                    root.controller.openRecentMap(searchDelegate.entry.path);
                                else if (searchDelegate.entry.kind === "bookmark")
                                    root.controller.restoreSavedLocation(searchDelegate.entry.index);
                                else if (searchDelegate.entry.kind === "track")
                                    root.controller.setTrackVisible(searchDelegate.entry.index, true);
                                else if (searchDelegate.entry.kind === "annotation")
                                    root.controller.setActiveAnnotationLayer(searchDelegate.entry.index);
                            }
                        }
                    }
                }
            }
        }
    }
}
