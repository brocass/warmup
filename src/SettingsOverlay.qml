import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Helper connection settings and the background service. Prefilled from the
// saved settings each time it opens; Save applies and reconnects. The service
// switch applies immediately (it is a systemd action, not a saved form field).
Item {
    id: root
    signal closed

    property var qualityValues: ["auto", "fast", "best", "both"]

    function refill() {
        helperField.text = backend.helperUrl
        keyField.text = backend.accessKey
        rootField.text = backend.musicRoot
        qualityBox.currentIndex = Math.max(0, qualityValues.indexOf(backend.quality))
        backend.refreshServiceStatus()
    }

    // Reads translator.language so the status re-renders on a language switch.
    function serviceText() {
        translator.language
        switch (backend.serviceState) {
        case "running": return qsTr("Running in the background")
        case "enabled": return qsTr("Enabled — starts when you close Warmup")
        case "failed": return qsTr("The service stopped with an error")
        case "notInstalled": return qsTr("The service file isn't installed")
        case "unavailable": return qsTr("systemd user services aren't available")
        default: return qsTr("Off — updates only while Warmup is open")
        }
    }

    function serviceColor() {
        switch (backend.serviceState) {
        case "running": return "#4caf50"
        case "enabled": return theme.accent
        case "failed":
        case "notInstalled":
        case "unavailable": return "#e53935"
        default: return Qt.rgba(1, 1, 1, 0.35)
        }
    }

    onVisibleChanged: if (visible) {
        refill()
        helperField.forceActiveFocus()
    }

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.82)

        MouseArea {
            anchors.fill: parent
            onClicked: root.closed()
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(520, parent.width - 64)
            height: Math.min(column.implicitHeight + saveButton.implicitHeight + 60, parent.height - 32)
            radius: theme.radius
            color: "#1a1a1e"
            border.color: Qt.rgba(1, 1, 1, 0.12)

            // Clicks on the panel itself must not close the overlay.
            MouseArea {
                anchors.fill: parent
                onClicked: { }
            }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 24
                spacing: 12

                Flickable {
                    id: flick
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    contentWidth: column.width
                    contentHeight: column.implicitHeight
                    clip: true

                    ColumnLayout {
                        id: column
                        width: flick.width
                        spacing: 12

                        Label {
                            text: qsTr("Settings")
                            font.pixelSize: 17
                            font.bold: true
                            Layout.bottomMargin: 4
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 1
                            Layout.topMargin: 6
                            color: Qt.rgba(1, 1, 1, 0.08)
                            visible: backend.serviceState !== "disabled"
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 12
                            visible: backend.serviceState !== "disabled"

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label {
                                    text: qsTr("Run in the background")
                                    font.pixelSize: 14
                                    font.bold: true
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: qsTr("Keep playlists and instrumentals up to date while this window is closed")
                                    font.pixelSize: 11
                                    opacity: 0.5
                                    wrapMode: Text.Wrap
                                }
                            }

                            Switch {
                                id: serviceSwitch
                                enabled: backend.serviceState !== "notInstalled"
                                         && backend.serviceState !== "unavailable"
                                checked: backend.serviceEnabled
                                onToggled: {
                                    backend.applyServiceEnabled(checked)
                                    checked = Qt.binding(function () { return backend.serviceEnabled })
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 6
                            visible: backend.serviceState !== "disabled"

                            Rectangle {
                                Layout.preferredWidth: 8
                                Layout.preferredHeight: 8
                                radius: 4
                                color: root.serviceColor()
                            }
                            Label {
                                Layout.fillWidth: true
                                text: root.serviceText()
                                font.pixelSize: 11
                                opacity: 0.65
                                wrapMode: Text.Wrap
                            }
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: backend.serviceDetail.length > 0
                            text: backend.serviceDetail
                            font.pixelSize: 11
                            color: "#e57373"
                            wrapMode: Text.Wrap
                        }

                        TextField {
                            id: helperField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Helper address")
                        }
                        Label {
                            text: "http://192.168.1.20:8765"
                            font.pixelSize: 11
                            opacity: 0.45
                            Layout.topMargin: -8
                        }

                        TextField {
                            id: keyField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Access key")
                        }
                        Label {
                            text: "stems/access-key.txt"
                            font.pixelSize: 11
                            opacity: 0.45
                            Layout.topMargin: -8
                        }

                        TextField {
                            id: rootField
                            Layout.fillWidth: true
                            placeholderText: qsTr("Music folder")
                        }
                        Label {
                            Layout.fillWidth: true
                            text: qsTr("The same folder the helper watches as /music")
                            font.pixelSize: 11
                            opacity: 0.45
                            wrapMode: Text.Wrap
                            Layout.topMargin: -8
                        }

                        ComboBox {
                            id: qualityBox
                            Layout.fillWidth: true
                            textRole: "text"
                            valueRole: "value"
                            model: [
                                { value: "auto", text: qsTr("Automatic") },
                                { value: "fast", text: qsTr("Fast") },
                                { value: "best", text: qsTr("Best") },
                                { value: "both", text: qsTr("Both") }
                            ]
                        }
                        Label {
                            text: qsTr("Quality")
                            font.pixelSize: 11
                            opacity: 0.45
                            Layout.topMargin: -8
                        }

                        Label {
                            Layout.fillWidth: true
                            visible: text.length > 0
                            text: backend.healthSummary
                            font.pixelSize: 11
                            opacity: 0.6
                            wrapMode: Text.Wrap
                            Layout.topMargin: 4
                        }

                    }
                }

                Button {
                    id: saveButton
                    Layout.alignment: Qt.AlignRight
                    text: qsTr("Save")
                    highlighted: true
                    onClicked: {
                        backend.applySettings(helperField.text, keyField.text, rootField.text,
                                              root.qualityValues[qualityBox.currentIndex])
                        root.closed()
                    }
                }
            }
        }
    }
}
