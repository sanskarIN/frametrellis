// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

FtDialog {
    id: dialog
    title: qsTr("Keyboard shortcuts")
    implicitWidth: 520

    readonly property var shortcuts: [
        ["Space", qsTr("Play / pause")],
        ["Left / Right", qsTr("Previous / next frame")],
        ["Shift+Left / Shift+Right", qsTr("Back / forward one second")],
        ["Ctrl+Left / Ctrl+Right", qsTr("Back / forward five seconds")],
        ["Up / Down", qsTr("Previous / next edit point")],
        ["Home / End", qsTr("Go to start / end")],
        ["S", qsTr("Split at playhead")],
        ["Ctrl+D", qsTr("Duplicate selected clips (preserve spacing)")],
        ["Ctrl+Alt+C", qsTr("Copy volume, fades and picture attributes")],
        ["Ctrl+Alt+V", qsTr("Paste attributes to selected clips")],
        ["Ctrl+Alt+M", qsTr("Mute selected clips")],
        ["Ctrl+Alt+U", qsTr("Unmute selected clips")],
        ["T", qsTr("Add a transition after the selected clip")],
        ["Delete", qsTr("Delete selected clips or transition")],
        ["Shift+Delete", qsTr("Ripple delete")],
        ["Ctrl+A", qsTr("Select all clips")],
        ["N", qsTr("Toggle snapping")],
        ["+ / −", qsTr("Zoom timeline in / out")],
        ["Ctrl+Z / Ctrl+Shift+Z", qsTr("Undo / redo")],
        ["Ctrl+I", qsTr("Import media")],
        ["Ctrl+E", qsTr("Export")],
        ["Ctrl+N / Ctrl+O", qsTr("New / open project")],
        ["Ctrl+S / Ctrl+Shift+S", qsTr("Save / save as")]
    ]

    contentItem: ListView {
        implicitHeight: Math.min(contentHeight, 420)
        clip: true
        model: dialog.shortcuts
        delegate: RowLayout {
            required property var modelData
            width: ListView.view.width
            height: 26
            Text {
                text: modelData[0]
                color: Theme.accent
                font.family: Theme.monoFamily
                font.pixelSize: Theme.fontSize
                Layout.preferredWidth: 210
            }
            Text { text: modelData[1]; color: Theme.text; font.pixelSize: Theme.fontSize; Layout.fillWidth: true }
        }
    }

    footer: DialogButtons {
        Item { Layout.fillWidth: true }
        FtButton { text: qsTr("Close"); primary: true; onClicked: dialog.close() }
    }
}
