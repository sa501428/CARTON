import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Rectangle {
    id: root
    property var tabSession: null
    signal hoverInfo(string text, bool active)
    color: Theme.appBg

    function formatBp(value) {
        if (value >= 1000000) return (value / 1000000).toFixed(2) + " Mb"
        if (value >= 1000) return (value / 1000).toFixed(1) + " kb"
        return Math.round(value) + " bp"
    }

    function trackHeight(controller, placement) {
        if (!controller) return 0
        var count = controller.trackCount
        var summaries = controller.trackSummaries()
        var total = 0
        for (var i = 0; i < summaries.length; ++i)
            if (summaries[i].visible && !summaries[i].collapsed && summaries[i].placement === placement)
                total += Math.max(20, summaries[i].height)
        return total
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            color: Theme.surfaceAlt
            border.color: Theme.borderSubtle
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8
                Label { text: "Synchronized diagonal strip"; color: Theme.textPrimary; font.weight: Font.DemiBold }
                Label { text: "Height"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                SpinBox {
                    from: 100; to: 1200; stepSize: 20; editable: true
                    value: root.tabSession ? root.tabSession.analysisPaneHeight : 260
                    onValueModified: if (root.tabSession) root.tabSession.analysisPaneHeight = value
                    Layout.preferredWidth: 100
                }
                Label { text: "Maximum distance"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                AppTextField {
                    text: root.tabSession ? String(root.tabSession.diagonalMaxDistance) : "2000000"
                    validator: DoubleValidator { bottom: 1000; top: 1000000000; decimals: 0 }
                    onEditingFinished: if (root.tabSession) root.tabSession.diagonalMaxDistance = Number(text)
                    Layout.preferredWidth: 130
                }
                Label {
                    text: root.tabSession ? root.formatBp(root.tabSession.diagonalMaxDistance) : ""
                    color: Theme.textMuted; font.pixelSize: Theme.textXs
                }
                Item { Layout.fillWidth: true }
                Label { text: "Wheel zooms; drag scrolls all maps"; color: Theme.textMuted; font.pixelSize: Theme.textXs }
            }
        }

        ScrollView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            contentWidth: availableWidth
            Column {
                width: parent.width
                spacing: 10
                padding: 10
                Repeater {
                    model: root.tabSession ? root.tabSession.cells : []
                    Rectangle {
                        id: pane
                        required property var modelData
                        width: parent.width - 20
                        height: (root.tabSession ? root.tabSession.analysisPaneHeight : 260) + 36 +
                                root.trackHeight(modelData.controller, "above") +
                                root.trackHeight(modelData.controller, "below")
                        color: Theme.surface
                        border.color: modelData.selected ? Theme.accent : Theme.border
                        radius: Theme.radiusSm
                        clip: true

                        ColumnLayout {
                            anchors.fill: parent
                            spacing: 0
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                color: Theme.surfaceAlt
                                RowLayout {
                                    anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8
                                    Label {
                                        text: pane.modelData.label
                                        color: Theme.textPrimary
                                        Layout.fillWidth: true
                                        elide: Text.ElideMiddle
                                    }
                                    Repeater {
                                        model: pane.modelData.controller ? pane.modelData.controller.trackSummaries() : []
                                        AppButton {
                                            required property var modelData
                                            text: modelData.name + (modelData.placement === "below" ? " ↓" : " ↑")
                                            tonal: true
                                            onClicked: pane.modelData.controller.setTrackPlacement(
                                                modelData.index, modelData.placement === "below" ? "above" : "below")
                                        }
                                    }
                                    AppCheckBox {
                                        text: "Diagonal at bottom"
                                        checked: pane.modelData.flipped
                                        onToggled: root.tabSession.setMapFlipped(pane.modelData.mapIndex, checked)
                                    }
                                }
                            }
                            TrackAxisStrip {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.trackHeight(pane.modelData.controller, "above")
                                visible: height > 0
                                controller: pane.modelData.controller
                                horizontal: true
                                placementFilter: "above"
                            }
                            Rectangle {
                                id: strip
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.tabSession ? root.tabSession.analysisPaneHeight : 260
                                color: "white"
                                border.color: Theme.borderStrong
                                clip: true
                                RotatedHeatmapItem {
                                    anchors.fill: parent
                                    controller: pane.modelData.controller
                                    maxDistance: root.tabSession ? root.tabSession.diagonalMaxDistance : 2000000
                                    flipped: pane.modelData.flipped
                                }
                                Canvas {
                                    id: annotationCanvas
                                    anchors.fill: parent
                                    Connections {
                                        target: pane.modelData.controller
                                        function onAnnotationsChanged() { annotationCanvas.requestPaint() }
                                        function onViewChanged() { annotationCanvas.requestPaint() }
                                    }
                                    onWidthChanged: requestPaint()
                                    onHeightChanged: requestPaint()
                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.reset()
                                        var c = pane.modelData.controller
                                        if (!c || c.chrX !== c.chrY) return
                                        var start = Math.min(c.x0, c.y0)
                                        var end = Math.max(c.x1, c.y1)
                                        var span = Math.max(1, end - start)
                                        var maximum = Math.max(1, root.tabSession.diagonalMaxDistance)
                                        var annotations = c.visibleAnnotations()
                                        var seen = ({})
                                        for (var i = 0; i < annotations.length; ++i) {
                                            var a = annotations[i]
                                            var lowX = Math.min(a.x0, a.y0)
                                            var highX = Math.max(a.x0, a.y0)
                                            var lowY = Math.min(a.x1, a.y1)
                                            var highY = Math.max(a.x1, a.y1)
                                            var key = lowX + ":" + highX + ":" + lowY + ":" + highY
                                            if (seen[key]) continue
                                            seen[key] = true
                                            var mid = (a.x0 + a.x1 + a.y0 + a.y1) / 4
                                            var distance = Math.abs((a.y0 + a.y1) / 2 - (a.x0 + a.x1) / 2)
                                            if (distance > maximum) continue
                                            var px = (mid - start) / span * width
                                            var py = distance / maximum * height
                                            if (pane.modelData.flipped) py = height - py
                                            var halfW = Math.max(3, ((a.x1 - a.x0) + (a.y1 - a.y0)) / 4 / span * width)
                                            var halfH = Math.max(3, ((a.x1 - a.x0) + (a.y1 - a.y0)) / 2 / maximum * height)
                                            ctx.strokeStyle = a.color
                                            ctx.globalAlpha = a.transparent ? 0.45 : 1
                                            ctx.lineWidth = 1.5
                                            ctx.beginPath()
                                            ctx.moveTo(px - halfW, py)
                                            ctx.lineTo(px, py - halfH)
                                            ctx.lineTo(px + halfW, py)
                                            ctx.lineTo(px, py + halfH)
                                            ctx.closePath()
                                            ctx.stroke()
                                        }
                                        ctx.globalAlpha = 1
                                    }
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    property real lastX: 0
                                    onPressed: function(mouse) {
                                        root.tabSession.activeCellIndex = pane.modelData.index
                                        lastX = mouse.x
                                        if (pane.modelData.controller) pane.modelData.controller.beginInteraction()
                                    }
                                    onPositionChanged: function(mouse) {
                                        var c = pane.modelData.controller
                                        if (!c) return
                                        var fx = Math.max(0, Math.min(1, mouse.x / Math.max(1, width)))
                                        root.hoverInfo(c.positionText(fx, fx), true)
                                        if (mouse.buttons & Qt.LeftButton) {
                                            var delta = -(mouse.x - lastX) / Math.max(1, width)
                                            c.pan(delta, delta)
                                            lastX = mouse.x
                                            root.tabSession.notifyViewportInteracted(pane.modelData.index)
                                        }
                                    }
                                    onExited: root.hoverInfo("", false)
                                    onReleased: if (pane.modelData.controller) pane.modelData.controller.endInteraction()
                                    onWheel: function(wheel) {
                                        var c = pane.modelData.controller
                                        if (!c) return
                                        var delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.pixelDelta.y
                                        var fx = Math.max(0, Math.min(1, wheel.x / Math.max(1, width)))
                                        c.zoom(Math.pow(2, delta / 360), fx, fx)
                                        root.tabSession.notifyViewportInteracted(pane.modelData.index)
                                        wheel.accepted = true
                                    }
                                }
                            }
                            TrackAxisStrip {
                                Layout.fillWidth: true
                                Layout.preferredHeight: root.trackHeight(pane.modelData.controller, "below")
                                visible: height > 0
                                controller: pane.modelData.controller
                                horizontal: true
                                placementFilter: "below"
                            }
                        }
                    }
                }
            }
        }
    }
}
