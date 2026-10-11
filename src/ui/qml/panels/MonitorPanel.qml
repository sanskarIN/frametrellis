// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Layouts
import FrameTrellis

// Program monitor: the edited sequence at the playhead, with transport controls.
PanelFrame {
    id: panel

    required property AppController app
    readonly property PlaybackController playback: app.playback

    title: qsTr("Program")

    tools: [
        Text {
            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
            text: panel.app.project.settingsSummary
            color: Theme.textFaint
            font.pixelSize: Theme.fontSizeSmall
            rightPadding: Theme.spacingS
        }
    ]

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            FrameView {
                id: view
                anchors.fill: parent
                source: panel.playback
                backgroundColor: Theme.monitorBackground
                Accessible.role: Accessible.Graphic
                Accessible.name: qsTr("Program monitor at %1").arg(panel.playback.timecode)
                onWidthChanged: resizeTimer.restart()
                onHeightChanged: resizeTimer.restart()
                Timer {
                    id: resizeTimer
                    interval: 150
                    onTriggered: panel.playback.setPreviewSize(Math.round(view.width * Screen.devicePixelRatio),
                                                               Math.round(view.height * Screen.devicePixelRatio))
                }
            }

            EmptyState {
                anchors.centerIn: parent
                visible: panel.app.timeline.empty
                iconName: "track-video"
                title: qsTr("Nothing to play yet")
                message: qsTr("Drag media from the bin onto the timeline, or double-click a media tile to append it.")
            }

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.margins: Theme.spacingS
                visible: panel.playback.warnings.length > 0
                radius: Theme.radiusSmall
                color: Theme.dangerFill
                width: warningRow.implicitWidth + 16
                height: warningRow.implicitHeight + 8
                Row {
                    id: warningRow
                    anchors.centerIn: parent
                    spacing: 6
                    IconImage {
                        source: "qrc:/qt/qml/FrameTrellis/icons/warning.svg"
                        sourceSize.width: 14
                        sourceSize.height: 14
                        color: Theme.danger
                    }
                    Text {
                        text: panel.playback.warnings.length === 1 ? panel.playback.warnings[0]
                              : qsTr("%1 clips are offline").arg(panel.playback.warnings.length)
                        color: Theme.danger
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            color: Theme.panelAlt
            Rectangle { width: parent.width; height: 1; color: Theme.border }

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.spacingM
                anchors.rightMargin: Theme.spacingM
                spacing: 2

                Text {
                    text: panel.playback.timecode
                    color: Theme.text
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSizeLarge
                    Accessible.name: qsTr("Playhead %1").arg(text)
                }
                Text {
                    text: " / " + panel.playback.durationTimecode
                    color: Theme.textFaint
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSize
                    Layout.fillWidth: true
                }

                IconButton { iconName: "skip-start"; tip: qsTr("Go to start (Home)"); onClicked: panel.playback.goToStart() }
                IconButton { iconName: "edit-prev"; tip: qsTr("Previous edit point (Up)"); onClicked: panel.app.timeline.previousEditPoint() }
                IconButton { iconName: "step-back"; tip: qsTr("Previous frame (Left)"); onClicked: panel.playback.step(-1) }
                IconButton {
                    iconName: panel.playback.playing ? "pause" : "play"
                    tip: panel.playback.playing ? qsTr("Pause (Space)") : qsTr("Play (Space)")
                    implicitWidth: 40
                    active: panel.playback.playing
                    enabled: !panel.app.timeline.empty
                    onClicked: panel.playback.togglePlay()
                }
                IconButton { iconName: "step-forward"; tip: qsTr("Next frame (Right)"); onClicked: panel.playback.step(1) }
                IconButton { iconName: "edit-next"; tip: qsTr("Next edit point (Down)"); onClicked: panel.app.timeline.nextEditPoint() }
                IconButton { iconName: "skip-end"; tip: qsTr("Go to end (End)"); onClicked: panel.playback.goToEnd() }

                Item { Layout.fillWidth: true }

                IconButton {
                    iconName: panel.playback.muted || !panel.playback.audioAvailable ? "volume-off" : "volume"
                    tip: !panel.playback.audioAvailable ? qsTr("No audio device: the preview plays silently")
                         : panel.playback.muted ? qsTr("Unmute preview") : qsTr("Mute preview")
                    enabled: panel.playback.audioAvailable
                    onClicked: panel.playback.muted = !panel.playback.muted
                }
            }
        }
    }
}
