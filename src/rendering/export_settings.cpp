// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "rendering/export_settings.h"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

#include <cmath>

namespace ft::rendering {

QList<ExportPreset> exportPresets()
{
    return {
        {QStringLiteral("match"), QStringLiteral("Match project"),
         QStringLiteral("H.264/AAC MP4 at the project's resolution and frame rate."), 0, 20,
         QStringLiteral("medium"), 192},
        {QStringLiteral("1080p"), QStringLiteral("1080p"),
         QStringLiteral("H.264/AAC MP4 scaled so the shorter side is 1080 pixels."), 1080, 20,
         QStringLiteral("medium"), 192},
        {QStringLiteral("720p"), QStringLiteral("720p"),
         QStringLiteral("H.264/AAC MP4 scaled so the shorter side is 720 pixels."), 720, 21,
         QStringLiteral("medium"), 160},
        {QStringLiteral("2160p"), QStringLiteral("2160p (4K)"),
         QStringLiteral("H.264/AAC MP4 scaled so the shorter side is 2160 pixels."), 2160, 20,
         QStringLiteral("medium"), 256},
        {QStringLiteral("quick"), QStringLiteral("Quick export (project size)"),
         QStringLiteral("Faster H.264 encoding at the original resolution; slightly larger files."),
         0, 22, QStringLiteral("veryfast"), 192, true},
        {QStringLiteral("quick1080p"), QStringLiteral("Quick export (up to 1080p)"),
         QStringLiteral("Fast encoding up to 1080p; never enlarge smaller projects."),
         1080, 22, QStringLiteral("veryfast"), 192, true},
        {QStringLiteral("draft"), QStringLiteral("Fast draft (up to 720p)"),
         QStringLiteral("Fast, lower-quality review export without unnecessary upscaling."),
         720, 28, QStringLiteral("veryfast"), 128, true},
        {QStringLiteral("turbo"), QStringLiteral("Turbo preview (up to 720p)"),
         QStringLiteral("Superfast encoding for reviews; larger files and lower image quality."),
         720, 26, QStringLiteral("superfast"), 128, true},
    };
}

ExportSettings applyPreset(const ExportPreset& preset, const QString& outputPath)
{
    ExportSettings s;
    s.outputPath = outputPath;
    s.shortSide = preset.shortSide;
    s.avoidUpscaling = preset.avoidUpscaling;
    s.crf = preset.crf;
    s.speedPreset = preset.speedPreset;
    s.audioBitrateKbps = preset.audioBitrateKbps;
    return s;
}

QStringList speedPresets()
{
    return {QStringLiteral("ultrafast"), QStringLiteral("superfast"), QStringLiteral("veryfast"),
            QStringLiteral("faster"),    QStringLiteral("fast"),      QStringLiteral("medium"),
            QStringLiteral("slow"),      QStringLiteral("slower"),    QStringLiteral("veryslow")};
}

QSize outputSize(const ExportSettings& settings, const ProjectSettings& project)
{
    if (settings.shortSide <= 0) {
        return {project.width, project.height};
    }
    const int shortSide = std::min(project.width, project.height);
    if (settings.avoidUpscaling && settings.shortSide >= shortSide) {
        return {project.width, project.height};
    }
    const double scale = static_cast<double>(settings.shortSide) / shortSide;
    auto even = [](double v) { return std::max(2, static_cast<int>(std::lround(v / 2.0)) * 2); };
    return {even(project.width * scale), even(project.height * scale)};
}

Status validateExport(const ExportSettings& settings, const Project& project,
                      const QString& projectFilePath)
{
    if (project.timeline().duration() <= 0) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("The timeline is empty. Add clips before exporting."));
    }
    int offline = 0;
    for (const Track& track : project.timeline().tracks()) {
        for (const Clip& clip : track.clips) {
            const MediaAsset* asset = project.asset(clip.assetId);
            if (!asset || !QFileInfo::exists(asset->filePath)) {
                ++offline;
            }
        }
    }
    if (offline > 0) {
        return makeError(
            ErrorCode::Media,
            QStringLiteral(
                "%1 clip(s) use media that is offline. Relink the missing files before exporting.")
                .arg(offline));
    }

    const QString path = settings.outputPath.trimmed();
    if (path.isEmpty()) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Choose where to save the exported file."));
    }
    const QFileInfo output(path);
    if (!output.isAbsolute()) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("The output path must be absolute."));
    }
    if (output.suffix().compare(QLatin1String("mp4"), Qt::CaseInsensitive) != 0) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("The output file name must end in .mp4."));
    }
    if (output.exists() && !output.isFile()) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("The output path is a folder, not a file."));
    }
    const QFileInfo directory(output.absolutePath());
    if (!directory.exists() || !directory.isDir()) {
        return makeError(ErrorCode::NotFound, QStringLiteral("The output folder does not exist."));
    }
    if (!directory.isWritable()) {
        return makeError(ErrorCode::Io, QStringLiteral("The output folder is not writable."));
    }
    const QString outputKey = canonicalMediaKey(output.absoluteFilePath());
    for (const MediaAsset& asset : project.media()) {
        if (canonicalMediaKey(asset.filePath) == outputKey) {
            return makeError(
                ErrorCode::Conflict,
                QStringLiteral(
                    "The output file would overwrite the source media \"%1\". Choose another name.")
                    .arg(asset.name));
        }
    }
    if (!projectFilePath.isEmpty() && canonicalMediaKey(projectFilePath) == outputKey) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("The output file would overwrite the project file."));
    }

    const QSize size = outputSize(settings, project.settings());
    if (size.width() < kMinFrameDimension || size.height() < kMinFrameDimension ||
        size.width() > kMaxFrameDimension || size.height() > kMaxFrameDimension) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The export resolution is out of range."));
    }
    if (settings.crf < 0 || settings.crf > 51) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The quality value must be between 0 and 51."));
    }
    if (!speedPresets().contains(settings.speedPreset)) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Unknown encoder speed preset."));
    }
    if (settings.includeAudio &&
        (settings.audioBitrateKbps < 32 || settings.audioBitrateKbps > 512)) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The audio bitrate must be between 32 and 512 kbit/s."));
    }
    const QStorageInfo storage(directory.absoluteFilePath());
    if (storage.isValid() && storage.bytesAvailable() >= 0 &&
        storage.bytesAvailable() < kMinimumFreeBytes) {
        return makeError(
            ErrorCode::Io,
            QStringLiteral("There is not enough free disk space in the output folder."));
    }
    return {};
}

QStringList preferredVideoEncoders()
{
    return {QStringLiteral("libx264"), QStringLiteral("libopenh264"), QStringLiteral("h264_mf"),
            QStringLiteral("h264_videotoolbox"), QStringLiteral("mpeg4")};
}

QString chooseVideoEncoder(const QStringList& available)
{
    for (const QString& candidate : preferredVideoEncoders()) {
        if (available.contains(candidate)) {
            return candidate;
        }
    }
    return {};
}

} // namespace ft::rendering
