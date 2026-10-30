// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "rendering/self_test.h"

#include "engine/frame_utils.h"
#include "engine/mlt_profile.h"
#include "engine/mlt_runtime.h"
#include "engine/timeline_graph.h"
#include "media/media_probe.h"
#include "project/project.h"
#include "project/project_serializer.h"
#include "rendering/export_settings.h"
#include "rendering/render_runner.h"
#include "rendering/sample_media.h"

#include <QDir>
#include <QImage>
#include <mlt++/Mlt.h>

#include <algorithm>
#include <atomic>

namespace ft::rendering {

namespace {

constexpr int kFps = 25;
constexpr std::int64_t kClipMicros = 2'000'000;

QString describe(const MediaInfo& info)
{
    QStringList parts;
    if (info.hasVideo) {
        parts << QStringLiteral("%1x%2 %3").arg(info.width).arg(info.height).arg(info.videoCodec);
    }
    if (info.hasAudio) {
        parts << QStringLiteral("%1 Hz %2").arg(info.audioSampleRate).arg(info.audioCodec);
    }
    if (info.durationUs > 0) {
        parts << QStringLiteral("%1 s").arg(static_cast<double>(info.durationUs) / 1e6, 0, 'f', 2);
    }
    return parts.join(QStringLiteral(", "));
}

bool closeTo(std::int64_t micros, std::int64_t expected)
{
    return std::abs(micros - expected) <= 100'000;
}

} // namespace

bool SelfTestReport::passed() const
{
    return !steps.empty() &&
           std::all_of(steps.begin(), steps.end(), [](const SelfTestStep& s) { return s.passed; });
}

SelfTestReport runSelfTest(const QString& workDirectory,
                           const std::function<void(const SelfTestStep&)>& onStep)
{
    SelfTestReport report;
    const auto record = [&](const QString& name, bool passed, const QString& detail) {
        report.steps.push_back({name, passed, detail});
        if (onStep) {
            onStep(report.steps.back());
        }
        return passed;
    };

    // 1. The engine and the services editing depends on.
    if (!engine::MltRuntime::isInitialized()) {
        record(QStringLiteral("engine"), false, QStringLiteral("The media engine is not running."));
        return report;
    }
    const engine::EngineCapabilities& caps = engine::MltRuntime::capabilities();
    const QStringList missing = caps.missingEssentials();
    QString engineDetail =
        QStringLiteral("MLT %1, modules in %2, compositor %3")
            .arg(caps.mltVersion, caps.repositoryPath,
                 caps.compositor.isEmpty() ? QStringLiteral("none") : caps.compositor);
    if (!caps.missingOptional().isEmpty()) {
        engineDetail += QStringLiteral("; optional services missing: %1")
                            .arg(caps.missingOptional().join(QStringLiteral(", ")));
    }
    if (!record(
            QStringLiteral("engine"), missing.isEmpty(),
            missing.isEmpty()
                ? engineDetail
                : QStringLiteral("missing services: %1").arg(missing.join(QStringLiteral(", "))))) {
        return report;
    }

    const QStringList encoders = availableVideoEncoders();
    const QString encoder = chooseVideoEncoder(encoders);
    if (!record(
            QStringLiteral("encoders"), !encoder.isEmpty(),
            encoder.isEmpty()
                ? QStringLiteral("no H.264 or MPEG-4 encoder is available")
                : QStringLiteral("%1 (of %2 video encoders)").arg(encoder).arg(encoders.size()))) {
        return report;
    }

    // 2. Sample media written by the engine itself.
    const QDir dir(workDirectory);
    if (!dir.exists() && !QDir().mkpath(workDirectory)) {
        record(QStringLiteral("media"), false,
               QStringLiteral("cannot create %1").arg(QDir::toNativeSeparators(workDirectory)));
        return report;
    }
    const QString videoPath = dir.filePath(QStringLiteral("self-test-video.mp4"));
    const QString tonePath = dir.filePath(QStringLiteral("self-test-tone.wav"));
    const QString stillPath = dir.filePath(QStringLiteral("self-test-still.png"));
    SampleVideoSpec spec;
    spec.segmentColors = {QColor(220, 60, 60), QColor(60, 160, 220)};
    spec.fps = kFps;
    spec.size = QSize(320, 180);
    QImage still(320, 180, QImage::Format_ARGB32);
    still.fill(QColor(60, 207, 183));
    const bool written = writeSampleVideo(videoPath, spec) && writeSampleTone(tonePath, 2.0) &&
                         still.save(stillPath);
    if (!record(QStringLiteral("media"), written,
                written ? QStringLiteral("wrote a sample video, tone and image")
                        : QStringLiteral("the engine could not write sample media"))) {
        return report;
    }

    // 3. Probing, as the editor does on import.
    std::vector<MediaAsset> assets;
    for (const QString& path : {videoPath, tonePath, stillPath}) {
        Result<MediaAsset> asset = media::createAsset(path);
        if (!asset) {
            record(QStringLiteral("probe"), false,
                   QStringLiteral("%1: %2").arg(QDir(path).dirName(), asset.error().toString()));
            return report;
        }
        assets.push_back(std::move(asset).value());
    }
    const MediaInfo& video = assets[0].info;
    const bool probed = video.hasVideo && video.hasAudio &&
                        closeTo(video.durationUs, kClipMicros) && assets[1].info.hasAudio &&
                        assets[2].info.kind == MediaKind::Image;
    if (!record(QStringLiteral("probe"), probed,
                QStringLiteral("video: %1; tone: %2; image: %3x%4")
                    .arg(describe(video), describe(assets[1].info))
                    .arg(assets[2].info.width)
                    .arg(assets[2].info.height))) {
        return report;
    }

    // 4. A project with video, an overlay and audio, saved and reloaded.
    ProjectSettings settings;
    settings.width = 640;
    settings.height = 360;
    settings.frameRate = FrameRate::integer(kFps);
    Project project = Project::createNew(QStringLiteral("FrameTrellis self-test"), settings);
    bool built = true;
    for (const MediaAsset& asset : assets) {
        built = built && project.addAsset(asset);
    }
    // Clips are as long as the shortest sample: encoders may round a file's length to whole
    // audio packets, so a sample can come out a frame shorter than requested.
    FrameCount clipLength = FrameCount{2} * kFps;
    for (const MediaAsset& asset : assets) {
        if (const std::optional<FrameCount> length = asset.lengthInFrames(settings.frameRate)) {
            clipLength = std::min(clipLength, *length);
        }
    }
    built = built && clipLength >= kFps;
    const std::vector<Track>& tracks = project.timeline().tracks(); // V1, V2, A1, A2
    const auto findTrack = [&](TrackKind kind, int ordinal) -> Id {
        int seen = 0;
        for (const Track& t : tracks) {
            if (t.kind == kind && seen++ == ordinal) {
                return t.id;
            }
        }
        return {};
    };
    const auto place = [&](const Id& trackId, const MediaAsset& asset, double opacity) {
        Clip clip;
        clip.id = createId();
        clip.assetId = asset.id;
        clip.duration = clipLength;
        clip.properties.video.opacity = opacity;
        return static_cast<bool>(
            project.timeline().insertClip(trackId, clip, project.sourceLookup()));
    };
    built = built && place(findTrack(TrackKind::Video, 0), assets[0], 1.0) &&
            place(findTrack(TrackKind::Video, 1), assets[2], 0.5) &&
            place(findTrack(TrackKind::Audio, 0), assets[1], 1.0) &&
            static_cast<bool>(project.validate());
    SubtitleTrack captions;
    captions.fontPercent = 6;
    captions.cues = {{QStringLiteral("self-test-subtitle"), 100, 1500,
                      QStringLiteral("FrameTrellis subtitles")}};
    built = built && project.setSubtitles(captions);
    report.projectPath = dir.filePath(QStringLiteral("self-test.ftproj"));
    const Status saved = built ? project_format::saveToFile(project, report.projectPath)
                               : Status(makeError(ErrorCode::InvalidArgument,
                                                  QStringLiteral("could not build the timeline")));
    Result<Project> reloaded =
        saved ? project_format::loadFromFile(report.projectPath) : Result<Project>(saved.error());
    const bool roundTrip = reloaded && reloaded->timeline() == project.timeline() &&
                           reloaded->media().size() == project.media().size() &&
                           reloaded->subtitles() == project.subtitles();
    if (!record(QStringLiteral("project"), roundTrip,
                roundTrip ? QStringLiteral("saved and reloaded %1 clips on %2 tracks")
                                .arg(3)
                                .arg(tracks.size())
                          : (reloaded ? QStringLiteral("the reloaded project differs")
                                      : reloaded.error().toString()))) {
        return report;
    }

    // 5. A real export through the same code the editor's render helper uses.
    ExportSettings exportSettings;
    report.outputPath = dir.filePath(QStringLiteral("self-test-export.mp4"));
    exportSettings.outputPath = report.outputPath;
    exportSettings.speedPreset = QStringLiteral("veryfast");
    RenderRunner runner(std::move(reloaded).value(), exportSettings, report.projectPath);
    std::atomic<bool> cancel{false};
    const Status rendered = runner.run(cancel);
    if (!record(QStringLiteral("export"), static_cast<bool>(rendered),
                rendered ? QStringLiteral("encoded with %1").arg(runner.encoder())
                         : rendered.error().toString())) {
        return report;
    }

    // Font plugins are runtime dependencies: decode actual subtitle pixels from the
    // packaged helper's export instead of accepting a successful encoder alone.
    auto profile = engine::createProfile(settings.width, settings.height, settings.frameRate);
    auto decoded = engine::TimelineGraphBuilder::openProducer(*profile, report.outputPath,
                                                              MediaKind::Video, false);
    const auto whitePixels = [](const QImage& image) {
        int count = 0;
        for (int y = image.height() / 2; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                const QColor pixel = image.pixelColor(x, y);
                if (pixel.red() > 200 && pixel.green() > 200 && pixel.blue() > 200)
                    ++count;
            }
        }
        return count;
    };
    const bool captionPixels =
        decoded &&
        whitePixels(engine::grabFrame(*decoded, 10, QSize(settings.width, settings.height))) > 10;
    if (!record(QStringLiteral("subtitles"), captionPixels,
                captionPixels
                    ? QStringLiteral("headless font rendering and encoded caption pixels verified")
                    : QStringLiteral("subtitle text was not visible in the encoded file"))) {
        return report;
    }

    // 6. The exported file has the expected shape.
    Result<MediaInfo> output = media::probeMedia(report.outputPath);
    const bool verified = output && output->hasVideo && output->hasAudio &&
                          output->width == settings.width && output->height == settings.height &&
                          closeTo(output->durationUs,
                                  settings.frameRate.framesToMicroseconds(clipLength).value_or(0));
    record(QStringLiteral("verify"), verified,
           output ? describe(*output) : output.error().toString());
    return report;
}

} // namespace ft::rendering
