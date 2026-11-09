import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

// The playlist-of-processed-tracks options, and the karaoke library for
// players like Emby. Prefilled from the saved settings each time it opens;
// Save applies and rewrites playlists right away.
Item {
    id: root
    signal closed

    function refill() {
        pathField.text = backend.playlistPath
        autoBox.checked = backend.playlistEnabled
        karaokeBox.checked = backend.karaokeEnabled
        karaokePathField.text = backend.karaokePath
        karaokeDirField.text = backend.karaokeDirectory
    }

    onVisibleChanged: if (visible) {
        refill()
        pathField.forceActiveFocus()
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
            height: Math.min(flick.contentHeight + 48, parent.height - 32)
            radius: theme.radius
            color: "#1a1a1e"
            border.color: Qt.rgba(1, 1, 1, 0.12)

            // Clicks on the panel itself must not close the overlay.
            MouseArea {
                anchors.fill: parent
                onClicked: { }
            }

            Flickable {
                id: flick
                anchors.fill: parent
                anchors.margins: 24
                contentWidth: column.width
                contentHeight: column.implicitHeight
                clip: true

                ColumnLayout {
                    id: column
                    width: flick.width
                    spacing: 12

                    Label {
                        text: qsTr("Playlist")
                        font.pixelSize: 17
                        font.bold: true
                        Layout.bottomMargin: 4
                    }

                    CheckBox {
                        id: autoBox
                        Layout.fillWidth: true
                        text: qsTr("Update automatically when new songs are ready")
                    }

                    TextField {
                        id: pathField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Ready playlist (.m3u)")
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Inside your music folder, paths stay relative so a music server can read them")
                        font.pixelSize: 11
                        opacity: 0.45
                        wrapMode: Text.Wrap
                        Layout.topMargin: -8
                    }

                    Label {
                        text: qsTr("Karaoke for other players")
                        font.pixelSize: 14
                        font.bold: true
                        Layout.topMargin: 10
                    }

                    CheckBox {
                        id: karaokeBox
                        Layout.fillWidth: true
                        text: qsTr("Build instrumentals with lyrics and a karaoke playlist")
                    }

                    TextField {
                        id: karaokePathField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Karaoke playlist (.m3u)")
                    }

                    TextField {
                        id: karaokeDirField
                        Layout.fillWidth: true
                        placeholderText: qsTr("Instrumentals folder")
                    }
                    Label {
                        Layout.fillWidth: true
                        text: qsTr("Instrumental .m4a files and matching .lrc lyrics live here — keep this folder outside your music folder")
                        font.pixelSize: 11
                        opacity: 0.45
                        wrapMode: Text.Wrap
                        Layout.topMargin: -8
                    }

                    Button {
                        Layout.alignment: Qt.AlignRight
                        Layout.topMargin: 8
                        text: qsTr("Save")
                        highlighted: true
                        onClicked: {
                            backend.applyPlaylistSettings(autoBox.checked, pathField.text)
                            backend.applyKaraokeSettings(karaokeBox.checked, karaokePathField.text,
                                                         karaokeDirField.text)
                            root.closed()
                        }
                    }
                }
            }
        }
    }
}
