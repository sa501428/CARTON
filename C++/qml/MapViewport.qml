import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Rectangle {
    id: root
    property var controller: null
    property string viewLabel: "Map"
    property bool selected: false
    property bool compact: false
    property bool blank: false
    property bool showTrackPanels: true
    property bool showHeader: true
    property bool allowAnnotationEditing: true
    property real hoverXFraction: 0.5
    property real hoverYFraction: 0.5
    property bool crosshairVisible: false
    property real crosshairX: 0.5
    property real crosshairY: 0.5
    onCrosshairVisibleChanged: overlays.requestPaint()
    onCrosshairXChanged: overlays.requestPaint()
    onCrosshairYChanged: overlays.requestPaint()
    signal activated()
    signal viewportInteracted()
    signal hoverInfo(string text, bool active)
    signal cursorMoved(real xFraction, real yFraction)
    signal annotationRequested(real xStartFraction, real yStartFraction, real xEndFraction, real yEndFraction)

    color: Theme.surface
    border.color: selected ? Theme.accent : Theme.border
    border.width: selected ? 2 : 1
    radius: compact ? 2 : Theme.radiusSm
    clip: true

    property int axisExtent: {
        if (compact) return 18
        if (!controller || !showTrackPanels || controller.trackCount === 0) return 34
        return Math.min(110, 34 + Math.max(26, controller.visibleTrackHeight / Math.max(1, controller.trackCount) * 0.16))
    }
    property int headerExtent: showHeader ? (compact ? 24 : 34) : 0

    function formatBp(value) {
        if (value >= 1000000000) return (value / 1000000000).toFixed(2) + " Gb"
        if (value >= 1000000) return (value / 1000000).toFixed(2) + " Mb"
        if (value >= 1000) return (value / 1000).toFixed(1) + " kb"
        return Math.round(value) + " bp"
    }

    function repaint() {
        topTracks.requestPaint()
        leftTracks.requestPaint()
        overlays.requestPaint()
    }

    function drawAxis(canvas, xAxis) {
        var ctx = canvas.getContext("2d")
        ctx.reset()
        ctx.fillStyle = Theme.surfaceSunken
        ctx.fillRect(0, 0, canvas.width, canvas.height)
        if (!root.controller) return
        var horizontal = xAxis
        var length = horizontal ? canvas.width : canvas.height
        var thickness = horizontal ? canvas.height : canvas.width
        var start = horizontal ? root.controller.x0 : root.controller.y0
        var end = horizontal ? root.controller.x1 : root.controller.y1
        var span = Math.max(1, end - start)
        var axisOffset = thickness - (root.compact ? 8 : 17)
        ctx.strokeStyle = Theme.borderStrong
        ctx.lineWidth = 1
        ctx.beginPath()
        if (horizontal) { ctx.moveTo(0, axisOffset + 0.5); ctx.lineTo(length, axisOffset + 0.5) }
        else { ctx.moveTo(axisOffset + 0.5, 0); ctx.lineTo(axisOffset + 0.5, length) }
        ctx.stroke()
        var ticks = root.compact ? 2 : 3
        ctx.fillStyle = Theme.textSecondary
        ctx.font = (root.compact ? "8px" : "10px") + " sans-serif"
        for (var t = 0; t < ticks; ++t) {
            var fraction = ticks === 1 ? 0 : t / (ticks - 1)
            var position = fraction * length
            if (horizontal) {
                ctx.textAlign = t === 0 ? "left" : (t === ticks - 1 ? "right" : "center")
                ctx.textBaseline = "bottom"
                ctx.fillText(root.formatBp(start + span * fraction), position, thickness - 1)
            } else if (!root.compact) {
                ctx.save()
                ctx.translate(thickness - 1, position)
                ctx.rotate(-Math.PI / 2)
                ctx.textAlign = t === 0 ? "left" : (t === ticks - 1 ? "right" : "center")
                ctx.textBaseline = "top"
                ctx.fillText(root.formatBp(start + span * fraction), 0, 0)
                ctx.restore()
            }
        }
        if (!root.showTrackPanels || root.controller.trackCount === 0 || root.compact) return
        var segments = root.controller.visibleTrackSegmentsForPixels(horizontal, Math.max(1, Math.ceil(length)))
        var summaries = root.controller.trackSummaries()
        var activeTracks = []
        for (var si = 0; si < summaries.length; ++si)
            if (summaries[si].visible && !summaries[si].collapsed) activeTracks.push(si)
        var trackSpace = Math.max(1, axisOffset - 2)
        var laneSize = trackSpace / Math.max(1, activeTracks.length)
        for (var i = 0; i < segments.length; ++i) {
            var segment = segments[i]
            var lane = activeTracks.indexOf(segment.trackIndex)
            if (lane < 0) continue
            var p0 = (segment.start - start) / span * length
            var p1 = (segment.end - start) / span * length
            var laneStart = lane * laneSize
            ctx.fillStyle = segment.color
            if (segment.kind === "feature") {
                if (horizontal) ctx.fillRect(Math.max(0, p0), laneStart + laneSize * 0.25,
                                             Math.max(1, Math.min(length, p1) - Math.max(0, p0)), Math.max(2, laneSize * 0.5))
                else ctx.fillRect(laneStart + laneSize * 0.25, Math.max(0, p0), Math.max(2, laneSize * 0.5),
                                  Math.max(1, Math.min(length, p1) - Math.max(0, p0)))
            } else {
                var range = Math.max(0.000001, segment.max - segment.min)
                var zero = (0 - segment.min) / range
                var value = (segment.value - segment.min) / range
                zero = Math.max(0, Math.min(1, zero))
                value = Math.max(0, Math.min(1, value))
                if (horizontal) {
                    var zy = laneStart + laneSize * (1 - zero)
                    var vy = laneStart + laneSize * (1 - value)
                    ctx.fillRect(Math.max(0, p0), Math.min(zy, vy),
                                 Math.max(1, Math.min(length, p1) - Math.max(0, p0)), Math.max(1, Math.abs(vy - zy)))
                } else {
                    var zx = laneStart + laneSize * zero
                    var vx = laneStart + laneSize * value
                    ctx.fillRect(Math.min(zx, vx), Math.max(0, p0), Math.max(1, Math.abs(vx - zx)),
                                 Math.max(1, Math.min(length, p1) - Math.max(0, p0)))
                }
            }
        }
    }

    Connections {
        target: root.controller
        function onViewChanged() { root.repaint() }
        function onTracksChanged() { root.repaint() }
        function onAnnotationsChanged() { root.repaint() }
        function onDisplayOptionsChanged() { root.repaint() }
        function onRecordsChanged() { overlays.requestPaint() }
    }

    Rectangle {
        id: header
        visible: root.showHeader
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.headerExtent
        color: root.selected ? Theme.selectedSurface : Theme.surfaceAlt
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: root.compact ? 5 : 9
            anchors.rightMargin: root.compact ? 5 : 9
            spacing: 6
            Label {
                text: root.viewLabel
                color: Theme.textPrimary
                font.pixelSize: root.compact ? Theme.textXs : Theme.textSm
                font.weight: root.selected ? Font.DemiBold : Font.Normal
                elide: Text.ElideMiddle
                Layout.fillWidth: true
            }
            Label {
                visible: !root.compact
                text: root.controller ? root.controller.matrixType.toUpperCase() : "—"
                color: Theme.accent
                font.pixelSize: Theme.textXs
            }
            BusyIndicator {
                running: root.controller && root.controller.busy
                visible: running
                Layout.preferredWidth: 16
                Layout.preferredHeight: 16
            }
        }
        TapHandler { onTapped: root.activated() }
    }

    Item {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: root.showHeader ? header.bottom : parent.top
        anchors.bottom: parent.bottom
        property real plotSize: Math.max(1, Math.min(width - root.axisExtent, height - root.axisExtent))
        property real plotX: Math.max(0, (width - root.axisExtent - plotSize) / 2)
        property real plotY: Math.max(0, (height - root.axisExtent - plotSize) / 2)

        Canvas {
            id: topTracks
            x: content.plotX + root.axisExtent
            y: content.plotY
            width: content.plotSize
            height: root.axisExtent
            onPaint: root.drawAxis(topTracks, true)
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }
        Canvas {
            id: leftTracks
            x: content.plotX
            y: content.plotY + root.axisExtent
            width: root.axisExtent
            height: content.plotSize
            onPaint: root.drawAxis(leftTracks, false)
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
        }

        Rectangle {
            id: heatmapHost
            x: content.plotX + root.axisExtent
            y: content.plotY + root.axisExtent
            width: content.plotSize
            height: content.plotSize
            color: "white"
            border.color: Theme.borderStrong
            clip: true

            HicHeatmapItem { anchors.fill: parent; controller: root.blank ? null : root.controller }

            Canvas {
                id: overlays
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    if (!root.controller || root.blank) return
                    if (root.controller.showGridlines && !root.compact) {
                        ctx.strokeStyle = Theme.gridline
                        ctx.lineWidth = 1
                        for (var g = 1; g < 5; ++g) {
                            ctx.beginPath()
                            ctx.moveTo(width * g / 5, 0); ctx.lineTo(width * g / 5, height)
                            ctx.moveTo(0, height * g / 5); ctx.lineTo(width, height * g / 5)
                            ctx.stroke()
                        }
                    }
                    if (root.controller.chrX === root.controller.chrY) {
                        ctx.strokeStyle = Theme.boundaryLine
                        ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(width, height); ctx.stroke()
                    }
                    var annotations = root.controller.visibleAnnotations()
                    var spanX = Math.max(1, root.controller.x1 - root.controller.x0)
                    var spanY = Math.max(1, root.controller.y1 - root.controller.y0)
                    for (var i = 0; i < annotations.length; ++i) {
                        var annotation = annotations[i]
                        var x = (annotation.x0 - root.controller.x0) / spanX * width
                        var y = (annotation.y0 - root.controller.y0) / spanY * height
                        var w = Math.max(root.compact ? 1 : 3, (annotation.x1 - annotation.x0) / spanX * width)
                        var h = Math.max(root.compact ? 1 : 3, (annotation.y1 - annotation.y0) / spanY * height)
                        ctx.globalAlpha = annotation.transparent ? 0.45 : 1
                        ctx.strokeStyle = annotation.color
                        ctx.lineWidth = root.compact ? 1 : 1.5
                        ctx.strokeRect(x, y, w, h)
                    }
                    ctx.globalAlpha = 1
                    if (root.crosshairVisible) {
                        ctx.strokeStyle = Theme.guideLine
                        ctx.lineWidth = 1
                        ctx.setLineDash([4, 4])
                        ctx.beginPath()
                        ctx.moveTo(root.crosshairX * width, 0); ctx.lineTo(root.crosshairX * width, height)
                        ctx.moveTo(0, root.crosshairY * height); ctx.lineTo(width, root.crosshairY * height)
                        ctx.stroke()
                        ctx.setLineDash([])
                    }
                }
            }

            MouseArea {
                id: interaction
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                property real lastX: 0
                property real lastY: 0
                property real startX: 0
                property real startY: 0
                property bool dragged: false
                onPressed: function(mouse) {
                    root.activated()
                    lastX = mouse.x; lastY = mouse.y; startX = mouse.x; startY = mouse.y; dragged = false
                    if (root.controller) root.controller.beginInteraction()
                }
                onPositionChanged: function(mouse) {
                    root.hoverXFraction = Math.max(0, Math.min(1, mouse.x / Math.max(1, width)))
                    root.hoverYFraction = Math.max(0, Math.min(1, mouse.y / Math.max(1, height)))
                    root.cursorMoved(root.hoverXFraction, root.hoverYFraction)
                    if (root.controller) root.hoverInfo(root.controller.positionText(root.hoverXFraction, root.hoverYFraction), true)
                    if ((mouse.buttons & Qt.LeftButton) && root.controller && !(mouse.modifiers & Qt.ShiftModifier)) {
                        if (Math.abs(mouse.x - startX) > 3 || Math.abs(mouse.y - startY) > 3) dragged = true
                        root.controller.pan(-(mouse.x - lastX) / Math.max(1, width), -(mouse.y - lastY) / Math.max(1, height))
                        lastX = mouse.x; lastY = mouse.y
                        root.viewportInteracted()
                    }
                }
                onExited: root.hoverInfo("", false)
                onReleased: function(mouse) {
                    if (!root.controller) return
                    root.controller.endInteraction()
                    var dx = Math.abs(mouse.x - startX)
                    var dy = Math.abs(mouse.y - startY)
                    if ((mouse.modifiers & Qt.ShiftModifier) && dx > 5 && dy > 5) {
                        if ((mouse.modifiers & Qt.ControlModifier) && root.allowAnnotationEditing)
                            root.annotationRequested(startX / width, startY / height, mouse.x / width, mouse.y / height)
                        else
                            root.controller.zoomToFractions(startX / width, startY / height, mouse.x / width, mouse.y / height)
                        root.viewportInteracted()
                    } else if (!dragged && mouse.button === Qt.LeftButton) {
                        root.controller.selectAnnotationAt(mouse.x / width, mouse.y / height)
                    }
                }
                onDoubleClicked: function(mouse) {
                    if (root.controller) {
                        root.controller.zoom(2, mouse.x / width, mouse.y / height)
                        root.viewportInteracted()
                    }
                }
                onWheel: function(wheel) {
                    if (!root.controller) return
                    var delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.pixelDelta.y
                    root.controller.zoom(Math.pow(2, delta / 360), wheel.x / width, wheel.y / height)
                    root.viewportInteracted()
                    wheel.accepted = true
                }
            }

            Rectangle {
                visible: root.blank
                anchors.fill: parent
                color: "#050505"
                Label { anchors.centerIn: parent; text: "Diagonal hidden"; color: "#9ca3af"; font.pixelSize: Theme.textXs }
                TapHandler { onTapped: root.activated() }
            }
        }
    }
}
