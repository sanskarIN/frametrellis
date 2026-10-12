// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Layouts
import FrameTrellis

// Footer row for dialogs with consistent outer margins. Put an Item with
// Layout.fillWidth before the buttons to align them to the right.
Item {
    default property alias buttons: row.data
    implicitHeight: row.implicitHeight + Theme.spacingL * 2
    implicitWidth: row.implicitWidth + Theme.spacingL * 2
    RowLayout {
        id: row
        anchors.fill: parent
        anchors.margins: Theme.spacingL
        anchors.topMargin: Theme.spacingS
        spacing: Theme.spacingS
    }
}
