import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import Carton

ApplicationWindow {
    id: window
    width: 1440
    height: 940
    visible: true
    title: activeController && activeController.filePath.length > 0 ? "CARTON - " + activeController.filePath : "CARTON"
    color: "#f5f6f8"

    property var controllers: []
    property var activeController: null
    property int tabSerial: 0

    ListModel { id: tabModel }

    function createController() {
        var controller = Qt.createQmlObject("import Carton; HicDataController {}", window)
        controller.metadataChanged.connect(function() {
            if (controller === activeController)
                syncControlModels()
        })
        controller.filePathChanged.connect(function() {
            var index = controllers.indexOf(controller)
            if (index >= 0) {
                var path = controller.filePath
                var slash = Math.max(path.lastIndexOf("/"), path.lastIndexOf("\\"))
                tabModel.setProperty(index, "title", slash >= 0 ? path.substring(slash + 1) : path)
            }
        })
        return controller
    }

    function addTab() {
        var controller = createController()
        controllers.push(controller)
        tabSerial += 1
        tabModel.append({ "title": "Map " + tabSerial })
        tabBar.currentIndex = controllers.length - 1
        setActiveTab(tabBar.currentIndex)
    }

    function closeCurrentTab() {
        if (controllers.length <= 1)
            return
        var index = tabBar.currentIndex
        var controller = controllers[index]
        controllers.splice(index, 1)
        tabModel.remove(index)
        controller.destroy()
        tabBar.currentIndex = Math.min(index, controllers.length - 1)
        setActiveTab(tabBar.currentIndex)
    }

    function setActiveTab(index) {
        if (index < 0 || index >= controllers.length)
            return
        activeController = controllers[index]
        syncControlModels()
    }

    function syncControlModels() {
        if (!activeController)
            return
        chromosomeX.model = activeController.chromosomeNames()
        chromosomeY.model = activeController.chromosomeNames()
        resolutionBox.model = activeController.resolutions()
        chromosomeX.currentIndex = Math.max(0, chromosomeX.find(activeController.chrX))
        chromosomeY.currentIndex = Math.max(0, chromosomeY.find(activeController.chrY))
        resolutionBox.currentIndex = Math.max(0, resolutionBox.find(String(activeController.resolution)))
        matrixBox.currentIndex = Math.max(0, matrixBox.find(activeController.matrixType))
        normBox.currentIndex = Math.max(0, normBox.find(activeController.norm))
        colorMapBox.currentIndex = Math.max(0, colorMapBox.find(activeController.colorMap))
    }

    Component.onCompleted: addTab()

    FileDialog {
        id: openDialog
        title: "Open Hi-C file"
        nameFilters: ["Hi-C files (*.hic)", "All files (*)"]
        onAccepted: {
            if (activeController)
                activeController.openFile(selectedFile)
        }
    }

    ColorDialog {
        id: lowColorDialog
        title: "Low Value Color"
        selectedColor: activeController ? activeController.customLowColor : "white"
        onAccepted: {
            if (activeController) {
                activeController.customLowColor = selectedColor
                activeController.colorMap = "Custom"
                syncControlModels()
            }
        }
    }

    ColorDialog {
        id: highColorDialog
        title: "High Value Color"
        selectedColor: activeController ? activeController.customHighColor : "#d7191c"
        onAccepted: {
            if (activeController) {
                activeController.customHighColor = selectedColor
                activeController.colorMap = "Custom"
                syncControlModels()
            }
        }
    }

    header: ToolBar {
        height: 48
        background: Rectangle { color: "#20252b" }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            ToolButton {
                text: "Open"
                onClicked: openDialog.open()
            }

            ComboBox {
                id: chromosomeX
                Layout.preferredWidth: 130
                model: []
                enabled: activeController && model.length > 0
                onActivated: if (activeController) activeController.chrX = currentText
            }

            ComboBox {
                id: chromosomeY
                Layout.preferredWidth: 130
                model: []
                enabled: activeController && model.length > 0
                onActivated: if (activeController) activeController.chrY = currentText
            }

            ComboBox {
                id: resolutionBox
                Layout.preferredWidth: 130
                model: []
                enabled: activeController && model.length > 0
                onActivated: if (activeController) activeController.resolution = Number(currentText)
            }

            ComboBox {
                id: matrixBox
                Layout.preferredWidth: 120
                model: ["observed", "oe", "expected"]
                onActivated: if (activeController) activeController.matrixType = currentText
            }

            ComboBox {
                id: normBox
                Layout.preferredWidth: 120
                model: ["NONE", "VC", "VC_SQRT", "KR"]
                onActivated: if (activeController) activeController.norm = currentText
            }

            ToolButton {
                text: "Reset"
                enabled: activeController && activeController.filePath.length > 0
                onClicked: activeController.resetView()
            }

            Item { Layout.fillWidth: true }

            BusyIndicator {
                running: activeController && activeController.busy
                visible: running
                Layout.preferredWidth: 28
                Layout.preferredHeight: 28
            }
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Rectangle {
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            color: "#eef1f4"

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    TabBar {
                        id: tabBar
                        Layout.fillWidth: true
                        background: Rectangle { color: "#dfe4ea" }
                        onCurrentIndexChanged: setActiveTab(currentIndex)

                        Repeater {
                            model: tabModel
                            TabButton {
                                text: model.title
                                width: Math.max(140, implicitWidth)
                            }
                        }
                    }

                    ToolButton {
                        text: "+"
                        Layout.preferredWidth: 42
                        onClicked: addTab()
                    }

                    ToolButton {
                        text: "x"
                        enabled: controllers.length > 1
                        Layout.preferredWidth: 42
                        onClicked: closeCurrentTab()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: "#ffffff"
                    border.color: "#c8d0d8"
                    border.width: 1

                    HicHeatmapItem {
                        anchors.fill: parent
                        anchors.margins: 1
                        controller: activeController
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 32
                        color: "#dd20252b"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 12
                            Label {
                                text: activeController ? activeController.chrX + ":" + activeController.x0 + "-" + activeController.x1 : ""
                                color: "#f5f7fa"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: activeController ? activeController.chrY + ":" + activeController.y0 + "-" + activeController.y1 : ""
                                color: "#f5f7fa"
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: activeController ? activeController.recordCount + " records" : ""
                                color: "#f5f7fa"
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 300
            color: "#f7f8fa"
            border.color: "#c8d0d8"

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 14

                Label {
                    text: "Inspector"
                    color: "#20252b"
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }

                GridLayout {
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 10
                    Layout.fillWidth: true

                    Label { text: "Genome"; color: "#5b6672" }
                    Label { text: activeController && activeController.genomeId ? activeController.genomeId : "-"; color: "#20252b"; elide: Text.ElideRight; Layout.fillWidth: true }

                    Label { text: "Resolution"; color: "#5b6672" }
                    Label { text: activeController && activeController.resolution > 0 ? activeController.resolution + " bp" : "-"; color: "#20252b" }

                    Label { text: "Matrix"; color: "#5b6672" }
                    Label { text: activeController ? activeController.matrixType : "-"; color: "#20252b" }

                    Label { text: "Norm"; color: "#5b6672" }
                    Label { text: activeController ? activeController.norm : "-"; color: "#20252b" }
                }

                Label {
                    text: "Color Scale"
                    color: "#20252b"
                    font.weight: Font.DemiBold
                }

                ComboBox {
                    id: colorMapBox
                    Layout.fillWidth: true
                    model: ["White-Red", "Viridis", "Blue-White-Red", "Grayscale", "Custom"]
                    onActivated: if (activeController) activeController.colorMap = currentText
                }

                RowLayout {
                    Layout.fillWidth: true
                    enabled: activeController && activeController.colorMap === "Custom"
                    Button {
                        text: "Low"
                        Layout.fillWidth: true
                        onClicked: lowColorDialog.open()
                    }
                    Rectangle {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        radius: 4
                        border.color: "#8b949e"
                        color: activeController ? activeController.customLowColor : "white"
                    }
                    Button {
                        text: "High"
                        Layout.fillWidth: true
                        onClicked: highColorDialog.open()
                    }
                    Rectangle {
                        Layout.preferredWidth: 28
                        Layout.preferredHeight: 28
                        radius: 4
                        border.color: "#8b949e"
                        color: activeController ? activeController.customHighColor : "#d7191c"
                    }
                }

                Slider {
                    Layout.fillWidth: true
                    from: 1
                    to: 500
                    value: activeController ? activeController.colorMax : 50
                    onMoved: if (activeController) activeController.colorMax = value
                }

                Label {
                    text: activeController ? Math.round(activeController.colorMax).toString() : "-"
                    color: "#5b6672"
                }

                Label {
                    text: "Annotations"
                    color: "#20252b"
                    font.weight: Font.DemiBold
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: ["Selection", "Loops", "TADs", "Genes", "Tracks"]
                    delegate: CheckDelegate {
                        width: ListView.view.width
                        text: modelData
                        checked: modelData === "Selection"
                    }
                }

                Label {
                    text: activeController ? activeController.status : ""
                    color: "#374151"
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
    }
}
