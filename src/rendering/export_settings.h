// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"
#include "project/project.h"

#include <QList>
#include <QSize>
#include <QString>
#include <QStringList>

namespace ft::rendering {

/// Settings for exporting the timeline. The documented profile is an MP4 file with H.264 video
/// (yuv420p) and AAC audio.
struct ExportSettings
{
    QString outputPath;
    int shortSide = 0; ///< 0 keeps the project size; otherwise scales so the shorter side matches
    bool avoidUpscaling = false; ///< fast exports never magnify smaller source resolutions
    int crf = 20;                ///< constant rate factor, 0 (lossless) .. 51 (worst)
    QString speedPreset = QStringLiteral("medium");
    int audioBitrateKbps = 192;
    bool includeAudio = true;
    QString frameRate =
        QStringLiteral("match");    ///< match project or exact fps: 30, 29.97, 30000/1001
    FrameCount rangeStartFrame = 0; ///< project frames, inclusive
    FrameCount rangeEndFrame = -1;  ///< project frames, exclusive; -1 means timeline end

    friend bool operator==(const ExportSettings&, const ExportSettings&) = default;
};

struct ExportPreset
{
    QString id;
    QString name;
    QString description;
    int shortSide = 0;
    int crf = 20;
    QString speedPreset;
    int audioBitrateKbps = 192;
    bool avoidUpscaling = false;
};

QList<ExportPreset> exportPresets();
ExportSettings applyPreset(const ExportPreset& preset, const QString& outputPath);

QStringList speedPresets();

/// Output frame size for the settings and project, keeping the aspect ratio and even dimensions.
QSize outputSize(const ExportSettings& settings, const ProjectSettings& project);

/// Resolves an optional export-only FPS override without editing the project's frame rate.
/// Accepts an integer, decimal or exact rational, and preserves NTSC rates such as 30000/1001.
Result<FrameRate> outputFrameRate(const ExportSettings& settings, const ProjectSettings& project);

/// Calculates output frame count from real timeline duration, rounding up the last partial frame.
Result<FrameCount> outputFrameCount(FrameCount projectFrames, const FrameRate& projectRate,
                                    const FrameRate& exportRate);

/// Validated inclusive/exclusive bounds in both the source-project and export-FPS timebases.
/// Output bounds use exact rational arithmetic, rounding up to avoid including
/// video/audio before the requested start, even for NTSC FPS conversion.
struct ExportFrameBounds
{
    FrameCount projectStart = 0;
    FrameCount projectEnd = 0;
    FrameCount outputStart = 0;
    FrameCount outputEnd = 0;
    [[nodiscard]] FrameCount outputLength() const { return outputEnd - outputStart; }
    [[nodiscard]] FrameCount projectLength() const { return projectEnd - projectStart; }
};

Result<ExportFrameBounds> resolveExportFrameBounds(const ExportSettings& settings,
                                                   FrameCount timelineFrames,
                                                   const FrameRate& projectRate,
                                                   const FrameRate& exportRate);

/// Checks everything that can be known before rendering starts: an empty timeline, offline
/// media, an unsafe or unwritable output path, and out-of-range encoder settings.
/// projectFilePath, when known, is protected from being overwritten as well.
Status validateExport(const ExportSettings& settings, const Project& project,
                      const QString& projectFilePath = {});

/// Video encoders in order of preference; the first one MLT/FFmpeg provides is used.
QStringList preferredVideoEncoders();
QString chooseVideoEncoder(const QStringList& available);

inline constexpr qint64 kMinimumFreeBytes = 64LL * 1024 * 1024;

} // namespace ft::rendering
