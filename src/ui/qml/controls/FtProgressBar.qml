// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import FrameTrellis

ProgressBar {
    id: bar
    implicitHeight: 6
    background: Rectangle {
        implicitWidth: 120
        implicitHeight: 6
        radius: 3
        color: Theme.raised
    }
    contentItem: Item {
        implicitHeight: 6
        Rectangle {
            width: bar.indeterminate ? parent.width * 0.3 : bar.visualPosition * parent.width
            height: parent.height
            radius: 3
            color: Theme.accent
            SequentialAnimation on x {
                running: bar.indeterminate && bar.visible
                loops: Animation.Infinite
                NumberAnimation { from: 0; to: bar.width * 0.7; duration: 900; easing.type: Easing.InOutQuad }
                NumberAnimation { from: bar.width * 0.7; to: 0; duration: 900; easing.type: Easing.InOutQuad }
            }
        }
    }
}
