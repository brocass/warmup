import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The "?" overlay: every shortcut, one per row.
Item {
    id: root
    signal closed

    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.82)

        MouseArea {
            anchors.fill: parent
            onClicked: root.closed()
        }

        Rectangle {
            anchors.centerIn: parent
            width: Math.min(440, parent.width - 64)
            height: column.implicitHeight + 48
            radius: theme.radius
            color: "#1a1a1e"
            border.color: Qt.rgba(1, 1, 1, 0.12)

            ColumnLayout {
                id: column
                anchors.centerIn: parent
                width: parent.width - 48
                spacing: 10

                Label {
                    text: qsTr("Shortcuts")
                    font.pixelSize: 17
                    font.bold: true
                    Layout.bottomMargin: 6
                }

                Repeater {
                    model: [
                        ["Space", qsTr("Pause or resume the queue")],
                        ["X", qsTr("Cancel the selected song")],
                        [",", qsTr("Open settings")],
                        ["?", qsTr("Show or hide this help")],
                        ["Q", qsTr("Quit")]
                    ]
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        Label {
                            text: modelData[0]
                            font.pixelSize: 13
                            font.bold: true
                            color: theme.accent
                            Layout.preferredWidth: 64
                        }
                        Label {
                            Layout.fillWidth: true
                            text: modelData[1]
                            font.pixelSize: 13
                            opacity: 0.85
                        }
                    }
                }
            }
        }
    }
}
