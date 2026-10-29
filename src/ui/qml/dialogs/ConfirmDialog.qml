// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

// Three-way question used for unsaved changes: Save / Don't Save / Cancel.
FtDialog {
    id: confirm

    property string message
    property string acceptText: qsTr("Save")
    property string discardText: qsTr("Don't Save")
    property bool showDiscard: true
    signal saveRequested()
    signal discardRequested()

    implicitWidth: 460
    closePolicy: Popup.CloseOnEscape

    contentItem: ColumnLayout {
        Text {
            Layout.fillWidth: true
            text: confirm.message
            color: Theme.textMuted
            font.pixelSize: Theme.fontSize
            wrapMode: Text.WordWrap
        }
    }

    footer: DialogButtons {
        Item { Layout.fillWidth: true }
        FtButton {
            objectName: confirm.objectName.length ? confirm.objectName + "Discard" : ""
            text: confirm.discardText
            destructive: true
            visible: confirm.showDiscard
            onClicked: { confirm.close(); confirm.discardRequested() }
        }
        FtButton {
            objectName: confirm.objectName.length ? confirm.objectName + "Cancel" : ""
            text: qsTr("Cancel")
            onClicked: confirm.close()
        }
        FtButton {
            objectName: confirm.objectName.length ? confirm.objectName + "Accept" : ""
            text: confirm.acceptText
            primary: true
            onClicked: { confirm.close(); confirm.saveRequested() }
        }
    }
}
