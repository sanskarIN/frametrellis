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
        ["Space", qsTr("Play / pause Program (Program tab only)")],
        ["Ctrl+Alt+L", qsTr("Toggle Loop Preview (after v1.4.0)")],
        ["Left / Right", qsTr("Step one frame in active Program / Source tab")],
        ["I / O", qsTr("Set Source In / Out mark (after v1.4.0)")],
        ["Ctrl+Shift+M", qsTr("Match Source Out to selected clip length (Source tab)")],
        ["Shift+Left / Shift+Right", qsTr("Back / forward one second")],
        ["Ctrl+Left / Ctrl+Right", qsTr("Back / forward five seconds")],
        ["Ctrl+Shift+Left / Right", qsTr("Back / forward ten seconds")],
        ["Up / Down", qsTr("Previous / next edit point")],
        ["Home / End", qsTr("Go to start / end of active monitor")],
        ["Ctrl+G", qsTr("Go to Program sequence timecode")],
        ["Shift+M", qsTr("Add a timeline review marker")],
        ["Ctrl+PgUp / PgDown", qsTr("Previous / next sequence marker")],
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
        ["Ctrl+Shift+J", qsTr("Select all clips beneath the playhead")],
        ["Ctrl+Alt+R", qsTr("Select every timeline use of one media source")],
        ["N", qsTr("Toggle snapping")],
        ["+ / −", qsTr("Zoom timeline in / out")],
        ["Ctrl+0", qsTr("Fit the whole timeline to the visible width")],
        ["Ctrl+Shift+0", qsTr("Fit all selected clips in the timeline")],
        ["Ctrl+Shift+L", qsTr("Center timeline viewport on the playhead")],
        ["Ctrl+Z / Ctrl+Shift+Z", qsTr("Undo / redo")],
        ["Ctrl+I", qsTr("Import media")],
        ["Ctrl+Alt+S", qsTr("Open the subtitle editor")],
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
