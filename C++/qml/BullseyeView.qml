import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Rectangle {
    id: root
    property var tabSession: null
    signal hoverInfo(string text, bool active)
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
                AppCheckBox {
                    text: "Pin center"
                    checked: root.tabSession && root.tabSession.bullseyePinned
                    onToggled: if (root.tabSession) root.tabSession.bullseyePinned = checked
                }
                Label { text: "X"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                AppTextField {
                    text: root.tabSession ? String(root.tabSession.bullseyeCenterX) : "0"
                    validator: DoubleValidator { bottom: 0; decimals: 0 }
                    onEditingFinished: if (root.tabSession) root.tabSession.bullseyeCenterX = Number(text)
                    Layout.preferredWidth: 112
                }
                Label { text: "Y"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                AppTextField {
                    text: root.tabSession ? String(root.tabSession.bullseyeCenterY) : "0"
                    validator: DoubleValidator { bottom: 0; decimals: 0 }
                    onEditingFinished: if (root.tabSession) root.tabSession.bullseyeCenterY = Number(text)
                    Layout.preferredWidth: 112
                }
                Label { text: "Radius (bins)"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                SpinBox {
                    from: 1; to: 100; editable: true
                    value: root.tabSession ? root.tabSession.bullseyeRadiusBins : 12
                    onValueModified: if (root.tabSession) root.tabSession.bullseyeRadiusBins = value
                    Layout.preferredWidth: 92
                }
                Label { text: "Radius (bp)"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                AppTextField {
                    text: root.tabSession ? String(root.tabSession.bullseyeRadiusBp) : "0"
                    validator: DoubleValidator { bottom: 1; decimals: 0 }
                    onEditingFinished: if (root.tabSession) root.tabSession.bullseyeRadiusBp = Number(text)
                    Layout.preferredWidth: 112
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: root.tabSession && root.tabSession.bullseyePinned
                          ? "Pinned; edit the locus fields to move"
                          : "Hover the source map to update every bullseye"
                    color: Theme.textMuted; font.pixelSize: Theme.textXs
                }
            }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            orientation: Qt.Horizontal
            ScrollView {
                SplitView.preferredWidth: 410
                SplitView.minimumWidth: 280
                clip: true
                MapViewport {
                    width: Math.max(280, parent.width)
                    height: width
                    property var sourceCell: root.tabSession && root.tabSession.cells.length > 0
                        ? root.tabSession.cells[root.tabSession.activeCellIndex] : null
                    controller: sourceCell ? sourceCell.controller : null
                    viewLabel: sourceCell ? sourceCell.label + " · hover source" : "Hover source"
                    selected: true
                    onCursorMoved: function(xFraction, yFraction) {
                        if (sourceCell) root.tabSession.updateBullseyeFromFractions(sourceCell.index, xFraction, yFraction)
                    }
                    onViewportInteracted: if (sourceCell) root.tabSession.notifyViewportInteracted(sourceCell.index)
                    onHoverInfo: function(text, active) { root.hoverInfo(text, active) }
                }
            }
            ScrollView {
                SplitView.fillWidth: true
                clip: true
                contentHeight: availableHeight
                Row {
                    height: parent.height
                    spacing: 10
                    padding: 10
                    Repeater {
                        model: root.tabSession ? root.tabSession.cells : []
                        Rectangle {
                            required property var modelData
                            width: Math.max(300, (parent.parent.width - 30) / Math.min(2, root.tabSession.cellCount))
                            height: parent.height - 20
                            color: Theme.surface
                            border.color: modelData.selected ? Theme.accent : Theme.border
                            radius: Theme.radiusSm
                            ColumnLayout {
                                anchors.fill: parent
                                Label {
                                    Layout.fillWidth: true; Layout.preferredHeight: 34
                                    text: modelData.label
                                    color: Theme.textPrimary
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideMiddle
                                }
                                Item {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    BullseyeItem {
                                        anchors.fill: parent
                                        controller: modelData.controller
                                        centerX: root.tabSession ? root.tabSession.bullseyeCenterX : 0
                                        centerY: root.tabSession ? root.tabSession.bullseyeCenterY : 0
                                        radiusBins: root.tabSession ? root.tabSession.bullseyeRadiusBins : 12
                                    }
                                    Canvas {
                                        anchors.fill: parent
                                        onPaint: {
                                            var ctx = getContext("2d"); ctx.reset()
                                            var radius = Math.min(width, height) * 0.46
                                            ctx.strokeStyle = Theme.borderStrong
                                            ctx.globalAlpha = 0.5
                                            ctx.lineWidth = 1
                                            for (var ring = 1; ring <= 4; ++ring) {
                                                ctx.beginPath()
                                                ctx.arc(width / 2, height / 2, radius * ring / 4, 0, Math.PI * 2)
                                                ctx.stroke()
                                            }
                                            ctx.globalAlpha = 1
                                        }
                                    }
                                }
                                Label {
                                    Layout.fillWidth: true; Layout.preferredHeight: 26
                                    text: root.tabSession
                                        ? modelData.controller.chrX + ":" + root.tabSession.bullseyeCenterX +
                                          " × " + modelData.controller.chrY + ":" + root.tabSession.bullseyeCenterY
                                        : ""
                                    color: Theme.textMuted
                                    font.pixelSize: Theme.textXs
                                    horizontalAlignment: Text.AlignHCenter
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
