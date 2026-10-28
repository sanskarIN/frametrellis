// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

// Create or edit a persistent, frame-accurate sequence review marker.
FtDialog {
    id: dialog
    required property AppController app
    property string markerId: ""
    property string initialLabel: ""
    property real initialFrame: 0

    title: markerId.length ? qsTr("Edit timeline marker") : qsTr("Add timeline marker")
    implicitWidth: 440
    closePolicy: Popup.CloseOnEscape

    function openNew(frame) {
        markerId = ""
        initialFrame = Math.round(frame)
        initialLabel = qsTr("Marker %1").arg(app.timeline.markers.length + 1)
        open()
    }

    function openExisting(id, frame, label) {
        markerId = id
        initialFrame = frame
        initialLabel = label
        open()
    }

    onOpened: {
        markerLabel.text = initialLabel
        markerFrame.text = initialFrame.toString()
        markerLabel.selectAll()
        markerLabel.forceActiveFocus()
    }

    function applyChanges() {
        if (!saveButton.enabled)
            return
        const frame = Number(markerFrame.text)
        const success = markerId.length
                        ? app.timeline.updateMarker(markerId, frame, markerLabel.text)
                        : app.timeline.addMarker(frame, markerLabel.text)
        if (success)
            close()
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS
        Label { text: qsTr("Marker label"); color: Theme.textMuted }
        TextField {
            id: markerLabel
            objectName: "markerLabelField"
            Layout.fillWidth: true
            maximumLength: 120
            Accessible.name: qsTr("Sequence marker label")
            onAccepted: dialog.applyChanges()
        }
        Label { text: qsTr("Timeline position (frame)"); color: Theme.textMuted }
        TextField {
            id: markerFrame
            objectName: "markerFrameField"
            Layout.fillWidth: true
            maximumLength: 10
            inputMethodHints: Qt.ImhDigitsOnly
            validator: IntValidator { bottom: 0; top: 1000000000 }
            Accessible.name: qsTr("Sequence marker frame")
            onAccepted: dialog.applyChanges()
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("Markers are saved with your project, can be moved independently of clips, and do not appear in exported video.")
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
        }
    }

    footer: DialogButtons {
        FtButton {
            text: qsTr("Delete marker")
            visible: dialog.markerId.length > 0
            onClicked: {
                if (dialog.app.timeline.removeMarker(dialog.markerId))
                    dialog.close()
            }
        }
        Item { Layout.fillWidth: true }
        FtButton { text: qsTr("Cancel"); onClicked: dialog.close() }
        FtButton {
            id: saveButton
            text: dialog.markerId.length ? qsTr("Apply") : qsTr("Add marker")
            primary: true
            enabled: markerLabel.text.trim().length > 0 && markerFrame.acceptableInput
                     && markerFrame.text.length > 0
                     && (dialog.markerId.length > 0 ||
                         Number(markerFrame.text) <= dialog.app.timeline.duration)
            onClicked: dialog.applyChanges()
        }
    }
}
