// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

// Properties of the selected clip, or an overview of the project when nothing is selected.
PanelFrame {
    id: panel

    required property AppController app
    readonly property var clip: app.timeline.selectedClip
    readonly property bool hasClip: clip.count === 1
    readonly property real fps: Math.max(1, app.timeline.frameRate)

    title: hasClip ? qsTr("Clip") : qsTr("Project")

    function setProp(name, value) {
        app.timeline.setClipProperty(clip.clipId, name, value)
    }

    ScrollView {
        id: scroll
        anchors.fill: parent
        anchors.margins: Theme.spacingM
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: scroll.availableWidth
            spacing: Theme.spacingM

            // ---- Project overview ----
            ColumnLayout {
                visible: !panel.hasClip
                Layout.fillWidth: true
                spacing: Theme.spacingS
                Text {
                    text: panel.app.project.name
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.DemiBold
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: panel.app.project.settingsSummary
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Text {
                    text: qsTr("Length: %1").arg(panel.app.timeline.clock(panel.app.timeline.duration))
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize
                }
                Text {
                    text: panel.app.project.hasFilePath ? panel.app.project.filePath : qsTr("Not saved yet")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                }
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border; Layout.topMargin: Theme.spacingS }
                Text {
                    text: panel.clip.count > 1 ? qsTr("%1 clips selected").arg(panel.clip.count)
                                               : qsTr("Select a clip on the timeline to adjust its volume, fades and picture.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSize
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }

            // ---- Clip details ----
            ColumnLayout {
                visible: panel.hasClip
                Layout.fillWidth: true
                spacing: 4
                Text {
                    text: panel.clip.name || ""
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.DemiBold
                    elide: Text.ElideMiddle
                    Layout.fillWidth: true
                }
                Text {
                    text: (panel.clip.details || "") + (panel.clip.offline ? "  ·  " + qsTr("offline") : "")
                    color: panel.clip.offline ? Theme.danger : Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                GridLayout {
                    columns: 2
                    columnSpacing: Theme.spacingM
                    rowSpacing: 2
                    Layout.topMargin: Theme.spacingXs
                    Repeater {
                        model: [
                            [qsTr("Track"), panel.clip.trackName + (panel.clip.locked ? qsTr(" (locked)") : "")],
                            [qsTr("Start"), panel.clip.startText],
                            [qsTr("End"), panel.clip.endText],
                            [qsTr("Duration"), panel.clip.durationText],
                            [qsTr("Source in"), panel.clip.sourceInText]
                        ]
                        delegate: RowLayout {
                            required property var modelData
                            Layout.columnSpan: 2
                            Text { text: modelData[0]; color: Theme.textFaint; font.pixelSize: Theme.fontSizeSmall; Layout.preferredWidth: 72 }
                            Text { text: modelData[1] || ""; color: Theme.text; font.pixelSize: Theme.fontSizeSmall; font.family: Theme.monoFamily }
                        }
                    }
                }
            }

            // ---- Audio ----
            ColumnLayout {
                visible: panel.hasClip && panel.clip.hasAudio
                enabled: !panel.clip.locked
                Layout.fillWidth: true
                spacing: Theme.spacingS
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("Audio"); color: Theme.text; font.weight: Font.DemiBold; font.pixelSize: Theme.fontSize; Layout.fillWidth: true }
                    CheckBox {
                        text: qsTr("Mute")
                        checked: panel.clip.muted || false
                        onToggled: { panel.setProp("muted", checked); panel.app.timeline.endEdit() }
                    }
                }
                ValueSlider {
                    label: qsTr("Volume")
                    from: 0; to: 4; stepSize: 0.01; defaultValue: 1
                    value: panel.clip.gain || 0
                    format: v => v <= 0 ? "−∞ dB" : ((20 * Math.log(v) / Math.LN10 >= 0 ? "+" : "") + (20 * Math.log(v) / Math.LN10).toFixed(1) + " dB")
                    onMoved: v => panel.setProp("gain", v)
                    onCommitted: panel.app.timeline.endEdit()
                }
            }

            // ---- Fades ----
            ColumnLayout {
                visible: panel.hasClip
                enabled: !panel.clip.locked
                Layout.fillWidth: true
                spacing: Theme.spacingS
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                Text { text: qsTr("Fades"); color: Theme.text; font.weight: Font.DemiBold; font.pixelSize: Theme.fontSize }
                ValueSlider {
                    label: qsTr("Fade in")
                    from: 0; to: Math.max(1, (panel.clip.duration || 1) - (panel.clip.fadeOut || 0)); stepSize: 1; defaultValue: 0
                    value: panel.clip.fadeIn || 0
                    format: v => (v / panel.fps).toFixed(2) + " s"
                    onMoved: v => panel.setProp("fadeIn", Math.round(v))
                    onCommitted: panel.app.timeline.endEdit()
                }
                ValueSlider {
                    label: qsTr("Fade out")
                    from: 0; to: Math.max(1, (panel.clip.duration || 1) - (panel.clip.fadeIn || 0)); stepSize: 1; defaultValue: 0
                    value: panel.clip.fadeOut || 0
                    format: v => (v / panel.fps).toFixed(2) + " s"
                    onMoved: v => panel.setProp("fadeOut", Math.round(v))
                    onCommitted: panel.app.timeline.endEdit()
                }
            }

            // ---- Picture ----
            ColumnLayout {
                visible: panel.hasClip && panel.clip.hasVideo
                enabled: !panel.clip.locked
                Layout.fillWidth: true
                spacing: Theme.spacingS
                Rectangle { Layout.fillWidth: true; height: 1; color: Theme.border }
                RowLayout {
                    Layout.fillWidth: true
                    Text { text: qsTr("Picture"); color: Theme.text; font.weight: Font.DemiBold; font.pixelSize: Theme.fontSize; Layout.fillWidth: true }
                    IconButton {
                        iconName: "undo"
                        tip: qsTr("Reset picture adjustments")
                        onClicked: panel.app.timeline.resetPictureAdjustments(panel.clip.clipId)
                    }
                }
                ValueSlider {
                    label: qsTr("Opacity")
                    from: 0; to: 1; defaultValue: 1
                    value: panel.clip.opacity === undefined ? 1 : panel.clip.opacity
                    format: v => Math.round(v * 100) + " %"
                    onMoved: v => panel.setProp("opacity", v)
                    onCommitted: panel.app.timeline.endEdit()
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    visible: panel.clip.adjustmentsAvailable || false
                    spacing: Theme.spacingS
                    ValueSlider {
                        label: qsTr("Brightness")
                        from: -1; to: 1; defaultValue: 0
                        value: panel.clip.brightness || 0
                        format: v => (v >= 0 ? "+" : "") + v.toFixed(2)
                        onMoved: v => panel.setProp("brightness", v)
                        onCommitted: panel.app.timeline.endEdit()
                    }
                    ValueSlider {
                        label: qsTr("Contrast")
                        from: 0; to: 2; defaultValue: 1
                        value: panel.clip.contrast === undefined ? 1 : panel.clip.contrast
                        format: v => v.toFixed(2)
                        onMoved: v => panel.setProp("contrast", v)
                        onCommitted: panel.app.timeline.endEdit()
                    }
                    ValueSlider {
                        label: qsTr("Saturation")
                        from: 0; to: 3; defaultValue: 1
                        value: panel.clip.saturation === undefined ? 1 : panel.clip.saturation
                        format: v => v.toFixed(2)
                        onMoved: v => panel.setProp("saturation", v)
                        onCommitted: panel.app.timeline.endEdit()
                    }
                }
                Text {
                    visible: !(panel.clip.adjustmentsAvailable || false)
                    text: qsTr("Brightness, contrast and saturation need the FFmpeg “eq” filter, which is not available in this installation.")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
    }
}
