// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// frametrellis-render: renders a FrameTrellis project to MP4 in a separate process so that an
// encoder crash can never take the editor (or the user's unsaved work) down with it.

#include "core/logging.h"
#include "core/version.h"
#include "engine/mlt_runtime.h"
#include "project/project_serializer.h"
#include "rendering/render_protocol.h"
#include "rendering/render_runner.h"
#include "rendering/self_test.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSet>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <string_view>
#include <thread>

#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

using namespace ft;
using namespace ft::rendering;

namespace {

std::atomic<bool> g_cancel{false};

void emitEvent(const QJsonObject& event)
{
    const QByteArray line = protocol::encode(event);
    std::fwrite(line.constData(), 1, static_cast<size_t>(line.size()), stdout);
    std::fflush(stdout);
}

void emitError(const Error& error)
{
    emitEvent({{QStringLiteral("event"), QStringLiteral("error")},
               {QStringLiteral("message"), error.message},
               {QStringLiteral("detail"), error.detail}});
}

extern "C" void handleSignal(int)
{
    g_cancel = true;
}

int runSelfTestCommand(const QString& directory)
{
    std::printf("FrameTrellis %s self-test\n", version::string);
    std::fflush(stdout);
    if (const Status engine = engine::MltRuntime::initialize(); !engine) {
        std::printf("FAIL engine: %s\n", qPrintable(engine.error().toString()));
        return protocol::Failure;
    }
    SelfTestReport report;
    {
        report = rendering::runSelfTest(directory, [](const SelfTestStep& step) {
            std::printf("%s %s: %s\n", step.passed ? "PASS" : "FAIL", qPrintable(step.name),
                        qPrintable(step.detail));
            std::fflush(stdout);
        });
    }
    engine::MltRuntime::shutdown();
    std::printf("Self-test %s.\n", report.passed() ? "passed" : "FAILED");
    return report.passed() ? protocol::Success : protocol::Failure;
}

void watchStdin()
{
    // A "cancel" line, or the parent closing the pipe (for example because it crashed), stops
    // the render. Raw OS reads are used instead of std::cin: a thread blocked inside stdio holds
    // the stream lock, which would deadlock the flush that runs when the process exits.
    std::thread([] {
        protocol::CancelCommandReader commands;
        char buffer[256];
        while (true) {
#ifdef Q_OS_WIN
            DWORD count = 0;
            if (!ReadFile(GetStdHandle(STD_INPUT_HANDLE), buffer, sizeof(buffer), &count,
                          nullptr) ||
                count == 0) {
                break;
            }
#else
            const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof(buffer));
            if (count < 0 && errno == EINTR) {
                continue;
            }
            if (count <= 0) {
                break;
            }
#endif
            if (commands.ingest(std::string_view(buffer, static_cast<size_t>(count)))) {
                break;
            }
        }
        g_cancel = true;
    }).detach();
}

} // namespace

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("frametrellis-render"));
    QCoreApplication::setApplicationVersion(QLatin1String(version::string));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Renders a FrameTrellis project to an H.264/AAC MP4 file."));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption projectOption(QStringLiteral("project"),
                                           QStringLiteral("Project file (.ftproj)."),
                                           QStringLiteral("file"));
    const QCommandLineOption outputOption(
        QStringLiteral("output"), QStringLiteral("Output .mp4 file."), QStringLiteral("file"));
    const QCommandLineOption inspectProjectOption(
        QStringLiteral("inspect-project"),
        QStringLiteral("Print a JSON project summary and media availability without rendering."));
    const QCommandLineOption shortSideOption(
        QStringLiteral("short-side"),
        QStringLiteral("Scale so the shorter side has this many pixels (0 = project size)."),
        QStringLiteral("pixels"), QStringLiteral("0"));
    const QCommandLineOption crfOption(QStringLiteral("crf"),
                                       QStringLiteral("Quality, 0 (best) to 51."),
                                       QStringLiteral("value"), QStringLiteral("20"));
    const QCommandLineOption presetOption(QStringLiteral("preset"),
                                          QStringLiteral("Encoder speed preset."),
                                          QStringLiteral("name"), QStringLiteral("medium"));
    const QCommandLineOption audioOption(QStringLiteral("audio-bitrate"),
                                         QStringLiteral("AAC bitrate in kbit/s."),
                                         QStringLiteral("kbps"), QStringLiteral("192"));
    const QCommandLineOption noAudioOption(QStringLiteral("no-audio"),
                                           QStringLiteral("Export without audio."));
    const QCommandLineOption noUpscaleOption(
        QStringLiteral("no-upscale"),
        QStringLiteral("Keep the source resolution if already smaller than the requested size."));
    const QCommandLineOption watchStdinOption(
        QStringLiteral("watch-stdin"),
        QStringLiteral("Cancel when stdin receives \"cancel\" or is closed."));
    const QCommandLineOption selfTestOption(
        QStringLiteral("self-test"),
        QStringLiteral("Check the installation: generate sample media in <directory>, edit and "
                       "export it, and report each step. Exits with 0 when everything works."),
        QStringLiteral("directory"));
    parser.addOptions({projectOption, outputOption, shortSideOption, crfOption, presetOption,
                       audioOption, noAudioOption, noUpscaleOption, watchStdinOption,
                       selfTestOption, inspectProjectOption});
    parser.process(app);

    if (parser.isSet(selfTestOption)) {
        return runSelfTestCommand(QFileInfo(parser.value(selfTestOption)).absoluteFilePath());
    }

    if (parser.isSet(inspectProjectOption)) {
        if (!parser.isSet(projectOption) || parser.isSet(outputOption)) {
            emitError(makeError(
                ErrorCode::InvalidArgument,
                QStringLiteral("--inspect-project requires --project and cannot use --output.")));
            return protocol::BadArguments;
        }
        const Result<Project> inspected = project_format::loadFromFile(parser.value(projectOption));
        if (!inspected) {
            emitError(inspected.error());
            return protocol::Failure;
        }
        int videoTracks = 0;
        int audioTracks = 0;
        QSet<Id> referencedIds;
        for (const Track& track : inspected->timeline().tracks()) {
            if (track.kind == TrackKind::Video) {
                ++videoTracks;
            } else {
                ++audioTracks;
            }
            for (const Clip& clip : track.clips) {
                referencedIds.insert(clip.assetId);
            }
        }
        int missingMedia = 0;
        int missingUsedMedia = 0;
        int unusedMedia = 0;
        for (const MediaAsset& asset : inspected->media()) {
            const QFileInfo file(asset.filePath);
            const bool missing = !file.exists() || !file.isFile();
            const bool used = referencedIds.contains(asset.id);
            if (missing) {
                ++missingMedia;
                if (used) {
                    ++missingUsedMedia;
                }
            }
            if (!used) {
                ++unusedMedia;
            }
        }
        // Metadata only: no MLT runtime, media decoding, destination files or project edits.
        // Local source paths are deliberately excluded from the machine-readable summary.
        emitEvent({{QStringLiteral("event"), QStringLiteral("project_inspection")},
                   {QStringLiteral("name"), inspected->name()},
                   {QStringLiteral("trackCount"), inspected->timeline().trackCount()},
                   {QStringLiteral("videoTrackCount"), videoTracks},
                   {QStringLiteral("audioTrackCount"), audioTracks},
                   {QStringLiteral("clipCount"), inspected->timeline().clipCount()},
                   {QStringLiteral("mediaCount"), static_cast<int>(inspected->media().size())},
                   {QStringLiteral("usedMediaCount"),
                    static_cast<int>(referencedIds.size())},
                   {QStringLiteral("unusedMediaCount"), unusedMedia},
                   {QStringLiteral("missingMediaCount"), missingMedia},
                   {QStringLiteral("missingUsedMediaCount"), missingUsedMedia},
                   {QStringLiteral("durationFrames"),
                    static_cast<double>(inspected->timeline().duration())}});
        return protocol::Success;
    }

    if (!parser.isSet(projectOption) || !parser.isSet(outputOption)) {
        emitError(makeError(ErrorCode::InvalidArgument,
                            QStringLiteral("Both --project and --output are required.")));
        return protocol::BadArguments;
    }
    ExportSettings settings;
    settings.outputPath = QFileInfo(parser.value(outputOption)).absoluteFilePath();
    bool ok1 = false;
    bool ok2 = false;
    bool ok3 = false;
    settings.shortSide = parser.value(shortSideOption).toInt(&ok1);
    settings.crf = parser.value(crfOption).toInt(&ok2);
    settings.audioBitrateKbps = parser.value(audioOption).toInt(&ok3);
    settings.speedPreset = parser.value(presetOption);
    settings.includeAudio = !parser.isSet(noAudioOption);
    settings.avoidUpscaling = parser.isSet(noUpscaleOption);
    if (!ok1 || !ok2 || !ok3) {
        emitError(makeError(ErrorCode::InvalidArgument,
                            QStringLiteral("Numeric options must be whole numbers.")));
        return protocol::BadArguments;
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);
    if (parser.isSet(watchStdinOption)) {
        watchStdin();
    }

    const QString projectPath = parser.value(projectOption);
    Result<Project> project = project_format::loadFromFile(projectPath);
    if (!project) {
        emitError(project.error());
        return protocol::Failure;
    }
    if (const Status engine = engine::MltRuntime::initialize(); !engine) {
        emitError(engine.error());
        return protocol::Failure;
    }

    Status result;
    {
        RenderRunner runner(std::move(project).value(), settings,
                            QFileInfo(projectPath).absoluteFilePath());
        result = runner.run(
            g_cancel,
            [](const QString& encoder, FrameCount total) {
                emitEvent({{QStringLiteral("event"), QStringLiteral("started")},
                           {QStringLiteral("encoder"), encoder},
                           {QStringLiteral("total"), static_cast<double>(total)}});
            },
            [](const RenderRunner::Progress& p) {
                emitEvent({{QStringLiteral("event"), QStringLiteral("progress")},
                           {QStringLiteral("frame"), static_cast<double>(p.frame)},
                           {QStringLiteral("total"), static_cast<double>(p.total)}});
            });
    }
    engine::MltRuntime::shutdown();

    if (result) {
        emitEvent({{QStringLiteral("event"), QStringLiteral("finished")},
                   {QStringLiteral("output"), settings.outputPath}});
        return protocol::Success;
    }
    if (result.error().code == ErrorCode::Cancelled) {
        emitEvent({{QStringLiteral("event"), QStringLiteral("cancelled")}});
        return protocol::Cancelled;
    }
    emitError(result.error());
    return protocol::Failure;
}
