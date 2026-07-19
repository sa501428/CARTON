pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Rectangle {
    id: root
    property var controller: null
    property bool collapsed: false
    property bool landscapeMode: false
    property bool hoverTextVisible: true
    property bool trackPanelsOpen: false
    property string hoverText: ""

    property bool viewControlsExpanded: true
    property bool dataExpanded: true
    property bool chromosomesExpanded: false
    property bool bookmarksExpanded: false
    property bool tracksExpanded: true
    property bool annotationsExpanded: true
    property bool resultsExpanded: false
    property bool searchResultsExpanded: false
    property bool statusExpanded: false

    signal toggleRequested()
    signal openDatasetRequested()
    signal loadTrackRequested()
    signal loadAnnotationsRequested()
    signal landscapeModeToggled(bool enabled)
    signal hoverTextToggled(bool enabled)
    signal trackPanelsToggled(bool enabled)

    color: Theme.panelBg
    border.color: Theme.borderSubtle
    implicitWidth: collapsed ? 48 : 320

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
            Layout.preferredHeight: 52
            Layout.leftMargin: root.collapsed ? 8 : 14
            Layout.rightMargin: 8
            spacing: 8
            Label {
                visible: !root.collapsed
                text: "Workspace"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textLg
                font.weight: Font.DemiBold
                Layout.fillWidth: true
            }
            AppToolButton {
                text: root.collapsed ? "›" : "‹"
                onLightSurface: true
                contentColor: Theme.textSecondary
                Accessible.name: root.collapsed ? "Expand navigation panel" : "Collapse navigation panel"
                onClicked: root.toggleRequested()
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderSubtle }

        AppTextField {
            id: searchField
            visible: !root.collapsed
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
            visible: !root.collapsed
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth

            ColumnLayout {
                width: parent.width
                spacing: 0

                SectionHeader {
                    title: "View controls"
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
                        rowSpacing: 6
                        Label { text: "Horizontal"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                        Label { text: "Vertical"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                        AppComboBox {
                            id: chromosomeX
                            Layout.fillWidth: true
                            Accessible.name: "Horizontal chromosome"
                            enabled: root.controller && model.length > 0
                            onActivated: if (root.controller) root.controller.chrX = currentText
                        }
                        AppComboBox {
                            id: chromosomeY
                            Layout.fillWidth: true
                            Accessible.name: "Vertical chromosome"
                            enabled: root.controller && model.length > 0
                            onActivated: if (root.controller) root.controller.chrY = currentText
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Hi-C bin"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                        AppComboBox {
                            id: resolutionBox
                            Layout.fillWidth: true
                            Accessible.name: "Hi-C bin size in base pairs"
                            enabled: root.controller && model.length > 0 && !root.controller.resolutionLocked
                            onActivated: if (root.controller) root.controller.resolution = Number(currentText)
                        }
                        AppToolButton {
                            text: root.controller && root.controller.resolutionLocked ? "Unlock" : "Lock"
                            onLightSurface: true
                            enabled: root.controller && root.controller.resolution > 0
                            onClicked: root.controller.resolutionLocked = !root.controller.resolutionLocked
                        }
                    }

                    Label { text: "Matrix display"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                    AppComboBox {
                        id: matrixBox
                        Layout.fillWidth: true
                        Accessible.name: "Matrix display mode"
                        enabled: !!root.controller
                        onActivated: if (root.controller) root.controller.matrixType = currentText
                    }

                    Label { text: "Normalization"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                    AppComboBox {
                        id: normBox
                        Layout.fillWidth: true
                        Accessible.name: "Matrix normalization"
                        enabled: root.controller && model.length > 0
                        onActivated: if (root.controller) root.controller.norm = currentText
                    }

                    AppTextField {
                        id: topLocationField
                        Layout.fillWidth: true
                        placeholderText: "Horizontal chr:start-end"
                        enabled: root.controller && root.controller.filePath.length > 0
                        onAccepted: if (root.controller)
                            root.controller.goTo(text, leftLocationField.text.length > 0 ? leftLocationField.text : text)
                    }
                    AppTextField {
                        id: leftLocationField
                        Layout.fillWidth: true
                        placeholderText: "Vertical locus (optional)"
                        enabled: root.controller && root.controller.filePath.length > 0
                        onAccepted: if (root.controller)
                            root.controller.goTo(topLocationField.text, text.length > 0 ? text : topLocationField.text)
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        AppButton {
                            text: "Go"; tonal: true; Layout.fillWidth: true
                            enabled: root.controller && topLocationField.text.length > 0
                            onClicked: root.controller.goTo(topLocationField.text,
                                                           leftLocationField.text.length > 0 ? leftLocationField.text : topLocationField.text)
                        }
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
                        AppCheckBox { text: "Lock X"; checked: root.controller && root.controller.xLocusLocked; onToggled: if (root.controller) root.controller.xLocusLocked = checked }
                        AppCheckBox { text: "Lock Y"; checked: root.controller && root.controller.yLocusLocked; onToggled: if (root.controller) root.controller.yLocusLocked = checked }
                    }
                    AppCheckBox {
                        text: "Show 1D track panels"
                        checked: root.trackPanelsOpen
                        onToggled: root.trackPanelsToggled(checked)
                    }
                    AppCheckBox {
                        text: "Show hover text"
                        checked: root.hoverTextVisible
                        onToggled: root.hoverTextToggled(checked)
                    }
                    AppCheckBox {
                        text: "Landscape viewport"
                        checked: root.landscapeMode
                        onToggled: root.landscapeModeToggled(checked)
                    }
                }

                SectionHeader {
                    title: "Data"
                    expanded: root.dataExpanded
                    onToggled: root.dataExpanded = !root.dataExpanded
                }
                ColumnLayout {
                    visible: root.dataExpanded
                    Layout.fillWidth: true
                    spacing: 2
                    AppButton {
                        Layout.fillWidth: true
                        Layout.margins: 10
                        text: "Open Hi-C dataset"
                        highlighted: true
                        onClicked: root.openDatasetRequested()
                    }
                    Label { text: "RECENT DATASETS"; color: Theme.textMuted; font.pixelSize: Theme.textXs; font.weight: Font.DemiBold; Layout.margins: 12 }
                    Repeater {
                        model: root.controller ? root.controller.datasetsModel : null
                        ItemDelegate {
                            required property var entry
                            Layout.fillWidth: true
                            height: 44
                            hoverEnabled: true
                            background: Rectangle { color: parent.hovered ? Theme.hoverSurface : "transparent" }
                            contentItem: Column {
                                leftPadding: 12
                                Text { text: entry.name; color: Theme.textPrimary; font.pixelSize: Theme.textSm; elide: Text.ElideRight; width: parent.width - 20 }
                                Text { text: entry.path; color: Theme.textMuted; font.pixelSize: Theme.textXs; elide: Text.ElideMiddle; width: parent.width - 20 }
                            }
                            onClicked: root.controller.openRecentMap(entry.path)
                        }
                    }
                }

                SectionHeader {
                    title: "Chromosomes"
                    expanded: root.chromosomesExpanded
                    detail: root.controller ? root.controller.chromosomeNames().length : ""
                    onToggled: root.chromosomesExpanded = !root.chromosomesExpanded
                }
                ColumnLayout {
                    visible: root.chromosomesExpanded
                    Layout.fillWidth: true
                    spacing: 2
                    Repeater {
                        model: root.controller ? root.controller.chromosomeNames() : []
                        RowLayout {
                            required property var modelData
                            visible: root.includesSearch(modelData)
                            Layout.fillWidth: true
                            Layout.leftMargin: 12
                            Layout.rightMargin: 10
                            Label { text: modelData; color: Theme.textPrimary; Layout.fillWidth: true; font.pixelSize: Theme.textSm }
                            AppToolButton { text: "X"; onLightSurface: true; onClicked: root.controller.chrX = modelData }
                            AppToolButton { text: "Y"; onLightSurface: true; onClicked: root.controller.chrY = modelData }
                        }
                    }
                }

                SectionHeader {
                    title: "Bookmarks"
                    expanded: root.bookmarksExpanded
                    onToggled: root.bookmarksExpanded = !root.bookmarksExpanded
                }
                ColumnLayout {
                    visible: root.bookmarksExpanded
                    Layout.fillWidth: true
                    spacing: 2
                    AppButton { text: "+ Save current location"; tonal: true; Layout.fillWidth: true; Layout.margins: 10; onClicked: if (root.controller) root.controller.saveCurrentLocation("") }
                    Repeater {
                        model: root.controller ? root.controller.bookmarksModel : null
                        Rectangle {
                            required property var entry
                            required property int index
                            Layout.fillWidth: true
                            Layout.leftMargin: 8
                            Layout.rightMargin: 8
                            height: 54
                            color: bookmarkHover.hovered ? Theme.hoverSurface : "transparent"
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 4
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 1
                                    Label { text: entry.name || "Saved locus"; color: Theme.textPrimary; font.pixelSize: Theme.textSm; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { text: entry.chrX + ":" + entry.x0 + "–" + entry.x1; color: Theme.textMuted; font.pixelSize: Theme.textXs; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                AppToolButton { text: "×"; onLightSurface: true; onClicked: root.controller.removeSavedLocation(index) }
                            }
                            HoverHandler { id: bookmarkHover }
                            TapHandler { onTapped: root.controller.restoreSavedLocation(index) }
                        }
                    }
                }

                SectionHeader {
                    title: "1D tracks"
                    expanded: root.tracksExpanded
                    detail: root.controller ? root.controller.trackCount : ""
                    onToggled: root.tracksExpanded = !root.tracksExpanded
                }
                ColumnLayout {
                    visible: root.tracksExpanded
                    Layout.fillWidth: true
                    spacing: 2
                    AppButton { text: "Load genomic track"; highlighted: true; Layout.fillWidth: true; Layout.margins: 10; onClicked: root.loadTrackRequested() }
                    Repeater {
                        model: root.controller ? root.controller.tracksModel : null
                        RowLayout {
                            required property var entry
                            Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10
                            AppCheckBox { checked: entry.visible; onToggled: root.controller.setTrackVisible(entry.index, checked) }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 0
                                Label { text: entry.name; color: Theme.textPrimary; font.pixelSize: Theme.textSm; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: entry.featureCount + " intervals"; color: Theme.textMuted; font.pixelSize: Theme.textXs }
                            }
                        }
                    }
                }

                SectionHeader {
                    title: "2D annotations"
                    expanded: root.annotationsExpanded
                    detail: root.controller ? root.controller.annotationCount : ""
                    onToggled: root.annotationsExpanded = !root.annotationsExpanded
                }
                ColumnLayout {
                    visible: root.annotationsExpanded
                    Layout.fillWidth: true
                    spacing: 2
                    AppButton { text: "Load 2D annotations"; highlighted: true; Layout.fillWidth: true; Layout.margins: 10; onClicked: root.loadAnnotationsRequested() }
                    Repeater {
                        model: root.controller ? root.controller.annotationsModel : null
                        RowLayout {
                            required property var entry
                            Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10
                            AppCheckBox { checked: entry.visible; onToggled: root.controller.setAnnotationLayerVisible(entry.index, checked) }
                            Label { text: entry.name; color: Theme.textPrimary; font.pixelSize: Theme.textSm; Layout.fillWidth: true; elide: Text.ElideRight }
                            Label { text: entry.count; color: Theme.textMuted; font.pixelSize: Theme.textXs }
                        }
                    }
                }

                SectionHeader {
                    title: "Results"
                    expanded: root.resultsExpanded
                    onToggled: root.resultsExpanded = !root.resultsExpanded
                }
                ColumnLayout {
                    visible: root.resultsExpanded
                    Layout.fillWidth: true
                    Layout.margins: 14
                    Label { text: "No analysis results yet"; color: Theme.textPrimary; font.pixelSize: Theme.textSm; font.weight: Font.DemiBold }
                    Label { text: "Derived tracks and analysis outputs will appear here."; color: Theme.textMuted; font.pixelSize: Theme.textXs; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }

                SectionHeader {
                    title: "Search results"
                    expanded: root.searchResultsExpanded
                    onToggled: root.searchResultsExpanded = !root.searchResultsExpanded
                }
                ColumnLayout {
                    visible: root.searchResultsExpanded
                    Layout.fillWidth: true
                    spacing: 2
                    Repeater {
                        model: root.controller ? root.controller.searchResultsModel : null
                        ItemDelegate {
                            required property var entry
                            Layout.fillWidth: true
                            height: 48
                            hoverEnabled: true
                            background: Rectangle { color: parent.hovered ? Theme.hoverSurface : "transparent" }
                            contentItem: Column {
                                leftPadding: 12
                                Text { text: entry.label; color: Theme.textPrimary; font.pixelSize: Theme.textSm; elide: Text.ElideRight; width: parent.width - 20 }
                                Text { text: entry.kind.toUpperCase() + " · " + entry.detail; color: Theme.textMuted; font.pixelSize: Theme.textXs; elide: Text.ElideMiddle; width: parent.width - 20 }
                            }
                            onClicked: {
                                if (entry.kind === "dataset") root.controller.openRecentMap(entry.path)
                                else if (entry.kind === "bookmark") root.controller.restoreSavedLocation(entry.index)
                                else if (entry.kind === "track") root.controller.setTrackVisible(entry.index, true)
                                else if (entry.kind === "annotation") root.controller.setActiveAnnotationLayer(entry.index)
                            }
                        }
                    }
                }

                SectionHeader {
                    title: "Status"
                    expanded: root.statusExpanded
                    detail: root.controller ? root.controller.recordCount + " records" : ""
                    onToggled: root.statusExpanded = !root.statusExpanded
                }
                ColumnLayout {
                    visible: root.statusExpanded
                    Layout.fillWidth: true
                    Layout.leftMargin: 12
                    Layout.rightMargin: 12
                    Layout.topMargin: 8
                    Layout.bottomMargin: 14
                    spacing: 5
                    Label { Layout.fillWidth: true; text: root.controller ? root.controller.chrX + ":" + root.controller.x0 + "–" + root.controller.x1 : ""; color: Theme.textPrimary; font.pixelSize: Theme.textSm; wrapMode: Text.WrapAnywhere }
                    Label { Layout.fillWidth: true; text: root.controller ? root.controller.chrY + ":" + root.controller.y0 + "–" + root.controller.y1 : ""; color: Theme.textPrimary; font.pixelSize: Theme.textSm; wrapMode: Text.WrapAnywhere }
                    Label { Layout.fillWidth: true; text: root.controller ? root.controller.matrixDimensions + " · " + root.controller.resolution + " bp" : ""; color: Theme.textSecondary; font.pixelSize: Theme.textXs; wrapMode: Text.WordWrap }
                    Label { Layout.fillWidth: true; text: root.controller ? "Cache " + root.controller.cacheMemoryMB.toFixed(1) + "/" + root.controller.cacheLimitMB + " MB · " + root.controller.renderingBackend : ""; color: Theme.textSecondary; font.pixelSize: Theme.textXs; wrapMode: Text.WordWrap }
                    Label { Layout.fillWidth: true; text: root.controller ? root.controller.status : ""; color: Theme.textSecondary; font.pixelSize: Theme.textSm; wrapMode: Text.WordWrap }
                    Rectangle {
                        visible: root.hoverTextVisible && root.hoverText.length > 0
                        Layout.fillWidth: true
                        implicitHeight: statusHoverText.implicitHeight + 16
                        radius: Theme.radiusSm
                        color: Theme.surfaceSunken
                        border.color: Theme.borderSubtle
                        Label {
                            id: statusHoverText
                            anchors.fill: parent
                            anchors.margins: 8
                            text: root.hoverText
                            color: Theme.textPrimary
                            font.pixelSize: Theme.textXs
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }
        }
    }
}
