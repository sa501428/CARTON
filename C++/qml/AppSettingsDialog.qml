pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Carton

Dialog {
    id: root
    objectName: "applicationSettings"
    property var applicationWindow: null
    property var controller: null
    title: "Application settings"
    modal: true
    width: Math.min(560, applicationWindow ? applicationWindow.width - 32 : 560)
    height: Math.min(460, applicationWindow ? applicationWindow.height - 60 : 460)
    standardButtons: Dialog.Close
    background: DialogFrame {}
    ColumnLayout {
        anchors.fill: parent
        TabBar {
            id: settingsTabs
            objectName: "settingsTabs"
            Layout.fillWidth: true
            AppTabButton {
                text: "Appearance"
            }
            AppTabButton {
                text: "Performance"
            }
            AppTabButton {
                text: "Developer"
            }
        }
        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: settingsTabs.currentIndex
            ScrollView {
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.width
                    spacing: 12
                    AppCheckBox {
                        text: "Dark appearance"
                        checked: root.controller && root.controller.darkMode
                        onToggled: if (root.applicationWindow)
                            root.applicationWindow.changeDarkMode(checked)
                    }
                    Label {
                        text: "Interface size"
                        color: Theme.textSecondary
                    }
                    RowLayout {
                        Slider {
                            Layout.fillWidth: true
                            from: 0.85
                            to: 1.35
                            stepSize: 0.05
                            value: root.applicationWindow ? root.applicationWindow.interfaceScale : 1
                            onMoved: root.applicationWindow.interfaceScale = value
                            Accessible.name: "Interface size"
                        }
                        Label {
                            text: root.applicationWindow ? Math.round(root.applicationWindow.interfaceScale * 100) + "%" : "100%"
                            color: Theme.textPrimary
                        }
                    }
                    Label {
                        text: "Text size"
                        color: Theme.textSecondary
                    }
                    RowLayout {
                        Slider {
                            Layout.fillWidth: true
                            from: 0.85
                            to: 1.4
                            stepSize: 0.05
                            value: root.applicationWindow ? root.applicationWindow.fontScale : 1
                            onMoved: root.applicationWindow.fontScale = value
                            Accessible.name: "Text size"
                        }
                        Label {
                            text: root.applicationWindow ? Math.round(root.applicationWindow.fontScale * 100) + "%" : "100%"
                            color: Theme.textPrimary
                        }
                    }
                    AppCheckBox {
                        text: "Reduce motion"
                        checked: root.applicationWindow && root.applicationWindow.reducedMotion
                        onToggled: if (root.applicationWindow)
                            root.applicationWindow.reducedMotion = checked
                    }
                    AppButton {
                        text: "Reset interface sizes"
                        onClicked: {
                            root.applicationWindow.interfaceScale = 1;
                            root.applicationWindow.fontScale = 1;
                        }
                    }
                    Item {
                        Layout.fillHeight: true
                    }
                }
            }
            ScrollView {
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.width
                    Label {
                        text: "Shared matrix cache"
                        color: Theme.textPrimary
                        font.weight: Font.DemiBold
                    }
                    RowLayout {
                        SpinBox {
                            from: 16
                            to: 4096
                            stepSize: 16
                            editable: true
                            value: DatasetRegistry.cacheLimitMB
                            onValueModified: DatasetRegistry.cacheLimitMB = value
                            Accessible.name: "Shared matrix cache in megabytes"
                        }
                        Label {
                            text: "MB"
                            color: Theme.textSecondary
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: "This cache is shared by every map and tab. Parsed tracks are kept separately for this workspace."
                        color: Theme.textSecondary
                    }
                    Label {
                        text: "Currently cached: " + DatasetRegistry.cacheMemoryMB.toFixed(1) + " MB"
                        color: Theme.textSecondary
                    }
                    Item {
                        Layout.fillHeight: true
                    }
                }
            }
            ScrollView {
                contentWidth: availableWidth
                clip: true
                ColumnLayout {
                    width: parent.width
                    AppCheckBox {
                        text: "Show matrix tile boundaries"
                        checked: root.controller && root.controller.showTilesDebug
                        onToggled: if (root.controller)
                            root.controller.showTilesDebug = checked
                    }
                    Label {
                        Layout.fillWidth: true
                        wrapMode: Text.WordWrap
                        text: root.controller ? "Rendering backend: " + root.controller.renderingBackend : "No map selected"
                        color: Theme.textSecondary
                    }
                    Item {
                        Layout.fillHeight: true
                    }
                }
            }
        }
    }
}
