// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "rendering/export_settings.h"

#include "core/rational.h"

#include <QDir>
#include <QFileInfo>
#include <QStorageInfo>

#include <cmath>
#include <numeric>

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
         QStringLiteral("Faster H.264 encoding at original resolution; file sizes may vary."), 0,
         22, QStringLiteral("veryfast"), 192, true},
        {QStringLiteral("quick1080p"), QStringLiteral("Quick export (up to 1080p)"),
         QStringLiteral("Fast encoding up to 1080p; never enlarge smaller projects."), 1080, 22,
         QStringLiteral("veryfast"), 192, true},
        {QStringLiteral("draft"), QStringLiteral("Fast draft (up to 720p)"),
         QStringLiteral("Fast, lower-quality review export without unnecessary upscaling."), 720,
         28, QStringLiteral("veryfast"), 128, true},
        {QStringLiteral("turbo"), QStringLiteral("Turbo preview (up to 720p)"),
         QStringLiteral("Superfast review encoding; compression efficiency may decrease."), 720, 26,
         QStringLiteral("superfast"), 128, true},
        {QStringLiteral("review480p"), QStringLiteral("Low-bandwidth review (up to 480p)"),
         QStringLiteral("Small H.264/AAC review MP4; never upscale smaller projects."), 480, 27,
         QStringLiteral("veryfast"), 96, true},
        {QStringLiteral("youtube1080p"), QStringLiteral("Creator upload (up to 1080p)"),
         QStringLiteral("High-quality H.264/AAC upload at up to 1080p, without upscaling."), 1080,
         18, QStringLiteral("medium"), 192, true},
        {QStringLiteral("youtube1440p"), QStringLiteral("Creator upload (up to 1440p)"),
         QStringLiteral("High-quality H.264/AAC upload at up to 1440p, without upscaling."), 1440,
         18, QStringLiteral("medium"), 256, true},
        {QStringLiteral("youtube4k"), QStringLiteral("Creator upload (up to 4K)"),
         QStringLiteral("High-quality H.264/AAC upload at up to 4K, without upscaling."), 2160, 18,
         QStringLiteral("medium"), 256, true},
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

Result<FrameRate> outputFrameRate(const ExportSettings& settings, const ProjectSettings& project)
{
    const QString text = settings.frameRate.trimmed();
    if (text == QLatin1String("match")) {
        return project.frameRate;
    }
    const std::optional<Rational> value = Rational::fromString(text);
    const std::optional<FrameRate> rate =
        value ? FrameRate::create(value->numerator(), value->denominator()) : std::nullopt;
    if (!rate || rate->toDouble() < 1.0 || rate->toDouble() > 240.0) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("Export frame rate must be from 1 to 240 fps (for "
                                        "example 24, 29.97 or 30000/1001)."));
    }
    return *rate;
}

Result<FrameCount> outputFrameCount(FrameCount projectFrames, const FrameRate& projectRate,
                                    const FrameRate& exportRate)
{
    if (projectFrames < 1 || projectFrames > kMaxTimelineFrames) {
        return makeError(ErrorCode::OutOfRange, QStringLiteral("The sequence length is invalid."));
    }
    if (projectRate == exportRate) {
        return projectFrames;
    }
    // Exact cross multiplication avoids rounding an intermediate microsecond clock.
    // Such rounding could incorrectly add a frame even when input and output rates match.
    const std::int64_t numerator = exportRate.numerator() * projectRate.denominator();
    const std::int64_t denominator = exportRate.denominator() * projectRate.numerator();
    const std::int64_t divisor = std::gcd(numerator, denominator);
    const auto frames =
        mulDiv(projectFrames, numerator / divisor, denominator / divisor, Rounding::Up);
    if (!frames || *frames < 1 || *frames > kMaxTimelineFrames) {
        return makeError(
            ErrorCode::OutOfRange,
            QStringLiteral("The selected FPS would exceed the output timeline limit."));
    }
    return *frames;
}

Result<ExportFrameBounds> resolveExportFrameBounds(const ExportSettings& settings,
                                                   FrameCount timelineFrames,
                                                   const FrameRate& projectRate,
                                                   const FrameRate& exportRate)
{
    const FrameCount start = settings.rangeStartFrame;
    const FrameCount end = settings.rangeEndFrame == -1 ? timelineFrames : settings.rangeEndFrame;
    if (timelineFrames <= 0 || timelineFrames > kMaxTimelineFrames || start < 0 || end <= start ||
        end > timelineFrames) {
        return makeError(
            ErrorCode::OutOfRange,
            QStringLiteral("Export range must have a start and a later exclusive end inside "
                           "the timeline (in project frames)."));
    }
    const auto outStart = start == 0 ? Result<FrameCount>(FrameCount{0})
                                     : outputFrameCount(start, projectRate, exportRate);
    const auto outEnd = outputFrameCount(end, projectRate, exportRate);
    if (!outStart) {
        return outStart.error();
    }
    if (!outEnd) {
        return outEnd.error();
    }
    if (*outStart >= *outEnd) {
        return makeError(
            ErrorCode::OutOfRange,
            QStringLiteral("The selected export range is shorter than one output frame at "
                           "the chosen FPS."));
    }
    return ExportFrameBounds{start, end, *outStart, *outEnd};
}

QSize outputSize(const ExportSettings& settings, const ProjectSettings& project)
{
    // A nonzero width overrides the preset's short-side sizing for this export
    // only. Keep the original aspect ratio and round the derived height to even
    // pixels, as required by H.264's yuv420p encoder. Validation checks bounds.
    if (settings.customWidth != 0) {
        if (settings.customWidth < 0 || settings.customWidth > kMaxFrameDimension) {
            return {};
        }
        if (settings.avoidUpscaling && settings.customWidth >= project.width) {
            return {project.width, project.height};
        }
        const double height = static_cast<double>(project.height) * settings.customWidth /
                              project.width;
        const int evenHeight = std::max(2, static_cast<int>(std::lround(height / 2.0)) * 2);
        return {settings.customWidth, evenHeight};
    }
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
    const Result<FrameRate> fps = outputFrameRate(settings, project.settings());
    if (!fps) {
        return fps.error();
    }
    if (const Result<FrameCount> frames =
            outputFrameCount(project.timeline().duration(), project.settings().frameRate, *fps);
        !frames) {
        return frames.error();
    }
    if (const auto bounds = resolveExportFrameBounds(settings, project.timeline().duration(),
                                                     project.settings().frameRate, *fps);
        !bounds) {
        return bounds.error();
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
    if (settings.protectExistingOutput && output.exists()) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("Batch output already exists. Choose another filename."));
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

    // Check the requested short side before outputSize(): absurdly large values could
    // overflow the intermediate integer dimensions while scaling an extreme aspect ratio.
    // A negative value is invalid; zero is the documented "match project" setting.
    if (settings.shortSide < 0 || settings.shortSide > kMaxFrameDimension) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The requested export size must be between 0 and %1.")
                             .arg(kMaxFrameDimension));
    }
    if (settings.customWidth < 0 || settings.customWidth > kMaxFrameDimension ||
        (settings.customWidth != 0 &&
         (settings.customWidth < kMinFrameDimension || settings.customWidth % 2 != 0))) {
        return makeError(
            ErrorCode::OutOfRange,
            QStringLiteral("Custom output width must be an even number between %1 and %2.")
                .arg(kMinFrameDimension)
                .arg(kMaxFrameDimension));
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

Status verifyExportStreams(const MediaInfo& encoded, bool includeAudio)
{
    if (encoded.kind != MediaKind::Video || !encoded.hasVideo) {
        return makeError(ErrorCode::Io, QStringLiteral("The rendered file has no video stream."));
    }
    if (includeAudio && (!encoded.hasAudio || encoded.audioCodec != QLatin1String("aac"))) {
        return makeError(
            ErrorCode::Io,
            QStringLiteral("The rendered file is missing the expected AAC audio stream."));
    }
    if (!includeAudio && encoded.hasAudio) {
        return makeError(ErrorCode::Io,
                         QStringLiteral("The rendered video unexpectedly contains audio."));
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
