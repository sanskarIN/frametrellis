// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import FrameTrellis

FtDialog {
    id: dialog
    required property AppController app
    property var report: ({})
    readonly property var summary: report.summary || ({})
    readonly property var allFindings: report.findings || []
    readonly property var tracks: report.tracks || []
    readonly property bool validReport: !report.error && report.schemaVersion === 1
    readonly property var categories: [
        { label: qsTr("All findings"), code: "" },
        { label: qsTr("Picture coverage"), code: "picture-gap" },
        { label: qsTr("Audio coverage"), code: "audio-gap" },
        { label: qsTr("Unavailable media"), code: "unavailable-media" },
        { label: qsTr("Markers beyond end"), code: "marker-beyond-end" },
        { label: qsTr("Subtitles beyond end"), code: "subtitle-beyond-end" },
        { label: qsTr("Subtitles without samples"), code: "subtitle-without-sample" }
    ]
    readonly property var filteredFindings: {
        const code = categoryBox.currentIndex >= 0
            ? categories[categoryBox.currentIndex].code : ""
        if (!code.length)
            return allFindings
        const results = []
        for (let i = 0; i < allFindings.length; ++i) {
            if (allFindings[i].code === code)
                results.push(allFindings[i])
        }
        return results
    }

    signal exportJsonRequested()
    signal exportHtmlRequested()

    title: qsTr("Timeline review")
    implicitWidth: 900
    height: Math.min(720, parent ? parent.height - 24 : 720)
    closePolicy: Popup.CloseOnEscape
    objectName: "timelineReviewDialog"

    function refresh() {
        report = app.project.timelineReview()
    }

    function seekToFinding(finding) {
        if (!finding.canSeek)
            return
        app.playback.pause()
        app.playback.seek(finding.startFrame)
        close()
    }

    onOpened: {
        app.playback.pause()
        categoryBox.currentIndex = 0
        viewTabs.currentIndex = 0
        refresh()
    }

    Connections {
        target: dialog.app.project
        function onDocumentReplaced() {
            if (dialog.visible)
                dialog.close()
        }
    }

    component MetricTile: Rectangle {
        required property string caption
        required property string value
        property color valueColor: Theme.text
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: 67
        color: Theme.panelAlt
        border.color: Theme.border
        radius: Theme.radius
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: Theme.spacingS
            spacing: Theme.spacingXs
            Text {
                Layout.fillWidth: true
                text: parent.parent.caption
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
            Text {
                Layout.fillWidth: true
                text: parent.parent.value
                color: parent.parent.valueColor
                font.pixelSize: Theme.fontSizeLarge
                font.weight: Font.DemiBold
                elide: Text.ElideRight
                textFormat: Text.PlainText
            }
        }
    }

    contentItem: ColumnLayout {
        spacing: Theme.spacingS

        Text {
            Layout.fillWidth: true
            visible: dialog.validReport
            text: qsTr("%1 · %2 × %3 · %4 fps")
                .arg(dialog.report.projectName || "")
                .arg(dialog.report.width || 0)
                .arg(dialog.report.height || 0)
                .arg(dialog.report.frameRate || "")
            color: Theme.text
            font.pixelSize: Theme.fontSizeLarge
            font.weight: Font.DemiBold
            elide: Text.ElideRight
            textFormat: Text.PlainText
        }
        Text {
            Layout.fillWidth: true
            text: dialog.report.error || qsTr("Checks available clip placements, not decoded pixels or sound. Gaps may be intentional; effects and automation can alter the final output.")
            color: dialog.report.error ? Theme.danger : Theme.textMuted
            font.pixelSize: Theme.fontSizeSmall
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
        }

        GridLayout {
            visible: dialog.validReport
            Layout.fillWidth: true
            columns: 3
            columnSpacing: Theme.spacingS
            rowSpacing: Theme.spacingS
            MetricTile {
                caption: qsTr("Sequence duration")
                value: dialog.summary.durationTimecode || "—"
            }
            MetricTile {
                caption: qsTr("Tracks / clips")
                value: qsTr("%1 / %2").arg(dialog.summary.tracks || 0).arg(dialog.summary.clips || 0)
            }
            MetricTile {
                caption: qsTr("Unavailable used assets")
                value: String(dialog.summary.unavailableMedia || 0)
                valueColor: (dialog.summary.unavailableMedia || 0) ? Theme.danger : Theme.text
            }
            MetricTile {
                caption: qsTr("Uncovered picture frames")
                value: String(dialog.summary.pictureGapFrames || 0)
                valueColor: (dialog.summary.pictureGapFrames || 0) ? Theme.warning : Theme.text
            }
            MetricTile {
                caption: qsTr("Uncovered audio frames")
                value: String(dialog.summary.audioGapFrames || 0)
                valueColor: (dialog.summary.audioGapFrames || 0) ? Theme.warning : Theme.text
            }
            MetricTile {
                caption: qsTr("Review findings")
                value: String(dialog.summary.findingCount || 0)
            }
        }

        TabBar {
            id: viewTabs
            Layout.fillWidth: true
            visible: dialog.validReport
            TabButton { text: qsTr("Findings") }
            TabButton { text: qsTr("Track summary") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: dialog.validReport
            currentIndex: viewTabs.currentIndex

            ColumnLayout {
                spacing: Theme.spacingS
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: qsTr("Show"); color: Theme.textMuted }
                    ComboBox {
                        id: categoryBox
                        objectName: "timelineReviewFilter"
                        Layout.preferredWidth: 245
                        model: dialog.categories
                        textRole: "label"
                    }
                    Item { Layout.fillWidth: true }
                    Text {
                        text: qsTr("%1 shown").arg(dialog.filteredFindings.length)
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: (dialog.report.omittedFindings || 0) > 0
                    text: qsTr("The dialog retains the first 200 findings. Export a report for up to 5,000 findings and full totals.")
                    color: Theme.warning
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: Theme.panelAlt
                    border.color: Theme.border
                    radius: Theme.radius
                    clip: true

                    ListView {
                        id: findingsList
                        objectName: "timelineReviewFindings"
                        anchors.fill: parent
                        anchors.margins: 1
                        clip: true
                        spacing: 1
                        model: dialog.filteredFindings
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property var modelData
                            width: ListView.view.width
                            height: Math.max(60, findingRow.implicitHeight + 16)
                            color: Theme.panel
                            RowLayout {
                                id: findingRow
                                anchors.fill: parent
                                anchors.margins: Theme.spacingS
                                spacing: Theme.spacingM
                                ColumnLayout {
                                    Layout.fillWidth: true
                                    spacing: Theme.spacingXs
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.title || ""
                                        color: modelData.code === "unavailable-media" ? Theme.danger : Theme.text
                                        font.weight: Font.DemiBold
                                        font.pixelSize: Theme.fontSize
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.subject || qsTr("Sequence")
                                        color: Theme.textMuted
                                        font.pixelSize: Theme.fontSizeSmall
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                    }
                                    Text {
                                        Layout.fillWidth: true
                                        text: qsTr("%1 → %2 (exclusive) · %3 frames")
                                            .arg(modelData.startTimecode || "")
                                            .arg(modelData.endTimecodeExclusive || "")
                                            .arg(modelData.durationFrames || 0)
                                        color: Theme.textFaint
                                        font.family: Theme.monoFamily
                                        font.pixelSize: Theme.fontSizeSmall
                                        textFormat: Text.PlainText
                                        elide: Text.ElideRight
                                    }
                                }
                                FtButton {
                                    text: qsTr("Go to")
                                    enabled: modelData.canSeek === true
                                    Accessible.name: qsTr("Go to finding at %1").arg(modelData.startTimecode)
                                    onClicked: dialog.seekToFinding(modelData)
                                }
                            }
                        }
                    }
                    Text {
                        anchors.centerIn: parent
                        width: parent.width - 24
                        visible: findingsList.count === 0
                        text: qsTr("No findings in the selected view.")
                        color: Theme.textMuted
                        wrapMode: Text.WordWrap
                        horizontalAlignment: Text.AlignHCenter
                    }
                }
            }

            ColumnLayout {
                spacing: Theme.spacingS
                Text {
                    Layout.fillWidth: true
                    text: qsTr("Occupied frames count clip placements on each track. Picture and audio totals below respect visibility, mute and source availability.")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontSizeSmall
                    wrapMode: Text.WordWrap
                }
                ListView {
                    objectName: "timelineReviewTracks"
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    spacing: Theme.spacingS
                    model: dialog.tracks
                    ScrollBar.vertical: ScrollBar {}
                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        height: trackRow.implicitHeight + 20
                        color: Theme.panelAlt
                        radius: Theme.radius
                        border.color: Theme.border
                        ColumnLayout {
                            id: trackRow
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: Theme.spacingXs
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("%1 · %2").arg(modelData.name || "")
                                    .arg(modelData.kind === "audio" ? qsTr("Audio") : qsTr("Video"))
                                color: Theme.text
                                font.weight: Font.DemiBold
                                font.pixelSize: Theme.fontSize
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("%1 clips · %2 transitions · %3 unavailable clips")
                                    .arg(modelData.clips || 0)
                                    .arg(modelData.transitions || 0)
                                    .arg(modelData.unavailableClips || 0)
                                color: modelData.unavailableClips ? Theme.danger : Theme.textMuted
                                font.pixelSize: Theme.fontSizeSmall
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Occupied %1 · Picture %2 · Audio %3 frames")
                                    .arg(modelData.occupiedFrames || 0)
                                    .arg(modelData.pictureFrames || 0)
                                    .arg(modelData.audioFrames || 0)
                                color: Theme.textMuted
                                font.pixelSize: Theme.fontSizeSmall
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: modelData.hidden || modelData.muted || modelData.locked
                                text: [modelData.hidden ? qsTr("Hidden") : "",
                                       modelData.muted ? qsTr("Muted") : "",
                                       modelData.locked ? qsTr("Locked") : ""]
                                      .filter(v => v.length).join(" · ")
                                color: Theme.warning
                                font.pixelSize: Theme.fontSizeSmall
                            }
                        }
                    }
                }
            }
        }

        Item { Layout.fillHeight: true; visible: !dialog.validReport }
    }

    footer: DialogButtons {
        FtButton {
            text: qsTr("Refresh")
            objectName: "timelineReviewRefresh"
            onClicked: dialog.refresh()
        }
        FtButton {
            text: qsTr("Export JSON…")
            enabled: dialog.validReport
            onClicked: dialog.exportJsonRequested()
        }
        FtButton {
            text: qsTr("Export HTML…")
            enabled: dialog.validReport
            onClicked: dialog.exportHtmlRequested()
        }
        Item { Layout.fillWidth: true }
        FtButton { text: qsTr("Close"); primary: true; onClicked: dialog.close() }
    }
}
