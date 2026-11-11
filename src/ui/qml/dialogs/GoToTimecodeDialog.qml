// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

// Frame-accurate seek without changing project contents or playback history.
FtDialog {
    id: dialog
    required property AppController app
    property bool invalidInput: false

    title: qsTr("Go to timecode")
    implicitWidth: 430
    closePolicy: Popup.CloseOnEscape

    function trySeek() {
        if (app.playback.seekToTimecode(timecodeField.text)) {
            close()
        } else {
            invalidInput = true
            timecodeField.forceActiveFocus()
        }
    }

    onOpened: {
        invalidInput = false
        timecodeField.text = app.playback.timecode
        timecodeField.selectAll()
        timecodeField.forceActiveFocus()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS
        Label {
            text: qsTr("Sequence position (HH:MM:SS:FF or HH:MM:SS;FF)")
            color: Theme.textMuted
            Layout.fillWidth: true
        }
        TextField {
            id: timecodeField
            objectName: "goToTimecodeField"
            Layout.fillWidth: true
            maximumLength: 32
            Accessible.name: qsTr("Target timecode")
            onTextChanged: dialog.invalidInput = false
            onAccepted: dialog.trySeek()
        }
        Text {
            Layout.fillWidth: true
            color: dialog.invalidInput ? Theme.danger : Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
            text: dialog.invalidInput
                  ? qsTr("Enter a valid timecode inside the current sequence.")
                  : qsTr("Use semicolons for drop-frame timecode when the project frame rate supports it.")
        }
    }

    footer: DialogButtons {
        Item { Layout.fillWidth: true }
        FtButton { text: qsTr("Cancel"); onClicked: dialog.close() }
        FtButton {
            text: qsTr("Go")
            primary: true
            enabled: timecodeField.text.trim().length > 0
            onClicked: dialog.trySeek()
        }
    }
}
