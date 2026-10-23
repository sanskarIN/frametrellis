// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts
import FrameTrellis

// Name and switches of one track (visibility, mute, lock).
Rectangle {
    id: header

    required property AppController app
    required property string trackId
    required property string name
    required property string kind
    required property bool muted
    required property bool hidden
    required property bool locked

    color: kind === "audio" ? Theme.trackAudio : Theme.trackVideo
    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
    Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.border }
    Rectangle {
        anchors.left: parent.left
        width: 3
        height: parent.height
        color: header.kind === "audio" ? Theme.amber : Theme.accent
        opacity: 0.8
    }

    Accessible.role: Accessible.Row
    Accessible.name: qsTr("%1 track %2").arg(kind === "audio" ? qsTr("Audio") : qsTr("Video")).arg(name)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.spacingS + 3
        anchors.rightMargin: Theme.spacingXs
        spacing: 0

        Text {
            id: label
            text: header.name
            color: header.hidden || header.muted ? Theme.textFaint : Theme.text
            font.pixelSize: Theme.fontSize
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            Layout.fillWidth: true
            visible: !editor.visible
            MouseArea {
                anchors.fill: parent
                onDoubleClicked: { editor.text = header.name; editor.visible = true; editor.forceActiveFocus(); editor.selectAll() }
            }
        }
        TextField {
            id: editor
            visible: false
            Layout.fillWidth: true
            font.pixelSize: Theme.fontSize
            maximumLength: 64
            Accessible.name: qsTr("Track name")
            onAccepted: { header.app.timeline.renameTrack(header.trackId, text); visible = false }
            onActiveFocusChanged: if (!activeFocus) visible = false
            Keys.onEscapePressed: visible = false
        }
        IconButton {
            visible: header.kind === "video"
            iconName: header.hidden ? "eye-off" : "eye"
            tip: header.hidden ? qsTr("Show track") : qsTr("Hide track")
            active: header.hidden
            implicitWidth: 28
            onClicked: header.app.timeline.setTrackFlag(header.trackId, "hidden", !header.hidden)
        }
        IconButton {
            iconName: header.muted ? "volume-off" : "volume"
            tip: header.muted ? qsTr("Unmute track") : qsTr("Mute track")
            active: header.muted
            implicitWidth: 28
            onClicked: header.app.timeline.setTrackFlag(header.trackId, "muted", !header.muted)
        }
        IconButton {
            iconName: header.locked ? "lock" : "unlock"
            tip: header.locked ? qsTr("Unlock track") : qsTr("Lock track")
            active: header.locked
            implicitWidth: 28
            onClicked: header.app.timeline.setTrackFlag(header.trackId, "locked", !header.locked)
        }
    }

    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: menu.popup()
    }
    Menu {
        id: menu
        MenuItem { text: qsTr("Rename…"); onTriggered: { editor.text = header.name; editor.visible = true; editor.forceActiveFocus(); editor.selectAll() } }
        MenuItem {
            text: qsTr("Move track up")
            enabled: header.app.timeline.canMoveTrack(header.trackId, -1)
            onTriggered: header.app.timeline.moveTrack(header.trackId, -1)
        }
        MenuItem {
            text: qsTr("Move track down")
            enabled: header.app.timeline.canMoveTrack(header.trackId, 1)
            onTriggered: header.app.timeline.moveTrack(header.trackId, 1)
        }
        MenuSeparator {}
        MenuItem { text: qsTr("Remove track"); enabled: !header.locked; onTriggered: header.app.timeline.removeTrack(header.trackId) }
    }
}
