// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

// Labelled slider with a formatted value. "moved" fires while dragging, "committed" on release.
ColumnLayout {
    id: row

    property string label
    property real from: 0
    property real to: 1
    property real value: 0
    property real stepSize: 0.01
    property real defaultValue: from
    property var format: function(v) { return v.toFixed(2) }
    signal moved(real value)
    signal committed()

    spacing: 2
    Layout.fillWidth: true

    RowLayout {
        Layout.fillWidth: true
        Text {
            text: row.label
            color: Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall
            Layout.fillWidth: true
        }
        Text {
            text: row.format(slider.value)
            color: Theme.text
            font.pixelSize: Theme.fontSizeSmall
            font.family: Theme.monoFamily
        }
    }
    FtSlider {
        id: slider
        Layout.fillWidth: true
        from: row.from
        to: row.to
        stepSize: row.stepSize
        value: row.value
        Accessible.name: row.label
        onMoved: row.moved(value)
        onPressedChanged: if (!pressed) row.committed()
        // Double-click restores the neutral value.
        TapHandler {
            acceptedButtons: Qt.LeftButton
            onDoubleTapped: { row.moved(row.defaultValue); row.committed() }
        }
    }
}
