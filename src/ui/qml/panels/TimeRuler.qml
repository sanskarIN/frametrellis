// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import FrameTrellis

// Time ruler for the visible part of the timeline. Click or drag to move the playhead.
Item {
    id: ruler

    required property AppController app
    property real contentX: 0
    property real pixelsPerFrame: 1
    signal scrubbed(real frame)
    signal editMarker(string markerId, real frame, string label)

    // Picks a label spacing (in frames) that keeps labels at least ~90 px apart.
    function labelStep() {
        const fps = Math.max(1, app.timeline.nominalRate())
        const candidates = [1, 2, 5, 10, fps, fps * 2, fps * 5, fps * 10, fps * 15, fps * 30, fps * 60,
                            fps * 120, fps * 300, fps * 600, fps * 1800, fps * 3600]
        for (let i = 0; i < candidates.length; ++i) {
            if (candidates[i] * pixelsPerFrame >= 90)
                return candidates[i]
        }
        return candidates[candidates.length - 1]
    }

    Rectangle {
        anchors.fill: parent
        color: Theme.panelAlt
        Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
    }

    Canvas {
        id: canvas
        anchors.fill: parent
        renderStrategy: Canvas.Cooperative
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const step = ruler.labelStep()
            const minor = step >= 10 ? step / 5 : (step >= 5 ? 1 : step)
            const first = Math.floor(ruler.contentX / ruler.pixelsPerFrame / minor) * minor
            const last = (ruler.contentX + width) / ruler.pixelsPerFrame
            ctx.strokeStyle = Theme.borderStrong
            ctx.fillStyle = Theme.textMuted
            ctx.font = "11px sans-serif"
            ctx.lineWidth = 1
            for (let f = first; f <= last; f += minor) {
                const x = Math.round(f * ruler.pixelsPerFrame - ruler.contentX) + 0.5
                const major = Math.round(f) % Math.round(step) === 0
                ctx.beginPath()
                ctx.moveTo(x, major ? height - 12 : height - 6)
                ctx.lineTo(x, height)
                ctx.stroke()
                if (major)
                    ctx.fillText(ruler.app.timeline.clock(f), x + 4, height - 14)
            }
        }
        Connections {
            target: ruler
            function onContentXChanged() { canvas.requestPaint() }
            function onPixelsPerFrameChanged() { canvas.requestPaint() }
            function onWidthChanged() { canvas.requestPaint() }
        }
        Connections {
            target: Theme
            function onModeChanged() { canvas.requestPaint() }
        }
    }

    MouseArea {
        anchors.fill: parent
        cursorShape: Qt.IBeamCursor
        function frameAt(x) { return Math.max(0, (x + ruler.contentX) / ruler.pixelsPerFrame) }
        onPressed: mouse => ruler.scrubbed(frameAt(mouse.x))
        onPositionChanged: mouse => { if (pressed) ruler.scrubbed(frameAt(mouse.x)) }
    }
    // Cues are kept in timeline frames, not pixels; zoom and horizontal scrolling never
    // change their frame positions. Double-click or right-click to edit a cue.
    Repeater {
        model: ruler.app.timeline.markers
        delegate: Rectangle {
            id: markerGlyph
            required property var modelData
            x: modelData.frame * ruler.pixelsPerFrame - ruler.contentX - width / 2
            y: 1
            z: 5
            width: 12
            height: 12
            rotation: 45
            radius: 2
            color: Theme.accent
            border.color: Theme.panel
            visible: x + width >= 0 && x <= ruler.width

            MouseArea {
                id: markerMouse
                anchors.fill: parent
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: mouse => {
                    if (mouse.button === Qt.RightButton)
                        ruler.editMarker(markerGlyph.modelData.id, markerGlyph.modelData.frame, markerGlyph.modelData.label)
                    else
                        ruler.app.timeline.seekToMarker(markerGlyph.modelData.id)
                }
                onDoubleClicked: ruler.editMarker(markerGlyph.modelData.id, markerGlyph.modelData.frame, markerGlyph.modelData.label)
            }
            ToolTip.visible: markerMouse.containsMouse
            ToolTip.text: modelData.label + " • " + modelData.timecode + qsTr(" (double-click to edit)")
        }
    }
}
