pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Rectangle {
    id: root
    property var tabSession: null
    signal loadRegionsRequested(string format)
    signal hoverInfo(string text, bool active)
    signal toastRequested(string text, string kind)
    signal contextMenuRequested(var controller, real xFraction, real yFraction)
    signal bullseyeHover(var controller, real xFraction, real yFraction)
    property real sharedCursorX: 0.5
    property real sharedCursorY: 0.5
    property bool sharedCursorActive: false

    color: Theme.appBg

    function cellAt(row, column) {
        if (!tabSession)
            return null;
        var values = tabSession.cellLayout;
        for (var i = 0; i < values.length; ++i)
            if (values[i].row === row && values[i].column === column)
                return values[i];
        return null;
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        WorkspaceLayoutBar {
            Layout.fillWidth: true
            tabSession: root.tabSession
            onLoadRegionsRequested: function (format) {
                root.loadRegionsRequested(format);
            }
        }
        RowLayout {
            visible: root.tabSession && ["multi-region", "map-region", "pairwise"].indexOf(root.tabSession.type) >= 0
            Layout.fillWidth: true
            Layout.margins: 8
            Label {
                text: "Region window (bp)"
                color: Theme.textSecondary
            }
            SpinBox {
                from: 1000
                to: 1000000000
                stepSize: 100000
                editable: true
                value: root.tabSession ? root.tabSession.windowSize : 2000000
                onValueModified: if (root.tabSession)
                    root.tabSession.windowSize = value
                Accessible.name: "Region window in base pairs"
            }
            Item {
                Layout.fillWidth: true
            }
        }

        Loader {
            Layout.fillWidth: true
            Layout.fillHeight: true
            sourceComponent: {
                if (!root.tabSession)
                    return galleryComponent;
                if (root.tabSession.type === "pairwise")
                    return pairwiseComponent;
                if (root.tabSession.type === "rotated-45")
                    return rotatedComponent;
                if (root.tabSession.type === "bullseye")
                    return bullseyeComponent;
                if (root.tabSession.type === "virtual-4c")
                    return virtual4CComponent;
                if (root.tabSession.type === "processing")
                    return processingComponent;
                return galleryComponent;
            }
        }
    }

    Component {
        id: rotatedComponent
        Rotated45View {
            tabSession: root.tabSession
            onHoverInfo: function (text, active) {
                root.hoverInfo(text, active);
            }
            onContextMenuRequested: function (controller, xFraction, yFraction) {
                root.contextMenuRequested(controller, xFraction, yFraction);
            }
        }
    }

    Component {
        id: bullseyeComponent
        BullseyeView {
            tabSession: root.tabSession
            onHoverInfo: function (text, active) {
                root.hoverInfo(text, active);
            }
            onContextMenuRequested: function (controller, xFraction, yFraction) {
                root.contextMenuRequested(controller, xFraction, yFraction);
            }
        }
    }

    Component {
        id: virtual4CComponent
        Virtual4CView {
            tabSession: root.tabSession
            onHoverInfo: function (text, active) {
                root.hoverInfo(text, active);
            }
            onToastRequested: function (text, kind) {
                root.toastRequested(text, kind);
            }
            onContextMenuRequested: function (controller, xFraction, yFraction) {
                root.contextMenuRequested(controller, xFraction, yFraction);
            }
        }
    }

    Component {
        id: processingComponent
        ProcessingView {
            tabSession: root.tabSession
            onHoverInfo: function (text, active) {
                root.hoverInfo(text, active);
            }
            onContextMenuRequested: function (controller, xFraction, yFraction) {
                root.contextMenuRequested(controller, xFraction, yFraction);
            }
        }
    }

    Component {
        id: galleryComponent
        ScrollView {
            clip: true
            contentWidth: availableWidth
            Item {
                width: parent.width
                implicitHeight: grid.implicitHeight + 20
                GridLayout {
                    id: grid
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    anchors.margins: 10
                    columns: root.tabSession ? Math.max(1, root.tabSession.type === "map-region" ? root.tabSession.columnCount : root.tabSession.layoutColumns) : 1
                    columnSpacing: 8
                    rowSpacing: 8
                    Repeater {
                        model: root.tabSession ? root.tabSession.cellLayout : []
                        MapViewport {
                            required property var modelData
                            Layout.row: modelData.row
                            Layout.column: modelData.column
                            Layout.fillWidth: true
                            Layout.preferredWidth: root.tabSession && root.tabSession.type === "map-region" ? 360 : 420
                            Layout.preferredHeight: Math.max(334, Layout.preferredWidth + 34)
                            controller: modelData.controller
                            viewLabel: modelData.label
                            selected: (root.tabSession && root.tabSession.activeCellIndex === modelData.index)
                            showTrackPanels: true
                            crosshairVisible: root.tabSession && root.tabSession.linkCrosshair && root.sharedCursorActive
                            crosshairX: root.sharedCursorX
                            crosshairY: root.sharedCursorY
                            onActivated: root.tabSession.activeCellIndex = modelData.index
                            onCursorMoved: function (xFraction, yFraction) {
                                root.sharedCursorX = xFraction;
                                root.sharedCursorY = yFraction;
                                root.sharedCursorActive = true;
                                root.bullseyeHover(modelData.controller, xFraction, yFraction);
                            }
                            onViewportInteracted: root.tabSession.notifyViewportInteracted(modelData.index)
                            scopedAnnotationEditing: true
                            onAnnotationRequested: function (x0, y0, x1, y1) {
                                root.tabSession.addScopedAnnotation(modelData.index, x0, y0, x1, y1, root.tabSession.layerScope);
                            }
                            onContextMenuRequested: function (controller, xFraction, yFraction) {
                                root.contextMenuRequested(controller, xFraction, yFraction);
                            }
                            onHoverInfo: function (text, active) {
                                if (!active)
                                    root.sharedCursorActive = false;
                                root.hoverInfo(text, active);
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: pairwiseComponent
        Flickable {
            id: pairwiseFlick
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            property int cellSize: 190
            property int headerSize: 72
            contentWidth: headerSize + (root.tabSession ? root.tabSession.columnCount : 0) * cellSize + 12
            contentHeight: headerSize + (root.tabSession ? root.tabSession.rowCount : 0) * cellSize + 12

            Item {
                width: pairwiseFlick.contentWidth
                height: pairwiseFlick.contentHeight

                Repeater {
                    model: root.tabSession ? root.tabSession.columnCount : 0
                    Item {
                        required property int index
                        x: pairwiseFlick.headerSize + index * pairwiseFlick.cellSize
                        y: 0
                        width: pairwiseFlick.cellSize
                        height: pairwiseFlick.headerSize
                        property var cell: root.cellAt(0, index)
                        Label {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 22
                            text: parent.cell ? parent.cell.label.split(" × ")[0] : ""
                            color: Theme.textSecondary
                            font.pixelSize: Theme.textXs
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                        TrackAxisStrip {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.topMargin: 22
                            anchors.bottom: parent.bottom
                            controller: parent.cell ? parent.cell.controller : null
                            horizontal: true
                        }
                    }
                }

                Repeater {
                    model: root.tabSession ? root.tabSession.rowCount : 0
                    Item {
                        required property int index
                        x: 0
                        y: pairwiseFlick.headerSize + index * pairwiseFlick.cellSize
                        width: pairwiseFlick.headerSize
                        height: pairwiseFlick.cellSize
                        property var cell: root.cellAt(index, 0)
                        Label {
                            anchors.left: parent.left
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            width: 22
                            text: parent.cell ? parent.cell.label.split(" × ").pop() : ""
                            color: Theme.textSecondary
                            font.pixelSize: Theme.textXs
                            rotation: -90
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                        }
                        TrackAxisStrip {
                            anchors.left: parent.left
                            anchors.leftMargin: 22
                            anchors.right: parent.right
                            anchors.top: parent.top
                            anchors.bottom: parent.bottom
                            controller: parent.cell ? parent.cell.controller : null
                            horizontal: false
                        }
                    }
                }

                Repeater {
                    model: root.tabSession ? root.tabSession.cellLayout : []
                    MapViewport {
                        required property var modelData
                        x: pairwiseFlick.headerSize + modelData.column * pairwiseFlick.cellSize
                        y: pairwiseFlick.headerSize + modelData.row * pairwiseFlick.cellSize
                        width: pairwiseFlick.cellSize
                        height: pairwiseFlick.cellSize
                        controller: modelData.controller
                        viewLabel: modelData.label
                        selected: (root.tabSession && root.tabSession.activeCellIndex === modelData.index)
                        blank: modelData.blank
                        compact: true
                        showHeader: false
                        showTrackPanels: false
                        crosshairVisible: root.tabSession && root.tabSession.linkCrosshair && root.sharedCursorActive
                        crosshairX: root.sharedCursorX
                        crosshairY: root.sharedCursorY
                        onActivated: root.tabSession.activeCellIndex = modelData.index
                        onCursorMoved: function (xFraction, yFraction) {
                            root.sharedCursorX = xFraction;
                            root.sharedCursorY = yFraction;
                            root.sharedCursorActive = true;
                            root.bullseyeHover(modelData.controller, xFraction, yFraction);
                        }
                        scopedAnnotationEditing: true
                        onAnnotationRequested: function (x0, y0, x1, y1) {
                            root.tabSession.addScopedAnnotation(modelData.index, x0, y0, x1, y1, root.tabSession.layerScope);
                        }
                        onContextMenuRequested: function (controller, xFraction, yFraction) {
                            root.contextMenuRequested(controller, xFraction, yFraction);
                        }
                        onHoverInfo: function (text, active) {
                            if (!active)
                                root.sharedCursorActive = false;
                            root.hoverInfo(text, active);
                        }
                    }
                }
            }
            ScrollBar.horizontal: ScrollBar {}
            ScrollBar.vertical: ScrollBar {}
        }
    }
}
