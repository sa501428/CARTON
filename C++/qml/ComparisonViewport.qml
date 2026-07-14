import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Rectangle {
    id: root
    property var controller: null
    property real crosshairX: 0
    property real crosshairY: 0
    property bool crosshairVisible: false
    property string viewLabel: "Comparison"
    signal viewportInteracted()
    signal crosshairMoved(real xFraction, real yFraction)

    onCrosshairXChanged: overlay.requestPaint()
    onCrosshairYChanged: overlay.requestPaint()
    onCrosshairVisibleChanged: overlay.requestPaint()

    color: Theme.appBg
    border.color: Theme.borderSubtle

    Connections {
        target: root.controller
        function onViewChanged() { overlay.requestPaint() }
        function onDisplayOptionsChanged() { overlay.requestPaint() }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Label { text: root.viewLabel; color: Theme.textPrimary; font.pixelSize: Theme.textMd; font.weight: Font.DemiBold; Layout.fillWidth: true }
            Label { text: root.controller ? root.controller.matrixType.toUpperCase() : "—"; color: Theme.accent; font.pixelSize: Theme.textXs; font.weight: Font.Bold }
            BusyIndicator { running: root.controller && root.controller.busy; visible: running; Layout.preferredWidth: 18; Layout.preferredHeight: 18 }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            color: "#ffffff"
            border.color: Theme.borderStrong
            clip: true

            HicHeatmapItem { anchors.fill: parent; controller: root.controller }

            Canvas {
                id: overlay
                anchors.fill: parent
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.reset()
                    ctx.strokeStyle = Theme.gridline
                    ctx.lineWidth = 1
                    for (var i = 1; i < 10; i++) {
                        ctx.beginPath()
                        ctx.moveTo(width * i / 10, 0); ctx.lineTo(width * i / 10, height)
                        ctx.moveTo(0, height * i / 10); ctx.lineTo(width, height * i / 10)
                        ctx.stroke()
                    }
                    if (root.controller && root.controller.chrX === root.controller.chrY) {
                        ctx.strokeStyle = Theme.boundaryLine
                        ctx.beginPath(); ctx.moveTo(0, 0); ctx.lineTo(width, height); ctx.stroke()
                    }
                    if (root.crosshairVisible) {
                        ctx.strokeStyle = Theme.guideLine
                        ctx.setLineDash([4, 4])
                        ctx.beginPath()
                        ctx.moveTo(root.crosshairX * width, 0); ctx.lineTo(root.crosshairX * width, height)
                        ctx.moveTo(0, root.crosshairY * height); ctx.lineTo(width, root.crosshairY * height)
                        ctx.stroke()
                    }
                }
            }

            MouseArea {
                Accessible.name: "Interactive comparison Hi-C matrix"
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton
                property real lastX: 0
                property real lastY: 0
                property real startX: 0
                property real startY: 0
                property bool moved: false
                onPressed: function(mouse) {
                    lastX = mouse.x; lastY = mouse.y; startX = mouse.x; startY = mouse.y; moved = false
                    if (root.controller) root.controller.beginInteraction()
                }
                onPositionChanged: function(mouse) {
                    var fx = Math.max(0, Math.min(1, mouse.x / Math.max(1, width)))
                    var fy = Math.max(0, Math.min(1, mouse.y / Math.max(1, height)))
                    root.crosshairMoved(fx, fy)
                    if (mouse.buttons & Qt.LeftButton && root.controller) {
                        if (Math.abs(mouse.x - startX) > 4 || Math.abs(mouse.y - startY) > 4) moved = true
                        root.controller.pan(-(mouse.x - lastX) / width, -(mouse.y - lastY) / height)
                        lastX = mouse.x; lastY = mouse.y
                        root.viewportInteracted()
                    }
                }
                onReleased: function(mouse) {
                    if (!root.controller) return
                    root.controller.endInteraction()
                    if (!moved) {
                        root.controller.zoom(2.0, mouse.x / Math.max(1, width), mouse.y / Math.max(1, height))
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
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 26; color: Theme.footerBg
                Label {
                    anchors.centerIn: parent
                    text: root.controller ? root.controller.chrX + ":" + root.controller.x0 + "–" + root.controller.x1 + "  ×  " + root.controller.chrY + ":" + root.controller.y0 + "–" + root.controller.y1 : "No comparison dataset"
                    color: Theme.chromeText; font.pixelSize: Theme.textXs; elide: Text.ElideMiddle; width: parent.width - 16; horizontalAlignment: Text.AlignHCenter
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true; Layout.preferredHeight: 24; color: "transparent"
            Rectangle {
                anchors.left: parent.left; anchors.verticalCenter: parent.verticalCenter
                width: parent.width; height: 8
                gradient: Gradient {
                    orientation: Gradient.Horizontal
                    GradientStop { position: 0; color: root.controller ? root.controller.missingValueColor : Theme.missingData }
                    GradientStop { position: 0.5; color: "white" }
                    GradientStop { position: 1; color: "#d7191c" }
                }
            }
            Label { anchors.left: parent.left; anchors.top: parent.verticalCenter; text: root.controller ? Number(root.controller.colorMin).toPrecision(3) : ""; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
            Label { anchors.right: parent.right; anchors.top: parent.verticalCenter; text: root.controller ? Number(root.controller.colorMax).toPrecision(3) : ""; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
        }
    }
}
