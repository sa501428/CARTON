import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Rectangle {
    id: root
    property var tabSession: null
    signal hoverInfo(string text, bool active)
    signal toastRequested(string text, string kind)
    signal contextMenuRequested(var controller, real xFraction, real yFraction)
    color: Theme.appBg

    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            color: Theme.surfaceAlt
            border.color: Theme.borderSubtle
            RowLayout {
                anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10; spacing: 8
                Label { text: "Anchor locus"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                AppComboBox {
                    model: ["Row (anchor Y)", "Column (anchor X)"]
                    currentIndex: root.tabSession && root.tabSession.virtual4CAxis === "column" ? 1 : 0
                    onActivated: if (root.tabSession) root.tabSession.virtual4CAxis = currentIndex === 1 ? "column" : "row"
                    Layout.preferredWidth: 156
                }
                AppTextField {
                    text: root.tabSession ? String(root.tabSession.virtual4CAnchor) : "0"
                    validator: DoubleValidator { bottom: 0; decimals: 0 }
                    onEditingFinished: if (root.tabSession) root.tabSession.virtual4CAnchor = Number(text)
                    Layout.preferredWidth: 140
                }
                Label {
                    text: root.tabSession && root.tabSession.activeController
                        ? "one " + root.tabSession.activeController.resolution + " bp bin"
                        : "one selected-resolution bin"
                    color: Theme.textMuted; font.pixelSize: Theme.textXs
                }
                Item { Layout.fillWidth: true }
                Label { text: "Move across the source map or enter an exact anchor"; color: Theme.textMuted; font.pixelSize: Theme.textXs }
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            MapViewport {
                SplitView.preferredWidth: 390
                SplitView.minimumWidth: 280
                property var sourceCell: root.tabSession && root.tabSession.cells.length > 0
                    ? root.tabSession.cells[root.tabSession.activeCellIndex] : null
                controller: sourceCell ? sourceCell.controller : null
                viewLabel: sourceCell ? sourceCell.label + " · anchor source" : "Anchor source"
                selected: true
                onCursorMoved: function(xFraction, yFraction) {
                    if (!sourceCell || !sourceCell.controller) return
                    var c = sourceCell.controller
                    var position = root.tabSession.virtual4CAxis === "column"
                        ? c.x0 + (c.x1 - c.x0) * xFraction
                        : c.y0 + (c.y1 - c.y0) * yFraction
                    root.tabSession.virtual4CAnchor = Math.floor(position / Math.max(1, c.resolution)) * Math.max(1, c.resolution)
                }
                onViewportInteracted: if (sourceCell) root.tabSession.notifyViewportInteracted(sourceCell.index)
                onHoverInfo: function(text, active) { root.hoverInfo(text, active) }
                onContextMenuRequested: function(controller, xFraction, yFraction) {
                    root.contextMenuRequested(controller, xFraction, yFraction)
                }
            }

            ScrollView {
                SplitView.fillWidth: true
                clip: true
                contentWidth: availableWidth
                Column {
                    width: parent.width
                    spacing: 10
                    padding: 10
                    Repeater {
                        model: root.tabSession ? root.tabSession.cells : []
                        Rectangle {
                            required property var modelData
                            width: parent.width - 20
                            height: 220
                            color: Theme.surface
                            border.color: modelData.selected ? Theme.accent : Theme.border
                            radius: Theme.radiusSm
                            ColumnLayout {
                                anchors.fill: parent
                                Rectangle {
                                    Layout.fillWidth: true; Layout.preferredHeight: 36
                                    color: Theme.surfaceAlt
                                    RowLayout {
                                        anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                                        Label { text: modelData.label; color: Theme.textPrimary; Layout.fillWidth: true; elide: Text.ElideMiddle }
                                        Label {
                                            text: modelData.controller ? modelData.controller.chrX + " · " + modelData.controller.resolution + " bp" : ""
                                            color: Theme.textMuted; font.pixelSize: Theme.textXs
                                        }
                                        AppButton {
                                            text: "Create pooled 1D track"
                                            tonal: true
                                            onClicked: {
                                                var id = root.tabSession.createVirtual4CTrack(modelData.index, "", "cell")
                                                root.toastRequested(id.length > 0 ? "Virtual 4C track created and loaded" : "Could not create virtual 4C track",
                                                                    id.length > 0 ? "success" : "error")
                                            }
                                        }
                                    }
                                }
                                Item {
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    Virtual4CItem {
                                        anchors.fill: parent
                                        anchors.margins: 12
                                        controller: modelData.controller
                                        anchor: root.tabSession ? root.tabSession.virtual4CAnchor : 0
                                        axis: root.tabSession ? root.tabSession.virtual4CAxis : "row"
                                    }
                                    Rectangle {
                                        anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                                        anchors.margins: 12; height: 1; color: Theme.borderStrong
                                    }
                                }
                            }
                            TapHandler { onTapped: root.tabSession.activeCellIndex = modelData.index }
                        }
                    }
                }
            }
        }
    }
}
