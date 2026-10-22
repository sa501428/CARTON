pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

ColumnLayout {
    id: root
    property var controller: null
    property var options: []
    property var normalizations: []
    property var families: []
    property string selectedFamily: "Contacts"
    property string synchronizedMatrixType: ""
    readonly property var familyOptions: options.filter(function (option) {
        return option.group === selectedFamily;
    })
    function syncModels() {
        options = controller ? controller.matrixTypeOptions() : [];
        normalizations = controller ? controller.normalizations() : [];
        var groups = [];
        for (var i = 0; i < options.length; ++i) {
            if (groups.indexOf(options[i].group) < 0)
                groups.push(options[i].group);
            if (controller && controller.matrixType !== synchronizedMatrixType && options[i].id === controller.matrixType)
                selectedFamily = options[i].group;
        }
        synchronizedMatrixType = controller ? controller.matrixType : "";
        families = groups;
        if (families.indexOf(selectedFamily) < 0 && families.length)
            selectedFamily = families[0];
    }
    onControllerChanged: {
        synchronizedMatrixType = "";
        syncModels();
    }
    Component.onCompleted: syncModels()
    Connections {
        target: root.controller
        function onMetadataChanged() {
            root.syncModels();
        }
        function onControlReadyChanged() {
            root.syncModels();
        }
        function onViewChanged() {
            if (root.controller && root.controller.matrixType !== root.synchronizedMatrixType)
                root.syncModels();
        }
    }
    spacing: 8
    Label {
        text: "Data calculation"
        color: Theme.textPrimary
        font.weight: Font.DemiBold
    }
    Label {
        text: "Calculation family"
        color: Theme.textSecondary
        font.pixelSize: Theme.textSm
    }
    AppComboBox {
        objectName: "calculationFamily"
        Layout.fillWidth: true
        model: root.families
        currentIndex: Math.max(0, root.families.indexOf(root.selectedFamily))
        onActivated: root.selectedFamily = currentText
        Accessible.name: "Calculation family"
    }
    Label {
        text: "Calculation"
        color: Theme.textSecondary
        font.pixelSize: Theme.textSm
    }
    AppComboBox {
        objectName: "matrixCalculation"
        Layout.fillWidth: true
        model: root.familyOptions
        textRole: "label"
        valueRole: "id"
        currentIndex: {
            for (var i = 0; i < root.familyOptions.length; ++i)
                if (root.controller && root.familyOptions[i].id === root.controller.matrixType)
                    return i;
            return -1;
        }
        displayText: currentIndex < 0 ? "Choose a calculation…" : currentText
        onActivated: if (root.controller)
            root.controller.matrixType = currentValue
        Accessible.name: "Matrix calculation"
    }
    Label {
        Layout.fillWidth: true
        wrapMode: Text.WordWrap
        visible: root.controller && !root.controller.controlReady
        text: "Load a control map in Data to enable comparisons."
        color: Theme.textSecondary
        font.pixelSize: Theme.textSm
    }
    Label {
        text: "Normalization"
        color: Theme.textSecondary
        font.pixelSize: Theme.textSm
    }
    AppComboBox {
        Layout.fillWidth: true
        model: root.normalizations
        currentIndex: root.controller ? Math.max(0, model.indexOf(root.controller.norm)) : -1
        onActivated: if (root.controller)
            root.controller.norm = currentText
        Accessible.name: "Normalization"
    }
}
