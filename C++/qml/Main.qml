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

    property var tabs: []
    property var activeTab: null
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
    property bool hoverTextVisible: true
    property int pendingAnnotationLayerExport: -1
    property int pendingTrackIndex: -1
    property bool navigationOpen: true
    property bool minimapVisible: true
    property bool trackPanelsOpen: false
    property bool landscapeMode: false
    property real interfaceScale: 1.0
    property real fontScale: 1.0
    property bool reducedMotion: false
    property string toastText: ""
    property string toastKind: "info"
    property var bullseyeInspectorCells: []
    property var bullseyeInspectorSource: null
    property real bullseyeInspectorX: 0
    property real bullseyeInspectorY: 0
    property int bullseyeInspectorRadiusBins: 12

    function showToast(message, kind) {
        toastText = message
        toastKind = kind || "info"
        toastTimer.restart()
    }

    function trackSummary(index) {
        if (!activeController || index < 0 || index >= activeController.trackCount)
            return null
        return activeController.trackSummaries()[index]
    }

    function openPlotTrackMenu(index) {
        var summary = trackSummary(index)
        if (!summary) return
        pendingTrackIndex = index
        plotTrackContextMenu.summary = summary
        plotTrackContextMenu.popup()
    }

    function chooseTrackColor(index, negative) {
        var summary = trackSummary(index)
        if (!summary) return
        pendingTrackIndex = index
        if (negative) {
            trackNegativeColorDialog.selectedColor = summary.negativeColor
            trackNegativeColorDialog.open()
        } else {
            trackPositiveColorDialog.selectedColor = summary.positiveColor
            trackPositiveColorDialog.open()
        }
    }

    function openTrackRangeEditor(index) {
        var summary = trackSummary(index)
        if (!summary) return
        pendingTrackIndex = index
        trackRangeMin.text = String(summary.min)
        trackRangeMax.text = String(summary.max)
        trackRangeLog.checked = summary.logScale
        trackRangeDialog.open()
    }

    function openTrackBinEditor(index) {
        var summary = trackSummary(index)
        if (!summary) return
        pendingTrackIndex = index
        trackBinSizeField.text = String(summary.effectiveBinSize)
        trackBinSizeDialog.open()
    }

    function openTrackHeightEditor(index) {
        var summary = trackSummary(index)
        if (!summary) return
        pendingTrackIndex = index
        trackHeightField.text = String(summary.height)
        trackHeightDialog.open()
    }

    function trackIndexAtPanelPosition(position, trackStart, trackEnd, minimumLaneSize) {
        if (!activeController || !trackPanelsOpen || position < trackStart || position >= trackEnd)
            return -1
        var summaries = activeController.trackSummaries()
        var totalHeight = 0
        for (var i = 0; i < summaries.length; ++i) {
            if (summaries[i].visible && !summaries[i].collapsed)
                totalHeight += Math.max(20, summaries[i].height)
        }
        var cursor = trackStart
        for (var j = 0; j < summaries.length; ++j) {
            if (!summaries[j].visible || summaries[j].collapsed) continue
            var laneSize = Math.max(minimumLaneSize, (trackEnd - trackStart) * Math.max(20, summaries[j].height) / Math.max(1, totalHeight))
            if (position >= cursor && position < cursor + laneSize)
                return j
            cursor += laneSize
        }
        return -1
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
    Menu {
        id: newTabMenu
        MenuItem { text: "Single map"; onTriggered: addTab("single") }
        MenuItem { text: "Multi-map"; onTriggered: addTab("multi-map") }
        MenuItem { text: "Multi-region (BEDPE)"; onTriggered: addTab("multi-region") }
        MenuItem { text: "Maps × regions"; onTriggered: addTab("map-region") }
        MenuItem { text: "Pairwise regions"; onTriggered: addTab("pairwise") }
        MenuSeparator {}
        MenuItem { text: "45° diagonal heatmaps"; onTriggered: addTab("rotated-45") }
        MenuItem { text: "SIP bullseye"; onTriggered: addTab("bullseye") }
        MenuItem { text: "Virtual 4C"; onTriggered: addTab("virtual-4c") }
        MenuItem { text: "Experimental processing"; onTriggered: addTab("processing") }
    }

    function createTabSession(type) {
        var session = Qt.createQmlObject("import Carton; TabSession {}", window)
        session.initialize(type || "single")
        session.titleChanged.connect(function() {
            var index = tabs.indexOf(session)
            if (index >= 0) tabModel.setProperty(index, "title", session.title)
        })
        session.activeCellChanged.connect(function() {
            if (session === activeTab) {
                activeController = session.activeController
                syncControlModels()
                if (session.type === "single") Qt.callLater(plotFrame.fitControllerToCanvas)
            }
        })
        session.cellsChanged.connect(function() {
            if (session === activeTab) {
                activeController = session.activeController
                syncControlModels()
            }
        })
        session.errorOccurred.connect(function(message) { showToast(message, "error") })
        return session
    }

    function addTab(type) {
        var session = createTabSession(type || "single")
        tabs.push(session)
        tabs = tabs.slice()
        tabSerial += 1
        var initialTitle = session.typeLabel + " " + tabSerial
        tabModel.append({ "title": initialTitle, "type": session.type })
        session.title = initialTitle
        tabBar.currentIndex = tabs.length - 1
        setActiveTab(tabBar.currentIndex)
    }

    function closeCurrentTab() {
        if (tabs.length <= 1)
            return
        var index = tabBar.currentIndex
        var session = tabs[index]
        tabs.splice(index, 1)
        tabs = tabs.slice()
        tabModel.remove(index)
        session.destroy()
        tabBar.currentIndex = Math.min(index, tabs.length - 1)
        setActiveTab(tabBar.currentIndex)
    }

    function openBullseyeAt(sourceController, fx, fy) {
        if (!sourceController) return
        var sourceTab = activeTab
        var sourceMaps = sourceTab ? sourceTab.maps : []
        var resolution = Math.max(1, sourceController.resolution)
        var centerX = Math.floor((sourceController.x0 + (sourceController.x1 - sourceController.x0) * fx) / resolution) * resolution
        var centerY = Math.floor((sourceController.y0 + (sourceController.y1 - sourceController.y0) * fy) / resolution) * resolution
        addTab("bullseye")
        while (activeTab.mapCount < sourceMaps.length) activeTab.addMap()
        while (activeTab.mapCount > Math.max(1, sourceMaps.length)) activeTab.removeMap(activeTab.mapCount - 1)
        for (var i = 0; i < sourceMaps.length; ++i) {
            if (sourceMaps[i].primaryPath && sourceMaps[i].primaryPath.length > 0)
                activeTab.setPrimaryFile(i, sourceMaps[i].primaryPath)
            if (sourceMaps[i].controlPath && sourceMaps[i].controlPath.length > 0)
                activeTab.setControlFile(i, sourceMaps[i].controlPath)
        }
        activeTab.bullseyeCenterX = centerX
        activeTab.bullseyeCenterY = centerY
        activeTab.bullseyePinned = false
    }

    function updateBullseyeInspector(sourceController, fx, fy) {
        if (!sourceController || !bullseyeInspector.visible) return
        var resolution = Math.max(1, sourceController.resolution)
        bullseyeInspectorSource = sourceController
        bullseyeInspectorX = Math.floor((sourceController.x0 + (sourceController.x1 - sourceController.x0) * fx) / resolution) * resolution
        bullseyeInspectorY = Math.floor((sourceController.y0 + (sourceController.y1 - sourceController.y0) * fy) / resolution) * resolution
    }

    function showBullseyeInspector(sourceController, fx, fy) {
        if (!sourceController || !activeTab) return
        bullseyeInspectorCells = activeTab.cells
        for (var i = 0; i < bullseyeInspectorCells.length; ++i)
            if (bullseyeInspectorCells[i].controller)
                bullseyeInspectorCells[i].controller.setAnalysisPaddingBins(bullseyeInspectorRadiusBins + 2)
        bullseyeInspector.open()
        updateBullseyeInspector(sourceController, fx, fy)
    }

    function setBullseyeInspectorRadius(value) {
        bullseyeInspectorRadiusBins = Math.max(1, Math.min(100, Math.round(value)))
        for (var i = 0; i < bullseyeInspectorCells.length; ++i)
            if (bullseyeInspectorCells[i].controller)
                bullseyeInspectorCells[i].controller.setAnalysisPaddingBins(bullseyeInspectorRadiusBins + 2)
    }

    function setActiveTab(index) {
        if (index < 0 || index >= tabs.length)
            return
        activeTab = tabs[index]
        activeController = activeTab.activeController
        syncControlModels()
        if (activeTab.type === "single") Qt.callLater(plotFrame.fitControllerToCanvas)
    }

    function syncControlModels() {
        if (!activeController)
            return
        navigationPanel.syncControllerModels()
    }

    function exportWorkspace(url) {
        var states = []
        for (var i = 0; i < tabs.length; ++i) states.push(tabs[i].state())
        if (DatasetRegistry.exportWorkspace(url, states)) showToast("Workspace exported", "success")
        else showToast("Workspace export failed", "error")
    }

    function importWorkspace(url) {
        var states = DatasetRegistry.importWorkspace(url)
        if (!states || states.length === 0) {
            showToast("Invalid CARTON workspace", "error")
            return
        }
        for (var old = 0; old < tabs.length; ++old) tabs[old].destroy()
        tabs = []
        tabModel.clear()
        for (var i = 0; i < states.length; ++i) {
            var session = createTabSession(states[i].tabType || "single")
            tabs.push(session)
            tabModel.append({ "title": states[i].title || session.typeLabel, "type": states[i].tabType || "single" })
            session.restoreState(states[i])
        }
        tabs = tabs.slice()
        tabBar.currentIndex = 0
        setActiveTab(0)
        showToast("Workspace imported", "success")
    }

    Component.onCompleted: {
        var initialType = "single"
        var initialRegions = ""
        var initialRegionFormat = ""
        for (var i = 0; i < Qt.application.arguments.length; ++i) {
            var argument = String(Qt.application.arguments[i])
            if (argument.indexOf("--tab-type=") === 0) initialType = argument.substring(11)
            else if (argument.indexOf("--regions=") === 0) initialRegions = argument.substring(10)
            else if (argument.indexOf("--regions-format=") === 0) initialRegionFormat = argument.substring(17)
        }
        addTab(initialType)
        if (initialRegions.length > 0 && activeTab) activeTab.loadRegions(initialRegions, initialRegionFormat)
    }

    FileDialog {
        id: openDialog
        title: "Open primary Hi-C file"
        nameFilters: ["Hi-C files (*.hic)", "All files (*)"]
        onAccepted: {
            if (activeTab)
                activeTab.setPrimaryFile(activeTab.activeCellIndex, selectedFile)
        }
    }

    FileDialog {
        id: controlDialog
        title: "Open control Hi-C file"
        nameFilters: ["Hi-C files (*.hic)", "All files (*)"]
        onAccepted: {
            if (activeTab)
                activeTab.setControlFile(activeTab.activeCellIndex, selectedFile)
        }
    }

    FileDialog {
        id: trackDialog
        title: "Load 1D Tracks"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Genomics tracks (*.bed *.bed.gz *.bedgraph *.bedGraph *.bedgraph.gz *.wig *.wig.gz *.bw *.bigWig *.bigwig *.bb *.bigBed *.bigbed *.txt *.tsv)", "All files (*)"]
        onAccepted: if (activeTab) {
            trackPanelsOpen = true
            for (var i = 0; i < selectedFiles.length; ++i)
                activeTab.loadTrack(selectedFiles[i], activeTab.layerScope)
        }
    }

    FileDialog {
        id: annotationDialog
        title: "Load 2D Annotations"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["BEDPE annotations (*.bedpe *.txt *.tsv)", "All files (*)"]
        onAccepted: if (activeTab) {
            for (var i = 0; i < selectedFiles.length; ++i)
                activeTab.loadAnnotations(selectedFiles[i], activeTab.layerScope)
        }
    }

    FileDialog {
        id: regionDialog
        property string regionFormat: "bedpe"
        title: regionFormat === "bed" ? "Load BED regions" :
               (regionFormat === "bedpe-as-bed" ? "Project BEDPE endpoints" : "Load BEDPE regions")
        nameFilters: regionFormat === "bed" ? ["BED regions (*.bed *.txt *.tsv)", "All files (*)"]
                                             : ["BEDPE regions (*.bedpe *.txt *.tsv)", "All files (*)"]
        onAccepted: if (activeTab) activeTab.loadRegions(selectedFile, regionFormat)
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
        onAccepted: importWorkspace(selectedFile)
    }

    FileDialog {
        id: exportStateDialog
        title: "Export CARTON state"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: ["CARTON state (*.json)", "All files (*)"]
        onAccepted: exportWorkspace(selectedFile)
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
        onAccepted: if (activeTab) {
            trackPanelsOpen = true
            activeTab.loadTrackFromPath(urlField.text, activeTab.layerScope)
        }
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

    ColorDialog {
        id: trackPositiveColorDialog
        title: "Track Color"
        onAccepted: {
            var summary = trackSummary(pendingTrackIndex)
            if (summary) activeController.setTrackColor(pendingTrackIndex, selectedColor, summary.negativeColor)
        }
    }

    ColorDialog {
        id: trackNegativeColorDialog
        title: "Negative Value Color"
        onAccepted: {
            var summary = trackSummary(pendingTrackIndex)
            if (summary) activeController.setTrackColor(pendingTrackIndex, summary.positiveColor, selectedColor)
        }
    }

    ColorDialog {
        id: annotationLayerColorDialog
        property var targetController: null
        property int layerIndex: -1
        title: "Override Annotation Color"
        onAccepted: {
            if (targetController && layerIndex >= 0)
                targetController.setAnnotationLayerColor(layerIndex, selectedColor)
        }
    }

    Dialog {
        id: trackRangeDialog
        title: "Set Track Data Range"
        modal: true
        width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: DialogFrame {}
        onAccepted: {
            var minimum = Number(trackRangeMin.text)
            var maximum = Number(trackRangeMax.text)
            if (!activeController || pendingTrackIndex < 0 || !isFinite(minimum) || !isFinite(maximum) || maximum <= minimum) {
                showToast("Track range requires a maximum greater than the minimum", "error")
                return
            }
            activeController.setTrackRange(pendingTrackIndex, minimum, maximum, trackRangeLog.checked)
            activeController.setTrackAutoscale(pendingTrackIndex, false)
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 10
            GridLayout {
                Layout.fillWidth: true
                columns: 2
                Label { text: "Minimum"; color: Theme.textSecondary }
                AppTextField { id: trackRangeMin; Layout.fillWidth: true; inputMethodHints: Qt.ImhFormattedNumbersOnly }
                Label { text: "Maximum"; color: Theme.textSecondary }
                AppTextField { id: trackRangeMax; Layout.fillWidth: true; inputMethodHints: Qt.ImhFormattedNumbersOnly }
            }
            AppCheckBox { id: trackRangeLog; text: "Log scale" }
        }
    }

    Dialog {
        id: trackBinSizeDialog
        title: "Set Fixed Track Bin Size"
        modal: true
        width: 380
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: DialogFrame {}
        onAccepted: {
            var value = Math.round(Number(trackBinSizeField.text))
            if (!activeController || pendingTrackIndex < 0 || !isFinite(value) || value < 1) {
                showToast("Bin size must be a positive number of base pairs", "error")
                return
            }
            activeController.setTrackBinSize(pendingTrackIndex, value)
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label { text: "Bin size (bp)"; color: Theme.textSecondary }
            AppTextField {
                id: trackBinSizeField
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhDigitsOnly
            }
            Label {
                text: "Use “Match Hi-C resolution” in the track menu to follow map resolution changes."
                color: Theme.textMuted
                font.pixelSize: Theme.textXs
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
    }

    Dialog {
        id: trackHeightDialog
        title: "Set Track Height"
        modal: true
        width: 340
        standardButtons: Dialog.Ok | Dialog.Cancel
        background: DialogFrame {}
        onAccepted: {
            var value = Math.round(Number(trackHeightField.text))
            if (!activeController || pendingTrackIndex < 0 || !isFinite(value)) return
            activeController.setTrackHeight(pendingTrackIndex, value)
        }
        ColumnLayout {
            anchors.fill: parent
            spacing: 8
            Label { text: "Height (20 px minimum)"; color: Theme.textSecondary }
            AppTextField {
                id: trackHeightField
                Layout.fillWidth: true
                inputMethodHints: Qt.ImhDigitsOnly
                validator: IntValidator { bottom: 20 }
            }
        }
    }

    Dialog {
        id: colorScaleDialog
        title: "Heatmap Colors"
        modal: true
        width: 340
        height: 300
        standardButtons: Dialog.Close
        background: DialogFrame {}
        onOpened: {
            if (!activeController) return
            quickColorMap.currentIndex = Math.max(0, quickColorMap.find(activeController.colorMap))
            quickColorMin.text = activeController.colorMin.toString()
            quickColorMax.text = activeController.colorMax.toString()
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 12

            Label { text: "Color map"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
            AppComboBox {
                id: quickColorMap
                Layout.fillWidth: true
                model: ["White-Red", "Viridis", "Blue-White-Red", "Grayscale", "Custom"]
                onActivated: if (activeController) activeController.colorMap = currentText
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                columnSpacing: 10
                rowSpacing: 6
                Label { text: "Minimum"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                Label { text: "Maximum"; color: Theme.textSecondary; font.pixelSize: Theme.textSm }
                AppTextField {
                    id: quickColorMin
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    onEditingFinished: {
                        var value = Number(text)
                        if (activeController && isFinite(value)) activeController.colorMin = value
                    }
                }
                AppTextField {
                    id: quickColorMax
                    Layout.fillWidth: true
                    inputMethodHints: Qt.ImhFormattedNumbersOnly
                    onEditingFinished: {
                        var value = Number(text)
                        if (activeController && isFinite(value)) activeController.colorMax = value
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                AppButton {
                    text: "Reset automatic range"
                    tonal: true
                    onClicked: {
                        if (!activeController) return
                        activeController.resetColorScale()
                        quickColorMin.text = activeController.colorMin.toString()
                        quickColorMax.text = activeController.colorMax.toString()
                    }
                }
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
            MenuItem { text: "Load Cytobands..."; onTriggered: cytobandDialog.open() }
            MenuSeparator {}
            MenuItem { text: "Import State..."; onTriggered: importStateDialog.open() }
            MenuItem { text: "Export State..."; enabled: !!activeController; onTriggered: exportStateDialog.open() }
            MenuItem { text: "Export PDF Figure..."; enabled: !!activeController; onTriggered: exportSizeDialog.open() }
            MenuItem { text: "Export PNG Image..."; enabled: !!activeController; onTriggered: exportPngDialog.open() }
            MenuItem { text: "Export Visible Matrix..."; enabled: activeController && activeController.recordCount > 0; onTriggered: exportMatrixDialog.open() }
            MenuSeparator {}
            Menu {
                title: "New Tab"
                MenuItem { text: "Single map"; onTriggered: addTab("single") }
                MenuItem { text: "Multi-map"; onTriggered: addTab("multi-map") }
                MenuItem { text: "Multi-region (BEDPE)"; onTriggered: addTab("multi-region") }
                MenuItem { text: "Maps × regions"; onTriggered: addTab("map-region") }
                MenuItem { text: "Pairwise regions"; onTriggered: addTab("pairwise") }
                MenuSeparator {}
                MenuItem { text: "45° diagonal heatmaps"; onTriggered: addTab("rotated-45") }
                MenuItem { text: "SIP bullseye"; onTriggered: addTab("bullseye") }
                MenuItem { text: "Virtual 4C"; onTriggered: addTab("virtual-4c") }
                MenuItem { text: "Experimental processing"; onTriggered: addTab("processing") }
            }
            MenuItem { text: "Close Tab"; enabled: tabs.length > 1; onTriggered: closeCurrentTab() }
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
            MenuItem { text: "Hover Text"; checkable: true; checked: hoverTextVisible; onTriggered: hoverTextVisible = checked }
            MenuSeparator {}
            MenuItem { text: "Navigation Panel"; checkable: true; checked: navigationOpen; onTriggered: navigationOpen = checked }
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

    SplitView {
        anchors.fill: parent
        orientation: Qt.Horizontal

        NavigationPanel {
            id: navigationPanel
            visible: navigationOpen
            SplitView.preferredWidth: 340
            SplitView.minimumWidth: 260
            SplitView.maximumWidth: 520
            controller: activeController
            tabSession: activeTab
            landscapeMode: window.landscapeMode
            hoverTextVisible: window.hoverTextVisible
            trackPanelsOpen: window.trackPanelsOpen
            minimapVisible: window.minimapVisible
            hoverText: window.hoverText
            interfaceScale: window.interfaceScale
            applicationFontScale: window.fontScale
            reducedMotion: window.reducedMotion
            onToggleRequested: navigationOpen = false
            onOpenDatasetRequested: openDialog.open()
            onLoadControlRequested: controlDialog.open()
            onLoadTrackRequested: trackDialog.open()
            onLoadTrackUrlRequested: urlDialog.open()
            onLoadAnnotationsRequested: annotationDialog.open()
            onPooledResourceRequested: function(resourceId, kind) {
                if (!activeTab) return
                if (kind === "hic")
                    activeTab.setPrimaryFile(activeTab.activeCellIndex, DatasetRegistry.resourcePath(resourceId))
                else if (kind === "track")
                    activeTab.loadTrackResource(resourceId, activeTab.layerScope)
                else if (kind === "annotation")
                    activeTab.loadAnnotationResource(resourceId, activeTab.layerScope)
            }
            onPooledControlRequested: function(resourceId) {
                if (activeTab) activeTab.setControlFile(activeTab.activeCellIndex, DatasetRegistry.resourcePath(resourceId))
            }
            onExportAnnotationRequested: function(index) {
                pendingAnnotationLayerExport = index
                exportAnnotationDialog.open()
            }
            onAnnotationColorRequested: function(index, currentColor) {
                annotationLayerColorDialog.targetController = activeController
                annotationLayerColorDialog.layerIndex = index
                annotationLayerColorDialog.selectedColor = currentColor
                annotationLayerColorDialog.open()
            }
            onTrackMenuRequested: function(index) { openPlotTrackMenu(index) }
            onTrackBinEditorRequested: function(index) { openTrackBinEditor(index) }
            onLandscapeModeToggled: function(enabled) {
                window.landscapeMode = enabled
                Qt.callLater(plotFrame.fitControllerToCanvas)
            }
            onHoverTextToggled: function(enabled) { window.hoverTextVisible = enabled }
            onTrackPanelsToggled: function(enabled) { window.trackPanelsOpen = enabled }
            onMinimapToggled: function(enabled) { window.minimapVisible = enabled }
            onInterfaceScaleRequested: function(value) { window.interfaceScale = value }
            onFontScaleRequested: function(value) { window.fontScale = value }
            onReducedMotionToggled: function(enabled) { window.reducedMotion = enabled }
            onLowColorRequested: lowColorDialog.open()
            onHighColorRequested: highColorDialog.open()
            onMissingColorRequested: missingColorDialog.open()
        }

        Rectangle {
            SplitView.fillWidth: true
            SplitView.fillHeight: true
            color: Theme.appBg

            AppToolButton {
                z: 200
                visible: !navigationOpen
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: 28
                height: 48
                text: "›"
                onLightSurface: true
                contentColor: Theme.textSecondary
                Accessible.name: "Open workspace"
                ToolTip.visible: hovered
                ToolTip.text: Accessible.name
                onClicked: navigationOpen = true
            }

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
                        onClicked: newTabMenu.popup()
                    }

                    AppToolButton {
                        text: "×"
                        onLightSurface: true
                        contentColor: Theme.textSecondary
                        enabled: tabs.length > 1
                        Layout.preferredWidth: 40
                        onClicked: closeCurrentTab()
                    }
                }

                Rectangle {
                    id: viewportArea
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.appBg

                    Connections {
                        target: activeController
                        function onViewChanged() {
                            topTrackCanvas.requestPaint()
                            leftTrackCanvas.requestPaint()
                            annotationCanvas.requestPaint()
                            guideCanvas.requestPaint()
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
                        }
                        function onColorMaxChanged() {
                        }
                        function onDisplayOptionsChanged() {
                            topTrackCanvas.requestPaint()
                            leftTrackCanvas.requestPaint()
                            annotationCanvas.requestPaint()
                            guideCanvas.requestPaint()
                        }
                    }

                    Menu {
                        id: plotTrackContextMenu
                        property int trackIndex: pendingTrackIndex
                        property var summary: ({})

                        MenuItem { text: plotTrackContextMenu.summary.name || "1D track"; enabled: false }
                        MenuSeparator {}
                        Menu {
                            title: "Windowing function"
                            enabled: plotTrackContextMenu.summary.renderMode === "signal"
                            MenuItem {
                                text: "Minimum"; checkable: true
                                checked: plotTrackContextMenu.summary.reduction === "min"
                                onTriggered: activeController.setTrackReduction(plotTrackContextMenu.trackIndex, "min")
                            }
                            MenuItem {
                                text: "Mean"; checkable: true
                                checked: plotTrackContextMenu.summary.reduction === "mean"
                                onTriggered: activeController.setTrackReduction(plotTrackContextMenu.trackIndex, "mean")
                            }
                            MenuItem {
                                text: "Maximum"; checkable: true
                                checked: plotTrackContextMenu.summary.reduction === "max"
                                onTriggered: activeController.setTrackReduction(plotTrackContextMenu.trackIndex, "max")
                            }
                            MenuItem {
                                text: "None"; checkable: true
                                checked: plotTrackContextMenu.summary.reduction === "none"
                                onTriggered: activeController.setTrackReduction(plotTrackContextMenu.trackIndex, "none")
                            }
                        }
                        Menu {
                            title: "Bin size"
                            enabled: plotTrackContextMenu.summary.renderMode === "signal"
                            MenuItem {
                                text: "Match Hi-C resolution (" + formatBp(activeController ? activeController.resolution : 0) + ")"
                                checkable: true
                                checked: plotTrackContextMenu.summary.binSize === 0
                                onTriggered: activeController.setTrackBinSize(plotTrackContextMenu.trackIndex, 0)
                            }
                            MenuSeparator {}
                            MenuItem { text: "1 kb"; checkable: true; checked: plotTrackContextMenu.summary.binSize === 1000; onTriggered: activeController.setTrackBinSize(plotTrackContextMenu.trackIndex, 1000) }
                            MenuItem { text: "5 kb"; checkable: true; checked: plotTrackContextMenu.summary.binSize === 5000; onTriggered: activeController.setTrackBinSize(plotTrackContextMenu.trackIndex, 5000) }
                            MenuItem { text: "10 kb"; checkable: true; checked: plotTrackContextMenu.summary.binSize === 10000; onTriggered: activeController.setTrackBinSize(plotTrackContextMenu.trackIndex, 10000) }
                            MenuItem { text: "25 kb"; checkable: true; checked: plotTrackContextMenu.summary.binSize === 25000; onTriggered: activeController.setTrackBinSize(plotTrackContextMenu.trackIndex, 25000) }
                            MenuItem { text: "50 kb"; checkable: true; checked: plotTrackContextMenu.summary.binSize === 50000; onTriggered: activeController.setTrackBinSize(plotTrackContextMenu.trackIndex, 50000) }
                            MenuItem { text: "100 kb"; checkable: true; checked: plotTrackContextMenu.summary.binSize === 100000; onTriggered: activeController.setTrackBinSize(plotTrackContextMenu.trackIndex, 100000) }
                            MenuItem { text: "250 kb"; checkable: true; checked: plotTrackContextMenu.summary.binSize === 250000; onTriggered: activeController.setTrackBinSize(plotTrackContextMenu.trackIndex, 250000) }
                            MenuItem { text: "500 kb"; checkable: true; checked: plotTrackContextMenu.summary.binSize === 500000; onTriggered: activeController.setTrackBinSize(plotTrackContextMenu.trackIndex, 500000) }
                            MenuItem { text: "1 Mb"; checkable: true; checked: plotTrackContextMenu.summary.binSize === 1000000; onTriggered: activeController.setTrackBinSize(plotTrackContextMenu.trackIndex, 1000000) }
                            MenuSeparator {}
                            MenuItem { text: "Custom…"; onTriggered: openTrackBinEditor(plotTrackContextMenu.trackIndex) }
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: "Autoscale"
                            enabled: plotTrackContextMenu.summary.renderMode === "signal"
                            checkable: true
                            checked: !!plotTrackContextMenu.summary.autoscale
                            onTriggered: activeController.setTrackAutoscale(plotTrackContextMenu.trackIndex, checked)
                        }
                        MenuItem {
                            text: "Set data range…"
                            enabled: plotTrackContextMenu.summary.renderMode === "signal"
                            onTriggered: openTrackRangeEditor(plotTrackContextMenu.trackIndex)
                        }
                        MenuItem {
                            text: "Log scale"
                            enabled: plotTrackContextMenu.summary.renderMode === "signal"
                            checkable: true
                            checked: !!plotTrackContextMenu.summary.logScale
                            onTriggered: activeController.setTrackRange(plotTrackContextMenu.trackIndex,
                                                                        plotTrackContextMenu.summary.min,
                                                                        plotTrackContextMenu.summary.max,
                                                                        checked)
                        }
                        MenuSeparator {}
                        MenuItem { text: "Change track color…"; onTriggered: chooseTrackColor(plotTrackContextMenu.trackIndex, false) }
                        MenuItem {
                            text: "Change negative-value color…"
                            enabled: plotTrackContextMenu.summary.renderMode === "signal"
                            onTriggered: chooseTrackColor(plotTrackContextMenu.trackIndex, true)
                        }
                        Menu {
                            title: "Track height"
                            MenuItem { text: "100 px"; onTriggered: activeController.setTrackHeight(plotTrackContextMenu.trackIndex, 100) }
                            MenuItem { text: "200 px"; onTriggered: activeController.setTrackHeight(plotTrackContextMenu.trackIndex, 200) }
                            MenuItem { text: "300 px"; onTriggered: activeController.setTrackHeight(plotTrackContextMenu.trackIndex, 300) }
                            MenuItem { text: "400 px"; onTriggered: activeController.setTrackHeight(plotTrackContextMenu.trackIndex, 400) }
                            MenuItem { text: "600 px"; onTriggered: activeController.setTrackHeight(plotTrackContextMenu.trackIndex, 600) }
                            MenuItem { text: "800 px"; onTriggered: activeController.setTrackHeight(plotTrackContextMenu.trackIndex, 800) }
                            MenuSeparator {}
                            MenuItem { text: "Custom…"; onTriggered: openTrackHeightEditor(plotTrackContextMenu.trackIndex) }
                        }
                        MenuSeparator {}
                        MenuItem {
                            text: "Move up"
                            enabled: plotTrackContextMenu.trackIndex > 0
                            onTriggered: activeController.moveTrack(plotTrackContextMenu.trackIndex, plotTrackContextMenu.trackIndex - 1)
                        }
                        MenuItem {
                            text: "Move down"
                            enabled: activeController && plotTrackContextMenu.trackIndex + 1 < activeController.trackCount
                            onTriggered: activeController.moveTrack(plotTrackContextMenu.trackIndex, plotTrackContextMenu.trackIndex + 1)
                        }
                        MenuItem { text: "Hide track"; onTriggered: activeController.setTrackVisible(plotTrackContextMenu.trackIndex, false) }
                        MenuItem { text: "Remove track"; onTriggered: activeController.removeTrack(plotTrackContextMenu.trackIndex) }
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
                            text: "Inspect live SIP bullseye"
                            enabled: !!activeController
                            onTriggered: showBullseyeInspector(activeController, contextFx, contextFy)
                        }
                        MenuItem {
                            text: "Open SIP bullseye tab"
                            enabled: !!activeController
                            onTriggered: openBullseyeAt(activeController, contextFx, contextFy)
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
                            enabled: activeController && activeController.chrX === activeController.chrY
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
                        visible: !activeTab || activeTab.type === "single"
                        anchors.centerIn: parent
                        anchors.horizontalCenterOffset: 0
                        anchors.verticalCenterOffset: 0
                        property real availableWidth: parent.width - 12
                        property real availableHeight: parent.height - 12
                        width: Math.max(240, landscapeMode ? availableWidth : Math.min(availableWidth, availableHeight))
                        height: Math.max(240, landscapeMode ? availableHeight : width)
                        onWidthChanged: aspectRefitTimer.restart()
                        onHeightChanged: aspectRefitTimer.restart()
                        function fitControllerToCanvas() {
                            if (activeController && heatmapHost.width > 0 && heatmapHost.height > 0)
                                activeController.fitViewToAspectRatio(heatmapHost.width / heatmapHost.height)
                        }
                        Timer {
                            id: aspectRefitTimer
                            interval: 80
                            repeat: false
                            onTriggered: plotFrame.fitControllerToCanvas()
                        }
                        property int axisSize: {
                            if (!activeController || !trackPanelsOpen) return 46
                            var extent = 38 + activeController.visibleTrackHeight
                            var available = Math.floor(Math.min(plotFrame.width, plotFrame.height) - 80)
                            return Math.max(46, Math.min(available, extent))
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
                                var segments = trackPanelsOpen ? activeController.visibleTrackSegmentsForPixels(true, Math.max(1, Math.ceil(width))) : []
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
                                var baselineDrawn = []
                                for (var i = 0; i < segments.length; i++) {
                                    var s = segments[i]
                                    var x0 = (s.start - activeController.x0) / span * width
                                    var x1 = (s.end - activeController.x0) / span * width
                                    var laneY = laneStart[s.trackIndex]
                                    var laneHeight = laneSize[s.trackIndex]
                                    if (s.kind === "feature") {
                                        var featureHeight = Math.max(3, Math.min(12, laneHeight * 0.55))
                                        var featureY = laneY + (laneHeight - featureHeight) * 0.5
                                        ctx.fillStyle = s.color
                                        ctx.fillRect(Math.max(0, x0), featureY, Math.max(1, Math.min(width, x1) - Math.max(0, x0)), featureHeight)
                                        continue
                                    }
                                    var range = Math.max(0.000001, s.max - s.min)
                                    var zero = laneY + laneHeight - (0 - s.min) / range * laneHeight
                                    var valueY = laneY + laneHeight - (s.value - s.min) / range * laneHeight
                                    zero = Math.max(laneY, Math.min(laneY + laneHeight, zero))
                                    valueY = Math.max(laneY, Math.min(laneY + laneHeight, valueY))
                                    if (!baselineDrawn[s.trackIndex]) {
                                        ctx.strokeStyle = Theme.borderStrong
                                        ctx.beginPath()
                                        ctx.moveTo(0, Math.round(zero) + 0.5)
                                        ctx.lineTo(width, Math.round(zero) + 0.5)
                                        ctx.stroke()
                                        baselineDrawn[s.trackIndex] = true
                                    }
                                    var barTop = Math.min(zero, valueY)
                                    var h = Math.max(1.5, Math.abs(valueY - zero))
                                    ctx.fillStyle = s.color
                                    ctx.fillRect(Math.max(0, x0), barTop,
                                                 Math.max(1, Math.min(width, x1) - Math.max(0, x0)), h)
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                cursorShape: Qt.PointingHandCursor
                                onPressed: function(mouse) {
                                    var trackTop = activeController && activeController.showChromosomeContext ? 10 : 2
                                    var trackBottom = parent.height - 18 - 0.5 - 3
                                    var index = trackIndexAtPanelPosition(mouse.y, trackTop, trackBottom, 8)
                                    if (index >= 0) {
                                        openPlotTrackMenu(index)
                                        mouse.accepted = true
                                    }
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
                                var segments = trackPanelsOpen ? activeController.visibleTrackSegmentsForPixels(false, Math.max(1, Math.ceil(height))) : []
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
                                var baselineDrawn = []
                                for (var i = 0; i < segments.length; i++) {
                                    var s = segments[i]
                                    var y0 = (s.start - activeController.y0) / span * height
                                    var y1 = (s.end - activeController.y0) / span * height
                                    var laneX = laneStart[s.trackIndex]
                                    var laneWidth = laneSize[s.trackIndex]
                                    if (s.kind === "feature") {
                                        var featureWidth = Math.max(3, Math.min(12, laneWidth * 0.55))
                                        var featureX = laneX + (laneWidth - featureWidth) * 0.5
                                        ctx.fillStyle = s.color
                                        ctx.fillRect(featureX, Math.max(0, y0), featureWidth, Math.max(1, Math.min(height, y1) - Math.max(0, y0)))
                                        continue
                                    }
                                    var range = Math.max(0.000001, s.max - s.min)
                                    var zero = laneX + (0 - s.min) / range * laneWidth
                                    var valueX = laneX + (s.value - s.min) / range * laneWidth
                                    zero = Math.max(laneX, Math.min(laneX + laneWidth, zero))
                                    valueX = Math.max(laneX, Math.min(laneX + laneWidth, valueX))
                                    if (!baselineDrawn[s.trackIndex]) {
                                        ctx.strokeStyle = Theme.borderStrong
                                        ctx.beginPath()
                                        ctx.moveTo(Math.round(zero) + 0.5, 0)
                                        ctx.lineTo(Math.round(zero) + 0.5, height)
                                        ctx.stroke()
                                        baselineDrawn[s.trackIndex] = true
                                    }
                                    var barLeft = Math.min(zero, valueX)
                                    var w = Math.max(1.5, Math.abs(valueX - zero))
                                    ctx.fillStyle = s.color
                                    ctx.fillRect(barLeft, Math.max(0, y0), w,
                                                 Math.max(1, Math.min(height, y1) - Math.max(0, y0)))
                                }
                            }
                            MouseArea {
                                anchors.fill: parent
                                acceptedButtons: Qt.RightButton
                                cursorShape: Qt.PointingHandCursor
                                onPressed: function(mouse) {
                                    var trackLeft = activeController && activeController.showChromosomeContext ? 10 : 2
                                    var trackRight = parent.width - 0.5 - 42 - 2
                                    var index = trackIndexAtPanelPosition(mouse.x, trackLeft, trackRight, 7)
                                    if (index >= 0) {
                                        openPlotTrackMenu(index)
                                        mouse.accepted = true
                                    }
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
                                    if (activeController.showGridlines) {
                                        ctx.strokeStyle = Theme.gridline
                                        ctx.lineWidth = 1
                                        var gridSteps = 10
                                        for (var g = 1; g < gridSteps; g++) {
                                            var gx = width * g / gridSteps
                                            var gy = height * g / gridSteps
                                            ctx.beginPath()
                                            ctx.moveTo(gx, 0)
                                            ctx.lineTo(gx, height)
                                            ctx.moveTo(0, gy)
                                            ctx.lineTo(width, gy)
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
                                            ctx.moveTo(bx, 0)
                                            ctx.lineTo(bx, height)
                                            ctx.moveTo(0, by)
                                            ctx.lineTo(width, by)
                                            ctx.stroke()
                                        }
                                    }
                                    if (activeController.showTilesDebug) {
                                        ctx.strokeStyle = Theme.tileDebugLine
                                        ctx.lineWidth = 1
                                        var tileCount = 8
                                        for (var tt = 0; tt <= tileCount; tt++) {
                                            var tx = width * tt / tileCount
                                            var ty = height * tt / tileCount
                                            ctx.beginPath()
                                            ctx.moveTo(tx, 0)
                                            ctx.lineTo(tx, height)
                                            ctx.moveTo(0, ty)
                                            ctx.lineTo(width, ty)
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
                                    ctx.strokeStyle = "#000000"
                                    ctx.lineWidth = 1.25
                                    ctx.setLineDash([5, 4])
                                    if (straightEdgeEnabled) {
                                        ctx.beginPath()
                                        ctx.moveTo(hoverPlotX, 0)
                                        ctx.lineTo(hoverPlotX, height)
                                        ctx.moveTo(0, hoverPlotY)
                                        ctx.lineTo(width, hoverPlotY)
                                        ctx.stroke()
                                    }
                                    if (diagonalEdgeEnabled && activeController && activeController.chrX === activeController.chrY) {
                                        var spanX = Math.max(1, activeController.x1 - activeController.x0)
                                        var spanY = Math.max(1, activeController.y1 - activeController.y0)
                                        var genomeX = activeController.x0 + hoverPlotX / Math.max(1, width) * spanX
                                        var genomeY = activeController.y0 + hoverPlotY / Math.max(1, height) * spanY
                                        var scaleY = height / spanY
                                        var delta = genomeY - genomeX
                                        var reflectedDelta = -delta
                                        var sum = genomeX + genomeY
                                        ctx.beginPath()
                                        // y = x + delta, then its true reflection y = x - delta
                                        // across the genomic Hi-C diagonal y = x.
                                        ctx.moveTo(0, (activeController.x0 + delta - activeController.y0) * scaleY)
                                        ctx.lineTo(width, (activeController.x1 + delta - activeController.y0) * scaleY)
                                        ctx.moveTo(0, (activeController.x0 + reflectedDelta - activeController.y0) * scaleY)
                                        ctx.lineTo(width, (activeController.x1 + reflectedDelta - activeController.y0) * scaleY)
                                        // The perpendicular through the cursor and reflected cursor.
                                        ctx.moveTo(0, (sum - activeController.x0 - activeController.y0) * scaleY)
                                        ctx.lineTo(width, (sum - activeController.x1 - activeController.y0) * scaleY)
                                        ctx.stroke()
                                    }
                                }
                            }

                            Rectangle {
                                id: colorLegend
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
                                MouseArea {
                                    id: legendMouseArea
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    Accessible.name: "Open heatmap color settings"
                                    onClicked: function(mouse) {
                                        colorScaleDialog.open()
                                        mouse.accepted = true
                                    }
                                }
                                ToolTip.visible: legendMouseArea.containsMouse
                                ToolTip.text: "Change color range and map"
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
                                    return clamp01(px / Math.max(1, width))
                                }

                                function fractionY(py) {
                                    return clamp01(py / Math.max(1, height))
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
                                    updateBullseyeInspector(activeController, contextFx, contextFy)
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
                                visible: hoverTextVisible && hoverActive && hoverText.length > 0
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
                        id: minimapFrame
                        z: 80
                        visible: (!activeTab || activeTab.type === "single") && minimapVisible && activeController && activeController.filePath.length > 0 &&
                                 activeController.xChromosomeLength > 0 && activeController.yChromosomeLength > 0
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.topMargin: 12
                        anchors.rightMargin: 12
                        width: 196
                        height: 220
                        radius: Theme.radiusMd
                        color: Theme.surfaceAlt
                        border.color: Theme.borderStrong

                        ColumnLayout {
                            anchors.fill: parent
                            anchors.margins: 8
                            spacing: 5
                            RowLayout {
                                Layout.fillWidth: true
                                Label {
                                    text: activeController ? activeController.chrX + " × " + activeController.chrY : "Minimap"
                                    color: Theme.textPrimary
                                    font.pixelSize: Theme.textXs
                                    font.weight: Font.DemiBold
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                                AppToolButton {
                                    text: "×"
                                    onLightSurface: true
                                    Layout.preferredWidth: 24
                                    Layout.preferredHeight: 24
                                    Accessible.name: "Hide minimap"
                                    onClicked: minimapVisible = false
                                }
                            }
                            Rectangle {
                                id: minimapMap
                                Layout.fillWidth: true
                                Layout.fillHeight: true
                                color: "#ffffff"
                                border.color: Theme.borderStrong
                                clip: true

                                HicHeatmapItem {
                                    anchors.fill: parent
                                    controller: activeController
                                    overviewMode: true
                                }

                                Canvas {
                                    anchors.fill: parent
                                    visible: activeController && activeController.chrX === activeController.chrY
                                    onPaint: {
                                        var ctx = getContext("2d")
                                        ctx.reset()
                                        ctx.strokeStyle = Theme.boundaryLine
                                        ctx.lineWidth = 1
                                        ctx.beginPath()
                                        ctx.moveTo(0, 0)
                                        ctx.lineTo(width, height)
                                        ctx.stroke()
                                    }
                                }

                                Rectangle {
                                    id: minimapViewportBox
                                    visible: !!activeController
                                    x: activeController ? Math.min(parent.width - width,
                                           Math.max(0, activeController.x0 / Math.max(1, activeController.xChromosomeLength) * parent.width)) : 0
                                    y: activeController ? Math.min(parent.height - height,
                                           Math.max(0, activeController.y0 / Math.max(1, activeController.yChromosomeLength) * parent.height)) : 0
                                    width: activeController ? Math.max(8, (activeController.x1 - activeController.x0) /
                                           Math.max(1, activeController.xChromosomeLength) * parent.width) : 8
                                    height: activeController ? Math.max(8, (activeController.y1 - activeController.y0) /
                                            Math.max(1, activeController.yChromosomeLength) * parent.height) : 8
                                    color: Qt.rgba(0.45, 0.52, 1.0, 0.16)
                                    border.color: Theme.accent
                                    border.width: 2
                                }

                                MouseArea {
                                    anchors.fill: parent
                                    acceptedButtons: Qt.LeftButton
                                    cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                    property real dragOffsetX: minimapViewportBox.width / 2
                                    property real dragOffsetY: minimapViewportBox.height / 2

                                    function moveViewport(mouse) {
                                        if (!activeController) return
                                        var centerX = (mouse.x - dragOffsetX + minimapViewportBox.width / 2) / Math.max(1, width)
                                        var centerY = (mouse.y - dragOffsetY + minimapViewportBox.height / 2) / Math.max(1, height)
                                        activeController.centerViewAtFractions(centerX, centerY)
                                    }

                                    onPressed: function(mouse) {
                                        var inside = mouse.x >= minimapViewportBox.x && mouse.x <= minimapViewportBox.x + minimapViewportBox.width &&
                                                     mouse.y >= minimapViewportBox.y && mouse.y <= minimapViewportBox.y + minimapViewportBox.height
                                        dragOffsetX = inside ? mouse.x - minimapViewportBox.x : minimapViewportBox.width / 2
                                        dragOffsetY = inside ? mouse.y - minimapViewportBox.y : minimapViewportBox.height / 2
                                        if (activeController) activeController.beginInteraction()
                                        moveViewport(mouse)
                                    }
                                    onPositionChanged: function(mouse) {
                                        if (mouse.buttons & Qt.LeftButton) moveViewport(mouse)
                                    }
                                    onReleased: if (activeController) activeController.endInteraction()
                                    onCanceled: if (activeController) activeController.endInteraction()
                                }
                            }
                            Label {
                                Layout.fillWidth: true
                                text: "Drag the outlined viewport to pan"
                                color: Theme.textMuted
                                font.pixelSize: Theme.textXs
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }
                    }

                    TabWorkspaceView {
                        z: 100
                        anchors.fill: parent
                        visible: activeTab && activeTab.type !== "single"
                        tabSession: activeTab
                        onLoadRegionsRequested: function(format) {
                            regionDialog.regionFormat = format
                            regionDialog.open()
                        }
                        onHoverInfo: function(text, active) {
                            hoverText = text
                            hoverActive = active
                        }
                        onToastRequested: function(text, kind) { showToast(text, kind) }
                        onContextMenuRequested: function(controller, xFraction, yFraction) {
                            activeController = controller
                            contextFx = xFraction
                            contextFy = yFraction
                            heatmapMenu.popup()
                        }
                        onBullseyeHover: function(controller, xFraction, yFraction) {
                            updateBullseyeInspector(controller, xFraction, yFraction)
                        }
                    }

                }
            }
        }

    }

    Popup {
        id: bullseyeInspector
        parent: Overlay.overlay
        x: Math.max(12, window.width - width - 22)
        y: 86
        width: Math.min(720, window.width * 0.48)
        height: Math.min(520, window.height - 130)
        modal: false
        dim: false
        padding: 0
        closePolicy: Popup.CloseOnEscape
        onClosed: {
            for (var i = 0; i < bullseyeInspectorCells.length; ++i)
                if (bullseyeInspectorCells[i].controller &&
                        (!activeTab || (activeTab.type !== "bullseye" && activeTab.type !== "rotated-45")))
                    bullseyeInspectorCells[i].controller.setAnalysisPaddingBins(0)
        }
        background: Rectangle {
            color: Theme.surface
            border.color: Theme.borderStrong
            border.width: 1
            radius: Theme.radiusMd
        }
        contentItem: ColumnLayout {
            spacing: 0
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                color: Theme.surfaceAlt
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 8; spacing: 8
                    Label { text: "Live SIP bullseye"; color: Theme.textPrimary; font.weight: Font.DemiBold }
                    Label {
                        text: bullseyeInspectorSource
                            ? bullseyeInspectorSource.chrX + ":" + bullseyeInspectorX + " × " +
                              bullseyeInspectorSource.chrY + ":" + bullseyeInspectorY
                            : ""
                        color: Theme.textMuted; font.pixelSize: Theme.textXs; Layout.fillWidth: true; elide: Text.ElideMiddle
                    }
                    Label { text: "Bins"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                    SpinBox {
                        from: 1; to: 100
                        value: bullseyeInspectorRadiusBins
                        onValueModified: setBullseyeInspectorRadius(value)
                        Layout.preferredWidth: 84
                    }
                    Label { text: "bp"; color: Theme.textSecondary; font.pixelSize: Theme.textXs }
                    AppTextField {
                        text: String(bullseyeInspectorRadiusBins *
                                     Math.max(1, bullseyeInspectorSource ? bullseyeInspectorSource.resolution : 1))
                        validator: DoubleValidator { bottom: 1; decimals: 0 }
                        onEditingFinished: {
                            var resolution = Math.max(1, bullseyeInspectorSource ? bullseyeInspectorSource.resolution : 1)
                            setBullseyeInspectorRadius(Number(text) / resolution)
                        }
                        Layout.preferredWidth: 96
                    }
                    AppButton {
                        text: "Pin to tab"
                        tonal: true
                        onClicked: {
                            var source = bullseyeInspectorSource
                            var fx = source ? (bullseyeInspectorX - source.x0) / Math.max(1, source.x1 - source.x0) : 0.5
                            var fy = source ? (bullseyeInspectorY - source.y0) / Math.max(1, source.y1 - source.y0) : 0.5
                            bullseyeInspector.close()
                            openBullseyeAt(source, fx, fy)
                            if (activeTab) {
                                activeTab.bullseyeRadiusBins = bullseyeInspectorRadiusBins
                                activeTab.bullseyePinned = true
                            }
                        }
                    }
                    AppToolButton { text: "×"; onClicked: bullseyeInspector.close() }
                }
            }
            ScrollView {
                Layout.fillWidth: true; Layout.fillHeight: true
                clip: true
                contentHeight: availableHeight
                Row {
                    height: parent.height
                    spacing: 8
                    padding: 8
                    Repeater {
                        model: bullseyeInspectorCells
                        Rectangle {
                            required property var modelData
                            width: Math.max(260, (bullseyeInspector.width - 32) /
                                            Math.min(2, Math.max(1, bullseyeInspectorCells.length)))
                            height: parent.height - 16
                            color: "white"
                            border.color: Theme.border
                            ColumnLayout {
                                anchors.fill: parent; spacing: 0
                                Label {
                                    Layout.fillWidth: true; Layout.preferredHeight: 28
                                    text: modelData.label
                                    color: Theme.textPrimary
                                    horizontalAlignment: Text.AlignHCenter
                                    verticalAlignment: Text.AlignVCenter
                                    elide: Text.ElideMiddle
                                }
                                BullseyeItem {
                                    Layout.fillWidth: true; Layout.fillHeight: true
                                    controller: modelData.controller
                                    centerX: bullseyeInspectorX
                                    centerY: bullseyeInspectorY
                                    radiusBins: bullseyeInspectorRadiusBins
                                }
                            }
                        }
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
