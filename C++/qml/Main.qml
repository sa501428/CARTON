import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import QtQuick.Effects
import Carton

ApplicationWindow {
    id: window
    width: 1440
    height: 940
    visible: true
    title: activeController && activeController.filePath.length > 0 ? "CARTON - " + activeController.filePath : "CARTON"
    color: Theme.appBg

    palette {
        window: Theme.surface
        windowText: Theme.textPrimary
        base: Theme.surface
        text: Theme.textPrimary
        button: Theme.surface
        buttonText: Theme.textPrimary
        highlight: Theme.accent
        highlightedText: Theme.accentForeground
        mid: Theme.border
        dark: Theme.borderStrong
        placeholderText: Theme.textMuted
    }

    Binding {
        target: Theme
        property: "dark"
        value: activeController ? activeController.darkMode : false
    }

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
        background: DialogFrame {}
        ColumnLayout {
            anchors.fill: parent
            AppTextField {
                id: urlField
                Layout.fillWidth: true
                placeholderText: "https://..."
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
        background: DialogFrame {}
        AppTextField {
            id: saveNameField
            anchors.fill: parent
            placeholderText: "Name"
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
        background: DialogFrame {}
        GridLayout {
            anchors.fill: parent
            columns: 2
            rowSpacing: 10
            columnSpacing: 12
            Label { text: "Width"; color: Theme.textSecondary }
            SpinBox { id: exportWidth; from: 300; to: 10000; value: 1800; editable: true }
            Label { text: "Height"; color: Theme.textSecondary }
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
        background: DialogFrame {}
        AppTextField {
            id: genomeNameField
            anchors.fill: parent
            text: activeController ? activeController.genomeId : ""
        }
        onAccepted: if (activeController) activeController.renameGenome(genomeNameField.text)
    }

    Dialog {
        id: aboutDialog
        title: "About CARTON"
        modal: true
        standardButtons: Dialog.Ok
        width: 560
        background: DialogFrame {}
        Label {
            anchors.fill: parent
            wrapMode: Text.WordWrap
            color: Theme.textPrimary
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
        height: 56
        background: Rectangle {
            color: Theme.chromeBg
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.chromeBorder
            }
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 10

            RowLayout {
                spacing: 8
                Rectangle {
                    width: 9
                    height: 9
                    radius: 3
                    color: Theme.accent
                }
                Label {
                    text: "CARTON"
                    color: Theme.chromeText
                    font.pixelSize: 15
                    font.weight: Font.Bold
                    font.letterSpacing: 0.4
                }
            }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Theme.chromeBorder }

            AppButton {
                text: "Open Hi-C"
                highlighted: true
                onClicked: openDialog.open()
            }

            AppButton {
                text: "Control"
                tonal: true
                enabled: !!activeController
                onClicked: controlDialog.open()
            }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Theme.chromeBorder }

            AppComboBox {
                id: chromosomeX
                Layout.preferredWidth: 118
                model: []
                enabled: activeController && model.length > 0
                onActivated: if (activeController) activeController.chrX = currentText
            }

            Label { text: "×"; color: Theme.chromeTextMuted; font.pixelSize: Theme.textBase }

            AppComboBox {
                id: chromosomeY
                Layout.preferredWidth: 118
                model: []
                enabled: activeController && model.length > 0
                onActivated: if (activeController) activeController.chrY = currentText
            }

            AppComboBox {
                id: resolutionBox
                Layout.preferredWidth: 118
                model: []
                enabled: activeController && model.length > 0
                onActivated: if (activeController) activeController.resolution = Number(currentText)
            }

            AppComboBox {
                id: matrixBox
                Layout.preferredWidth: 136
                model: activeController ? activeController.matrixTypes() : ["observed", "log", "oe", "expected", "vs"]
                onActivated: if (activeController) activeController.matrixType = currentText
            }

            AppComboBox {
                id: normBox
                Layout.preferredWidth: 110
                model: ["NONE"]
                onActivated: if (activeController) activeController.norm = currentText
            }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Theme.chromeBorder }

            AppTextField {
                id: topLocationField
                Layout.preferredWidth: 180
                placeholderText: "chr:start-end"
                enabled: activeController && activeController.filePath.length > 0
                onAccepted: if (activeController) activeController.goTo(text, leftLocationField.text.length > 0 ? leftLocationField.text : text)
            }

            AppTextField {
                id: leftLocationField
                Layout.preferredWidth: 180
                placeholderText: "left / optional"
                enabled: activeController && activeController.filePath.length > 0
                onAccepted: if (activeController) activeController.goTo(topLocationField.text, text.length > 0 ? text : topLocationField.text)
            }

            AppButton {
                text: "Go"
                tonal: true
                enabled: activeController && topLocationField.text.length > 0
                onClicked: activeController.goTo(topLocationField.text, leftLocationField.text.length > 0 ? leftLocationField.text : topLocationField.text)
            }

            AppToolButton {
                text: "Reset"
                enabled: activeController && activeController.filePath.length > 0
                onClicked: activeController.resetView()
            }

            AppToolButton {
                text: "All"
                enabled: activeController && activeController.filePath.length > 0
                onClicked: activeController.setWholeGenomeView()
            }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Theme.chromeBorder }

            AppToolButton {
                text: "Track"
                enabled: !!activeController
                onClicked: trackDialog.open()
            }

            AppToolButton {
                text: "2D"
                enabled: !!activeController
                onClicked: annotationDialog.open()
            }

            Item { Layout.fillWidth: true }

            RowLayout {
                spacing: 8
                visible: activeController && activeController.busy
                BusyIndicator {
                    running: parent.visible
                    Layout.preferredWidth: 20
                    Layout.preferredHeight: 20
                    palette.dark: Theme.accent
                }
                Label {
                    text: "Loading…"
                    color: Theme.chromeTextMuted
                    font.pixelSize: Theme.textSm
                }
            }
        }
    }

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        Rectangle {
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            color: Theme.appBg

            ColumnLayout {
                anchors.fill: parent
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 0

                    TabBar {
                        id: tabBar
                        Layout.fillWidth: true
                        background: Rectangle {
                            color: Theme.surface
                            Rectangle {
                                anchors.left: parent.left
                                anchors.right: parent.right
                                anchors.bottom: parent.bottom
                                height: 1
                                color: Theme.border
                            }
                        }
                        onCurrentIndexChanged: setActiveTab(currentIndex)

                        Repeater {
                            model: tabModel
                            AppTabButton {
                                text: model.title
                                width: Math.max(140, implicitWidth)
                            }
                        }
                    }

                    AppToolButton {
                        text: "+"
                        onLightSurface: true
                        contentColor: Theme.textSecondary
                        Layout.preferredWidth: 40
                        onClicked: addTab()
                    }

                    AppToolButton {
                        text: "×"
                        onLightSurface: true
                        contentColor: Theme.textSecondary
                        enabled: controllers.length > 1
                        Layout.preferredWidth: 40
                        onClicked: closeCurrentTab()
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.appBg

                    Connections {
                        target: activeController
                        function onViewChanged() {
                            resolutionBox.currentIndex = Math.max(0, resolutionBox.find(String(activeController.resolution)))
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
                        property int axisSize: activeController ? Math.min(150, 72 + Math.max(0, activeController.trackCount - 1) * 20) : 72

                        Rectangle {
                            id: shadowSource
                            anchors.fill: parent
                            radius: Theme.radiusMd
                            color: "black"
                            visible: false
                        }

                        MultiEffect {
                            anchors.fill: shadowSource
                            source: shadowSource
                            shadowEnabled: true
                            shadowColor: Theme.shadow
                            shadowVerticalOffset: 3
                            shadowBlur: 0.6
                            shadowOpacity: 0.55
                            blurMax: 28
                        }

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.radiusMd
                            color: Theme.surfaceSunken
                            border.color: Theme.border
                            border.width: 1
                        }

                        Canvas {
                            id: topTrackCanvas
                            x: plotFrame.axisSize
                            y: 0
                            width: plotFrame.width - plotFrame.axisSize
                            height: plotFrame.axisSize
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = Theme.surfaceSunken
                                ctx.fillRect(0, 0, width, height)
                                if (!activeController) return
                                var segments = activeController.visibleTrackSegments(true)
                                var span = Math.max(1, activeController.x1 - activeController.x0)
                                var axisLabelHeight = 18
                                var axisY = height - axisLabelHeight - 0.5
                                if (activeController.showChromosomeContext) {
                                    ctx.fillStyle = activeController.wholeGenomeView ? Theme.borderStrong : Theme.surfaceHover
                                    ctx.fillRect(0, 0, width, 8)
                                    ctx.fillStyle = Theme.textMuted
                                    ctx.fillRect(0, 0, width, 8)
                                }
                                ctx.strokeStyle = Theme.borderStrong
                                ctx.beginPath()
                                ctx.moveTo(0, axisY)
                                ctx.lineTo(width, axisY)
                                ctx.stroke()
                                ctx.font = "11px sans-serif"
                                ctx.fillStyle = Theme.textSecondary
                                ctx.textBaseline = "top"
                                var ticks = activeController.axisEndpointsOnly ? 2 : 5
                                for (var t = 0; t < ticks; t++) {
                                    var f = ticks === 1 ? 0 : t / (ticks - 1)
                                    var tx = f * width
                                    var label = formatBp(activeController.x0 + f * span)
                                    ctx.strokeStyle = Theme.borderStrong
                                    ctx.beginPath()
                                    ctx.moveTo(tx + 0.5, axisY)
                                    ctx.lineTo(tx + 0.5, axisY + 5)
                                    ctx.stroke()
                                    ctx.textAlign = t === 0 ? "left" : (t === ticks - 1 ? "right" : "center")
                                    ctx.fillText(label, tx, axisY + 6)
                                }
                                var laneCount = Math.max(1, activeController.trackCount)
                                var trackTop = activeController.showChromosomeContext ? 10 : 2
                                var trackBottom = axisY - 3
                                var laneHeight = Math.max(8, (trackBottom - trackTop) / laneCount)
                                for (var i = 0; i < segments.length; i++) {
                                    var s = segments[i]
                                    var x0 = (s.start - activeController.x0) / span * width
                                    var x1 = (s.end - activeController.x0) / span * width
                                    var laneY = trackTop + s.trackIndex * laneHeight
                                    var range = Math.max(0.000001, s.max - s.min)
                                    var zero = laneY + laneHeight - (0 - s.min) / range * laneHeight
                                    var valueY = laneY + laneHeight - (s.value - s.min) / range * laneHeight
                                    zero = Math.max(laneY, Math.min(laneY + laneHeight, zero))
                                    valueY = Math.max(laneY, Math.min(laneY + laneHeight, valueY))
                                    var barTop = Math.min(zero, valueY)
                                    var h = Math.max(1.5, Math.abs(valueY - zero))
                                    ctx.fillStyle = s.color
                                    ctx.fillRect(Math.max(0, x0), barTop, Math.max(1, x1 - x0), h)
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
                                ctx.fillStyle = Theme.surfaceSunken
                                ctx.fillRect(0, 0, width, height)
                                if (!activeController) return
                                var segments = activeController.visibleTrackSegments(false)
                                var span = Math.max(1, activeController.y1 - activeController.y0)
                                var labelWidth = 42
                                var axisX = width - 0.5
                                if (activeController.showChromosomeContext) {
                                    ctx.fillStyle = activeController.wholeGenomeView ? Theme.borderStrong : Theme.surfaceHover
                                    ctx.fillRect(0, 0, 8, height)
                                    ctx.fillStyle = Theme.textMuted
                                    ctx.fillRect(0, 0, 8, height)
                                }
                                ctx.strokeStyle = Theme.borderStrong
                                ctx.beginPath()
                                ctx.moveTo(axisX, 0)
                                ctx.lineTo(axisX, height)
                                ctx.stroke()
                                ctx.font = "11px sans-serif"
                                ctx.fillStyle = Theme.textSecondary
                                ctx.textAlign = "left"
                                ctx.textBaseline = "middle"
                                var ticks = activeController.axisEndpointsOnly ? 2 : 5
                                for (var t = 0; t < ticks; t++) {
                                    var f = ticks === 1 ? 0 : t / (ticks - 1)
                                    var ty = f * height
                                    var label = formatBp(activeController.y0 + f * span)
                                    ctx.strokeStyle = Theme.borderStrong
                                    ctx.beginPath()
                                    ctx.moveTo(axisX - 5, ty + 0.5)
                                    ctx.lineTo(axisX, ty + 0.5)
                                    ctx.stroke()
                                    ctx.fillText(label, 2, Math.max(8, Math.min(height - 8, ty)))
                                }
                                var laneCount = Math.max(1, activeController.trackCount)
                                var trackLeft = activeController.showChromosomeContext ? 10 : 2
                                var trackRight = axisX - labelWidth - 2
                                var laneWidth = Math.max(7, (trackRight - trackLeft) / laneCount)
                                for (var i = 0; i < segments.length; i++) {
                                    var s = segments[i]
                                    var y0 = (s.start - activeController.y0) / span * height
                                    var y1 = (s.end - activeController.y0) / span * height
                                    var laneX = trackLeft + s.trackIndex * laneWidth
                                    var range = Math.max(0.000001, s.max - s.min)
                                    var zero = laneX + (0 - s.min) / range * laneWidth
                                    var valueX = laneX + (s.value - s.min) / range * laneWidth
                                    zero = Math.max(laneX, Math.min(laneX + laneWidth, zero))
                                    valueX = Math.max(laneX, Math.min(laneX + laneWidth, valueX))
                                    var barLeft = Math.min(zero, valueX)
                                    var w = Math.max(1.5, Math.abs(valueX - zero))
                                    ctx.fillStyle = s.color
                                    ctx.fillRect(barLeft, Math.max(0, y0), w, Math.max(1, y1 - y0))
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
                                    var scaleX = width / spanX
                                    var scaleY = height / spanY
                                    var ox = 0
                                    var oy = 0
                                    var side = Math.min(width, height)
                                    if (activeController.showGridlines) {
                                        ctx.strokeStyle = Theme.gridline
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
                                        ctx.strokeStyle = Theme.boundaryLine
                                        ctx.lineWidth = 1.2
                                        for (var b = 0; b < boundaries.length; b++) {
                                            var bx = (boundaries[b].end - activeController.x0) * scaleX
                                            var by = (boundaries[b].end - activeController.y0) * scaleY
                                            ctx.beginPath()
                                            ctx.moveTo(bx, oy)
                                            ctx.lineTo(bx, oy + side)
                                            ctx.moveTo(ox, by)
                                            ctx.lineTo(ox + side, by)
                                            ctx.stroke()
                                        }
                                    }
                                    if (activeController.showTilesDebug) {
                                        ctx.strokeStyle = Theme.tileDebugLine
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
                                        var x = (a.x0 - activeController.x0) * scaleX
                                        var y = (a.y0 - activeController.y0) * scaleY
                                        var w = Math.max(a.enlarged ? 6 : 2, (a.x1 - a.x0) * scaleX)
                                        var h = Math.max(a.enlarged ? 6 : 2, (a.y1 - a.y0) * scaleY)
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
                                    ctx.strokeStyle = Theme.guideLine
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
                                        // Parallel diagonal through the cursor and its reflection
                                        // across the matrix diagonal.
                                        ctx.moveTo(-height, -height + d)
                                        ctx.lineTo(width + height, width + height + d)
                                        ctx.moveTo(-height, -height - d)
                                        ctx.lineTo(width + height, width + height - d)
                                        // Perpendicular diagonal through the cursor (the reflected
                                        // cursor lies on this same line because x + y is invariant).
                                        var sum = hoverPlotX + hoverPlotY
                                        ctx.moveTo(-height, sum + height)
                                        ctx.lineTo(width + height, sum - width - height)
                                        ctx.stroke()
                                    }
                                }
                            }

                            Rectangle {
                                id: selectionRect
                                visible: false
                                radius: 2
                                color: Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                                border.color: Theme.accent
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
                                property bool moved: false

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
                                    moved = false
                                    selecting = (mouse.modifiers & Qt.AltModifier) !== 0
                                    annotating = (mouse.modifiers & Qt.ShiftModifier) !== 0
                                    if (selecting || annotating) {
                                        selectionRect.x = mouse.x
                                        selectionRect.y = mouse.y
                                        selectionRect.width = 0
                                        selectionRect.height = 0
                                        selectionRect.visible = true
                                        selectionRect.border.color = annotating ? Theme.warn : Theme.accent
                                        selectionRect.color = annotating
                                            ? Qt.rgba(Theme.warn.r, Theme.warn.g, Theme.warn.b, 0.16)
                                            : Qt.rgba(Theme.accent.r, Theme.accent.g, Theme.accent.b, 0.16)
                                    } else {
                                        activeController.beginInteraction()
                                    }
                                }

                                onPositionChanged: function(mouse) {
                                    updateHover(mouse)
                                    if (!activeController || !(mouse.buttons & Qt.LeftButton))
                                        return
                                    if (Math.abs(mouse.x - startX) > 4 || Math.abs(mouse.y - startY) > 4)
                                        moved = true
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
                                        if (!moved) {
                                            activeController.selectAnnotationAt(fractionX(mouse.x), fractionY(mouse.y))
                                            activeController.zoom(2.0, fractionX(mouse.x), fractionY(mouse.y))
                                        }
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
                                onWheel: function(wheel) {
                                    if (!activeController) return
                                    var delta = wheel.angleDelta.y !== 0 ? wheel.angleDelta.y : wheel.pixelDelta.y
                                    if (delta === 0) return
                                    var factor = Math.pow(2.0, delta / 360.0)
                                    activeController.zoom(factor, fractionX(wheel.x), fractionY(wheel.y))
                                    wheel.accepted = true
                                }
                            }

                            Rectangle {
                                id: hoverBadge
                                z: 20
                                visible: hoverActive && hoverText.length > 0
                                width: Math.min(parent.width - 16, hoverBadgeLabel.implicitWidth + 16)
                                height: hoverBadgeLabel.implicitHeight + 10
                                x: Math.max(8, Math.min(parent.width - width - 8, hoverPlotX + 14))
                                y: Math.max(8, Math.min(parent.height - height - 8, hoverPlotY + 14))
                                radius: Theme.radiusSm
                                color: Theme.tooltipBg
                                border.color: Theme.chromeBorder
                                border.width: 1

                                Label {
                                    id: hoverBadgeLabel
                                    anchors.centerIn: parent
                                    width: parent.width - 16
                                    text: hoverText
                                    color: "#ffffff"
                                    font.pixelSize: 12
                                    wrapMode: Text.Wrap
                                }
                            }
                        }
                    }

                    Rectangle {
                        anchors.left: parent.left
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        height: 32
                        color: Theme.footerBg
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 16
                            anchors.rightMargin: 16
                            spacing: 14
                            Label {
                                text: activeController ? activeController.chrX + ":" + activeController.x0 + "-" + activeController.x1 : ""
                                color: Theme.chromeText
                                font.pixelSize: Theme.textSm
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: activeController ? activeController.chrY + ":" + activeController.y0 + "-" + activeController.y1 : ""
                                color: Theme.chromeText
                                font.pixelSize: Theme.textSm
                                elide: Text.ElideRight
                                Layout.fillWidth: true
                            }
                            Label {
                                text: activeController ? activeController.recordCount + " records" : ""
                                color: Theme.chromeTextMuted
                                font.pixelSize: Theme.textSm
                            }
                            Label {
                                text: hoverText
                                color: Theme.chromeText
                                font.pixelSize: Theme.textSm
                                elide: Text.ElideRight
                                Layout.preferredWidth: 280
                            }
                        }
                    }
                }
            }
        }

        Rectangle {
            SplitView.preferredWidth: 360
            SplitView.minimumWidth: 320
            color: Theme.surfaceAlt
            border.color: Theme.border

            ScrollView {
                anchors.fill: parent
                clip: true
                contentWidth: availableWidth

                ColumnLayout {
                    width: parent.width
                    spacing: 16

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.margins: 16
                        Layout.topMargin: 16
                        Layout.bottomMargin: 0
                        spacing: 2
                        Label {
                            text: "Inspector"
                            color: Theme.textPrimary
                            font.pixelSize: Theme.textXl
                            font.weight: Font.Bold
                        }
                        Label {
                            text: activeController && activeController.filePath.length > 0 ? activeController.filePath : "No file loaded"
                            color: Theme.textMuted
                            font.pixelSize: Theme.textSm
                            elide: Text.ElideMiddle
                            Layout.fillWidth: true
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        implicitHeight: overviewGrid.implicitHeight + 24

                        GridLayout {
                            id: overviewGrid
                            anchors.fill: parent
                            anchors.margins: 12
                            columns: 2
                            columnSpacing: 12
                            rowSpacing: 10

                            Label { text: "Genome"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                            Label { text: activeController && activeController.genomeId ? activeController.genomeId : "—"; color: Theme.textPrimary; elide: Text.ElideRight; Layout.fillWidth: true }

                            Label { text: "Resolution"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                            Label { text: activeController && activeController.resolution > 0 ? activeController.resolution + " bp" : "—"; color: Theme.textPrimary }

                            Label { text: "Matrix"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                            Label { text: activeController ? activeController.matrixType : "—"; color: Theme.textPrimary }

                            Label { text: "Norm"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                            Label { text: activeController ? activeController.norm : "—"; color: Theme.textPrimary }
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        implicitHeight: navigatorColumn.implicitHeight + 24

                        ColumnLayout {
                            id: navigatorColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10

                            Label {
                                text: "Navigator"
                                color: Theme.textPrimary
                                font.pixelSize: Theme.textMd
                                font.weight: Font.DemiBold
                            }

                            Canvas {
                                id: miniMapCanvas
                                Layout.fillWidth: true
                                Layout.preferredHeight: 160
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.fillStyle = Theme.surfaceSunken
                                    ctx.fillRect(0, 0, width, height)
                                    ctx.strokeStyle = Theme.borderStrong
                                    ctx.strokeRect(0.5, 0.5, width - 1, height - 1)
                                    if (!activeController) return
                                    var lx = Math.max(1, activeController.wholeGenomeView ? activeController.x1 : activeController.x1 - activeController.x0)
                                    var ly = Math.max(1, activeController.wholeGenomeView ? activeController.y1 : activeController.y1 - activeController.y0)
                                    if (activeController.wholeGenomeView) {
                                        var bounds = activeController.chromosomeBoundaries()
                                        ctx.strokeStyle = Theme.border
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
                                    ctx.strokeStyle = Theme.danger
                                    ctx.lineWidth = 2
                                    ctx.strokeRect(4, 4, width - 8, height - 8)
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 10
                                AppCheckBox {
                                    text: "Lock resolution"
                                    checked: activeController && activeController.resolutionLocked
                                    onToggled: if (activeController) activeController.resolutionLocked = checked
                                }
                                Slider {
                                    id: resolutionSlider
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
                                    background: Rectangle {
                                        x: resolutionSlider.leftPadding
                                        y: resolutionSlider.topPadding + resolutionSlider.availableHeight / 2 - height / 2
                                        width: resolutionSlider.availableWidth
                                        height: 4
                                        radius: 2
                                        color: Theme.border
                                        Rectangle {
                                            width: resolutionSlider.visualPosition * parent.width
                                            height: parent.height
                                            radius: 2
                                            color: Theme.accent
                                        }
                                    }
                                    handle: Rectangle {
                                        x: resolutionSlider.leftPadding + resolutionSlider.visualPosition * (resolutionSlider.availableWidth - width)
                                        y: resolutionSlider.topPadding + resolutionSlider.availableHeight / 2 - height / 2
                                        width: 16
                                        height: 16
                                        radius: 8
                                        color: Theme.surface
                                        border.width: 2
                                        border.color: Theme.accent
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 16
                                AppCheckBox {
                                    text: "Gridlines"
                                    checked: activeController && activeController.showGridlines
                                    onToggled: if (activeController) activeController.showGridlines = checked
                                }
                                AppCheckBox {
                                    text: "Chromosome context"
                                    checked: activeController && activeController.showChromosomeContext
                                    onToggled: if (activeController) activeController.showChromosomeContext = checked
                                }
                            }
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        implicitHeight: colorScaleColumn.implicitHeight + 24

                        ColumnLayout {
                            id: colorScaleColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 10

                            Label {
                                text: "Color Scale"
                                color: Theme.textPrimary
                                font.pixelSize: Theme.textMd
                                font.weight: Font.DemiBold
                            }

                            AppComboBox {
                                id: colorMapBox
                                Layout.fillWidth: true
                                model: ["White-Red", "Viridis", "Blue-White-Red", "Grayscale", "Custom"]
                                onActivated: if (activeController) activeController.colorMap = currentText
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                enabled: activeController && activeController.colorMap === "Custom"
                                spacing: 8
                                AppButton {
                                    text: "Low"
                                    tonal: true
                                    Layout.fillWidth: true
                                    onClicked: lowColorDialog.open()
                                }
                                Rectangle {
                                    Layout.preferredWidth: 26
                                    Layout.preferredHeight: 26
                                    radius: Theme.radiusSm
                                    border.color: Theme.border
                                    color: activeController ? activeController.customLowColor : "white"
                                }
                                AppButton {
                                    text: "High"
                                    tonal: true
                                    Layout.fillWidth: true
                                    onClicked: highColorDialog.open()
                                }
                                Rectangle {
                                    Layout.preferredWidth: 26
                                    Layout.preferredHeight: 26
                                    radius: Theme.radiusSm
                                    border.color: Theme.border
                                    color: activeController ? activeController.customHighColor : "#d7191c"
                                }
                            }

                            RangeSlider {
                                id: colorRangeSlider
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                from: colorRangeLower()
                                to: colorRangeUpper()
                                first.value: activeController ? activeController.colorMin : 0
                                second.value: activeController ? activeController.colorMax : 50
                                first.onMoved: if (activeController) activeController.colorMin = first.value
                                second.onMoved: if (activeController) activeController.colorMax = second.value

                                background: Rectangle {
                                    x: colorRangeSlider.leftPadding
                                    y: colorRangeSlider.topPadding + colorRangeSlider.availableHeight / 2 - height / 2
                                    width: colorRangeSlider.availableWidth
                                    height: 4
                                    radius: 2
                                    color: Theme.border
                                    Rectangle {
                                        x: colorRangeSlider.first.visualPosition * parent.width
                                        width: (colorRangeSlider.second.visualPosition - colorRangeSlider.first.visualPosition) * parent.width
                                        height: parent.height
                                        radius: 2
                                        color: Theme.accent
                                    }
                                }
                                first.handle: Rectangle {
                                    x: colorRangeSlider.leftPadding + colorRangeSlider.first.visualPosition * (colorRangeSlider.availableWidth - width)
                                    y: colorRangeSlider.topPadding + colorRangeSlider.availableHeight / 2 - height / 2
                                    width: 16
                                    height: 16
                                    radius: 8
                                    color: Theme.surface
                                    border.width: 2
                                    border.color: Theme.accent
                                }
                                second.handle: Rectangle {
                                    x: colorRangeSlider.leftPadding + colorRangeSlider.second.visualPosition * (colorRangeSlider.availableWidth - width)
                                    y: colorRangeSlider.topPadding + colorRangeSlider.availableHeight / 2 - height / 2
                                    width: 16
                                    height: 16
                                    radius: 8
                                    color: Theme.surface
                                    border.width: 2
                                    border.color: Theme.accent
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                AppToolButton {
                                    text: "−"
                                    onLightSurface: true
                                    contentColor: Theme.textPrimary
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
                                    color: Theme.textSecondary
                                    font.pixelSize: Theme.textSm
                                }
                                AppTextField {
                                    id: colorMinField
                                    Layout.fillWidth: true
                                    text: activeController ? activeController.colorMin.toString() : ""
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
                                AppTextField {
                                    id: colorMaxField
                                    Layout.fillWidth: true
                                    text: activeController ? activeController.colorMax.toString() : ""
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
                                AppButton {
                                    text: "Auto"
                                    tonal: true
                                    enabled: activeController && !activeController.colorMaxAuto
                                    onClicked: activeController.resetColorScale()
                                }
                                AppToolButton {
                                    text: "+"
                                    onLightSurface: true
                                    contentColor: Theme.textPrimary
                                    enabled: !!activeController
                                    onClicked: if (activeController) {
                                        var center = (activeController.colorMin + activeController.colorMax) * 0.5
                                        var half = Math.max(0.000001, activeController.colorMax - activeController.colorMin)
                                        activeController.colorMin = center - half
                                        activeController.colorMax = center + half
                                    }
                                }
                            }
                        }
                    }

                    Card {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        implicitHeight: hoverColumn.implicitHeight + 24

                        ColumnLayout {
                            id: hoverColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8

                            Label {
                                text: "Hover"
                                color: Theme.textPrimary
                                font.pixelSize: Theme.textMd
                                font.weight: Font.DemiBold
                            }

                            Rectangle {
                                Layout.fillWidth: true
                                Layout.preferredHeight: 84
                                radius: Theme.radiusSm
                                color: Theme.surfaceSunken
                                border.color: Theme.borderSubtle

                                ScrollView {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    clip: true
                                    TextArea {
                                        text: hoverText
                                        readOnly: true
                                        wrapMode: Text.WordWrap
                                        color: Theme.textPrimary
                                        font.pixelSize: Theme.textSm
                                        background: null
                                    }
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        Layout.bottomMargin: 16
                        Layout.preferredHeight: 420
                        spacing: 0

                        TabBar {
                            id: sideTabs
                            Layout.fillWidth: true
                            background: Rectangle { color: "transparent" }
                            AppTabButton { text: "Layers" }
                            AppTabButton { text: "Tracks" }
                        }

                        Card {
                            Layout.fillWidth: true
                            Layout.fillHeight: true

                            StackLayout {
                                anchors.fill: parent
                                anchors.margins: 12
                                currentIndex: sideTabs.currentIndex

                                ScrollView {
                                    clip: true
                                    ColumnLayout {
                                        width: parent.width
                                        spacing: 8
                                        RowLayout {
                                            Layout.fillWidth: true
                                            spacing: 8
                                            AppButton { text: "New"; tonal: true; onClicked: if (activeController) activeController.addAnnotationLayer("Layer") }
                                            AppButton { text: "Merge"; tonal: true; onClicked: if (activeController) activeController.mergeVisibleAnnotationLayers("Merged") }
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
                                                height: 100
                                                radius: Theme.radiusMd
                                                color: modelData.active ? Theme.accentSoft : Theme.surface
                                                border.color: modelData.active ? Theme.accent : Theme.border
                                                ColumnLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: 8
                                                    spacing: 6
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        AppCheckBox { checked: modelData.visible; onToggled: activeController.setAnnotationLayerVisible(modelData.index, checked) }
                                                        Label { text: modelData.name + "  ·  " + modelData.count; color: Theme.textPrimary; Layout.fillWidth: true; elide: Text.ElideRight }
                                                        AppButton { text: "Active"; tonal: true; onClicked: activeController.setActiveAnnotationLayer(modelData.index) }
                                                    }
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        AppCheckBox { text: "Trans"; checked: modelData.transparent; onToggled: activeController.setAnnotationLayerTransparent(modelData.index, checked) }
                                                        AppCheckBox { text: "Sparse"; checked: modelData.sparse; onToggled: activeController.setAnnotationLayerSparse(modelData.index, checked) }
                                                        AppCheckBox { text: "Large"; checked: modelData.enlarged; onToggled: activeController.setAnnotationLayerEnlarged(modelData.index, checked) }
                                                    }
                                                    RowLayout {
                                                        spacing: 6
                                                        AppButton { text: "Dup"; tonal: true; onClicked: activeController.duplicateAnnotationLayer(modelData.index) }
                                                        AppButton { text: "Clear"; tonal: true; onClicked: activeController.clearAnnotationLayer(modelData.index) }
                                                        AppButton {
                                                            text: "Export"
                                                            tonal: true
                                                            onClicked: {
                                                                pendingAnnotationLayerExport = modelData.index
                                                                exportAnnotationDialog.open()
                                                            }
                                                        }
                                                        AppButton { text: "Del"; tonal: true; onClicked: activeController.removeAnnotationLayer(modelData.index) }
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
                                            spacing: 8
                                            AppButton { text: "Load"; tonal: true; onClicked: trackDialog.open() }
                                            AppButton { text: "URL"; tonal: true; onClicked: urlDialog.open() }
                                            AppButton { text: "Clear"; tonal: true; enabled: activeController && activeController.trackCount > 0; onClicked: activeController.clearTracks() }
                                        }
                                        Repeater {
                                            model: activeController ? activeController.trackSummaries() : []
                                            Rectangle {
                                                Layout.fillWidth: true
                                                height: 122
                                                radius: Theme.radiusMd
                                                color: Theme.surface
                                                border.color: Theme.border
                                                ColumnLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: 8
                                                    spacing: 6
                                                    AppTextField {
                                                        Layout.fillWidth: true
                                                        text: modelData.name
                                                        onAccepted: activeController.setTrackName(modelData.index, text)
                                                        onEditingFinished: activeController.setTrackName(modelData.index, text)
                                                    }
                                                    Label {
                                                        text: modelData.featureCount + " intervals"
                                                        color: Theme.textMuted
                                                        font.pixelSize: Theme.textXs
                                                    }
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        AppCheckBox {
                                                            text: "Log"
                                                            checked: modelData.logScale
                                                            onToggled: activeController.setTrackRange(modelData.index, modelData.min, modelData.max, checked)
                                                        }
                                                        AppComboBox {
                                                            Layout.preferredWidth: 90
                                                            model: ["mean", "max"]
                                                            currentIndex: modelData.reduction === "max" ? 1 : 0
                                                            onActivated: activeController.setTrackReduction(modelData.index, currentText)
                                                        }
                                                        AppButton { text: "Up"; tonal: true; enabled: modelData.index > 0; onClicked: activeController.moveTrack(modelData.index, modelData.index - 1) }
                                                        AppButton { text: "Down"; tonal: true; onClicked: activeController.moveTrack(modelData.index, modelData.index + 1) }
                                                        AppButton { text: "Del"; tonal: true; onClicked: activeController.removeTrack(modelData.index) }
                                                    }
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 6
                                                        Label { text: "Min"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                                                        AppTextField { text: String(modelData.min); Layout.fillWidth: true; onAccepted: activeController.setTrackRange(modelData.index, Number(text), modelData.max, modelData.logScale) }
                                                        Label { text: "Max"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                                                        AppTextField { text: String(modelData.max); Layout.fillWidth: true; onAccepted: activeController.setTrackRange(modelData.index, modelData.min, Number(text), modelData.logScale) }
                                                    }
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    Label {
                        Layout.fillWidth: true
                        Layout.leftMargin: 16
                        Layout.rightMargin: 16
                        Layout.bottomMargin: 16
                        text: activeController ? activeController.status : ""
                        color: Theme.textSecondary
                        font.pixelSize: Theme.textSm
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
