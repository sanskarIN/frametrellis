// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import FrameTrellis

// Modal dialog with the FrameTrellis look.
Dialog {
    id: dialog

    modal: true
    anchors.centerIn: Overlay.overlay
    padding: Theme.spacingL
    topPadding: Theme.spacingM
    width: Math.min(parent ? parent.width - 48 : 520, implicitWidth > 0 ? implicitWidth : 520)

    Overlay.modal: Rectangle { color: "#99000000" }

    background: Rectangle {
        color: Theme.panel
        radius: Theme.radius + 2
        border.color: Theme.borderStrong
    }

    header: Text {
        text: dialog.title
        visible: text.length > 0
        color: Theme.text
        font.pixelSize: Theme.fontSizeLarge
        font.weight: Font.DemiBold
        leftPadding: Theme.spacingL
        rightPadding: Theme.spacingL
        topPadding: Theme.spacingL
        elide: Text.ElideRight
        Accessible.role: Accessible.TitleBar
    }
}
