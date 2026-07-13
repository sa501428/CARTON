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

    FileDialog {
        id: trackDialog
        title: "Load 1D Track"
        nameFilters: ["Track files (*.bed *.bedgraph *.wig *.txt *.tsv)", "All files (*)"]
        onAccepted: if (activeController) activeController.loadTrack(selectedFile)
    }

    FileDialog {
        id: annotationDialog
        title: "Load 2D Annotations"
        nameFilters: ["Annotation files (*.bedpe *.txt *.tsv)", "All files (*)"]
        onAccepted: if (activeController) activeController.loadAnnotations(selectedFile)
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
            MenuItem { text: "Open Hi-C..."; onTriggered: openDialog.open() }
            MenuItem { text: "Load 1D Track..."; onTriggered: trackDialog.open() }
            MenuItem { text: "Load 2D Annotations..."; onTriggered: annotationDialog.open() }
            MenuSeparator {}
            MenuItem { text: "New Tab"; onTriggered: addTab() }
            MenuItem { text: "Close Tab"; enabled: controllers.length > 1; onTriggered: closeCurrentTab() }
        }
        Menu {
            title: "Navigate"
            MenuItem { text: "Undo Zoom"; enabled: activeController && activeController.canUndoView; onTriggered: activeController.undoView() }
            MenuItem { text: "Redo Zoom"; enabled: activeController && activeController.canRedoView; onTriggered: activeController.redoView() }
            MenuItem { text: "Reset View"; enabled: activeController && activeController.filePath.length > 0; onTriggered: activeController.resetView() }
            MenuItem { text: "Jump to Diagonal"; enabled: activeController && activeController.chrX === activeController.chrY; onTriggered: activeController.jumpToDiagonal(contextFx, contextFy) }
            MenuItem { text: "Copy Current Position"; enabled: !!activeController; onTriggered: activeController.copyPosition(contextFx, contextFy) }
        }
        Menu {
            title: "Display"
            MenuItem { text: "White-Red"; checkable: true; checked: activeController && activeController.colorMap === "White-Red"; onTriggered: if (activeController) activeController.colorMap = "White-Red" }
            MenuItem { text: "Viridis"; checkable: true; checked: activeController && activeController.colorMap === "Viridis"; onTriggered: if (activeController) activeController.colorMap = "Viridis" }
            MenuItem { text: "Blue-White-Red"; checkable: true; checked: activeController && activeController.colorMap === "Blue-White-Red"; onTriggered: if (activeController) activeController.colorMap = "Blue-White-Red" }
            MenuItem { text: "Grayscale"; checkable: true; checked: activeController && activeController.colorMap === "Grayscale"; onTriggered: if (activeController) activeController.colorMap = "Grayscale" }
        }
        Menu {
            title: "Layers"
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

                        Canvas {
                            id: topTrackCanvas
                            x: 42
                            y: 0
                            width: plotFrame.width - 42
                            height: 42
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = "#f7f8fa"
                                ctx.fillRect(0, 0, width, height)
                                if (!activeController) return
                                var segments = activeController.visibleTrackSegments(true)
                                var span = Math.max(1, activeController.x1 - activeController.x0)
                                ctx.strokeStyle = "#8b949e"
                                ctx.beginPath()
                                ctx.moveTo(0, height - 0.5)
                                ctx.lineTo(width, height - 0.5)
                                ctx.stroke()
                                for (var i = 0; i < segments.length; i++) {
                                    var s = segments[i]
                                    var x0 = (s.start - activeController.x0) / span * width
                                    var x1 = (s.end - activeController.x0) / span * width
                                    var h = Math.max(2, Math.min(height - 6, Math.abs(s.value) * 8))
                                    ctx.fillStyle = s.color
                                    ctx.fillRect(Math.max(0, x0), height - h - 2, Math.max(1, x1 - x0), h)
                                }
                            }
                        }

                        Canvas {
                            id: leftTrackCanvas
                            x: 0
                            y: 42
                            width: 42
                            height: plotFrame.height - 42
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.reset()
                                ctx.fillStyle = "#f7f8fa"
                                ctx.fillRect(0, 0, width, height)
                                if (!activeController) return
                                var segments = activeController.visibleTrackSegments(false)
                                var span = Math.max(1, activeController.y1 - activeController.y0)
                                ctx.strokeStyle = "#8b949e"
                                ctx.beginPath()
                                ctx.moveTo(width - 0.5, 0)
                                ctx.lineTo(width - 0.5, height)
                                ctx.stroke()
                                for (var i = 0; i < segments.length; i++) {
                                    var s = segments[i]
                                    var y0 = (s.start - activeController.y0) / span * height
                                    var y1 = (s.end - activeController.y0) / span * height
                                    var w = Math.max(2, Math.min(width - 6, Math.abs(s.value) * 8))
                                    ctx.fillStyle = s.color
                                    ctx.fillRect(width - w - 2, Math.max(0, y0), w, Math.max(1, y1 - y0))
                                }
                            }
                        }

                        Item {
                            id: heatmapHost
                            x: 42
                            y: 42
                            width: plotFrame.width - 42
                            height: plotFrame.height - 42
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
                                    var extentW = spanX * scale
                                    var extentH = spanY * scale
                                    var ox = (width - extentW) * 0.5
                                    var oy = (height - extentH) * 0.5
                                    ctx.lineWidth = 1.5
                                    for (var i = 0; i < annotations.length; i++) {
                                        var a = annotations[i]
                                        var x = ox + (a.x0 - activeController.x0) * scale
                                        var y = oy + (a.y0 - activeController.y0) * scale
                                        var w = Math.max(2, (a.x1 - a.x0) * scale)
                                        var h = Math.max(2, (a.y1 - a.y0) * scale)
                                        ctx.strokeStyle = a.color
                                        ctx.strokeRect(x, y, w, h)
                                        if (w > 7 && h > 7)
                                            ctx.strokeRect(x + 1, y + 1, w - 2, h - 2)
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

                                function clamp01(v) {
                                    return Math.max(0, Math.min(1, v))
                                }

                                function fractionX(px) {
                                    var spanX = Math.max(1, activeController ? activeController.x1 - activeController.x0 : 1)
                                    var spanY = Math.max(1, activeController ? activeController.y1 - activeController.y0 : 1)
                                    var side = Math.min(width, height)
                                    var scale = side / Math.max(spanX, spanY)
                                    var extentW = spanX * scale
                                    var ox = (width - extentW) * 0.5
                                    return clamp01((px - ox) / Math.max(1, extentW))
                                }

                                function fractionY(py) {
                                    var spanX = Math.max(1, activeController ? activeController.x1 - activeController.x0 : 1)
                                    var spanY = Math.max(1, activeController ? activeController.y1 - activeController.y0 : 1)
                                    var side = Math.min(width, height)
                                    var scale = side / Math.max(spanX, spanY)
                                    var extentH = spanY * scale
                                    var oy = (height - extentH) * 0.5
                                    return clamp01((py - oy) / Math.max(1, extentH))
                                }

                                function updateHover(mouse) {
                                    if (!activeController) {
                                        hoverText = ""
                                        return
                                    }
                                    contextFx = fractionX(mouse.x)
                                    contextFy = fractionY(mouse.y)
                                    hoverText = activeController.positionText(contextFx, contextFy)
                                }

                                onWheel: function(wheel) {
                                    if (!activeController) return
                                    contextFx = fractionX(wheel.x)
                                    contextFy = fractionY(wheel.y)
                                    activeController.zoom(wheel.angleDelta.y > 0 ? 1.35 : 0.74, contextFx, contextFy)
                                    wheel.accepted = true
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
                                    selecting = (mouse.modifiers & Qt.ShiftModifier) !== 0
                                    if (selecting) {
                                        selectionRect.x = mouse.x
                                        selectionRect.y = mouse.y
                                        selectionRect.width = 0
                                        selectionRect.height = 0
                                        selectionRect.visible = true
                                    } else {
                                        activeController.beginInteraction()
                                    }
                                }

                                onPositionChanged: function(mouse) {
                                    updateHover(mouse)
                                    if (!activeController || !(mouse.buttons & Qt.LeftButton))
                                        return
                                    if (selecting) {
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
                                    if (selecting && mouse.button === Qt.LeftButton) {
                                        selectionRect.visible = false
                                        if (Math.abs(mouse.x - startX) > 8 && Math.abs(mouse.y - startY) > 8) {
                                            activeController.zoomToFractions(fractionX(startX), fractionY(startY),
                                                                            fractionX(mouse.x), fractionY(mouse.y))
                                        }
                                        selecting = false
                                    } else if (mouse.button === Qt.LeftButton) {
                                        activeController.endInteraction()
                                    }
                                }

                                onCanceled: {
                                    selectionRect.visible = false
                                    selecting = false
                                    if (activeController)
                                        activeController.endInteraction()
                                }

                                onExited: hoverText = ""
                                cursorShape: selecting ? Qt.CrossCursor : Qt.OpenHandCursor
                                preventStealing: true
                                propagateComposedEvents: false
                                onDoubleClicked: function(mouse) {
                                    if (!activeController) return
                                    activeController.zoom(2.0, fractionX(mouse.x), fractionY(mouse.y))
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
                    to: Math.max(500, activeController ? activeController.colorMax : 500)
                    value: activeController ? activeController.colorMax : 50
                    onMoved: if (activeController) activeController.colorMax = value
                }

                RowLayout {
                    Layout.fillWidth: true
                    Label {
                        text: activeController && activeController.colorMaxAuto ? "Max Auto" : "Max"
                        color: "#5b6672"
                    }
                    TextField {
                        id: colorMaxField
                        Layout.fillWidth: true
                        text: activeController ? activeController.colorMax.toString() : ""
                        selectByMouse: true
                        validator: DoubleValidator {
                            bottom: 0.000001
                            notation: DoubleValidator.ScientificNotation
                        }
                        onAccepted: {
                            if (activeController && Number(text) > 0)
                                activeController.colorMax = Number(text)
                        }
                        onEditingFinished: {
                            if (activeController && Number(text) > 0)
                                activeController.colorMax = Number(text)
                        }
                    }
                    Button {
                        text: "Auto"
                        enabled: activeController && !activeController.colorMaxAuto
                        onClicked: activeController.resetColorScale()
                    }
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
