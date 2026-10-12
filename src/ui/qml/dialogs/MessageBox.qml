// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts
import FrameTrellis

// Error or information message with optional details that can be copied.
FtDialog {
    id: box

    property string message
    property string iconName: "warning"
    property bool isError: true

    function show(title, text) {
        box.title = title
        box.message = text
        open()
    }

    implicitWidth: 500

    contentItem: RowLayout {
        spacing: Theme.spacingM
        IconImage {
            Layout.alignment: Qt.AlignTop
            source: "qrc:/qt/qml/FrameTrellis/icons/" + box.iconName + ".svg"
            sourceSize.width: 28
            sourceSize.height: 28
            color: box.isError ? Theme.warning : Theme.accent
        }
        TextArea {
            Layout.fillWidth: true
            readOnly: true
            selectByMouse: true
            wrapMode: Text.WordWrap
            text: box.message
            color: Theme.text
            font.pixelSize: Theme.fontSize
            background: null
            padding: 0
            Accessible.name: box.title
        }
    }

    footer: DialogButtons {
        Item { Layout.fillWidth: true }
        FtButton {
            text: qsTr("OK")
            primary: true
            onClicked: box.accept()
        }
    }
}
