import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// Helper connection settings. Prefilled from the saved settings each time it
// opens; Save applies and reconnects.
Item {
    id: root
    signal closed

    property var qualityValues: ["auto", "fast", "best", "both"]

    function refill() {
        helperField.text = backend.helperUrl
        keyField.text = backend.accessKey
        rootField.text = backend.musicRoot
        qualityBox.currentIndex = Math.max(0, qualityValues.indexOf(backend.quality))
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
            height: column.implicitHeight + 48
            radius: theme.radius
            color: "#1a1a1e"
            border.color: Qt.rgba(1, 1, 1, 0.12)

            // Clicks on the panel itself must not close the overlay.
            MouseArea {
                anchors.fill: parent
                onClicked: { }
            }

            ColumnLayout {
                id: column
                anchors.centerIn: parent
                width: parent.width - 48
                spacing: 12

                Label {
                    text: qsTr("Settings")
                    font.pixelSize: 17
                    font.bold: true
                    Layout.bottomMargin: 4
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

                Button {
                    Layout.alignment: Qt.AlignRight
                    Layout.topMargin: 8
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
