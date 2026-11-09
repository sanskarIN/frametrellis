// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Controls.impl
import QtQuick.Dialogs
import QtQuick.Layouts
import FrameTrellis

// Export settings, live validation, progress and result.
FtDialog {
    id: dialog

    required property AppController app
    readonly property ExportController exporter: app.exporter
    readonly property bool showProgress: exporter.state !== "idle" || exporter.batchTotal > 0
    readonly property string exportState: exporter.batchTotal > 0 ? exporter.batchState : exporter.state
    property string batchWarning: ""
    readonly property int batchSelectionCount: Number(batchCreator.checked) +
                                               Number(batchReview.checked) +
                                               Number(batchMobile.checked)
    property string outputPath: ""
    property bool presetInitialized: false
    readonly property var frameRates: [
        { label: qsTr("Match project"), value: "match" },
        { label: "23.976", value: "24000/1001" },
        { label: "24", value: "24" },
        { label: "25", value: "25" },
        { label: "29.97", value: "30000/1001" },
        { label: "30", value: "30" },
        { label: "50", value: "50" },
        { label: "59.94", value: "60000/1001" },
        { label: "60", value: "60" },
        { label: qsTr("Custom FPS…"), value: "custom" }
    ]

    title: qsTr("Export video")
    implicitWidth: 560
    closePolicy: exporter.busy ? Popup.NoAutoClose : Popup.CloseOnEscape

    function settings() {
        const selection = app.timeline.selectedFrameRange()
        const useSelection = rangeBox.currentIndex === 1
        const useCustom = rangeBox.currentIndex === 2
        const startFrame = useSelection ? (selection.start === undefined ? "-2" : String(selection.start))
                           : useCustom ? startFrameBox.text.trim() : "0"
        const endFrame = useSelection ? (selection.end === undefined ? "-2" : String(selection.end))
                         : useCustom ? endFrameBox.text.trim() : "-1"
        return {
            rangeStartFrame: startFrame,
            rangeEndFrame: endFrame,
            presetId: presetBox.currentIndex >= 0 ? exporter.presets[presetBox.currentIndex].id : "match",
            outputPath: outputPath,
            crf: Math.round(quality.value),
            speedPreset: speedBox.currentText,
            avoidUpscaling: noUpscaleCheck.checked,
            audioBitrateKbps: parseInt(audioBox.currentText),
            includeAudio: audioCheck.checked,
            frameRate: fpsBox.currentIndex < 0 ? "match"
                : dialog.frameRates[fpsBox.currentIndex].value === "custom"
                  ? customFps.text.trim() : dialog.frameRates[fpsBox.currentIndex].value
        }
    }
    function applyPreset(index) {
        const p = exporter.presets[index]
        quality.value = p.crf
        speedBox.currentIndex = Math.max(0, exporter.speedPresets.indexOf(p.speedPreset))
        noUpscaleCheck.checked = p.avoidUpscaling === true
        audioBox.currentIndex = Math.max(0, audioBox.model.indexOf(String(p.audioBitrateKbps)))
    }
    function selectedBatchIds() {
        const ids = []
        if (batchCreator.checked) ids.push("youtube1080p")
        if (batchReview.checked) ids.push("draft")
        if (batchMobile.checked) ids.push("review480p")
        return ids
    }
    function startBatchExport() {
        batchWarning = exporter.validateBatch(settings(), selectedBatchIds())
        if (batchWarning.length === 0)
            exporter.startBatch(settings(), selectedBatchIds())
    }
    function startExport() {
        if (exporter.fileExists(outputPath)) {
            overwrite.open()
            return
        }
        exporter.start(settings())
    }

    onOpened: {
        if (!exporter.busy && (exporter.state !== "idle" || exporter.batchTotal > 0))
            exporter.reset()
        batchWarning = ""
        if (outputPath.length === 0)
            outputPath = exporter.suggestedOutputPath()
        if (!presetInitialized && presetBox.currentIndex >= 0) {
            applyPreset(presetBox.currentIndex)
            presetInitialized = true
        }
    }

    FileDialog {
        id: fileDialog
        title: qsTr("Export to")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "mp4"
        nameFilters: [qsTr("MP4 video (*.mp4)")]
        selectedFile: dialog.app.fileUrl(dialog.outputPath)
        onAccepted: dialog.outputPath = dialog.app.localPath(selectedFile)
    }

    ConfirmDialog {
        id: overwrite
        title: qsTr("Replace the existing file?")
        message: qsTr("“%1” already exists. It will be replaced only if the export succeeds.").arg(dialog.outputPath)
        acceptText: qsTr("Replace")
        showDiscard: false
        onSaveRequested: dialog.exporter.start(dialog.settings())
    }

    contentItem: ColumnLayout {

        // ---- Settings ----
        GridLayout {
            visible: !dialog.showProgress
            Layout.fillWidth: true
            columns: 2
            columnSpacing: Theme.spacingM
            rowSpacing: Theme.spacingS

            Label { text: qsTr("Preset"); color: Theme.textMuted }
            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: presetBox
                    Layout.fillWidth: true
                    model: dialog.exporter.presets
                    textRole: "name"
                    onActivated: index => dialog.applyPreset(index)
                    Accessible.name: qsTr("Export preset")
                }
                FtButton {
                    text: qsTr("Reset preset")
                    enabled: presetBox.currentIndex >= 0
                    onClicked: dialog.applyPreset(presetBox.currentIndex)
                }
            }
            Item { width: 1; height: 1 }
            Text {
                Layout.fillWidth: true
                text: presetBox.currentIndex >= 0 ? dialog.exporter.presets[presetBox.currentIndex].description : ""
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }

            Label { text: qsTr("Save to"); color: Theme.textMuted }
            RowLayout {
                Layout.fillWidth: true
                TextField {
                    Layout.fillWidth: true
                    text: dialog.outputPath
                    onEditingFinished: dialog.outputPath = text
                    Accessible.name: qsTr("Output file")
                }
                FtButton { text: qsTr("Browse…"); onClicked: fileDialog.open() }
            }

            Label { text: qsTr("Export FPS"); color: Theme.textMuted }
            RowLayout {
                Layout.fillWidth: true
                ComboBox {
                    id: fpsBox
                    objectName: "exportFrameRateBox"
                    Layout.fillWidth: true
                    model: dialog.frameRates
                    textRole: "label"
                    valueRole: "value"
                    Accessible.name: qsTr("Export frame rate")
                }
                TextField {
                    id: customFps
                    objectName: "exportCustomFrameRate"
                    visible: fpsBox.currentIndex === dialog.frameRates.length - 1
                    Layout.preferredWidth: 155
                    placeholderText: qsTr("e.g. 48 or 30000/1001")
                    Accessible.name: qsTr("Custom export frame rate")
                }
            }
            Item { width: 1; height: 1 }
            Text {
                Layout.fillWidth: true
                text: qsTr("Changes the MP4 FPS only; project timing and audio speed stay unchanged.")
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }

            Label { text: qsTr("Timeline range"); color: Theme.textMuted }
            ComboBox {
                id: rangeBox
                objectName: "exportRangeBox"
                Layout.fillWidth: true
                model: [qsTr("Entire timeline"), qsTr("Selected clips' time span"),
                        qsTr("Custom frame interval")]
                currentIndex: 0
                Accessible.name: qsTr("Export timeline range")
            }
            RowLayout {
                Layout.fillWidth: true
                visible: rangeBox.currentIndex === 2
                spacing: Theme.spacingS
                Label { text: qsTr("Start"); color: Theme.textMuted }
                TextField {
                    id: startFrameBox
                    objectName: "exportStartFrame"
                    Layout.fillWidth: true
                    text: "0"
                    placeholderText: qsTr("First frame")
                    validator: IntValidator { bottom: 0; top: 1000000000 }
                    Accessible.name: qsTr("First project frame to export (inclusive)")
                }
                Label { text: qsTr("End"); color: Theme.textMuted }
                TextField {
                    id: endFrameBox
                    objectName: "exportEndFrame"
                    Layout.fillWidth: true
                    text: String(dialog.app.timeline.duration)
                    placeholderText: qsTr("Exclusive end frame")
                    validator: IntValidator { bottom: 0; top: 1000000000 }
                    Accessible.name: qsTr("Last project boundary (exclusive)")
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("Frame numbers use project FPS; start is included, end is excluded. " +
                           "Selection mode exports every visible track across the selected time span. " +
                           "The project and playhead are not changed.")
                visible: rangeBox.currentIndex !== 0
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
            Item { width: 1; height: 1 }

            Label { text: qsTr("Quality"); color: Theme.textMuted }
            RowLayout {
                Layout.fillWidth: true
                Text { text: qsTr("Best"); color: Theme.textFaint; font.pixelSize: Theme.fontSizeSmall }
                FtSlider {
                    id: quality
                    objectName: "exportQualitySlider"
                    Layout.fillWidth: true
                    from: 14; to: 34; stepSize: 1; value: 20
                    Accessible.name: qsTr("Quality (constant rate factor)")
                }
                Text { text: qsTr("Smallest"); color: Theme.textFaint; font.pixelSize: Theme.fontSizeSmall }
                Text { text: "CRF " + quality.value; color: Theme.text; font.family: Theme.monoFamily; font.pixelSize: Theme.fontSizeSmall }
            }

            Label { text: qsTr("Encoder speed"); color: Theme.textMuted }
            ComboBox {
                id: speedBox
                Layout.fillWidth: true
                model: dialog.exporter.speedPresets
                currentIndex: 5
                Accessible.name: qsTr("Encoder speed")
            }

            Label { text: qsTr("Scaling"); color: Theme.textMuted }
            FtCheckBox {
                id: noUpscaleCheck
                Layout.fillWidth: true
                text: qsTr("Do not upscale smaller projects (faster)")
                Accessible.name: qsTr("Avoid upscaling smaller projects")
            }

            Label { text: qsTr("Audio"); color: Theme.textMuted }
            RowLayout {
                FtCheckBox { id: audioCheck; checked: true; text: qsTr("Include audio (AAC)") }
                ComboBox {
                    id: audioBox
                    enabled: audioCheck.checked
                    model: ["96", "128", "160", "192", "256", "320"]
                    currentIndex: 3
                    Accessible.name: qsTr("Audio bitrate in kilobits per second")
                }
                Text { text: qsTr("kbit/s"); color: Theme.textFaint; font.pixelSize: Theme.fontSizeSmall }
            }

            Label { text: qsTr("Batch delivery"); color: Theme.textMuted }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: Theme.spacingS
                RowLayout {
                    Layout.fillWidth: true
                    FtCheckBox {
                        id: batchCreator
                        objectName: "exportBatchCreator"
                        text: qsTr("Creator 1080p")
                        checked: true
                    }
                    FtCheckBox {
                        id: batchReview
                        objectName: "exportBatchReview"
                        text: qsTr("Draft 720p")
                        checked: true
                    }
                    FtCheckBox {
                        id: batchMobile
                        objectName: "exportBatchMobile"
                        text: qsTr("Small 480p")
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    FtButton {
                        objectName: "exportBatchPresets"
                        text: qsTr("Export %1 variants").arg(dialog.batchSelectionCount)
                        enabled: dialog.batchSelectionCount >= 2 && validation.text.length === 0
                        onClicked: dialog.startBatchExport()
                    }
                    Text {
                        Layout.fillWidth: true
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSizeSmall
                        text: qsTr("Each preset gets its own -preset.mp4 file. " +
                                   "Existing outputs are never replaced; all variants use one project snapshot.")
                        wrapMode: Text.WordWrap
                    }
                }
                Text {
                    Layout.fillWidth: true
                    color: Theme.danger
                    visible: dialog.batchWarning.length > 0
                    text: dialog.batchWarning
                    wrapMode: Text.WordWrap
                }
            }

            Item { width: 1; height: 1 }
            Text {
                Layout.fillWidth: true
                text: dialog.exporter.describeOutput(dialog.settings())
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }

            Item { width: 1; height: 1 }
            Text {
                id: validation
                Layout.fillWidth: true
                text: dialog.opened ? dialog.exporter.validate(dialog.settings()) : ""
                visible: text.length > 0
                color: Theme.danger
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }

        // ---- Progress and result ----
        ColumnLayout {
            visible: dialog.showProgress
            Layout.fillWidth: true
            spacing: Theme.spacingM
            RowLayout {
                spacing: Theme.spacingS
                IconImage {
                    source: "qrc:/qt/qml/FrameTrellis/icons/" + (dialog.exportState === "succeeded" ? "check"
                            : dialog.exportState === "failed" ? "warning" : "export") + ".svg"
                    sourceSize.width: 22
                    sourceSize.height: 22
                    color: dialog.exportState === "succeeded" ? Theme.success
                         : dialog.exportState === "failed" ? Theme.danger : Theme.accent
                }
                Text {
                    Layout.fillWidth: true
                    text: {
                        switch (dialog.exportState) {
                        case "succeeded": return dialog.exporter.batchTotal > 0
                            ? qsTr("Batch finished") : qsTr("Export finished")
                        case "failed": return dialog.exporter.batchTotal > 0
                            ? qsTr("Batch stopped") : qsTr("Export failed")
                        case "cancelled": return dialog.exporter.batchTotal > 0
                            ? qsTr("Batch cancelled") : qsTr("Export cancelled")
                        case "cancelling": return qsTr("Cancelling…")
                        default: return qsTr("Exporting…")
                        }
                    }
                    color: Theme.text
                    font.pixelSize: Theme.fontSizeLarge
                    font.weight: Font.DemiBold
                }
            }
            Text {
                Layout.fillWidth: true
                text: dialog.exporter.outputPath
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WrapAnywhere
            }
            FtProgressBar {
                Layout.fillWidth: true
                from: 0; to: 1
                value: dialog.exporter.batchTotal > 0 ? dialog.exporter.batchProgress
                                                     : dialog.exporter.progress
                indeterminate: dialog.exporter.state === "starting"
                Accessible.name: qsTr("Export progress")
            }
            Text {
                text: (dialog.exporter.batchTotal > 0 ? dialog.exporter.batchText
                                                 : dialog.exporter.progressText) +
                      (dialog.exporter.encoder.length > 0 ? "  ·  " + dialog.exporter.encoder : "")
                color: Theme.textMuted
                font.pixelSize: Theme.fontSizeSmall
            }
            Text {
                Layout.fillWidth: true
                visible: dialog.exporter.batchTotal > 0
                         ? dialog.exporter.batchState === "failed"
                         : dialog.exporter.state === "failed"
                text: (dialog.exporter.batchTotal > 0 ? dialog.exporter.batchText
                                                     : dialog.exporter.errorMessage +
                                                       (dialog.exporter.errorDetail.length > 0 ? "\n" + dialog.exporter.errorDetail : ""))
                color: Theme.danger
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                visible: dialog.exporter.busy
                text: qsTr("You can keep editing while the export runs using a project snapshot. Preview playback pauses to conserve resources.")
                color: Theme.textFaint
                font.pixelSize: Theme.fontSizeSmall
                wrapMode: Text.WordWrap
            }
        }
    }

    footer: DialogButtons {
        Item { Layout.fillWidth: true }
        FtButton {
            text: qsTr("Copy error details")
            visible: dialog.exporter.state === "failed"
            onClicked: dialog.exporter.copyFailureDetails()
        }
        FtButton {
            text: qsTr("Show in folder")
            visible: dialog.exporter.state === "succeeded"
            onClicked: dialog.exporter.revealOutput()
        }
        FtButton {
            text: dialog.exporter.busy ? qsTr("Cancel export") : qsTr("Close")
            destructive: dialog.exporter.busy
            onClicked: dialog.exporter.busy ? dialog.exporter.cancel() : dialog.close()
        }
        FtButton {
            text: dialog.showProgress ? qsTr("New export") : qsTr("Export")
            primary: true
            visible: !dialog.exporter.busy
            enabled: dialog.showProgress || validation.text.length === 0
            iconName: "export"
            onClicked: dialog.showProgress ? dialog.exporter.reset() : dialog.startExport()
        }
    }
}
