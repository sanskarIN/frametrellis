// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick

// Paint only the visible part of a clip, independently of its full zoomed width.
Canvas {
    id: surface

    required property var points
    required property real clipOffset
    required property real clipWidth
    required property real pixelsPerFrame
    required property real viewportLeft
    required property real viewportWidth
    required property color lineColor
    property string valueField: "value"
    property real minimum: 0
    property real maximum: 1
    property bool diamonds: true
    property bool highlighted: false

    readonly property real visibleStart: Math.max(clipOffset, viewportLeft)
    x: visibleStart - clipOffset
    width: Math.max(0, Math.min(viewportWidth, Math.min(clipOffset + clipWidth, viewportLeft + viewportWidth) - visibleStart))
    visible: points.length > 0 && clipWidth > 8 && width > 0 && height > 26

    onPaint: {
        const ctx = getContext("2d")
        ctx.reset()
        const values = surface.points
        if (values.length === 0) return
        const pointX = p => p.frame * surface.pixelsPerFrame - surface.x
        const pointY = p => height - 5 - ((p[surface.valueField] - surface.minimum) / Math.max(0.000001, surface.maximum - surface.minimum)) * (height - 25)
        ctx.strokeStyle = surface.lineColor
        ctx.fillStyle = surface.lineColor
        ctx.lineWidth = surface.highlighted ? 2 : 1
        ctx.beginPath()
        ctx.moveTo(pointX(values[0]), pointY(values[0]))
        for (let i = 1; i < values.length; ++i) ctx.lineTo(pointX(values[i]), pointY(values[i]))
        ctx.stroke()
        for (let i = 0; i < values.length; ++i) {
            if (!values[i].keyframe) continue
            const px = pointX(values[i])
            const py = pointY(values[i])
            if (surface.diamonds) {
                ctx.beginPath()
                ctx.moveTo(px, py - 3); ctx.lineTo(px + 3, py)
                ctx.lineTo(px, py + 3); ctx.lineTo(px - 3, py)
                ctx.closePath(); ctx.fill()
            } else {
                ctx.fillRect(px - 3, py - 3, 6, 6)
            }
        }
    }
    onPointsChanged: requestPaint()
    onPixelsPerFrameChanged: requestPaint()
    onLineColorChanged: requestPaint()
    onHighlightedChanged: requestPaint()
    onMinimumChanged: requestPaint()
    onMaximumChanged: requestPaint()
    onValueFieldChanged: requestPaint()
    onDiamondsChanged: requestPaint()
    onWidthChanged: requestPaint()
    onHeightChanged: requestPaint()
    onXChanged: requestPaint()
}
