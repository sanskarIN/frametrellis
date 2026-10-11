// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
pragma Singleton
import QtQuick

// Colours, spacing and type for the whole interface. Text colours keep at least 4.5:1 contrast
// against the panel colours they are used on.
QtObject {
    id: theme

    property string mode: "dark"
    readonly property bool dark: mode !== "light"

    readonly property color window: dark ? "#0f1216" : "#eef1f4"
    readonly property color panel: dark ? "#161a20" : "#ffffff"
    readonly property color panelAlt: dark ? "#1b2027" : "#f6f8fa"
    readonly property color raised: dark ? "#232932" : "#e8ecf0"
    readonly property color hover: dark ? "#2a313b" : "#dde3e9"
    readonly property color pressed: dark ? "#333b47" : "#cfd6de"
    readonly property color border: dark ? "#272e38" : "#d5dbe2"
    readonly property color borderStrong: dark ? "#3a4350" : "#b4bec9"
    readonly property color text: dark ? "#e8ecf1" : "#141a21"
    readonly property color textMuted: dark ? "#a5afbc" : "#4d5867"
    readonly property color textFaint: dark ? "#8691a0" : "#5f6b7a"
    readonly property color accent: dark ? "#3ccfb7" : "#0f8f7c"
    readonly property color accentStrong: dark ? "#5fe3cc" : "#0b7464"
    readonly property color accentText: dark ? "#06231e" : "#ffffff"
    readonly property color amber: dark ? "#f3b443" : "#b7791f"
    readonly property color iris: dark ? "#8592fb" : "#4c5bd4"
    readonly property color danger: dark ? "#ff7a7a" : "#c53030"
    readonly property color dangerFill: dark ? "#4a1f24" : "#fde2e2"
    readonly property color warning: dark ? "#f5c451" : "#9a6700"
    readonly property color success: dark ? "#58d68d" : "#1f8a4c"
    readonly property color focus: dark ? "#7fe8d6" : "#0b7464"
    readonly property color playhead: dark ? "#f4f6f8" : "#141a21"
    readonly property color selection: dark ? "#ffffff" : "#141a21"
    readonly property color monitorBackground: "#07090b"

    readonly property color trackVideo: dark ? "#151a20" : "#f7f9fb"
    readonly property color trackAudio: dark ? "#131820" : "#f2f5f8"
    readonly property color clipVideo: dark ? "#1f6f63" : "#7fd1c4"
    readonly property color clipVideoEdge: dark ? "#3ccfb7" : "#0f8f7c"
    readonly property color clipAudio: dark ? "#6b4f1d" : "#f2cf8d"
    readonly property color clipAudioEdge: dark ? "#f3b443" : "#b7791f"
    readonly property color clipImage: dark ? "#353d78" : "#bcc3fb"
    readonly property color clipImageEdge: dark ? "#8592fb" : "#4c5bd4"
    readonly property color clipOffline: dark ? "#5a2020" : "#f5c2c2"
    readonly property color clipText: dark ? "#f4f6f8" : "#0d1217"

    readonly property int spacingXs: 4
    readonly property int spacingS: 8
    readonly property int spacingM: 12
    readonly property int spacingL: 16
    readonly property int spacingXl: 24
    readonly property int radius: 6
    readonly property int radiusSmall: 4
    readonly property int fontSize: 13
    readonly property int fontSizeSmall: 11
    readonly property int fontSizeLarge: 16
    readonly property int fontSizeTitle: 20
    readonly property string monoFamily: Qt.platform.os === "windows" ? "Consolas" : (Qt.platform.os === "osx" ? "Menlo" : "monospace")
    readonly property int animation: 120

    function clipColor(kind, offline) {
        if (offline)
            return clipOffline
        return kind === "audio" ? clipAudio : (kind === "image" ? clipImage : clipVideo)
    }
    function clipEdge(kind, offline) {
        if (offline)
            return danger
        return kind === "audio" ? clipAudioEdge : (kind === "image" ? clipImageEdge : clipVideoEdge)
    }
}
