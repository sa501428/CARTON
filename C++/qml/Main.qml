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
        value: activeController ? activeController.darkMode : true
    }
    Binding { target: Theme; property: "uiScale"; value: interfaceScale }
    Binding { target: Theme; property: "fontScale"; value: fontScale }
    Binding { target: Theme; property: "reducedMotion"; value: reducedMotion }

    property var controllers: []
    property var activeController: null
    property var comparisonController: null
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
    property bool navigationOpen: false
    property bool inspectorOpen: false
    property bool trackPanelsOpen: false
    property bool comparisonOpen: false
    property bool linkNavigation: true
    property bool linkCrosshair: true
    property bool linkColorScale: true
    property real interfaceScale: 1.0
    property real fontScale: 1.0
    property bool reducedMotion: false
    property string toastText: ""
    property string toastKind: "info"

    function showToast(message, kind) {
        toastText = message
        toastKind = kind || "info"
        toastTimer.restart()
    }

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

    function legendLowColor() {
        if (!activeController) return "#ffffff"
        if (activeController.colorMap === "Viridis") return "#440154"
        if (activeController.colorMap === "Grayscale") return "#ffffff"
        if (activeController.colorMap === "Blue-White-Red") return "#2166ac"
        if (activeController.colorMap === "Custom") return activeController.customLowColor
        return "#ffffff"
    }

    function legendHighColor() {
        if (!activeController) return "#d7191c"
        if (activeController.colorMap === "Viridis") return "#fde725"
        if (activeController.colorMap === "Grayscale") return "#111827"
        if (activeController.colorMap === "Blue-White-Red") return "#b2182b"
        if (activeController.colorMap === "Custom") return activeController.customHighColor
        return "#d7191c"
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
    Timer { id: toastTimer; interval: 3200; onTriggered: toastText = "" }

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
        if (comparisonOpen && comparisonController === activeController)
            comparisonController = controllers.length > 1 ? controllers[(index + 1) % controllers.length] : null
        syncControlModels()
    }

    function ensureComparisonController() {
        if (comparisonController && comparisonController !== activeController)
            return
        for (var i = 0; i < controllers.length; i++) {
            if (controllers[i] !== activeController) {
                comparisonController = controllers[i]
                return
            }
        }
        var source = activeController
        var controller = createController()
        controllers.push(controller)
        tabModel.append({ "title": "Comparison" })
        comparisonController = controller
        controller.metadataChanged.connect(function() {
            if (source) controller.syncViewFrom(source, linkColorScale)
        })
        if (source && source.filePath.length > 0)
            controller.openRecentMap(source.filePath)
    }

    onComparisonOpenChanged: if (comparisonOpen) ensureComparisonController()

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
        id: cytobandDialog
        title: "Load Cytobands"
        nameFilters: ["UCSC cytobands (*.txt *.tsv *.bed)", "All files (*)"]
        onAccepted: if (activeController) activeController.loadCytobands(selectedFile)
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
        id: exportPngDialog
        title: "Export raster image"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "png"
        nameFilters: ["PNG image (*.png)"]
        onAccepted: if (activeController) {
            if (activeController.exportFigurePng(selectedFile, exportWidth.value, exportHeight.value))
                showToast("PNG image exported", "success")
            else
                showToast("PNG export failed", "error")
        }
    }

    FileDialog {
        id: exportMatrixDialog
        title: "Export visible matrix"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "tsv"
        nameFilters: ["Tab-separated values (*.tsv)", "Comma-separated values (*.csv)"]
        onAccepted: if (activeController) {
            if (activeController.exportVisibleMatrix(selectedFile))
                showToast("Visible matrix exported", "success")
            else
                showToast("Matrix export failed", "error")
        }
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

    ColorDialog {
        id: missingColorDialog
        title: "Missing Value Color"
        selectedColor: activeController ? activeController.missingValueColor : Theme.missingData
        onAccepted: if (activeController) activeController.missingValueColor = selectedColor
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
            MenuItem { text: "Load Cytobands..."; onTriggered: cytobandDialog.open() }
            MenuSeparator {}
            MenuItem { text: "Import State..."; onTriggered: importStateDialog.open() }
            MenuItem { text: "Export State..."; enabled: !!activeController; onTriggered: exportStateDialog.open() }
            MenuItem { text: "Export PDF Figure..."; enabled: !!activeController; onTriggered: exportSizeDialog.open() }
            MenuItem { text: "Export PNG Image..."; enabled: !!activeController; onTriggered: exportPngDialog.open() }
            MenuItem { text: "Export Visible Matrix..."; enabled: activeController && activeController.recordCount > 0; onTriggered: exportMatrixDialog.open() }
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
            MenuItem { text: "Navigation Panel"; checkable: true; checked: navigationOpen; onTriggered: navigationOpen = checked }
            MenuItem { text: "Inspector"; checkable: true; checked: inspectorOpen; onTriggered: inspectorOpen = checked }
            MenuItem { text: "Comparison View"; checkable: true; checked: comparisonOpen; onTriggered: comparisonOpen = checked }
            MenuItem { text: "Fullscreen Visualization"; onTriggered: window.visibility = window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen }
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

    Shortcut {
        sequence: "F11"
        onActivated: window.visibility = window.visibility === Window.FullScreen ? Window.Windowed : Window.FullScreen
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
                    Layout.preferredWidth: 9
                    Layout.preferredHeight: 9
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

            AppToolButton {
                iconSource: Qt.resolvedUrl("icons/navigation.svg")
                text: "Nav"
                idleColor: navigationOpen ? Qt.rgba(1, 1, 1, 0.1) : "transparent"
                Accessible.name: navigationOpen ? "Hide navigation panel" : "Show navigation panel"
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: navigationOpen = !navigationOpen
            }

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
                Accessible.name: "Horizontal chromosome"
                Layout.preferredWidth: 118
                model: []
                enabled: activeController && model.length > 0
                onActivated: if (activeController) activeController.chrX = currentText
            }

            Label { text: "×"; color: Theme.chromeTextMuted; font.pixelSize: Theme.textBase }

            AppComboBox {
                id: chromosomeY
                Accessible.name: "Vertical chromosome"
                Layout.preferredWidth: 118
                model: []
                enabled: activeController && model.length > 0
                onActivated: if (activeController) activeController.chrY = currentText
            }

            AppComboBox {
                id: resolutionBox
                Accessible.name: "Matrix resolution"
                Layout.preferredWidth: 118
                model: []
                enabled: activeController && model.length > 0
                onActivated: if (activeController) activeController.resolution = Number(currentText)
            }

            AppComboBox {
                id: matrixBox
                Accessible.name: "Matrix display mode"
                Layout.preferredWidth: 136
                model: activeController ? activeController.matrixTypes() : ["observed", "log", "oe", "expected", "vs"]
                onActivated: if (activeController) activeController.matrixType = currentText
            }

            AppComboBox {
                id: normBox
                Accessible.name: "Matrix normalization"
                Layout.preferredWidth: 110
                model: ["NONE"]
                onActivated: if (activeController) activeController.norm = currentText
            }

            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 24; color: Theme.chromeBorder }

            AppTextField {
                id: topLocationField
                Accessible.name: "Horizontal genomic locus"
                Layout.preferredWidth: 180
                placeholderText: "chr:start-end"
                enabled: activeController && activeController.filePath.length > 0
                onAccepted: if (activeController) activeController.goTo(text, leftLocationField.text.length > 0 ? leftLocationField.text : text)
            }

            AppTextField {
                id: leftLocationField
                Accessible.name: "Vertical genomic locus"
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
                text: "Tracks"
                enabled: !!activeController
                idleColor: trackPanelsOpen ? Qt.rgba(1, 1, 1, 0.1) : "transparent"
                Accessible.name: trackPanelsOpen ? "Collapse genomic track panels" : "Expand genomic track panels"
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: trackPanelsOpen = !trackPanelsOpen
            }

            AppToolButton {
                text: "2D"
                enabled: !!activeController
                onClicked: annotationDialog.open()
            }

            Item { Layout.fillWidth: true }

            AppToolButton {
                iconSource: Qt.resolvedUrl("icons/compare.svg")
                text: "Compare"
                idleColor: comparisonOpen ? Qt.rgba(1, 1, 1, 0.1) : "transparent"
                Accessible.name: "Toggle comparison workspace"
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: comparisonOpen = !comparisonOpen
            }

            AppToolButton {
                iconSource: Qt.resolvedUrl("icons/inspector.svg")
                text: "Inspect"
                idleColor: inspectorOpen ? Qt.rgba(1, 1, 1, 0.1) : "transparent"
                Accessible.name: inspectorOpen ? "Hide inspector" : "Show inspector"
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: inspectorOpen = !inspectorOpen
            }

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

        NavigationPanel {
            id: navigationPanel
            visible: navigationOpen
            SplitView.preferredWidth: 240
            SplitView.minimumWidth: 180
            SplitView.maximumWidth: 340
            controller: activeController
            onToggleRequested: navigationOpen = false
            onOpenDatasetRequested: openDialog.open()
            onLoadTrackRequested: trackDialog.open()
            onLoadAnnotationsRequested: annotationDialog.open()
        }

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
                            if (comparisonOpen && linkNavigation && comparisonController)
                                comparisonController.syncViewFrom(activeController, linkColorScale)
                        }
                        function onTracksChanged() {
                            topTrackCanvas.requestPaint()
                            leftTrackCanvas.requestPaint()
                        }
                        function onAnnotationsChanged() {
                            annotationCanvas.requestPaint()
                        }
                        function onCytobandsChanged() {
                            topTrackCanvas.requestPaint()
                            leftTrackCanvas.requestPaint()
                        }
                        function onRecordsChanged() {
                            annotationCanvas.requestPaint()
                            miniMapCanvas.requestPaint()
                            histogramCanvas.requestPaint()
                        }
                        function onColorMaxChanged() {
                            histogramCanvas.requestPaint()
                            if (comparisonOpen && linkColorScale && comparisonController)
                                comparisonController.syncViewFrom(activeController, true)
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
                        anchors.horizontalCenterOffset: comparisonOpen ? -parent.width * 0.25 : 0
                        anchors.verticalCenterOffset: -16
                        width: Math.max(240, Math.min((comparisonOpen ? parent.width * 0.5 : parent.width) - 12,
                                                     parent.height - 40))
                        height: width
                        property int axisSize: {
                            if (!activeController || !trackPanelsOpen) return 46
                            var tracks = activeController.trackSummaries()
                            var extent = 38
                            for (var i = 0; i < tracks.length; ++i) {
                                if (tracks[i].visible && !tracks[i].collapsed)
                                    extent += Math.max(20, Math.min(80, tracks[i].height))
                            }
                            return Math.max(46, Math.min(220, extent))
                        }

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
                                var segments = trackPanelsOpen ? activeController.visibleTrackSegments(true) : []
                                var span = Math.max(1, activeController.x1 - activeController.x0)
                                var axisLabelHeight = 18
                                var axisY = height - axisLabelHeight - 0.5
                                if (activeController.showChromosomeContext) {
                                    var xBands = activeController.visibleCytobands(true)
                                    if (xBands.length > 0) {
                                        for (var xb = 0; xb < xBands.length; ++xb) {
                                            var bx0 = (xBands[xb].start - activeController.x0) / span * width
                                            var bx1 = (xBands[xb].end - activeController.x0) / span * width
                                            ctx.fillStyle = xBands[xb].color
                                            ctx.fillRect(Math.max(0, bx0), 0, Math.max(1, bx1 - bx0), 8)
                                        }
                                    } else {
                                        ctx.fillStyle = activeController.wholeGenomeView ? Theme.borderStrong : Theme.textMuted
                                        ctx.fillRect(0, 0, width, 8)
                                    }
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
                                var trackTop = activeController.showChromosomeContext ? 10 : 2
                                var trackBottom = axisY - 3
                                var summaries = trackPanelsOpen ? activeController.trackSummaries() : []
                                var totalHeight = 0
                                var laneStart = []
                                var laneSize = []
                                for (var li = 0; li < summaries.length; ++li) {
                                    if (summaries[li].visible && !summaries[li].collapsed)
                                        totalHeight += Math.max(20, summaries[li].height)
                                }
                                var laneCursor = trackTop
                                for (var lj = 0; lj < summaries.length; ++lj) {
                                    if (!summaries[lj].visible || summaries[lj].collapsed) continue
                                    var sized = Math.max(8, (trackBottom - trackTop) * Math.max(20, summaries[lj].height) / Math.max(1, totalHeight))
                                    laneStart[lj] = laneCursor
                                    laneSize[lj] = sized
                                    laneCursor += sized
                                }
                                for (var i = 0; i < segments.length; i++) {
                                    var s = segments[i]
                                    var x0 = (s.start - activeController.x0) / span * width
                                    var x1 = (s.end - activeController.x0) / span * width
                                    var laneY = laneStart[s.trackIndex]
                                    var laneHeight = laneSize[s.trackIndex]
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
                                var segments = trackPanelsOpen ? activeController.visibleTrackSegments(false) : []
                                var span = Math.max(1, activeController.y1 - activeController.y0)
                                var labelWidth = 42
                                var axisX = width - 0.5
                                if (activeController.showChromosomeContext) {
                                    var yBands = activeController.visibleCytobands(false)
                                    if (yBands.length > 0) {
                                        for (var yb = 0; yb < yBands.length; ++yb) {
                                            var by0 = (yBands[yb].start - activeController.y0) / span * height
                                            var by1 = (yBands[yb].end - activeController.y0) / span * height
                                            ctx.fillStyle = yBands[yb].color
                                            ctx.fillRect(0, Math.max(0, by0), 8, Math.max(1, by1 - by0))
                                        }
                                    } else {
                                        ctx.fillStyle = activeController.wholeGenomeView ? Theme.borderStrong : Theme.textMuted
                                        ctx.fillRect(0, 0, 8, height)
                                    }
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
                                var trackLeft = activeController.showChromosomeContext ? 10 : 2
                                var trackRight = axisX - labelWidth - 2
                                var summaries = trackPanelsOpen ? activeController.trackSummaries() : []
                                var totalHeight = 0
                                var laneStart = []
                                var laneSize = []
                                for (var li = 0; li < summaries.length; ++li) {
                                    if (summaries[li].visible && !summaries[li].collapsed)
                                        totalHeight += Math.max(20, summaries[li].height)
                                }
                                var laneCursor = trackLeft
                                for (var lj = 0; lj < summaries.length; ++lj) {
                                    if (!summaries[lj].visible || summaries[lj].collapsed) continue
                                    var sized = Math.max(7, (trackRight - trackLeft) * Math.max(20, summaries[lj].height) / Math.max(1, totalHeight))
                                    laneStart[lj] = laneCursor
                                    laneSize[lj] = sized
                                    laneCursor += sized
                                }
                                for (var i = 0; i < segments.length; i++) {
                                    var s = segments[i]
                                    var y0 = (s.start - activeController.y0) / span * height
                                    var y1 = (s.end - activeController.y0) / span * height
                                    var laneX = laneStart[s.trackIndex]
                                    var laneWidth = laneSize[s.trackIndex]
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
                                z: 15
                                anchors.top: parent.top
                                anchors.right: parent.right
                                anchors.margins: 10
                                width: 54
                                height: 144
                                radius: Theme.radiusSm
                                color: Qt.rgba(0.04, 0.06, 0.09, 0.82)
                                border.color: Qt.rgba(1, 1, 1, 0.18)
                                visible: activeController && activeController.recordCount > 0
                                Accessible.name: "Matrix color scale from " + (activeController ? activeController.colorMin : 0) + " to " + (activeController ? activeController.colorMax : 0)

                                Label {
                                    anchors.top: parent.top; anchors.horizontalCenter: parent.horizontalCenter; anchors.topMargin: 6
                                    text: activeController ? Number(activeController.colorMax).toPrecision(3) : ""
                                    color: "white"; font.pixelSize: Theme.textXs
                                }
                                Rectangle {
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    anchors.top: parent.top; anchors.topMargin: 25
                                    width: 14; height: 92
                                    border.color: Qt.rgba(1, 1, 1, 0.28)
                                    gradient: Gradient {
                                        orientation: Gradient.Vertical
                                        GradientStop { position: 0; color: legendHighColor() }
                                        GradientStop { position: activeController && activeController.colorMin < 0 && activeController.colorMax > 0 ? 0.5 : 1; color: "white" }
                                        GradientStop { position: 1; color: legendLowColor() }
                                    }
                                }
                                Label {
                                    anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter; anchors.bottomMargin: 6
                                    text: activeController ? Number(activeController.colorMin).toPrecision(3) : ""
                                    color: "white"; font.pixelSize: Theme.textXs
                                }
                            }

                            Rectangle {
                                z: 16
                                anchors.left: parent.left
                                anchors.top: parent.top
                                anchors.margins: 10
                                visible: activeController && activeController.busy
                                width: loadingRow.implicitWidth + 18
                                height: 30
                                radius: Theme.radiusSm
                                color: Qt.rgba(0.04, 0.06, 0.09, 0.82)
                                RowLayout {
                                    id: loadingRow
                                    anchors.centerIn: parent
                                    spacing: 6
                                    BusyIndicator { running: parent.parent.visible; Layout.preferredWidth: 16; Layout.preferredHeight: 16 }
                                    Label { text: "Refining view"; color: "white"; font.pixelSize: Theme.textXs }
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
                                Accessible.name: "Interactive Hi-C contact matrix"
                                Accessible.description: "Click to zoom, drag to pan, use the wheel to zoom, Alt-drag to select a region, and Shift-drag to add an annotation"
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
                                property real pendingHoverFx: 0.5
                                property real pendingHoverFy: 0.5

                                Timer {
                                    id: hoverValueTimer
                                    interval: 32
                                    repeat: false
                                    onTriggered: {
                                        if (activeController && interactionArea.containsMouse)
                                            hoverText = activeController.positionText(interactionArea.pendingHoverFx, interactionArea.pendingHoverFy)
                                    }
                                }

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
                                    pendingHoverFx = contextFx
                                    pendingHoverFy = contextFy
                                    if (!hoverValueTimer.running)
                                        hoverValueTimer.start()
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

                    ComparisonViewport {
                        id: comparisonViewport
                        visible: comparisonOpen
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.bottom: parent.bottom
                        anchors.topMargin: 10
                        anchors.rightMargin: 10
                        anchors.bottomMargin: 42
                        width: parent.width * 0.5 - 16
                        controller: comparisonController
                        viewLabel: comparisonController && comparisonController.filePath.length > 0
                                   ? String(comparisonController.filePath).split(/[\\/]/).pop() : "Comparison view"
                        crosshairVisible: linkCrosshair && hoverActive
                        crosshairX: contextFx
                        crosshairY: contextFy
                        onCrosshairMoved: function(xFraction, yFraction) {
                            if (linkCrosshair) {
                                contextFx = xFraction
                                contextFy = yFraction
                                hoverPlotX = xFraction * heatmapHost.width
                                hoverPlotY = yFraction * heatmapHost.height
                                hoverActive = true
                                guideCanvas.requestPaint()
                            }
                        }
                        onViewportInteracted: if (linkNavigation && activeController && comparisonController)
                            activeController.syncViewFrom(comparisonController, linkColorScale)
                    }

                    Rectangle {
                        visible: comparisonOpen
                        z: 40
                        anchors.top: parent.top
                        anchors.horizontalCenter: parent.horizontalCenter
                        anchors.topMargin: 8
                        width: comparisonLinks.implicitWidth + 16
                        height: 34
                        radius: Theme.radiusSm
                        color: Theme.surfaceAlt
                        border.color: Theme.borderStrong
                        RowLayout {
                            id: comparisonLinks
                            anchors.centerIn: parent
                            spacing: 4
                            AppCheckBox { text: "Loci"; checked: linkNavigation; onToggled: linkNavigation = checked; Accessible.name: "Link comparison navigation" }
                            AppCheckBox { text: "Cursor"; checked: linkCrosshair; onToggled: linkCrosshair = checked; Accessible.name: "Link comparison crosshair" }
                            AppCheckBox { text: "Scale"; checked: linkColorScale; onToggled: linkColorScale = checked; Accessible.name: "Link comparison color scale" }
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
                            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 14; color: Theme.chromeBorder }
                            Label {
                                text: activeController ? activeController.matrixDimensions + " · " + activeController.resolution + " bp" : ""
                                color: Theme.chromeTextMuted
                                font.pixelSize: Theme.textXs
                            }
                            Label {
                                text: activeController ? "Cache " + activeController.cacheMemoryMB.toFixed(1) + "/" + activeController.cacheLimitMB + " MB" : ""
                                color: Theme.chromeTextMuted
                                font.pixelSize: Theme.textXs
                            }
                            Label {
                                text: activeController ? activeController.renderingBackend : ""
                                color: Theme.chromeTextMuted
                                font.pixelSize: Theme.textXs
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
            visible: inspectorOpen
            SplitView.preferredWidth: 300
            SplitView.minimumWidth: 260
            SplitView.maximumWidth: 420
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
                        implicitHeight: performanceColumn.implicitHeight + 24
                        ColumnLayout {
                            id: performanceColumn
                            anchors.fill: parent
                            anchors.margins: 12
                            spacing: 8
                            Label { text: "Performance & Interface"; color: Theme.textPrimary; font.pixelSize: Theme.textMd; font.weight: Font.DemiBold }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "Cache"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                                SpinBox {
                                    from: 16; to: 4096; stepSize: 16; editable: true
                                    value: activeController ? activeController.cacheLimitMB : 128
                                    onValueModified: if (activeController) activeController.cacheLimitMB = value
                                    Accessible.name: "Matrix cache limit in megabytes"
                                }
                                Label { text: "MB"; color: Theme.textMuted; font.pixelSize: Theme.textSm }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "UI scale"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                                Slider { Layout.fillWidth: true; from: 0.85; to: 1.35; stepSize: 0.05; value: interfaceScale; onMoved: interfaceScale = value; Accessible.name: "Interface scale" }
                                Label { text: Math.round(interfaceScale * 100) + "%"; color: Theme.textPrimary; font.pixelSize: Theme.textSm }
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "Font size"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                                Slider { Layout.fillWidth: true; from: 0.85; to: 1.4; stepSize: 0.05; value: fontScale; onMoved: fontScale = value; Accessible.name: "Application font size" }
                                Label { text: Math.round(fontScale * 100) + "%"; color: Theme.textPrimary; font.pixelSize: Theme.textSm }
                            }
                            AppCheckBox { text: "Reduce motion"; checked: reducedMotion; onToggled: reducedMotion = checked; Accessible.name: "Reduce interface motion" }
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

                            Label { text: "Visible matrix"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                            Label { text: activeController ? activeController.matrixDimensions : "—"; color: Theme.textPrimary }

                            Label { text: "Renderer"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                            Label { text: activeController ? activeController.renderingBackend : "—"; color: Theme.textPrimary; elide: Text.ElideRight; Layout.fillWidth: true }

                            Label { text: "Cache"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                            Label { text: activeController ? activeController.cacheTileCount + " regions · " + activeController.cacheMemoryMB.toFixed(1) + " MB" : "—"; color: Theme.textPrimary }
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
                                    text: "Lock X"
                                    checked: activeController && activeController.xLocusLocked
                                    Accessible.name: "Lock X axis locus"
                                    onToggled: if (activeController) activeController.xLocusLocked = checked
                                }
                                AppCheckBox {
                                    text: "Lock Y"
                                    checked: activeController && activeController.yLocusLocked
                                    Accessible.name: "Lock Y axis locus"
                                    onToggled: if (activeController) activeController.yLocusLocked = checked
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

                            Canvas {
                                id: histogramCanvas
                                Layout.fillWidth: true
                                Layout.preferredHeight: 52
                                Accessible.name: "Visible matrix value distribution"
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.reset()
                                    ctx.fillStyle = Theme.surfaceSunken
                                    ctx.fillRect(0, 0, width, height)
                                    if (!activeController) return
                                    var histogram = activeController.colorHistogram(40)
                                    var barWidth = width / Math.max(1, histogram.length)
                                    ctx.fillStyle = Theme.accent
                                    for (var i = 0; i < histogram.length; i++) {
                                        var h = Math.max(1, histogram[i].fraction * (height - 6))
                                        ctx.fillRect(i * barWidth, height - h, Math.max(1, barWidth - 1), h)
                                    }
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                AppCheckBox {
                                    text: "Symmetric zero"
                                    checked: activeController && activeController.symmetricColorScale
                                    onToggled: if (activeController) activeController.symmetricColorScale = checked
                                }
                                AppCheckBox {
                                    text: "Transparent zero"
                                    checked: activeController && activeController.zeroTransparent
                                    onToggled: if (activeController) activeController.zeroTransparent = checked
                                }
                            }

                            RowLayout {
                                Layout.fillWidth: true
                                Label { text: "Clip"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                                Slider {
                                    Layout.fillWidth: true
                                    from: 50; to: 100; stepSize: 0.5
                                    value: activeController ? activeController.colorPercentile : 95
                                    onMoved: if (activeController) activeController.colorPercentile = value
                                    Accessible.name: "Visible value clipping percentile"
                                }
                                Label { text: activeController ? activeController.colorPercentile.toFixed(1) + "%" : "95%"; color: Theme.textPrimary; font.pixelSize: Theme.textSm }
                                AppButton { text: "Missing"; tonal: true; onClicked: missingColorDialog.open() }
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
                                                height: modelData.collapsed ? 46 : 184
                                                radius: Theme.radiusMd
                                                color: Theme.surface
                                                border.color: Theme.border
                                                Accessible.name: "Track " + modelData.name
                                                ColumnLayout {
                                                    anchors.fill: parent
                                                    anchors.margins: 8
                                                    spacing: 6
                                                    RowLayout {
                                                        Layout.fillWidth: true
                                                        spacing: 5
                                                        AppCheckBox {
                                                            checked: modelData.visible
                                                            text: ""
                                                            Accessible.name: "Show " + modelData.name
                                                            ToolTip.visible: hovered
                                                            ToolTip.text: checked ? "Hide track" : "Show track"
                                                            onToggled: activeController.setTrackVisible(modelData.index, checked)
                                                        }
                                                        AppToolButton {
                                                            text: modelData.collapsed ? "›" : "⌄"
                                                            Accessible.name: (modelData.collapsed ? "Expand " : "Collapse ") + modelData.name
                                                            ToolTip.visible: hovered
                                                            ToolTip.text: modelData.collapsed ? "Expand track controls" : "Collapse track controls"
                                                            onClicked: activeController.setTrackCollapsed(modelData.index, !modelData.collapsed)
                                                        }
                                                        AppTextField {
                                                            Layout.fillWidth: true
                                                            text: modelData.name
                                                            Accessible.name: "Track name"
                                                            onAccepted: activeController.setTrackName(modelData.index, text)
                                                            onEditingFinished: activeController.setTrackName(modelData.index, text)
                                                        }
                                                        AppToolButton {
                                                            id: trackMenuButton
                                                            text: "⋯"
                                                            Accessible.name: "Track actions for " + modelData.name
                                                            onClicked: trackContextMenu.popup()
                                                            Menu {
                                                                id: trackContextMenu
                                                                MenuItem { text: "Move up"; enabled: modelData.index > 0; onTriggered: activeController.moveTrack(modelData.index, modelData.index - 1) }
                                                                MenuItem { text: "Move down"; enabled: modelData.index + 1 < activeController.trackCount; onTriggered: activeController.moveTrack(modelData.index, modelData.index + 1) }
                                                                MenuSeparator {}
                                                                MenuItem { text: "Remove track"; onTriggered: activeController.removeTrack(modelData.index) }
                                                            }
                                                        }
                                                    }
                                                    Label {
                                                        visible: !modelData.collapsed
                                                        text: modelData.featureCount + " intervals"
                                                        color: Theme.textMuted
                                                        font.pixelSize: Theme.textXs
                                                    }
                                                    RowLayout {
                                                        visible: !modelData.collapsed
                                                        Layout.fillWidth: true
                                                        AppCheckBox {
                                                            text: "Autoscale"
                                                            checked: modelData.autoscale
                                                            onToggled: activeController.setTrackAutoscale(modelData.index, checked)
                                                        }
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
                                                    }
                                                    RowLayout {
                                                        visible: !modelData.collapsed
                                                        Layout.fillWidth: true
                                                        spacing: 6
                                                        Label { text: "Min"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                                                        AppTextField { text: String(modelData.min); enabled: !modelData.autoscale; Layout.fillWidth: true; onAccepted: activeController.setTrackRange(modelData.index, Number(text), modelData.max, modelData.logScale) }
                                                        Label { text: "Max"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                                                        AppTextField { text: String(modelData.max); enabled: !modelData.autoscale; Layout.fillWidth: true; onAccepted: activeController.setTrackRange(modelData.index, modelData.min, Number(text), modelData.logScale) }
                                                    }
                                                    RowLayout {
                                                        visible: !modelData.collapsed
                                                        Layout.fillWidth: true
                                                        Label { text: "Height"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                                                        Slider {
                                                            Layout.fillWidth: true
                                                            from: 20
                                                            to: 160
                                                            stepSize: 4
                                                            value: modelData.height
                                                            Accessible.name: "Height of " + modelData.name
                                                            onMoved: activeController.setTrackHeight(modelData.index, value)
                                                        }
                                                        Label { text: Math.round(modelData.height) + " px"; color: Theme.textMuted; font.pixelSize: Theme.textXs }
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

    Rectangle {
        id: toast
        z: 1000
        visible: toastText.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 48
        width: Math.min(520, toastLabel.implicitWidth + 44)
        height: 40
        radius: Theme.radiusSm
        color: Theme.surfaceAlt
        border.color: toastKind === "error" ? Theme.danger : toastKind === "success" ? Theme.success : Theme.borderStrong
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 8
            Rectangle {
                Layout.preferredWidth: 7; Layout.preferredHeight: 7; radius: 4
                color: toastKind === "error" ? Theme.danger : toastKind === "success" ? Theme.success : Theme.accent
            }
            Label { id: toastLabel; text: toastText; color: Theme.textPrimary; font.pixelSize: Theme.textSm; Layout.fillWidth: true }
        }
    }
}
