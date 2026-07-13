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
    property real contextFx: 0.5
    property real contextFy: 0.5
    property string hoverText: ""
    property bool hoverActive: false
    property real hoverPlotX: 0
    property real hoverPlotY: 0
    property bool straightEdgeEnabled: false
    property bool diagonalEdgeEnabled: false
    property int pendingAnnotationLayerExport: -1

    function formatBp(value) {
        if (value >= 1000000000)
            return (value / 1000000000).toFixed(value >= 10000000000 ? 1 : 2) + "Gb"
        if (value >= 1000000)
            return (value / 1000000).toFixed(value >= 10000000 ? 1 : 2) + "Mb"
        if (value >= 1000)
            return (value / 1000).toFixed(value >= 10000 ? 1 : 2) + "kb"
        return Math.round(value) + "bp"
    }

    function colorRangeLower() {
        if (!activeController)
            return 0
        var min = Number(activeController.colorMin)
        var max = Number(activeController.colorMax)
        var span = Math.max(1, Math.abs(max - min))
        return min < 0 ? min - span : Math.max(0, min - span)
    }

    function colorRangeUpper() {
        if (!activeController)
            return 100
        var min = Number(activeController.colorMin)
        var max = Number(activeController.colorMax)
        var span = Math.max(1, Math.abs(max - min))
        return max + span
    }

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
        normBox.model = activeController.normalizations()
        chromosomeX.currentIndex = Math.max(0, chromosomeX.find(activeController.chrX))
        chromosomeY.currentIndex = Math.max(0, chromosomeY.find(activeController.chrY))
        resolutionBox.currentIndex = Math.max(0, resolutionBox.find(String(activeController.resolution)))
        matrixBox.model = activeController.matrixTypes()
        matrixBox.currentIndex = Math.max(0, matrixBox.find(activeController.matrixType))
        normBox.currentIndex = Math.max(0, normBox.find(activeController.norm))
        colorMapBox.currentIndex = Math.max(0, colorMapBox.find(activeController.colorMap))
    }

    Component.onCompleted: addTab()

    FileDialog {
        id: openDialog
        title: "Open primary Hi-C file"
        nameFilters: ["Hi-C files (*.hic)", "All files (*)"]
        onAccepted: {
            if (activeController)
                activeController.openFile(selectedFile)
        }
    }

    FileDialog {
        id: controlDialog
        title: "Open control Hi-C file"
        nameFilters: ["Hi-C files (*.hic)", "All files (*)"]
        onAccepted: {
            if (activeController)
                activeController.openControlFile(selectedFile)
        }
    }

    FileDialog {
        id: trackDialog
        title: "Load 1D Track"
        nameFilters: ["Genomics tracks (*.bed *.bed.gz *.bedgraph *.bedGraph *.bedgraph.gz *.wig *.wig.gz *.bw *.bigWig *.bigwig *.bb *.bigBed *.bigbed *.txt *.tsv)", "All files (*)"]
        onAccepted: if (activeController) activeController.loadTrack(selectedFile)
    }

    FileDialog {
        id: annotationDialog
        title: "Load 2D Annotations"
        nameFilters: ["BEDPE annotations (*.bedpe *.txt *.tsv)", "All files (*)"]
        onAccepted: if (activeController) activeController.loadAnnotations(selectedFile)
    }

    FileDialog {
        id: importStateDialog
        title: "Import CARTON state"
        nameFilters: ["CARTON state (*.json)", "All files (*)"]
        onAccepted: if (activeController) activeController.importState(selectedFile)
    }

    FileDialog {
        id: exportStateDialog
        title: "Export CARTON state"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["CARTON state (*.json)", "All files (*)"]
        onAccepted: if (activeController) activeController.exportState(selectedFile)
    }

    FileDialog {
        id: exportPdfDialog
        title: "Export PDF figure"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "pdf"
        nameFilters: ["PDF files (*.pdf)", "All files (*)"]
        onAccepted: if (activeController) activeController.exportFigurePdf(selectedFile, exportWidth.value, exportHeight.value)
    }

    FileDialog {
        id: exportAnnotationDialog
        title: "Export annotation layer"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "bedpe"
        nameFilters: ["BEDPE files (*.bedpe)", "All files (*)"]
        onAccepted: if (activeController && pendingAnnotationLayerExport >= 0) activeController.exportAnnotationLayer(pendingAnnotationLayerExport, selectedFile)
    }

    Dialog {
        id: urlDialog
        title: "Load Track from URL"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 520
        ColumnLayout {
            anchors.fill: parent
            TextField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: "https://..."
                selectByMouse: true
            }
        }
        onAccepted: if (activeController) activeController.loadTrackFromPath(urlField.text)
    }

    Dialog {
        id: saveNameDialog
        property string mode: "location"
        title: mode === "state" ? "Save Current State" : "Save Current Location"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 360
        TextField {
            id: saveNameField
            anchors.fill: parent
            placeholderText: "Name"
            selectByMouse: true
        }
        onAccepted: {
            if (activeController) {
                if (mode === "state")
                    activeController.saveCurrentState(saveNameField.text)
                else
                    activeController.saveCurrentLocation(saveNameField.text)
            }
        }
    }

    Dialog {
        id: exportSizeDialog
        title: "Export PDF Figure"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 360
        GridLayout {
            anchors.fill: parent
            columns: 2
            Label { text: "Width" }
            SpinBox { id: exportWidth; from: 300; to: 10000; value: 1800; editable: true }
            Label { text: "Height" }
            SpinBox { id: exportHeight; from: 300; to: 10000; value: 1800; editable: true }
        }
        onAccepted: exportPdfDialog.open()
    }

    Dialog {
        id: renameGenomeDialog
        title: "Rename Genome"
        modal: true
        standardButtons: Dialog.Ok | Dialog.Cancel
        width: 320
        TextField {
            id: genomeNameField
            anchors.fill: parent
            text: activeController ? activeController.genomeId : ""
            selectByMouse: true
        }
        onAccepted: if (activeController) activeController.renameGenome(genomeNameField.text)
    }

    Dialog {
        id: aboutDialog
        title: "About CARTON"
        modal: true
        standardButtons: Dialog.Ok
        width: 560
        Label {
            anchors.fill: parent
            wrapMode: Text.WordWrap
            text: "CARTON is a Qt/C++ desktop viewer for Hi-C contact maps, inspired by Juicebox core viewer workflows. It supports .hic maps, control maps, normalization, derived display modes, annotations, tracks, saved states, and GPU-backed rendering.\n\nIf using Juicebox-derived workflows in research, cite the original Juicebox publication: Durand, Robinson et al., Cell Systems 2016."
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

    menuBar: MenuBar {
        Menu {
            title: "File"
            MenuItem { text: "Open Primary Hi-C..."; onTriggered: openDialog.open() }
            MenuItem { text: "Open Control Hi-C..."; onTriggered: controlDialog.open() }
            Menu {
                title: "Open Recent"
                Repeater {
                    model: activeController ? activeController.recentMaps() : []
                    MenuItem {
                        text: modelData
                        onTriggered: activeController.openRecentMap(modelData)
                    }
                }
            }
            Menu {
                title: "Open Recent as Control"
                Repeater {
                    model: activeController ? activeController.recentControlMaps() : []
                    MenuItem {
                        text: modelData
                        onTriggered: activeController.openRecentControlMap(modelData)
                    }
                }
            }
            MenuSeparator {}
            MenuItem { text: "Load 1D Track..."; onTriggered: trackDialog.open() }
            MenuItem { text: "Load 1D Track from URL..."; onTriggered: urlDialog.open() }
            MenuItem { text: "Load 2D Annotations..."; onTriggered: annotationDialog.open() }
            MenuSeparator {}
            MenuItem { text: "Import State..."; onTriggered: importStateDialog.open() }
            MenuItem { text: "Export State..."; enabled: !!activeController; onTriggered: exportStateDialog.open() }
            MenuItem { text: "Export PDF Figure..."; enabled: !!activeController; onTriggered: exportSizeDialog.open() }
            MenuSeparator {}
            MenuItem { text: "New Tab"; onTriggered: addTab() }
            MenuItem { text: "Close Tab"; enabled: controllers.length > 1; onTriggered: closeCurrentTab() }
            MenuSeparator {}
            MenuItem { text: "About CARTON"; onTriggered: aboutDialog.open() }
        }
        Menu {
            title: "Navigate"
            MenuItem { text: "Undo Zoom"; enabled: activeController && activeController.canUndoView; onTriggered: activeController.undoView() }
            MenuItem { text: "Redo Zoom"; enabled: activeController && activeController.canRedoView; onTriggered: activeController.redoView() }
            MenuItem { text: "Reset View"; enabled: activeController && activeController.filePath.length > 0; onTriggered: activeController.resetView() }
            MenuItem { text: "Whole Genome All by All"; enabled: activeController && activeController.filePath.length > 0; onTriggered: activeController.setWholeGenomeView() }
            MenuItem { text: "Jump to Diagonal"; enabled: activeController && activeController.chrX === activeController.chrY; onTriggered: activeController.jumpToDiagonal(contextFx, contextFy) }
            MenuItem { text: "Copy Current Position"; enabled: !!activeController; onTriggered: activeController.copyPosition(contextFx, contextFy) }
            MenuItem { text: "Copy Hover Text"; enabled: hoverText.length > 0; onTriggered: activeController.copyText(hoverText) }
            MenuItem { text: "Copy Top Position"; enabled: !!activeController; onTriggered: activeController.copyTopPosition(contextFx) }
            MenuItem { text: "Copy Left Position"; enabled: !!activeController; onTriggered: activeController.copyLeftPosition(contextFy) }
            MenuSeparator {}
            MenuItem {
                text: "Save Current Location..."
                enabled: !!activeController
                onTriggered: {
                    saveNameDialog.mode = "location"
                    saveNameField.text = ""
                    saveNameDialog.open()
                }
            }
            Menu {
                title: "Restore Saved Location"
                Repeater {
                    model: activeController ? activeController.savedLocations() : []
                    MenuItem {
                        text: modelData.name ? modelData.name : modelData.created
                        onTriggered: activeController.restoreSavedLocation(index)
                    }
                }
            }
            MenuItem {
                text: "Save Current State..."
                enabled: !!activeController
                onTriggered: {
                    saveNameDialog.mode = "state"
                    saveNameField.text = ""
                    saveNameDialog.open()
                }
            }
            Menu {
                title: "Restore Saved State"
                Repeater {
                    model: activeController ? activeController.savedStates() : []
                    MenuItem {
                        text: modelData.name ? modelData.name : modelData.created
                        onTriggered: activeController.restoreSavedState(index)
                    }
                }
            }
        }
        Menu {
            title: "Display"
            MenuItem { text: "Darkula Mode"; checkable: true; checked: activeController && activeController.darkMode; onTriggered: if (activeController) activeController.darkMode = checked }
            MenuItem { text: "Gridlines"; checkable: true; checked: activeController && activeController.showGridlines; onTriggered: if (activeController) activeController.showGridlines = checked }
            MenuItem { text: "Axis Endpoints Only"; checkable: true; checked: activeController && activeController.axisEndpointsOnly; onTriggered: if (activeController) activeController.axisEndpointsOnly = checked }
            MenuItem { text: "Chromosome Context"; checkable: true; checked: activeController && activeController.showChromosomeContext; onTriggered: if (activeController) activeController.showChromosomeContext = checked }
            MenuItem { text: "Display Tiles"; checkable: true; checked: activeController && activeController.showTilesDebug; onTriggered: if (activeController) activeController.showTilesDebug = checked }
            MenuSeparator {}
            MenuItem { text: "White-Red"; checkable: true; checked: activeController && activeController.colorMap === "White-Red"; onTriggered: if (activeController) activeController.colorMap = "White-Red" }
            MenuItem { text: "Viridis"; checkable: true; checked: activeController && activeController.colorMap === "Viridis"; onTriggered: if (activeController) activeController.colorMap = "Viridis" }
            MenuItem { text: "Blue-White-Red"; checkable: true; checked: activeController && activeController.colorMap === "Blue-White-Red"; onTriggered: if (activeController) activeController.colorMap = "Blue-White-Red" }
            MenuItem { text: "Grayscale"; checkable: true; checked: activeController && activeController.colorMap === "Grayscale"; onTriggered: if (activeController) activeController.colorMap = "Grayscale" }
            MenuItem { text: "Change Heatmap Low Color..."; onTriggered: lowColorDialog.open() }
            MenuItem { text: "Change Heatmap High Color..."; onTriggered: highColorDialog.open() }
        }
        Menu {
            title: "Layers"
            MenuItem { text: "Add Annotation Layer"; enabled: !!activeController; onTriggered: activeController.addAnnotationLayer("Layer") }
            MenuItem { text: "Merge Visible Layers"; enabled: !!activeController; onTriggered: activeController.mergeVisibleAnnotationLayers("Merged") }
            MenuSeparator {}
            MenuItem { text: "Clear 1D Tracks"; enabled: activeController && activeController.trackCount > 0; onTriggered: activeController.clearTracks() }
            MenuItem { text: "Clear 2D Annotations"; enabled: activeController && activeController.annotationCount > 0; onTriggered: activeController.clearAnnotations() }
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

            Button {
                text: "Open Hi-C"
                highlighted: true
                onClicked: openDialog.open()
            }

            Button {
                text: "Control"
                enabled: !!activeController
                onClicked: controlDialog.open()
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
                Layout.preferredWidth: 150
                model: activeController ? activeController.matrixTypes() : ["observed", "log", "oe", "expected", "vs"]
                onActivated: if (activeController) activeController.matrixType = currentText
            }

            ComboBox {
                id: normBox
                Layout.preferredWidth: 120
                model: ["NONE"]
                onActivated: if (activeController) activeController.norm = currentText
            }

            TextField {
                id: topLocationField
                Layout.preferredWidth: 190
                placeholderText: "chr:start-end"
                enabled: activeController && activeController.filePath.length > 0
                selectByMouse: true
                onAccepted: if (activeController) activeController.goTo(text, leftLocationField.text.length > 0 ? leftLocationField.text : text)
            }

            TextField {
                id: leftLocationField
                Layout.preferredWidth: 190
                placeholderText: "left / optional"
                enabled: activeController && activeController.filePath.length > 0
                selectByMouse: true
                onAccepted: if (activeController) activeController.goTo(topLocationField.text, text.length > 0 ? text : topLocationField.text)
            }

            ToolButton {
                text: "Go"
                enabled: activeController && topLocationField.text.length > 0
                onClicked: activeController.goTo(topLocationField.text, leftLocationField.text.length > 0 ? leftLocationField.text : topLocationField.text)
            }

            ToolButton {
                text: "Reset"
                enabled: activeController && activeController.filePath.length > 0
                onClicked: activeController.resetView()
            }

            ToolButton {
                text: "All"
                enabled: activeController && activeController.filePath.length > 0
                onClicked: activeController.setWholeGenomeView()
            }

            ToolButton {
                text: "Track"
                enabled: !!activeController
                onClicked: trackDialog.open()
            }

            ToolButton {
                text: "2D"
                enabled: !!activeController
                onClicked: annotationDialog.open()
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
                    color: "#eef1f4"
                    border.color: "#c8d0d8"
                    border.width: 1

                    Connections {
                        target: activeController
                        function onViewChanged() {
                            topTrackCanvas.requestPaint()
                            leftTrackCanvas.requestPaint()
                            annotationCanvas.requestPaint()
                            guideCanvas.requestPaint()
                            miniMapCanvas.requestPaint()
                        }
                        function onTracksChanged() {
                            topTrackCanvas.requestPaint()
                            leftTrackCanvas.requestPaint()
                        }
                        function onAnnotationsChanged() {
                            annotationCanvas.requestPaint()
                        }
                        function onRecordsChanged() {
                            annotationCanvas.requestPaint()
                            miniMapCanvas.requestPaint()
                        }
                        function onDisplayOptionsChanged() {
                            topTrackCanvas.requestPaint()
                            leftTrackCanvas.requestPaint()
                            annotationCanvas.requestPaint()
                            guideCanvas.requestPaint()
                            miniMapCanvas.requestPaint()
                        }
                    }

                    Menu {
                        id: heatmapMenu
                        MenuItem {
                            text: "Undo Zoom"
                            enabled: activeController && activeController.canUndoView
                            onTriggered: activeController.undoView()
                        }
                        MenuItem {
                            text: "Redo Zoom"
                            enabled: activeController && activeController.canRedoView
                            onTriggered: activeController.redoView()
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: "Jump to Diagonal"
                            enabled: activeController && activeController.chrX === activeController.chrY
                            onTriggered: activeController.jumpToDiagonal(contextFx, contextFy)
                        }
                        MenuItem {
                            text: "Copy Position"
                            enabled: !!activeController
                            onTriggered: activeController.copyPosition(contextFx, contextFy)
                        }
                        MenuItem {
                            text: "Copy Hover Text"
                            enabled: hoverText.length > 0
                            onTriggered: activeController.copyText(hoverText)
                        }
                        MenuItem {
                            text: "Copy Top Position"
                            enabled: !!activeController
                            onTriggered: activeController.copyTopPosition(contextFx)
                        }
                        MenuItem {
                            text: "Copy Left Position"
                            enabled: !!activeController
                            onTriggered: activeController.copyLeftPosition(contextFy)
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: "Enable straight edge"
                            checkable: true
                            checked: straightEdgeEnabled
                            onTriggered: {
                                straightEdgeEnabled = checked
                                if (checked) diagonalEdgeEnabled = false
                                guideCanvas.requestPaint()
                            }
                        }
                        MenuItem {
                            text: "Enable diagonal edge"
                            checkable: true
                            checked: diagonalEdgeEnabled
                            onTriggered: {
                                diagonalEdgeEnabled = checked
                                if (checked) straightEdgeEnabled = false
                                guideCanvas.requestPaint()
                            }
                        }
                        MenuItem {
                            text: "Highlight Selected Feature"
                            enabled: activeController && activeController.selectedAnnotationId.length > 0
                            onTriggered: activeController.toggleSelectedAnnotationHighlight()
                        }
                        MenuItem {
                            text: "Delete Selected Feature"
                            enabled: activeController && activeController.selectedAnnotationId.length > 0
                            onTriggered: activeController.deleteSelectedAnnotation()
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: "Load 1D Track..."
                            onTriggered: trackDialog.open()
                        }
                        MenuItem {
                            text: "Load 2D Annotations..."
                            onTriggered: annotationDialog.open()
                        }
                        MenuItem {
                            text: "Clear 1D Tracks"
                            enabled: activeController && activeController.trackCount > 0
                            onTriggered: activeController.clearTracks()
                        }
                        MenuItem {
                            text: "Clear 2D Annotations"
                            enabled: activeController && activeController.annotationCount > 0
                            onTriggered: activeController.clearAnnotations()
                        }
                    }

                    Item {
                        id: plotFrame
                        anchors.centerIn: parent
                        width: Math.max(240, Math.min(parent.width - 58, parent.height - 90))
                        height: width
                        property int axisSize: 72

                        Canvas {
                            id: topTrackCanvas
                            x: plotFrame.axisSize
                            y: 0
                            width: plotFrame.width - plotFrame.axisSize
                            height: plotFrame.axisSize
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = "#f7f8fa"
                                ctx.fillRect(0, 0, width, height)
                                if (!activeController) return
                                var segments = activeController.visibleTrackSegments(true)
                                var span = Math.max(1, activeController.x1 - activeController.x0)
                                var axisLabelHeight = 18
                                var axisY = height - axisLabelHeight - 0.5
                                if (activeController.showChromosomeContext) {
                                    ctx.fillStyle = activeController.wholeGenomeView ? "#e5e7eb" : "#edf0f3"
                                    ctx.fillRect(0, 0, width, 8)
                                    ctx.fillStyle = "#6b7280"
                                    ctx.fillRect(0, 0, width, 8)
                                }
                                ctx.strokeStyle = "#8b949e"
                                ctx.beginPath()
                                ctx.moveTo(0, axisY)
                                ctx.lineTo(width, axisY)
                                ctx.stroke()
                                ctx.font = "11px sans-serif"
                                ctx.fillStyle = "#4b5563"
                                ctx.textBaseline = "top"
                                var ticks = activeController.axisEndpointsOnly ? 2 : 5
                                for (var t = 0; t < ticks; t++) {
                                    var f = ticks === 1 ? 0 : t / (ticks - 1)
                                    var tx = f * width
                                    var label = formatBp(activeController.x0 + f * span)
                                    ctx.strokeStyle = "#8b949e"
                                    ctx.beginPath()
                                    ctx.moveTo(tx + 0.5, axisY)
                                    ctx.lineTo(tx + 0.5, axisY + 5)
                                    ctx.stroke()
                                    ctx.textAlign = t === 0 ? "left" : (t === ticks - 1 ? "right" : "center")
                                    ctx.fillText(label, tx, axisY + 6)
                                }
                                for (var i = 0; i < segments.length; i++) {
                                    var s = segments[i]
                                    var x0 = (s.start - activeController.x0) / span * width
                                    var x1 = (s.end - activeController.x0) / span * width
                                    var h = Math.max(2, Math.min(axisY - 6, Math.abs(s.value) * 8))
                                    ctx.fillStyle = s.color
                                    ctx.fillRect(Math.max(0, x0), axisY - h - 2, Math.max(1, x1 - x0), h)
                                }
                            }
                        }

                        Canvas {
                            id: leftTrackCanvas
                            x: 0
                            y: plotFrame.axisSize
                            width: plotFrame.axisSize
                            height: plotFrame.height - plotFrame.axisSize
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = "#f7f8fa"
                                ctx.fillRect(0, 0, width, height)
                                if (!activeController) return
                                var segments = activeController.visibleTrackSegments(false)
                                var span = Math.max(1, activeController.y1 - activeController.y0)
                                var labelWidth = 42
                                var axisX = width - 0.5
                                if (activeController.showChromosomeContext) {
                                    ctx.fillStyle = activeController.wholeGenomeView ? "#e5e7eb" : "#edf0f3"
                                    ctx.fillRect(0, 0, 8, height)
                                    ctx.fillStyle = "#6b7280"
                                    ctx.fillRect(0, 0, 8, height)
                                }
                                ctx.strokeStyle = "#8b949e"
                                ctx.beginPath()
                                ctx.moveTo(axisX, 0)
                                ctx.lineTo(axisX, height)
                                ctx.stroke()
                                ctx.font = "11px sans-serif"
                                ctx.fillStyle = "#4b5563"
                                ctx.textAlign = "left"
                                ctx.textBaseline = "middle"
                                var ticks = activeController.axisEndpointsOnly ? 2 : 5
                                for (var t = 0; t < ticks; t++) {
                                    var f = ticks === 1 ? 0 : t / (ticks - 1)
                                    var ty = f * height
                                    var label = formatBp(activeController.y0 + f * span)
                                    ctx.strokeStyle = "#8b949e"
                                    ctx.beginPath()
                                    ctx.moveTo(axisX - 5, ty + 0.5)
                                    ctx.lineTo(axisX, ty + 0.5)
                                    ctx.stroke()
                                    ctx.fillText(label, 2, Math.max(8, Math.min(height - 8, ty)))
                                }
                                for (var i = 0; i < segments.length; i++) {
                                    var s = segments[i]
                                    var y0 = (s.start - activeController.y0) / span * height
                                    var y1 = (s.end - activeController.y0) / span * height
                                    var w = Math.max(2, Math.min(width - labelWidth - 8, Math.abs(s.value) * 8))
                                    ctx.fillStyle = s.color
                                    ctx.fillRect(axisX - w - 2, Math.max(0, y0), w, Math.max(1, y1 - y0))
                                }
                            }
                        }

                        Item {
                            id: heatmapHost
                            x: plotFrame.axisSize
                            y: plotFrame.axisSize
                            width: plotFrame.width - plotFrame.axisSize
                            height: plotFrame.height - plotFrame.axisSize
                            clip: true

                            HicHeatmapItem {
                                anchors.fill: parent
                                controller: activeController
                            }

                            Canvas {
                                id: annotationCanvas
                                anchors.fill: parent
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.reset()
                                    if (!activeController) return
                                    var annotations = activeController.visibleAnnotations()
                                    var spanX = Math.max(1, activeController.x1 - activeController.x0)
                                    var spanY = Math.max(1, activeController.y1 - activeController.y0)
                                    var side = Math.min(width, height)
                                    var scale = side / Math.max(spanX, spanY)
                                    var ox = (width - side) * 0.5
                                    var oy = (height - side) * 0.5
                                    if (activeController.showGridlines) {
                                        ctx.strokeStyle = "#22000000"
                                        ctx.lineWidth = 1
                                        var gridSteps = 10
                                        for (var g = 1; g < gridSteps; g++) {
                                            var gx = ox + side * g / gridSteps
                                            var gy = oy + side * g / gridSteps
                                            ctx.beginPath()
                                            ctx.moveTo(gx, oy)
                                            ctx.lineTo(gx, oy + side)
                                            ctx.moveTo(ox, gy)
                                            ctx.lineTo(ox + side, gy)
                                            ctx.stroke()
                                        }
                                    }
                                    if (activeController.wholeGenomeView) {
                                        var boundaries = activeController.chromosomeBoundaries()
                                        ctx.strokeStyle = "#77374151"
                                        ctx.lineWidth = 1.2
                                        for (var b = 0; b < boundaries.length; b++) {
                                            var bx = ox + (boundaries[b].end - activeController.x0) / spanX * side
                                            var by = oy + (boundaries[b].end - activeController.y0) / spanY * side
                                            ctx.beginPath()
                                            ctx.moveTo(bx, oy)
                                            ctx.lineTo(bx, oy + side)
                                            ctx.moveTo(ox, by)
                                            ctx.lineTo(ox + side, by)
                                            ctx.stroke()
                                        }
                                    }
                                    if (activeController.showTilesDebug) {
                                        ctx.strokeStyle = "#553b82f6"
                                        ctx.lineWidth = 1
                                        var tileCount = 8
                                        for (var tt = 0; tt <= tileCount; tt++) {
                                            var tx = ox + side * tt / tileCount
                                            var ty = oy + side * tt / tileCount
                                            ctx.beginPath()
                                            ctx.moveTo(tx, oy)
                                            ctx.lineTo(tx, oy + side)
                                            ctx.moveTo(ox, ty)
                                            ctx.lineTo(ox + side, ty)
                                            ctx.stroke()
                                        }
                                    }
                                    ctx.lineWidth = 1.5
                                    for (var i = 0; i < annotations.length; i++) {
                                        var a = annotations[i]
                                        var x = ox + (a.x0 - activeController.x0) * scale
                                        var y = oy + (a.y0 - activeController.y0) * scale
                                        var w = Math.max(a.enlarged ? 6 : 2, (a.x1 - a.x0) * scale)
                                        var h = Math.max(a.enlarged ? 6 : 2, (a.y1 - a.y0) * scale)
                                        ctx.globalAlpha = a.transparent ? 0.45 : 1.0
                                        ctx.strokeStyle = a.color
                                        ctx.strokeRect(x, y, w, h)
                                        if (w > 7 && h > 7)
                                            ctx.strokeRect(x + 1, y + 1, w - 2, h - 2)
                                        ctx.globalAlpha = 1.0
                                    }
                                }
                            }

                            Canvas {
                                id: guideCanvas
                                anchors.fill: parent
                                z: 10
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.reset()
                                    if (!hoverActive) return
                                    ctx.strokeStyle = "#cc111827"
                                    ctx.lineWidth = 1
                                    ctx.setLineDash([5, 4])
                                    if (straightEdgeEnabled) {
                                        ctx.beginPath()
                                        ctx.moveTo(hoverPlotX, 0)
                                        ctx.lineTo(hoverPlotX, height)
                                        ctx.moveTo(0, hoverPlotY)
                                        ctx.lineTo(width, hoverPlotY)
                                        ctx.stroke()
                                    }
                                    if (diagonalEdgeEnabled) {
                                        ctx.beginPath()
                                        var d = hoverPlotY - hoverPlotX
                                        ctx.moveTo(Math.max(0, -d), Math.max(0, d))
                                        ctx.lineTo(Math.min(width, height - d), Math.min(height, width + d))
                                        ctx.stroke()
                                    }
                                }
                            }

                            Rectangle {
                                id: selectionRect
                                visible: false
                                color: "#22578dff"
                                border.color: "#2f6fed"
                                border.width: 1
                            }

                            MouseArea {
                                id: interactionArea
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton | Qt.RightButton
                                property real lastX: 0
                                property real lastY: 0
                                property real startX: 0
                                property real startY: 0
                                property bool selecting: false
                                property bool annotating: false

                                function clamp01(v) {
                                    return Math.max(0, Math.min(1, v))
                                }

                                function fractionX(px) {
                                    var side = Math.min(width, height)
                                    var ox = (width - side) * 0.5
                                    return clamp01((px - ox) / Math.max(1, side))
                                }

                                function fractionY(py) {
                                    var side = Math.min(width, height)
                                    var oy = (height - side) * 0.5
                                    return clamp01((py - oy) / Math.max(1, side))
                                }

                                function updateHover(mouse) {
                                    if (!activeController) {
                                        hoverText = ""
                                        hoverActive = false
                                        return
                                    }
                                    hoverPlotX = mouse.x
                                    hoverPlotY = mouse.y
                                    hoverActive = true
                                    contextFx = fractionX(mouse.x)
                                    contextFy = fractionY(mouse.y)
                                    hoverText = activeController.positionText(contextFx, contextFy)
                                    guideCanvas.requestPaint()
                                }

                                onPressed: function(mouse) {
                                    if (!activeController) return
                                    updateHover(mouse)
                                    if (mouse.button === Qt.RightButton) {
                                        heatmapMenu.popup()
                                        return
                                    }
                                    lastX = mouse.x
                                    lastY = mouse.y
                                    startX = mouse.x
                                    startY = mouse.y
                                    selecting = (mouse.modifiers & Qt.AltModifier) !== 0
                                    annotating = (mouse.modifiers & Qt.ShiftModifier) !== 0
                                    if (selecting || annotating) {
                                        selectionRect.x = mouse.x
                                        selectionRect.y = mouse.y
                                        selectionRect.width = 0
                                        selectionRect.height = 0
                                        selectionRect.visible = true
                                        selectionRect.border.color = annotating ? "#f59e0b" : "#2f6fed"
                                    } else {
                                        activeController.beginInteraction()
                                    }
                                }

                                onPositionChanged: function(mouse) {
                                    updateHover(mouse)
                                    if (!activeController || !(mouse.buttons & Qt.LeftButton))
                                        return
                                    if (selecting || annotating) {
                                        selectionRect.x = Math.min(startX, mouse.x)
                                        selectionRect.y = Math.min(startY, mouse.y)
                                        selectionRect.width = Math.abs(mouse.x - startX)
                                        selectionRect.height = Math.abs(mouse.y - startY)
                                    } else {
                                        activeController.pan(-(mouse.x - lastX) / Math.max(1, width),
                                                             -(mouse.y - lastY) / Math.max(1, height))
                                        lastX = mouse.x
                                        lastY = mouse.y
                                    }
                                }

                                onReleased: function(mouse) {
                                    if (!activeController)
                                        return
                                    if ((selecting || annotating) && mouse.button === Qt.LeftButton) {
                                        selectionRect.visible = false
                                        if (Math.abs(mouse.x - startX) > 8 && Math.abs(mouse.y - startY) > 8) {
                                            if (annotating)
                                                activeController.addAnnotationFromFractions(fractionX(startX), fractionY(startY),
                                                                                           fractionX(mouse.x), fractionY(mouse.y))
                                            else
                                                activeController.zoomToFractions(fractionX(startX), fractionY(startY),
                                                                                fractionX(mouse.x), fractionY(mouse.y))
                                        } else {
                                            activeController.selectAnnotationAt(fractionX(mouse.x), fractionY(mouse.y))
                                        }
                                        selecting = false
                                        annotating = false
                                    } else if (mouse.button === Qt.LeftButton) {
                                        activeController.endInteraction()
                                        activeController.selectAnnotationAt(fractionX(mouse.x), fractionY(mouse.y))
                                    }
                                }

                                onCanceled: {
                                    selectionRect.visible = false
                                    selecting = false
                                    annotating = false
                                    hoverActive = false
                                    if (activeController)
                                        activeController.endInteraction()
                                }

                                onExited: {
                                    hoverText = ""
                                    hoverActive = false
                                    guideCanvas.requestPaint()
                                }
                                cursorShape: (selecting || annotating || straightEdgeEnabled || diagonalEdgeEnabled) ? Qt.CrossCursor : Qt.OpenHandCursor
                                preventStealing: true
                                propagateComposedEvents: false
                                onDoubleClicked: function(mouse) {
                                    if (!activeController) return
                                    activeController.zoom(2.0, fractionX(mouse.x), fractionY(mouse.y))
                                }
                            }

                            Rectangle {
                                id: hoverBadge
                                z: 20
                                visible: hoverActive && hoverText.length > 0
                                width: hoverBadgeLabel.implicitWidth + 16
                                height: hoverBadgeLabel.implicitHeight + 10
                                x: Math.max(8, Math.min(parent.width - width - 8, hoverPlotX + 14))
                                y: Math.max(8, Math.min(parent.height - height - 8, hoverPlotY + 14))
                                radius: 4
                                color: "#ee111827"
                                border.color: "#66374151"
                                border.width: 1

                                Label {
                                    id: hoverBadgeLabel
                                    anchors.centerIn: parent
                                    text: hoverText
                                    color: "#ffffff"
                                    font.pixelSize: 12
                                }
                            }
                        }
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
                            Label {
                                text: hoverText
                                color: "#f5f7fa"
                                elide: Text.ElideRight
                                Layout.preferredWidth: 280
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
                    text: "Navigator"
                    color: "#20252b"
                    font.weight: Font.DemiBold
                }

                Canvas {
                    id: miniMapCanvas
                    Layout.fillWidth: true
                    Layout.preferredHeight: 180
                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.reset()
                        ctx.fillStyle = activeController && activeController.darkMode ? "#111827" : "#ffffff"
                        ctx.fillRect(0, 0, width, height)
                        ctx.strokeStyle = "#9ca3af"
                        ctx.strokeRect(0.5, 0.5, width - 1, height - 1)
                        if (!activeController) return
                        var lx = Math.max(1, activeController.wholeGenomeView ? activeController.x1 : activeController.x1 - activeController.x0)
                        var ly = Math.max(1, activeController.wholeGenomeView ? activeController.y1 : activeController.y1 - activeController.y0)
                        if (activeController.wholeGenomeView) {
                            var bounds = activeController.chromosomeBoundaries()
                            ctx.strokeStyle = "#d1d5db"
                            for (var i = 0; i < bounds.length; i++) {
                                var bx = bounds[i].end / Math.max(1, activeController.x1) * width
                                var by = bounds[i].end / Math.max(1, activeController.y1) * height
                                ctx.beginPath()
                                ctx.moveTo(bx, 0)
                                ctx.lineTo(bx, height)
                                ctx.moveTo(0, by)
                                ctx.lineTo(width, by)
                                ctx.stroke()
                            }
                        }
                        ctx.strokeStyle = "#ef4444"
                        ctx.lineWidth = 2
                        ctx.strokeRect(4, 4, width - 8, height - 8)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    CheckBox {
                        text: "Lock resolution"
                        checked: activeController && activeController.resolutionLocked
                        onToggled: if (activeController) activeController.resolutionLocked = checked
                    }
                    Slider {
                        Layout.fillWidth: true
                        enabled: activeController && !activeController.resolutionLocked
                        from: 0
                        to: activeController ? Math.max(0, activeController.resolutions().length - 1) : 0
                        stepSize: 1
                        snapMode: Slider.SnapAlways
                        value: resolutionBox.currentIndex
                        onMoved: {
                            if (activeController && activeController.resolutions().length > value)
                                activeController.resolution = Number(activeController.resolutions()[Math.round(value)])
                        }
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    CheckBox {
                        text: "Gridlines"
                        checked: activeController && activeController.showGridlines
                        onToggled: if (activeController) activeController.showGridlines = checked
                    }
                    CheckBox {
                        text: "Chromosome context"
                        checked: activeController && activeController.showChromosomeContext
                        onToggled: if (activeController) activeController.showChromosomeContext = checked
                    }
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

                RangeSlider {
                    Layout.fillWidth: true
                    from: colorRangeLower()
                    to: colorRangeUpper()
                    first.value: activeController ? activeController.colorMin : 0
                    second.value: activeController ? activeController.colorMax : 50
                    first.onMoved: if (activeController) activeController.colorMin = first.value
                    second.onMoved: if (activeController) activeController.colorMax = second.value
                }

                RowLayout {
                    Layout.fillWidth: true
                    Button {
                        text: "-"
                        enabled: !!activeController
                        onClicked: if (activeController) {
                            var center = (activeController.colorMin + activeController.colorMax) * 0.5
                            var half = Math.max(0.000001, (activeController.colorMax - activeController.colorMin) * 0.25)
                            activeController.colorMin = center - half
                            activeController.colorMax = center + half
                        }
                    }
                    Label {
                        text: activeController && activeController.colorMaxAuto ? "Auto" : "Min / Max"
                        color: "#5b6672"
                    }
                    TextField {
                        id: colorMinField
                        Layout.fillWidth: true
                        text: activeController ? activeController.colorMin.toString() : ""
                        selectByMouse: true
                        validator: DoubleValidator {
                            notation: DoubleValidator.ScientificNotation
                        }
                        onAccepted: {
                            if (activeController && isFinite(Number(text)))
                                activeController.colorMin = Number(text)
                        }
                        onEditingFinished: {
                            if (activeController && isFinite(Number(text)))
                                activeController.colorMin = Number(text)
                        }
                    }
                    TextField {
                        id: colorMaxField
                        Layout.fillWidth: true
                        text: activeController ? activeController.colorMax.toString() : ""
                        selectByMouse: true
                        validator: DoubleValidator {
                            notation: DoubleValidator.ScientificNotation
                        }
                        onAccepted: {
                            if (activeController && isFinite(Number(text)))
                                activeController.colorMax = Number(text)
                        }
                        onEditingFinished: {
                            if (activeController && isFinite(Number(text)))
                                activeController.colorMax = Number(text)
                        }
                    }
                    Button {
                        text: "Auto"
                        enabled: activeController && !activeController.colorMaxAuto
                        onClicked: activeController.resetColorScale()
                    }
                    Button {
                        text: "+"
                        enabled: !!activeController
                        onClicked: if (activeController) {
                            var center = (activeController.colorMin + activeController.colorMax) * 0.5
                            var half = Math.max(0.000001, activeController.colorMax - activeController.colorMin)
                            activeController.colorMin = center - half
                            activeController.colorMax = center + half
                        }
                    }
                }

                Label {
                    text: "Hover"
                    color: "#20252b"
                    font.weight: Font.DemiBold
                }

                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 96
                    TextArea {
                        text: hoverText
                        readOnly: true
                        wrapMode: Text.WordWrap
                    }
                }

                TabBar {
                    id: sideTabs
                    Layout.fillWidth: true
                    TabButton { text: "Layers" }
                    TabButton { text: "Tracks" }
                }

                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: sideTabs.currentIndex

                    ScrollView {
                        clip: true
                        ColumnLayout {
                            width: parent.width
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                Button { text: "New"; onClicked: if (activeController) activeController.addAnnotationLayer("Layer") }
                                Button { text: "Merge"; onClicked: if (activeController) activeController.mergeVisibleAnnotationLayers("Merged") }
                                SpinBox {
                                    from: 1
                                    to: 1000000
                                    value: activeController ? activeController.sparseFeatureLimit : 10000
                                    editable: true
                                    onValueModified: if (activeController) activeController.sparseFeatureLimit = value
                                }
                            }
                            Repeater {
                                model: activeController ? activeController.annotationLayerSummaries() : []
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 92
                                    radius: 6
                                    color: modelData.active ? "#e0f2fe" : "#ffffff"
                                    border.color: "#cbd5e1"
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        spacing: 4
                                        RowLayout {
                                            Layout.fillWidth: true
                                            CheckBox { checked: modelData.visible; onToggled: activeController.setAnnotationLayerVisible(modelData.index, checked) }
                                            Label { text: modelData.name + " (" + modelData.count + ")"; Layout.fillWidth: true; elide: Text.ElideRight }
                                            Button { text: "Active"; onClicked: activeController.setActiveAnnotationLayer(modelData.index) }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            CheckBox { text: "Trans"; checked: modelData.transparent; onToggled: activeController.setAnnotationLayerTransparent(modelData.index, checked) }
                                            CheckBox { text: "Sparse"; checked: modelData.sparse; onToggled: activeController.setAnnotationLayerSparse(modelData.index, checked) }
                                            CheckBox { text: "Large"; checked: modelData.enlarged; onToggled: activeController.setAnnotationLayerEnlarged(modelData.index, checked) }
                                        }
                                        RowLayout {
                                            Button { text: "Dup"; onClicked: activeController.duplicateAnnotationLayer(modelData.index) }
                                            Button { text: "Clear"; onClicked: activeController.clearAnnotationLayer(modelData.index) }
                                            Button {
                                                text: "Export"
                                                onClicked: {
                                                    pendingAnnotationLayerExport = modelData.index
                                                    exportAnnotationDialog.open()
                                                }
                                            }
                                            Button { text: "Del"; onClicked: activeController.removeAnnotationLayer(modelData.index) }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    ScrollView {
                        clip: true
                        ColumnLayout {
                            width: parent.width
                            spacing: 8
                            RowLayout {
                                Layout.fillWidth: true
                                Button { text: "Load"; onClicked: trackDialog.open() }
                                Button { text: "URL"; onClicked: urlDialog.open() }
                                Button { text: "Clear"; enabled: activeController && activeController.trackCount > 0; onClicked: activeController.clearTracks() }
                            }
                            Repeater {
                                model: activeController ? activeController.trackSummaries() : []
                                Rectangle {
                                    Layout.fillWidth: true
                                    height: 112
                                    radius: 6
                                    color: "#ffffff"
                                    border.color: "#cbd5e1"
                                    ColumnLayout {
                                        anchors.fill: parent
                                        anchors.margins: 6
                                        TextField {
                                            Layout.fillWidth: true
                                            text: modelData.name
                                            selectByMouse: true
                                            onAccepted: activeController.setTrackName(modelData.index, text)
                                            onEditingFinished: activeController.setTrackName(modelData.index, text)
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            CheckBox {
                                                text: "Log"
                                                checked: modelData.logScale
                                                onToggled: activeController.setTrackRange(modelData.index, modelData.min, modelData.max, checked)
                                            }
                                            ComboBox {
                                                model: ["mean", "max"]
                                                currentIndex: modelData.reduction === "max" ? 1 : 0
                                                onActivated: activeController.setTrackReduction(modelData.index, currentText)
                                            }
                                            Button { text: "Up"; enabled: modelData.index > 0; onClicked: activeController.moveTrack(modelData.index, modelData.index - 1) }
                                            Button { text: "Down"; onClicked: activeController.moveTrack(modelData.index, modelData.index + 1) }
                                            Button { text: "Del"; onClicked: activeController.removeTrack(modelData.index) }
                                        }
                                        RowLayout {
                                            Layout.fillWidth: true
                                            Label { text: "Min" }
                                            TextField { text: String(modelData.min); Layout.fillWidth: true; onAccepted: activeController.setTrackRange(modelData.index, Number(text), modelData.max, modelData.logScale) }
                                            Label { text: "Max" }
                                            TextField { text: String(modelData.max); Layout.fillWidth: true; onAccepted: activeController.setTrackRange(modelData.index, modelData.min, Number(text), modelData.logScale) }
                                        }
                                    }
                                }
                            }
                        }
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
