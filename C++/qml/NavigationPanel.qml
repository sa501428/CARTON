import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Rectangle {
    id: root
    property var controller: null
    property bool collapsed: false
    signal toggleRequested()
    signal openDatasetRequested()
    signal loadTrackRequested()
    signal loadAnnotationsRequested()

    color: Theme.panelBg
    border.color: Theme.borderSubtle
    implicitWidth: collapsed ? 48 : 272

    function includesSearch(value) {
        return searchField.text.length === 0 || String(value).toLowerCase().indexOf(searchField.text.toLowerCase()) >= 0
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            Layout.leftMargin: root.collapsed ? 8 : 14
            Layout.rightMargin: 8
            spacing: 8
            Label {
                visible: !root.collapsed
                text: "Workspace"
                color: Theme.textPrimary
                font.family: Theme.fontFamily
                font.pixelSize: Theme.textMd
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
                if (text.length > 0) navStack.currentIndex = 6
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.leftMargin: root.collapsed ? 4 : 8
            Layout.rightMargin: root.collapsed ? 4 : 8
            spacing: 2
            Repeater {
                model: ["Data", "Chr", "Marks", "Tracks", "2D", "Results", "Find"]
                AppToolButton {
                    required property string modelData
                    required property int index
                    text: root.collapsed ? modelData.charAt(0) : modelData
                    onLightSurface: true
                    idleColor: navStack.currentIndex === index ? Theme.selectedSurface : "transparent"
                    contentColor: navStack.currentIndex === index ? Theme.accent : Theme.textSecondary
                    Layout.fillWidth: true
                    Accessible.name: ["Datasets", "Chromosomes", "Bookmarks", "Tracks", "Annotations", "Analysis results", "Search results"][index]
                    onClicked: navStack.currentIndex = index
                }
            }
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: Theme.borderSubtle }

        StackLayout {
            id: navStack
            visible: !root.collapsed
            Layout.fillWidth: true
            Layout.fillHeight: true

            ScrollView {
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 2
                    AppButton {
                        Layout.fillWidth: true
                        Layout.margins: 10
                        text: "Open Hi-C dataset"
                        highlighted: true
                        Accessible.name: "Open Hi-C dataset"
                        onClicked: root.openDatasetRequested()
                    }
                    Label { text: "RECENT DATASETS"; color: Theme.textMuted; font.pixelSize: Theme.textXs; font.weight: Font.DemiBold; Layout.margins: 12 }
                    Repeater {
                        model: root.controller ? root.controller.datasetsModel : null
                        ItemDelegate {
                            required property var entry
                            width: parent.width
                            height: 38
                            hoverEnabled: true
                            Accessible.name: "Open recent dataset " + entry.path
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
            }

            ScrollView {
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 1
                    Label { text: "CHROMOSOMES"; color: Theme.textMuted; font.pixelSize: Theme.textXs; font.weight: Font.DemiBold; Layout.margins: 12 }
                    Repeater {
                        model: root.controller ? root.controller.chromosomeNames() : []
                        RowLayout {
                            required property var modelData
                            visible: root.includesSearch(modelData)
                            Layout.fillWidth: true
                            Layout.leftMargin: 10
                            Layout.rightMargin: 10
                            Label { text: modelData; color: Theme.textPrimary; Layout.fillWidth: true; font.pixelSize: Theme.textSm }
                            AppToolButton { text: "X"; onLightSurface: true; Accessible.name: "Set X chromosome to " + modelData; onClicked: root.controller.chrX = modelData }
                            AppToolButton { text: "Y"; onLightSurface: true; Accessible.name: "Set Y chromosome to " + modelData; onClicked: root.controller.chrY = modelData }
                        }
                    }
                }
            }

            ScrollView {
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 2
                    RowLayout {
                        Layout.fillWidth: true; Layout.margins: 10
                        Label { text: "BOOKMARKS"; color: Theme.textMuted; font.pixelSize: Theme.textXs; font.weight: Font.DemiBold; Layout.fillWidth: true }
                        AppButton { text: "+ Save"; tonal: true; onClicked: if (root.controller) root.controller.saveCurrentLocation("") }
                    }
                    Repeater {
                        model: root.controller ? root.controller.bookmarksModel : null
                        Rectangle {
                            required property var entry
                            required property int index
                            Layout.fillWidth: true
                            Layout.leftMargin: 8; Layout.rightMargin: 8
                            height: visible ? 54 : 0
                            color: bookmarkHover.hovered ? Theme.hoverSurface : "transparent"
                            RowLayout {
                                anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 4
                                ColumnLayout {
                                    Layout.fillWidth: true; spacing: 1
                                    Label { text: entry.name || "Saved locus"; color: Theme.textPrimary; font.pixelSize: Theme.textSm; elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { text: entry.chrX + ":" + entry.x0 + "–" + entry.x1; color: Theme.textMuted; font.pixelSize: Theme.textXs; elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                                AppToolButton { text: "×"; onLightSurface: true; Accessible.name: "Delete bookmark"; onClicked: root.controller.removeSavedLocation(index) }
                            }
                            HoverHandler { id: bookmarkHover }
                            TapHandler { onTapped: root.controller.restoreSavedLocation(index) }
                        }
                    }
                }
            }

            ScrollView {
                clip: true
                ColumnLayout {
                    width: parent.width; spacing: 2
                    AppButton { text: "Load genomic track"; highlighted: true; Layout.fillWidth: true; Layout.margins: 10; onClicked: root.loadTrackRequested() }
                    Repeater {
                        model: root.controller ? root.controller.tracksModel : null
                        RowLayout {
                            required property var entry
                            Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10
                            AppCheckBox { checked: entry.visible; Accessible.name: "Show track " + entry.name; onToggled: root.controller.setTrackVisible(entry.index, checked) }
                            ColumnLayout {
                                Layout.fillWidth: true; spacing: 0
                                Label { text: entry.name; color: Theme.textPrimary; font.pixelSize: Theme.textSm; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: entry.featureCount + " intervals"; color: Theme.textMuted; font.pixelSize: Theme.textXs }
                            }
                        }
                    }
                }
            }

            ScrollView {
                clip: true
                ColumnLayout {
                    width: parent.width; spacing: 2
                    AppButton { text: "Load 2D annotations"; highlighted: true; Layout.fillWidth: true; Layout.margins: 10; onClicked: root.loadAnnotationsRequested() }
                    Repeater {
                        model: root.controller ? root.controller.annotationsModel : null
                        RowLayout {
                            required property var entry
                            Layout.fillWidth: true; Layout.leftMargin: 10; Layout.rightMargin: 10
                            AppCheckBox { checked: entry.visible; Accessible.name: "Show annotation layer " + entry.name; onToggled: root.controller.setAnnotationLayerVisible(entry.index, checked) }
                            Label { text: entry.name; color: Theme.textPrimary; font.pixelSize: Theme.textSm; Layout.fillWidth: true; elide: Text.ElideRight }
                            Label { text: entry.count; color: Theme.textMuted; font.pixelSize: Theme.textXs }
                        }
                    }
                }
            }

            Item {
                ColumnLayout {
                    anchors.centerIn: parent; width: Math.min(parent.width - 32, 220); spacing: 8
                    Label { text: "No analysis results yet"; color: Theme.textPrimary; font.pixelSize: Theme.textMd; font.weight: Font.DemiBold; Layout.alignment: Qt.AlignHCenter }
                    Label { text: "Derived tracks and analysis outputs will appear here."; color: Theme.textMuted; font.pixelSize: Theme.textSm; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter; Layout.fillWidth: true }
                }
            }

            ScrollView {
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 2
                    Label { text: "SEARCH RESULTS"; color: Theme.textMuted; font.pixelSize: Theme.textXs; font.weight: Font.DemiBold; Layout.margins: 12 }
                    Repeater {
                        model: root.controller ? root.controller.searchResultsModel : null
                        ItemDelegate {
                            required property var entry
                            width: parent.width
                            height: 48
                            hoverEnabled: true
                            Accessible.name: entry.kind + " " + entry.label
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
            }
        }
    }
}
