import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 920
    height: 640
    minimumWidth: 600
    minimumHeight: 440
    visible: true
    title: "Warmup"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property bool helpVisible: false
    property bool settingsVisible: !backend.configured
    property bool playlistVisible: false
    property bool shortcutsBlocked: settingsVisible || playlistVisible
    property color accentColor: theme.accent

    // Reads translator.language so the text re-renders on a language switch.
    function fmtEta(seconds) {
        translator.language
        if (seconds < 0)
            return ""
        const minutes = Math.floor(seconds / 60)
        if (minutes >= 60)
            return qsTr("about %1 h %2 min").arg(Math.floor(minutes / 60)).arg(minutes % 60)
        if (minutes >= 1)
            return qsTr("about %1 min").arg(minutes)
        return qsTr("under a minute")
    }

    function stateText(state) {
        translator.language
        switch (state) {
        case "queued": return qsTr("Queued")
        case "running": return qsTr("Separating")
        case "done": return qsTr("Ready")
        case "failed": return qsTr("Failed")
        case "cancelled": return qsTr("Cancelled")
        default: return state
        }
    }

    // Typing in the settings fields must not trigger single-key shortcuts,
    // so they're disabled while the overlay is up.
    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.shortcutsBlocked; onActivated: win.helpVisible = !win.helpVisible }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: !win.shortcutsBlocked; onActivated: Qt.quit() }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: !win.shortcutsBlocked; onActivated: backend.togglePause() }
    Shortcut { sequence: ","; context: Qt.ApplicationShortcut; enabled: !win.shortcutsBlocked; onActivated: win.settingsVisible = true }
    Shortcut { sequence: "P"; context: Qt.ApplicationShortcut; enabled: !win.shortcutsBlocked; onActivated: win.playlistVisible = true }
    Shortcut {
        sequences: ["X", "Delete"]; context: Qt.ApplicationShortcut
        enabled: !win.shortcutsBlocked && list.currentItem && list.currentItem.cancellable
        onActivated: backend.cancelJob(list.currentItem.jobId)
    }
    Shortcut {
        sequence: "Escape"; context: Qt.ApplicationShortcut
        onActivated: {
            if (win.helpVisible)
                win.helpVisible = false
            else if (win.playlistVisible)
                win.playlistVisible = false
            else if (win.settingsVisible)
                win.settingsVisible = false
        }
    }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 16
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                spacing: 10

                ColumnLayout {
                    spacing: 0
                    Label {
                        text: "Warmup"
                        font.pixelSize: 20
                        font.bold: true
                    }
                    Label {
                        text: qsTr("Prepare Sing stems ahead of time")
                        font.pixelSize: 12
                        opacity: 0.6
                    }
                }

                Item { Layout.fillWidth: true }

                Rectangle {
                    width: 10
                    height: 10
                    radius: 5
                    color: !backend.connected ? "#e53935" : backend.paused ? accentColor : "#4caf50"
                }
                Label {
                    text: backend.helperLine
                    font.pixelSize: 13
                    opacity: 0.8
                }
                Button {
                    text: qsTr("Playlist")
                    flat: true
                    onClicked: win.playlistVisible = true
                }
                Button {
                    text: qsTr("Settings")
                    flat: true
                    onClicked: win.settingsVisible = true
                }
            }

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                ListView {
                    id: list
                    anchors.fill: parent
                    clip: true
                    focus: true
                    model: backend.items
                    spacing: 4
                    section.property: "section"
                    section.delegate: Label {
                        required property string section
                        text: section
                        font.pixelSize: 12
                        font.bold: true
                        opacity: 0.55
                        topPadding: 10
                        bottomPadding: 4
                    }

                    delegate: Item {
                        id: row
                        width: ListView.view.width
                        height: 54

                        property string jobId: modelData.jobId
                        property bool cancellable: modelData.cancellable

                        Rectangle {
                            anchors.fill: parent
                            radius: theme.radius
                            color: row.ListView.isCurrentItem
                                   ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.16)
                                   : Qt.rgba(1, 1, 1, 0.045)

                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                anchors.rightMargin: 12
                                spacing: 12

                                Label {
                                    text: modelData.state === "done" ? "✓" : modelData.state === "failed" ? "✕" : modelData.state === "running" ? "▶" : "…"
                                    color: modelData.state === "done" ? "#4caf50" : modelData.state === "failed" ? "#e53935" : accentColor
                                    font.pixelSize: 14
                                    Layout.preferredWidth: 16
                                }

                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: 2
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.name
                                        elide: Text.ElideRight
                                        font.bold: modelData.state === "running"
                                    }
                                    Label {
                                        Layout.fillWidth: true
                                        text: modelData.subtitle
                                        elide: Text.ElideMiddle
                                        font.pixelSize: 12
                                        opacity: 0.55
                                    }
                                }

                                Label {
                                    visible: modelData.state === "running" && modelData.eta >= 0
                                    text: win.fmtEta(modelData.eta)
                                    font.pixelSize: 12
                                    opacity: 0.6
                                }
                                ProgressBar {
                                    visible: modelData.state === "running"
                                    value: modelData.progress
                                    Layout.preferredWidth: 140
                                }
                                Label {
                                    text: modelData.state === "running"
                                          ? Math.round(modelData.progress * 100) + "%"
                                          : win.stateText(modelData.state)
                                    font.pixelSize: 12
                                    opacity: 0.75
                                    Layout.preferredWidth: 76
                                    horizontalAlignment: Text.AlignRight
                                }
                            }

                            MouseArea {
                                anchors.fill: parent
                                onClicked: list.currentIndex = index
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: list.count === 0
                    text: qsTr("Nothing in the helper's queue yet")
                    font.pixelSize: 15
                    opacity: 0.5
                }
            }

            Rectangle {
                id: dropZone
                Layout.fillWidth: true
                Layout.preferredHeight: 88
                radius: theme.radius
                color: dropArea.containsDrag ? Qt.rgba(accentColor.r, accentColor.g, accentColor.b, 0.14)
                                             : Qt.rgba(1, 1, 1, 0.03)
                border.width: 2
                border.color: dropArea.containsDrag ? accentColor : Qt.rgba(1, 1, 1, 0.18)

                Label {
                    anchors.centerIn: parent
                    text: qsTr("Drop songs, folders or playlists here")
                    font.pixelSize: 14
                    opacity: dropArea.containsDrag ? 1.0 : 0.6
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                BusyIndicator {
                    visible: backend.probingLeft > 0 || backend.downloadsLeft > 0
                    running: visible
                    Layout.preferredWidth: 22
                    Layout.preferredHeight: 22
                }
                Label {
                    Layout.fillWidth: true
                    text: backend.downloadsLeft > 0
                          ? qsTr("Downloading instrumentals — %1 left").arg(backend.downloadsLeft)
                          : backend.probingLeft > 0
                            ? qsTr("Reading song details — %1 left").arg(backend.probingLeft)
                            : backend.statusText
                    elide: Text.ElideRight
                    font.pixelSize: 12
                    opacity: 0.75
                }
            }
        }

        DropArea {
            id: dropArea
            anchors.fill: parent
            onDropped: function (drop) {
                if (drop.hasUrls) {
                    backend.enqueueUrls(drop.urls)
                    drop.accept()
                }
            }
        }

        HelpOverlay {
            anchors.fill: parent
            visible: win.helpVisible
            onClosed: win.helpVisible = false
        }

        SettingsOverlay {
            anchors.fill: parent
            visible: win.settingsVisible
            onClosed: win.settingsVisible = false
        }

        PlaylistOverlay {
            anchors.fill: parent
            visible: win.playlistVisible
            onClosed: win.playlistVisible = false
        }
    }
}
