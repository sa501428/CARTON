import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Rectangle {
    id: root
    property var tabSession: null
    signal hoverInfo(string text, bool active)
    signal contextMenuRequested(var controller, real xFraction, real yFraction)
    color: Theme.appBg

    property var operatorNames: ["Gaussian smoothing", "Gradient X", "Gradient Y", "Gradient magnitude", "Gradient orientation", "Laplacian", "Hessian determinant", "Hessian ridge", "Structure anisotropy", "Structure orientation", "Difference of Gaussians", "Laplacian of Gaussian", "Distance transform", "Erosion", "Dilation", "Opening", "Closing", "Steerable filter", "Gabor filter", "Local Binary Pattern", "Polar transform", "Matrix square", "Graph diffusion"]
    property var operatorKeys: ["gaussian", "gradient-x", "gradient-y", "gradient-magnitude", "gradient-orientation", "laplacian", "hessian-determinant", "hessian-ridge", "structure-anisotropy", "structure-orientation", "dog", "log", "distance-transform", "erosion", "dilation", "opening", "closing", "steerable", "gabor", "lbp", "polar", "matrix-square", "graph-diffusion"]

    function parameterLabel() {
        var op = tabSession ? tabSession.processingOperator : "";
        if (["gaussian", "dog", "log"].indexOf(op) >= 0)
            return "Sigma (bins)";
        if (["erosion", "dilation", "opening", "closing"].indexOf(op) >= 0)
            return "Radius (bins)";
        if (op === "graph-diffusion")
            return "Iterations";
        return "Parameter";
    }
    function thresholdLabel() {
        var op = tabSession ? tabSession.processingOperator : "";
        return ["steerable", "gabor"].indexOf(op) >= 0 ? "Angle (degrees)" : (op === "graph-diffusion" ? "Diffusion weight" : "Threshold");
    }
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: analysisTools.implicitHeight + 16
            color: Theme.surfaceAlt
            border.color: Theme.borderSubtle
            Flow {
                id: analysisTools
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8
                Row {
                    spacing: 6
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Operator"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.textXs
                    }
                    AppComboBox {
                        model: root.operatorNames
                        currentIndex: {
                            if (!root.tabSession)
                                return 3;
                            return Math.max(0, root.operatorKeys.indexOf(root.tabSession.processingOperator));
                        }
                        onActivated: if (root.tabSession)
                            root.tabSession.processingOperator = root.operatorKeys[currentIndex]
                        width: 205
                    }
                }
                Row {
                    visible: root.tabSession && ["gaussian", "dog", "log", "erosion", "dilation", "opening", "closing", "gabor", "graph-diffusion"].indexOf(root.tabSession.processingOperator) >= 0
                    spacing: 6
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.tabSession && root.tabSession.processingOperator === "gabor" ? "Sigma (≤ 12 bins)" : root.parameterLabel()
                        color: Theme.textSecondary
                        font.pixelSize: Theme.textXs
                    }
                    AppTextField {
                        text: root.tabSession ? String(root.tabSession.processingParameter) : "1"
                        validator: DoubleValidator {
                            bottom: 0.1
                            top: root.tabSession && root.tabSession.processingOperator === "gabor" ? 12 : 100
                        }
                        onEditingFinished: {
                            if (!root.tabSession)
                                return;
                            var value = Number(text);
                            if (root.tabSession.processingOperator === "gabor")
                                value = Math.min(12, value);
                            root.tabSession.processingParameter = value;
                        }
                        width: 76
                    }
                }
                Row {
                    visible: root.tabSession && ["distance-transform", "gabor", "steerable", "graph-diffusion"].indexOf(root.tabSession.processingOperator) >= 0
                    spacing: 6
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: root.thresholdLabel()
                        color: Theme.textSecondary
                        font.pixelSize: Theme.textXs
                    }
                    AppTextField {
                        text: root.tabSession ? String(root.tabSession.processingThreshold) : "0"
                        validator: DoubleValidator {}
                        onEditingFinished: if (root.tabSession)
                            root.tabSession.processingThreshold = Number(text)
                        width: 76
                    }
                }
                Row {
                    spacing: 6
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        text: "Maximum"
                        color: Theme.textSecondary
                        font.pixelSize: Theme.textXs
                    }
                    AppComboBox {
                        model: ["512 × 512", "1024 × 1024"]
                        currentIndex: root.tabSession && root.tabSession.processingMaximumBins > 512 ? 1 : 0
                        onActivated: if (root.tabSession)
                            root.tabSession.processingMaximumBins = currentIndex === 1 ? 1024 : 512
                        width: 124
                    }
                }

                Label {
                    width: Math.min(implicitWidth, analysisTools.width)
                    wrapMode: Text.WordWrap
                    text: "One operator per result"
                    color: Theme.textMuted
                    font.pixelSize: Theme.textXs
                }
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
                    model: root.tabSession ? root.tabSession.cellLayout : []
                    Rectangle {
                        required property var modelData
                        width: parent.width - 20
                        height: Math.max(360, Math.min(620, width / 2))
                        color: Theme.surface
                        border.color: (root.tabSession && root.tabSession.activeCellIndex === modelData.index) ? Theme.accent : Theme.border
                        radius: Theme.radiusSm
                        ColumnLayout {
                            anchors.fill: parent
                            Label {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 32
                                text: modelData.label + " · synchronized input and result"
                                color: Theme.textPrimary
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                spacing: 8
                                MapViewport {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    controller: modelData.controller
                                    viewLabel: "Input"
                                    selected: false
                                    compact: true
                                    showHeader: true
                                    showTrackPanels: false
                                    onViewportInteracted: root.tabSession.notifyViewportInteracted(modelData.index)
                                    onHoverInfo: function (text, active) {
                                        root.hoverInfo(text, active);
                                    }
                                    onContextMenuRequested: function (controller, xFraction, yFraction) {
                                        root.contextMenuRequested(controller, xFraction, yFraction);
                                    }
                                }
                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    color: "white"
                                    border.color: Theme.borderStrong
                                    ColumnLayout {
                                        anchors.fill: parent
                                        spacing: 0
                                        Label {
                                            Layout.fillWidth: true
                                            Layout.preferredHeight: 24
                                            text: root.tabSession ? root.tabSession.processingOperator : "Result"
                                            color: Theme.textPrimary
                                            horizontalAlignment: Text.AlignHCenter
                                            verticalAlignment: Text.AlignVCenter
                                        }
                                        ProcessedHeatmapItem {
                                            id: processedResult
                                            Layout.fillWidth: true
                                            Layout.fillHeight: true
                                            controller: modelData.controller
                                            operation: root.tabSession ? root.tabSession.processingOperator : "gradient-magnitude"
                                            parameter: root.tabSession ? root.tabSession.processingParameter : 1
                                            threshold: root.tabSession ? root.tabSession.processingThreshold : 0
                                            maximumBins: root.tabSession ? root.tabSession.processingMaximumBins : 512
                                        }
                                        Label {
                                            Layout.fillWidth: true
                                            visible: processedResult.errorString.length > 0
                                            text: processedResult.errorString
                                            color: Theme.danger
                                            wrapMode: Text.Wrap
                                            horizontalAlignment: Text.AlignHCenter
                                            font.pixelSize: Theme.textXs
                                            padding: 8
                                        }
                                    }
                                }
                            }
                        }
                        TapHandler {
                            onTapped: root.tabSession.activeCellIndex = modelData.index
                        }
                    }
                }
            }
        }
    }
}
