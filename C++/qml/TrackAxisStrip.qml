import QtQuick
import Carton

Canvas {
    id: root
    property var controller: null
    property bool horizontal: true
    property color backgroundColor: Theme.surfaceSunken
    property string placementFilter: ""

    Connections {
        target: root.controller
        function onViewChanged() { root.requestPaint() }
        function onTracksChanged() { root.requestPaint() }
    }
    onControllerChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        ctx.fillStyle = backgroundColor
        ctx.fillRect(0, 0, width, height)
        if (!controller) return
        var length = horizontal ? width : height
        var thickness = horizontal ? height : width
        var start = horizontal ? controller.x0 : controller.y0
        var end = horizontal ? controller.x1 : controller.y1
        var span = Math.max(1, end - start)
        var segments = controller.visibleTrackSegmentsForPixels(horizontal, Math.max(1, Math.ceil(length)))
        var summaries = controller.trackSummaries()
        var active = []
        for (var i = 0; i < summaries.length; ++i)
            if (summaries[i].visible && !summaries[i].collapsed &&
                    (placementFilter.length === 0 || summaries[i].placement === placementFilter)) active.push(i)
        var laneSize = thickness / Math.max(1, active.length)
        for (var j = 0; j < segments.length; ++j) {
            var segment = segments[j]
            var lane = active.indexOf(segment.trackIndex)
            if (lane < 0) continue
            var p0 = (segment.start - start) / span * length
            var p1 = (segment.end - start) / span * length
            var laneStart = lane * laneSize
            ctx.fillStyle = segment.color
            if (segment.kind === "feature") {
                if (horizontal) ctx.fillRect(p0, laneStart + laneSize * 0.25, Math.max(1, p1 - p0), Math.max(2, laneSize * 0.5))
                else ctx.fillRect(laneStart + laneSize * 0.25, p0, Math.max(2, laneSize * 0.5), Math.max(1, p1 - p0))
            } else {
                var range = Math.max(0.000001, segment.max - segment.min)
                var zero = Math.max(0, Math.min(1, (0 - segment.min) / range))
                var value = Math.max(0, Math.min(1, (segment.value - segment.min) / range))
                if (horizontal) {
                    var y0 = laneStart + laneSize * (1 - zero)
                    var y1 = laneStart + laneSize * (1 - value)
                    ctx.fillRect(p0, Math.min(y0, y1), Math.max(1, p1 - p0), Math.max(1, Math.abs(y1 - y0)))
                } else {
                    var x0 = laneStart + laneSize * zero
                    var x1 = laneStart + laneSize * value
                    ctx.fillRect(Math.min(x0, x1), p0, Math.max(1, Math.abs(x1 - x0)), Math.max(1, p1 - p0))
                }
            }
        }
        ctx.strokeStyle = Theme.borderStrong
        ctx.strokeRect(0.5, 0.5, width - 1, height - 1)
    }
}
