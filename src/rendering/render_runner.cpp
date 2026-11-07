// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "rendering/render_runner.h"

#include "core/logging.h"
#include "engine/mlt_profile.h"
#include "engine/mlt_runtime.h"
#include "engine/timeline_graph.h"
#include "media/media_probe.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QThread>
#include <QUuid>
#include <mlt++/Mlt.h>

#include <cstdio>
#include <fcntl.h>
#include <filesystem>
#include <memory>

#ifdef Q_OS_WIN
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace ft::rendering {

namespace {

/// Points the process's standard output at the null device while it exists. MLT prints the
/// encoder list as YAML to stdout when asked for it, and stdout is the render helper's protocol
/// channel to the editor.
class StdoutSilencer
{
public:
    StdoutSilencer()
    {
        std::fflush(stdout);
#ifdef Q_OS_WIN
        m_saved = _dup(_fileno(stdout));
        const int null = _open("NUL", _O_WRONLY);
        if (m_saved >= 0 && null >= 0) {
            _dup2(null, _fileno(stdout));
        }
        if (null >= 0) {
            _close(null);
        }
#else
        m_saved = ::dup(STDOUT_FILENO);
        const int null = ::open("/dev/null", O_WRONLY | O_CLOEXEC);
        if (m_saved >= 0 && null >= 0) {
            ::dup2(null, STDOUT_FILENO);
        }
        if (null >= 0) {
            ::close(null);
        }
#endif
    }
    ~StdoutSilencer()
    {
        std::fflush(stdout);
        if (m_saved < 0) {
            return;
        }
#ifdef Q_OS_WIN
        _dup2(m_saved, _fileno(stdout));
        _close(m_saved);
#else
        ::dup2(m_saved, STDOUT_FILENO);
        ::close(m_saved);
#endif
    }
    StdoutSilencer(const StdoutSilencer&) = delete;
    StdoutSilencer& operator=(const StdoutSilencer&) = delete;

private:
    int m_saved = -1;
};

QString temporaryOutputPath(const QString& outputPath)
{
    const QFileInfo info(outputPath);
    const QString token = QUuid::createUuid().toString(QUuid::Id128).left(8);
    return info.absoluteDir().filePath(
        QStringLiteral(".%1.%2.part.mp4").arg(info.completeBaseName(), token));
}

/// Replaces the destination with the finished file in one step, so an existing export is never
/// lost to a half-finished rename.
Status moveIntoPlace(const QString& from, const QString& to)
{
#ifdef Q_OS_WIN
    const std::wstring source = QDir::toNativeSeparators(from).toStdWString();
    const std::wstring target = QDir::toNativeSeparators(to).toStdWString();
    if (!MoveFileExW(source.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return makeError(ErrorCode::Io,
                         QStringLiteral("The exported file could not be moved into place."),
                         QStringLiteral("Windows error %1").arg(GetLastError()));
    }
#else
    std::error_code error;
    std::filesystem::rename(std::filesystem::path(from.toStdString()),
                            std::filesystem::path(to.toStdString()), error);
    if (error) {
        return makeError(ErrorCode::Io,
                         QStringLiteral("The exported file could not be moved into place."),
                         QString::fromStdString(error.message()));
    }
#endif
    return {};
}

} // namespace

QStringList availableVideoEncoders()
{
    QStringList encoders;
    if (!engine::MltRuntime::isInitialized()) {
        return encoders;
    }
    Mlt::Profile profile;
    Mlt::Consumer consumer(profile, "avformat");
    if (!consumer.is_valid()) {
        return encoders;
    }
    consumer.set("vcodec", "list");
    {
        const StdoutSilencer silence;
        consumer.start(); // reports the list and returns without running
    }
    auto* list = static_cast<mlt_properties>(consumer.get_data("vcodec"));
    if (list) {
        Mlt::Properties codecs(list);
        for (int i = 0; i < codecs.count(); ++i) {
            if (const char* name = codecs.get(i)) {
                encoders << QString::fromUtf8(name);
            }
        }
    }
    consumer.stop();
    return encoders;
}

RenderRunner::RenderRunner(Project project, ExportSettings settings, QString projectFilePath)
    : m_project(std::move(project))
    , m_settings(std::move(settings))
    , m_projectFilePath(std::move(projectFilePath))
{}

Status RenderRunner::run(const std::atomic<bool>& cancelRequested, const StartedCallback& started,
                         const ProgressCallback& progress)
{
    if (!engine::MltRuntime::isInitialized()) {
        return makeError(ErrorCode::Engine, QStringLiteral("The media engine is not initialised."));
    }
    if (const Status valid = validateExport(m_settings, m_project, m_projectFilePath); !valid) {
        return valid;
    }
    m_encoder = chooseVideoEncoder(availableVideoEncoders());
    if (m_encoder.isEmpty()) {
        return makeError(
            ErrorCode::Engine,
            QStringLiteral(
                "No supported video encoder (H.264 or MPEG-4) is available in this FFmpeg build."));
    }

    const ProjectSettings& projectSettings = m_project.settings();
    const Result<FrameRate> selectedRate = outputFrameRate(m_settings, projectSettings);
    if (!selectedRate) {
        return selectedRate.error();
    }
    const FrameRate& exportRate = *selectedRate;
    const QSize size = outputSize(m_settings, projectSettings);
    // Build the edit in its original timebase: its clips, fades, keyframes and subtitle
    // cues must not be reinterpreted as frames at the chosen output rate.
    std::unique_ptr<Mlt::Profile> projectProfile =
        engine::createProfile(size.width(), size.height(), projectSettings.frameRate);
    engine::TimelineGraphBuilder builder(*projectProfile);
    engine::GraphOptions options;
    options.placeholdersForMissingMedia = false;
    Result<engine::GraphBuildResult> graph = builder.build(m_project, options);
    if (!graph) {
        return graph.error();
    }
    const FrameCount projectTotal = graph->duration;
    const Result<FrameCount> outputTotal =
        outputFrameCount(projectTotal, projectSettings.frameRate, exportRate);
    if (!outputTotal) {
        return outputTotal.error();
    }
    const FrameCount total = *outputTotal;
    std::unique_ptr<Mlt::Tractor> tractor = std::move(graph->tractor);

    std::unique_ptr<Mlt::Profile> selectedProfile;
    std::unique_ptr<Mlt::Chain> frameRateChain;
    std::unique_ptr<Mlt::Link> frameRateLink;
    Mlt::Profile* encodingProfile = projectProfile.get();
    Mlt::Producer* encodingProducer = tractor.get();
    if (exportRate != projectSettings.frameRate) {
        selectedProfile = engine::createProfile(size.width(), size.height(), exportRate);
        frameRateChain = std::make_unique<Mlt::Chain>(*selectedProfile);
        frameRateLink = std::make_unique<Mlt::Link>("timeremap");
        if (!frameRateChain->is_valid() || !frameRateLink->is_valid()) {
            return makeError(ErrorCode::Engine,
                             QStringLiteral("This MLT installation cannot convert export FPS."));
        }

        // Chain's timeremap link uses output-frame timestamps to fetch the corresponding
        // source timeline frames, including time-aligned audio. Explicit source-rate
        // metadata stops MLT from interpreting the original tractor at the output FPS.
        tractor->set("meta.media.frame_rate_num",
                     static_cast<int>(projectSettings.frameRate.numerator()));
        tractor->set("meta.media.frame_rate_den",
                     static_cast<int>(projectSettings.frameRate.denominator()));
        frameRateChain->set_source(*tractor);
        if (frameRateChain->attach(*frameRateLink) != 0) {
            return makeError(ErrorCode::Engine,
                             QStringLiteral("Could not prepare export FPS conversion."));
        }
        frameRateChain->set("length", static_cast<int>(total));
        frameRateChain->set_in_and_out(0, static_cast<int>(total - 1));
        encodingProfile = selectedProfile.get();
        encodingProducer = frameRateChain.get();
    }

    const QString finalPath = QFileInfo(m_settings.outputPath).absoluteFilePath();
    const QString partPath = temporaryOutputPath(finalPath);
    const QByteArray partEncoded = partPath.toUtf8();
    // Keep the consumer alive only while the encoder is running. On Windows its
    // destructor must release all avformat handles and producer references before
    // we destroy the FPS-conversion chain or probe/replace the temporary MP4.
    auto consumer = std::make_unique<Mlt::Consumer>(*encodingProfile, "avformat",
                                                    partEncoded.constData());
    if (!consumer->is_valid()) {
        return makeError(ErrorCode::Engine,
                         QStringLiteral("The encoder could not be initialised."));
    }
    consumer->set("f", "mp4");
    consumer->set("vcodec", m_encoder.toUtf8().constData());
    consumer->set("pix_fmt", "yuv420p");
    consumer->set("movflags", "+faststart");
    consumer->set("g", std::max(1, static_cast<int>(exportRate.toDouble() * 2.0)));
    if (m_encoder == QLatin1String("libx264")) {
        consumer->set("crf", m_settings.crf);
        consumer->set("preset", m_settings.speedPreset.toUtf8().constData());
    } else {
        // Encoders without CRF get a bitrate derived from resolution, frame rate and quality.
        const double pixelsPerSecond =
            static_cast<double>(size.width()) * size.height() * exportRate.toDouble();
        const double bitsPerPixel = std::clamp(0.25 - (m_settings.crf - 18) * 0.012, 0.03, 0.4);
        consumer->set("vb", QStringLiteral("%1k")
                               .arg(static_cast<qint64>(pixelsPerSecond * bitsPerPixel / 1000.0))
                               .toUtf8()
                               .constData());
    }
    if (m_settings.includeAudio) {
        consumer->set("acodec", "aac");
        consumer->set("ab",
                     QStringLiteral("%1k").arg(m_settings.audioBitrateKbps).toUtf8().constData());
        consumer->set("ar", projectSettings.sampleRate);
        consumer->set("ac", projectSettings.audioChannels);
    } else {
        consumer->set("an", 1);
    }
    // Frames are rendered one at a time (real_time = 0). MLT's parallel frame rendering
    // (negative real_time) produced corrupt (NaN) audio with MLT 7.22 on Linux and MLT 7.40 on
    // Windows, which made the AAC encoder fail. The video encoder still uses its own threads.
    consumer->set("real_time", 0);
    consumer->set("threads", 0);
    consumer->set("terminate_on_pause", 1);
    consumer->set("in", 0);
    consumer->set("out", static_cast<int>(total - 1));
    consumer->connect(*encodingProducer);
    tractor->set_speed(0);
    encodingProducer->set_speed(0);
    encodingProducer->seek(0);

    qCInfo(lcRender) << "Rendering" << total << "frames to" << finalPath << "with" << m_encoder;
    if (started) {
        started(m_encoder, total);
    }
    if (consumer->start() != 0) {
        // Release any open FFmpeg file handle before trying to remove its output,
        // particularly important for failed encoder startup on Windows.
        consumer.reset();
        QFile::remove(partPath);
        return makeError(ErrorCode::Engine, QStringLiteral("The encoder could not be started."));
    }
    tractor->set_speed(1);
    encodingProducer->set_speed(1);

    bool cancelled = false;
    FrameCount lastReported = -1;
    while (!consumer->is_stopped()) {
        if (cancelRequested.load()) {
            cancelled = true;
            break;
        }
        QThread::msleep(100);
        const FrameCount position = std::clamp<FrameCount>(consumer->position(), 0, total);
        if (progress && position != lastReported) {
            lastReported = position;
            progress(Progress{position, total});
        }
    }
    consumer->stop();
    consumer->purge();
    consumer.reset(); // release FFmpeg/MLT references and output handles first
    frameRateChain.reset();
    frameRateLink.reset();
    tractor.reset();

    // A cancel request counts until the result replaces anything, even if the encoder finished
    // in the meantime: the user asked for no output.
    const auto cancelledResult = [&] {
        QFile::remove(partPath);
        qCInfo(lcRender) << "Render cancelled";
        return makeError(ErrorCode::Cancelled, QStringLiteral("The export was cancelled."));
    };
    if (cancelled || cancelRequested.load()) {
        return cancelledResult();
    }

    // Verify the result before it replaces anything: it must exist and have the expected length.
    const QFileInfo part(partPath);
    Result<MediaInfo> written =
        part.exists() && part.size() > 0
            ? media::probeMedia(partPath)
            : Result<MediaInfo>(makeError(ErrorCode::Io, QStringLiteral("no output")));
    const auto expectedUs =
        projectSettings.frameRate.framesToMicroseconds(projectTotal).value_or(0);
    const auto toleranceUs = exportRate.framesToMicroseconds(3).value_or(0) + 50'000;
    // Verify the size as well as the duration. Fast/no-upscale presets must never
    // publish a video with dimensions different from those shown in the dialog.
    const bool expectedSize =
        written && written->width == size.width() && written->height == size.height();
    const bool expectedRate =
        written && written->frameRate == Rational(exportRate.numerator(), exportRate.denominator());
    if (!written || written->kind != MediaKind::Video || !expectedSize || !expectedRate ||
        std::abs(written->durationUs - expectedUs) > toleranceUs) {
        QFile::remove(partPath);
        const QStorageInfo storage(QFileInfo(finalPath).absolutePath());
        const bool diskFull = storage.isValid() && storage.bytesAvailable() >= 0 &&
                              storage.bytesAvailable() < kMinimumFreeBytes;
        const QString detail =
            written ? QStringLiteral(
                          "expected %1x%2 at %3 fps and %4 us, got %5x%6 at %7 fps and %8 us")
                          .arg(size.width())
                          .arg(size.height())
                          .arg(exportRate.displayName())
                          .arg(expectedUs)
                          .arg(written->width)
                          .arg(written->height)
                          .arg(written->frameRate.toString())
                          .arg(written->durationUs)
                    : written.error().toString();
        qCWarning(lcRender) << "Render verification failed:" << detail;
        return makeError(
            ErrorCode::Io,
            diskFull
                ? QStringLiteral("Rendering failed: the disk is full.")
                : QStringLiteral(
                      "Rendering failed: the output dimensions, FPS or duration were incorrect."),
            detail);
    }
    if (progress) {
        progress(Progress{total, total});
    }
    if (cancelRequested.load()) {
        return cancelledResult();
    }
    if (const Status moved = moveIntoPlace(partPath, finalPath); !moved) {
        QFile::remove(partPath);
        return moved;
    }
    qCInfo(lcRender) << "Render finished:" << finalPath;
    return {};
}

} // namespace ft::rendering
