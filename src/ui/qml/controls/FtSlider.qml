// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import FrameTrellis

Slider {
    id: slider
    implicitHeight: 24
    background: Rectangle {
        x: slider.leftPadding
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        width: slider.availableWidth
        height: 4
        radius: 2
        color: Theme.raised
        Rectangle {
            width: slider.visualPosition * parent.width
            height: parent.height
            radius: 2
            color: slider.enabled ? Theme.accent : Theme.borderStrong
        }
    }
    handle: Rectangle {
        x: slider.leftPadding + slider.visualPosition * (slider.availableWidth - width)
        y: slider.topPadding + slider.availableHeight / 2 - height / 2
        width: 14
        height: 14
        radius: 7
        color: slider.pressed ? Theme.accentStrong : (slider.enabled ? Theme.text : Theme.textFaint)
        border.width: slider.visualFocus ? 2 : 0
        border.color: Theme.focus
    }
}
