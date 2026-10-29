// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "engine/frame_utils.h"
#include "engine/mlt_profile.h"
#include "engine/mlt_runtime.h"
#include "media/media_probe.h"
#include "media_fixtures.h"
#include "project/project_serializer.h"
#include "rendering/render_job.h"
#include "rendering/render_protocol.h"
#include "rendering/render_runner.h"
#include "rendering/self_test.h"

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <mlt++/Mlt.h>

using namespace ft;
using namespace ft::rendering;
using namespace ft::testing;

class RenderTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_video;
    QString m_tone;

    Project project(FrameCount videoFrames = 50)
    {
        ProjectSettings settings;
        settings.width = 160;
        settings.height = 90;
        settings.frameRate = *FrameRate::create(25, 1);
        Project p = Project::createNew(QStringLiteral("Render"), settings);
        Result<MediaAsset> video = media::createAsset(m_video);
        Result<MediaAsset> tone = media::createAsset(m_tone);
        if (!video || !tone) {
            return p;
        }
        (void)p.addAsset(*video);
        (void)p.addAsset(*tone);
        Clip v;
        v.id = createId();
        v.assetId = video->id;
        v.sourceIn = 25; // the green second, then blue
        v.duration = videoFrames;
        (void)p.timeline().insertClip(p.timeline().tracks()[0].id, v, p.sourceLookup());
        Clip a;
        a.id = createId();
        a.assetId = tone->id;
        a.duration = 40;
        (void)p.timeline().insertClip(p.timeline().tracks()[2].id, a, p.sourceLookup());
        return p;
    }

    QImage frameOf(const QString& path, int position)
    {
        auto profile = engine::createProfile(160, 90, *FrameRate::create(25, 1));
        const QByteArray resource = QByteArrayLiteral("avformat:") + path.toUtf8();
        Mlt::Producer producer(*profile, resource.constData());
        return engine::grabFrame(producer, position, QSize(160, 90));
    }

    QStringList partFiles() const
    {
        return QDir(m_dir.path())
            .entryList({QStringLiteral(".*.part.mp4")}, QDir::Files | QDir::Hidden);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(engine::MltRuntime::initialize());
        m_video = makeVideo(m_dir.path(), QStringLiteral("source.mp4"));
        m_tone = makeTone(m_dir.path(), QStringLiteral("tone.wav"), 2.0, 880.0);
        QVERIFY(!m_video.isEmpty() && !m_tone.isEmpty());
        QVERIFY(!availableVideoEncoders().isEmpty());
    }

    void rendersTheTimeline()
    {
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("export.mp4"));
        settings.speedPreset = QStringLiteral("ultrafast");
        RenderRunner runner(project(), settings);
        std::atomic<bool> cancel{false};
        QList<FrameCount> progress;
        QString startedEncoder;
        const Status rendered = runner.run(
            cancel, [&](const QString& encoder, FrameCount) { startedEncoder = encoder; },
            [&](const RenderRunner::Progress& p) { progress.append(p.frame); });
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error().toString()));
        QVERIFY(!startedEncoder.isEmpty());
        QCOMPARE(runner.encoder(), startedEncoder);
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.last(), FrameCount{50});
        QVERIFY(std::is_sorted(progress.begin(), progress.end()));

        Result<MediaInfo> info = media::probeMedia(settings.outputPath);
        QVERIFY(info);
        QCOMPARE(info->kind, MediaKind::Video);
        QCOMPARE(info->width, 160);
        QCOMPARE(info->height, 90);
        QVERIFY(info->hasAudio);
        QCOMPARE(info->audioCodec, QStringLiteral("aac"));
        QVERIFY2(qAbs(info->durationUs - 2'000'000) <= 100'000,
                 qPrintable(QString::number(info->durationUs)));
        // The picture follows the edit: green first, then blue.
        QVERIFY(isCloseTo(averageColor(frameOf(settings.outputPath, 5)), Qt::green));
        QVERIFY(isCloseTo(averageColor(frameOf(settings.outputPath, 40)), Qt::blue));
        QVERIFY(partFiles().isEmpty());
    }

    void unicodeExportPathProducesPlayableMedia()
    {
        const QString folder = m_dir.filePath(QStringLiteral("चैनल-媒体"));
        QVERIFY(QDir().mkpath(folder));
        ExportSettings settings;
        settings.outputPath = QDir(folder).filePath(QStringLiteral("वीडियो-测试.mp4"));
        settings.speedPreset = QStringLiteral("ultrafast");

        const std::atomic<bool> cancelled{false};
        const Status status = RenderRunner(project(25), settings).run(cancelled);
        QVERIFY2(status, qPrintable(status ? QString() : status.error().toString()));
        QVERIFY(QFile::exists(settings.outputPath));

        const Result<MediaInfo> info = media::probeMedia(settings.outputPath);
        QVERIFY(info);
        QCOMPARE(info->width, 160);
        QCOMPARE(info->height, 90);
        QVERIFY(info->hasAudio);
        QVERIFY(QDir(folder)
                    .entryList({QStringLiteral("*.part.mp4")}, QDir::Files | QDir::Hidden)
                    .isEmpty());
    }

    void exportPreservesTransparentTitleOverlay()
    {
        // An image overlay must survive encoding: opaque title pixels appear on top of the
        // video, while transparent pixels continue to reveal the footage underneath.
        const QString overlayPath = m_dir.filePath(QStringLiteral("export-title.png"));
        QImage title(QSize(160, 90), QImage::Format_ARGB32);
        title.fill(Qt::transparent);
        for (int y = 30; y < 60; ++y) {
            for (int x = 40; x < 120; ++x) {
                title.setPixelColor(x, y, Qt::yellow);
            }
        }
        QVERIFY(title.save(overlayPath));

        Project p = project(25);
        Result<MediaAsset> asset = media::createAsset(overlayPath);
        QVERIFY(asset);
        QVERIFY(p.addAsset(*asset));
        Clip clip;
        clip.id = createId();
        clip.assetId = asset->id;
        clip.duration = 25;
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[1].id, clip, p.sourceLookup()));

        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("export-title.mp4"));
        settings.includeAudio = false;
        settings.speedPreset = QStringLiteral("ultrafast");
        std::atomic<bool> cancel{false};
        const Status rendered = RenderRunner(p, settings).run(cancel);
        QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error().toString()));
        const QImage frame = frameOf(settings.outputPath, 5);
        QVERIFY(!frame.isNull());
        QVERIFY(isCloseTo(averageColor(frame, QRect(0, 0, 20, 20)), Qt::green));
        QVERIFY(isCloseTo(averageColor(frame, QRect(55, 37, 40, 15)), Qt::yellow));
        QVERIFY(partFiles().isEmpty());
    }

    void scalesWithShortSide()
    {
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("small.mp4"));
        settings.shortSide = 72;
        settings.speedPreset = QStringLiteral("ultrafast");
        settings.includeAudio = false;
        std::atomic<bool> cancel{false};
        QVERIFY(RenderRunner(project(25), settings).run(cancel));
        Result<MediaInfo> info = media::probeMedia(settings.outputPath);
        QVERIFY(info);
        QCOMPARE(info->width, 128);
        QCOMPARE(info->height, 72);
        QVERIFY(!info->hasAudio);
    }

    void quickExportAvoidsUnnecessaryUpscale()
    {
        // 160x90 fixture stays native size with a quick 1080p request. The audio
        // must remain valid; faster encoder presets must never re-enable MLT's
        // known-broken parallel audio path.
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("quick-no-upscale.mp4"));
        settings.shortSide = 1080;
        settings.avoidUpscaling = true;
        settings.speedPreset = QStringLiteral("veryfast");
        settings.includeAudio = true;
        const std::atomic<bool> cancelled{false};
        const Status result = RenderRunner(project(25), settings).run(cancelled);
        QVERIFY2(result, qPrintable(result ? QString() : result.error().toString()));
        const Result<MediaInfo> video = media::probeMedia(settings.outputPath);
        QVERIFY(video);
        QCOMPARE(video->width, 160);
        QCOMPARE(video->height, 90);
        QVERIFY(video->hasAudio);
        QCOMPARE(video->audioCodec, QStringLiteral("aac"));
        QVERIFY(partFiles().isEmpty());
    }

    void helperQuickExportHonoursNoUpscale()
    {
        // Cover the QProcess -> CLI -> RenderRunner propagation, not just its
        // direct in-process API.
        RenderJob job(QStringLiteral(FT_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("helper-quick.mp4"));
        settings.shortSide = 1080;
        settings.avoidUpscaling = true;
        settings.speedPreset = QStringLiteral("veryfast");
        QVERIFY(job.start(project(25), settings));
        QVERIFY(finished.wait(60'000));
        QCOMPARE(job.state(), RenderJob::State::Succeeded);
        const Result<MediaInfo> video = media::probeMedia(settings.outputPath);
        QVERIFY(video);
        QCOMPARE(video->width, 160);
        QCOMPARE(video->height, 90);
        QVERIFY(video->hasAudio);
    }

    void cancellationLeavesNothingBehind()
    {
        const QString output = m_dir.filePath(QStringLiteral("cancelled.mp4"));
        ExportSettings settings;
        settings.outputPath = output;
        settings.speedPreset = QStringLiteral("veryslow");
        std::atomic<bool> cancel{false};
        RenderRunner runner(project(), settings);
        const Status result =
            runner.run(cancel, {}, [&](const RenderRunner::Progress&) { cancel = true; });
        QVERIFY(!result);
        QCOMPARE(result.error().code, ErrorCode::Cancelled);
        QVERIFY(!QFile::exists(output));
        QVERIFY(partFiles().isEmpty());
    }

    void cancelAfterTheLastFrameStillDiscardsTheOutput()
    {
        // The encoder may finish in the moment the user cancels; the request still wins.
        const QString output = m_dir.filePath(QStringLiteral("late-cancel.mp4"));
        ExportSettings settings;
        settings.outputPath = output;
        settings.speedPreset = QStringLiteral("ultrafast");
        std::atomic<bool> cancel{false};
        const Status result =
            RenderRunner(project(), settings).run(cancel, {}, [&](const RenderRunner::Progress& p) {
                if (p.frame == p.total) {
                    cancel = true;
                }
            });
        QVERIFY(!result);
        QCOMPARE(result.error().code, ErrorCode::Cancelled);
        QVERIFY(!QFile::exists(output));
        QVERIFY(partFiles().isEmpty());
    }

    void invalidExportSizePreservesExistingFile()
    {
        const QString output = m_dir.filePath(QStringLiteral("bad-size-existing.mp4"));
        {
            QFile existing(output);
            QVERIFY(existing.open(QIODevice::WriteOnly));
            QCOMPARE(existing.write("previous successful output"), qint64{26});
        }

        ExportSettings settings;
        settings.outputPath = output;
        settings.shortSide = 1000000;
        settings.avoidUpscaling = true;
        std::atomic<bool> cancel{false};
        const Status result = RenderRunner(project(25), settings).run(cancel);
        QVERIFY(!result);
        QCOMPARE(result.error().code, ErrorCode::OutOfRange);

        QFile preserved(output);
        QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(), QByteArray("previous successful output"));
        QVERIFY(partFiles().isEmpty());
    }

    void existingFileIsReplacedOnlyOnSuccess()
    {
        const QString output = m_dir.filePath(QStringLiteral("existing.mp4"));
        {
            QFile old(output);
            QVERIFY(old.open(QIODevice::WriteOnly));
            old.write("previous export");
        }
        ExportSettings settings;
        settings.outputPath = output;
        settings.speedPreset = QStringLiteral("ultrafast");
        std::atomic<bool> cancel{false};
        RenderRunner cancelled(project(), settings);
        (void)cancelled.run(cancel, {}, [&](const RenderRunner::Progress&) { cancel = true; });
        QFile check(output);
        QVERIFY(check.open(QIODevice::ReadOnly));
        QCOMPARE(check.readAll(), QByteArray("previous export"));
        check.close();
        cancel = false;
        QVERIFY(RenderRunner(project(), settings).run(cancel));
        QVERIFY(media::probeMedia(output));
    }

    void refusesToOverwriteSourceMedia()
    {
        ExportSettings settings;
        settings.outputPath = m_video;
        std::atomic<bool> cancel{false};
        const qint64 before = QFileInfo(m_video).size();
        const Status result = RenderRunner(project(), settings).run(cancel);
        QVERIFY(!result);
        QCOMPARE(result.error().code, ErrorCode::Conflict);
        QCOMPARE(QFileInfo(m_video).size(), before);
    }

    void helperCannotOverwriteProjectWithMp4Extension()
    {
        // The headless CLI accepts a valid project document regardless of its filename.
        // A .mp4-named project must still be protected from export destination replacement.
        const QString projectFile = m_dir.filePath(QStringLiteral("source-project.mp4"));
        QVERIFY(project_format::saveToFile(project(25), projectFile));
        QFile original(projectFile);
        QVERIFY(original.open(QIODevice::ReadOnly));
        const QByteArray before = original.readAll();
        original.close();

        QProcess helper;
        helper.start(QStringLiteral(FT_RENDER_HELPER),
                     {QStringLiteral("--project"), projectFile, QStringLiteral("--output"),
                      projectFile, QStringLiteral("--preset"), QStringLiteral("ultrafast")});
        QVERIFY2(helper.waitForStarted(10'000), qPrintable(helper.errorString()));
        QVERIFY2(helper.waitForFinished(30'000), qPrintable(helper.errorString()));
        QCOMPARE(helper.exitStatus(), QProcess::NormalExit);
        QCOMPARE(helper.exitCode(), int(protocol::Failure));
        QVERIFY(helper.readAllStandardOutput().contains("overwrite the project file"));
        QFile preserved(projectFile);
        QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(), before);
        QVERIFY(partFiles().isEmpty());
    }

    void helperInspectsProjectWithoutProducingAnExport()
    {
        const QString source = m_dir.filePath(QStringLiteral("inspect-source.ftproj"));
        Project p = project(25);
        QVERIFY(project_format::saveToFile(p, source));
        QFile existing(source);
        QVERIFY(existing.open(QIODevice::ReadOnly));
        const QByteArray original = existing.readAll();
        existing.close();

        QProcess helper;
        helper.start(QStringLiteral(FT_RENDER_HELPER),
                     {QStringLiteral("--inspect-project"), QStringLiteral("--project"), source});
        QVERIFY2(helper.waitForStarted(10'000), qPrintable(helper.errorString()));
        QVERIFY2(helper.waitForFinished(15'000), qPrintable(helper.errorString()));
        QCOMPARE(helper.exitStatus(), QProcess::NormalExit);
        QCOMPARE(helper.exitCode(), int(protocol::Success));
        const auto report = protocol::decode(helper.readAllStandardOutput().trimmed());
        QVERIFY(report);
        QCOMPARE(report->value(QStringLiteral("event")).toString(),
                 QStringLiteral("project_inspection"));
        QCOMPARE(report->value(QStringLiteral("name")).toString(), QStringLiteral("Render"));
        QCOMPARE(report->value(QStringLiteral("trackCount")).toInt(), 4);
        QCOMPARE(report->value(QStringLiteral("videoTrackCount")).toInt(), 2);
        QCOMPARE(report->value(QStringLiteral("audioTrackCount")).toInt(), 2);
        QCOMPARE(report->value(QStringLiteral("clipCount")).toInt(), 2);
        QCOMPARE(report->value(QStringLiteral("mediaCount")).toInt(), 2);
        QCOMPARE(report->value(QStringLiteral("usedMediaCount")).toInt(), 2);
        QCOMPARE(report->value(QStringLiteral("unusedMediaCount")).toInt(), 0);
        QCOMPARE(report->value(QStringLiteral("missingMediaCount")).toInt(), 0);
        QCOMPARE(report->value(QStringLiteral("missingUsedMediaCount")).toInt(), 0);
        QCOMPARE(report->value(QStringLiteral("durationFrames")).toInt(), 40);
        QVERIFY(!report->contains(QStringLiteral("filePath")));

        QFile preserved(source);
        QVERIFY(preserved.open(QIODevice::ReadOnly));
        QCOMPARE(preserved.readAll(), original);
        QVERIFY(partFiles().isEmpty());

        // Unavailable source files are reported without making a structurally valid
        // project unreadable or causing the inspector to fail.
        MediaAsset offline = p.media().front();
        offline.filePath = m_dir.filePath(QStringLiteral("not-present.mp4"));
        QVERIFY(p.replaceAsset(offline));
        MediaAsset unused = offline;
        unused.id = createId();
        unused.name = QStringLiteral("Unused missing file");
        unused.filePath = m_dir.filePath(QStringLiteral("unused-not-present.mp4"));
        QVERIFY(p.addAsset(unused));
        const QString offlinePath = m_dir.filePath(QStringLiteral("inspect-offline.ftproj"));
        QVERIFY(project_format::saveToFile(p, offlinePath));
        QProcess second;
        second.start(QStringLiteral(FT_RENDER_HELPER), {QStringLiteral("--inspect-project"),
                                                        QStringLiteral("--project"), offlinePath});
        QVERIFY2(second.waitForStarted(10'000), qPrintable(second.errorString()));
        QVERIFY2(second.waitForFinished(15'000), qPrintable(second.errorString()));
        QCOMPARE(second.exitCode(), int(protocol::Success));
        const auto unavailable = protocol::decode(second.readAllStandardOutput().trimmed());
        QVERIFY(unavailable);
        QCOMPARE(unavailable->value(QStringLiteral("mediaCount")).toInt(), 3);
        QCOMPARE(unavailable->value(QStringLiteral("usedMediaCount")).toInt(), 2);
        QCOMPARE(unavailable->value(QStringLiteral("unusedMediaCount")).toInt(), 1);
        QCOMPARE(unavailable->value(QStringLiteral("missingMediaCount")).toInt(), 2);
        QCOMPARE(unavailable->value(QStringLiteral("missingUsedMediaCount")).toInt(), 1);
    }

    void helperInspectionRejectsInvalidArgumentAndMissingProject()
    {
        QProcess invalidArguments;
        invalidArguments.start(QStringLiteral(FT_RENDER_HELPER),
                               {QStringLiteral("--inspect-project"), QStringLiteral("--project"),
                                m_dir.filePath(QStringLiteral("missing.ftproj")),
                                QStringLiteral("--output"),
                                m_dir.filePath(QStringLiteral("should-not-exist.mp4"))});
        QVERIFY(invalidArguments.waitForStarted(10'000));
        QVERIFY(invalidArguments.waitForFinished(15'000));
        QCOMPARE(invalidArguments.exitCode(), int(protocol::BadArguments));

        QProcess missingProject;
        missingProject.start(QStringLiteral(FT_RENDER_HELPER),
                             {QStringLiteral("--inspect-project"), QStringLiteral("--project"),
                              m_dir.filePath(QStringLiteral("missing.ftproj"))});
        QVERIFY(missingProject.waitForStarted(10'000));
        QVERIFY(missingProject.waitForFinished(15'000));
        QCOMPARE(missingProject.exitCode(), int(protocol::Failure));
        const auto error = protocol::decode(missingProject.readAllStandardOutput().trimmed());
        QVERIFY(error);
        QCOMPARE(error->value(QStringLiteral("event")).toString(), QStringLiteral("error"));
        QVERIFY(!QFile::exists(m_dir.filePath(QStringLiteral("should-not-exist.mp4"))));
    }

    void helperProcessEndToEnd()
    {
        RenderJob job(QStringLiteral(FT_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        QSignalSpy progress(&job, &RenderJob::progressChanged);
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("helper.mp4"));
        settings.speedPreset = QStringLiteral("ultrafast");
        const Status started = job.start(project(), settings);
        QVERIFY2(started, qPrintable(started ? QString() : started.error().toString()));
        QVERIFY(finished.wait(60'000));
        QCOMPARE(job.state(), RenderJob::State::Succeeded);
        QVERIFY2(job.errorMessage().isEmpty(), qPrintable(job.errorMessage()));
        QCOMPARE(job.progress(), 1.0);
        QVERIFY(!job.encoder().isEmpty());
        QVERIFY(progress.count() >= 2);
        QVERIFY(media::probeMedia(settings.outputPath));
    }

    void helperProcessPreservesVolumeKeyframesInEncodedAudio()
    {
        Project p = project();
        const Id video = p.timeline().tracks()[0].clips.front().id;
        const Id audio = p.timeline().tracks()[2].clips.front().id;
        ClipProperties muted;
        muted.muted = true;
        QVERIFY(p.timeline().setClipProperties(video, muted));
        for (const AudioKeyframe& point :
             std::vector<AudioKeyframe>{{0, 0}, {10, -20}, {20, -20}, {30, 0}, {39, 0}}) {
            QVERIFY(p.timeline().setAudioKeyframe(audio, point.sourceFrame, point.decibels,
                                                  p.sourceLookup()));
        }
        RenderJob job(QStringLiteral(FT_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("volume-keyframes.mp4"));
        settings.speedPreset = QStringLiteral("ultrafast");
        QVERIFY(job.start(p, settings));
        QVERIFY(finished.wait(60'000));
        QVERIFY2(job.state() == RenderJob::State::Succeeded,
                 qPrintable(job.errorMessage() + job.errorDetail()));
        auto profile = engine::createProfile(160, 90, p.settings().frameRate);
        const QByteArray resource = QByteArrayLiteral("avformat:") + settings.outputPath.toUtf8();
        Mlt::Producer output(*profile, resource.constData());
        QVERIFY(output.is_valid());
        const auto peakAt = [&](int frame) {
            output.seek(frame);
            std::unique_ptr<Mlt::Frame> f(output.get_frame());
            return peakLevel(engine::frameToAudio(*f, 25.0, 48000, 2, frame).samples);
        };
        const int quiet = peakAt(15);
        const int full = peakAt(35);
        QVERIFY2(full > 5000 && quiet > full * 0.05 && quiet < full * 0.17,
                 qPrintable(QStringLiteral("encoded levels %1 / %2").arg(quiet).arg(full)));
        QVERIFY(partFiles().isEmpty());
    }

    void helperProcessPreservesOpacityKeyframesInEncodedPicture()
    {
        ProjectSettings profileSettings;
        profileSettings.width = 160;
        profileSettings.height = 90;
        profileSettings.frameRate = *FrameRate::create(25, 1);
        Project p = Project::createNew(QStringLiteral("Opacity export"), profileSettings);
        const QString blue =
            makeStill(m_dir.path(), QStringLiteral("opacity-blue.png"), Qt::blue, QSize(160, 90));
        const QString red =
            makeStill(m_dir.path(), QStringLiteral("opacity-red.png"), Qt::red, QSize(160, 90));
        const auto bottom = media::createAsset(blue);
        const auto top = media::createAsset(red);
        QVERIFY(bottom && top);
        QVERIFY(p.addAsset(*bottom));
        QVERIFY(p.addAsset(*top));
        Clip lower;
        lower.id = createId();
        lower.assetId = bottom->id;
        lower.duration = 50;
        Clip upper = lower;
        upper.id = createId();
        upper.assetId = top->id;
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[0].id, lower, p.sourceLookup()));
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[1].id, upper, p.sourceLookup()));
        for (const OpacityKeyframe& point :
             std::vector<OpacityKeyframe>{{0, 0}, {10, 0}, {20, 0.5}, {30, 1}, {49, 1}}) {
            QVERIFY(p.timeline().setOpacityKeyframe(upper.id, point.sourceFrame, point.opacity,
                                                    p.sourceLookup()));
        }
        RenderJob job(QStringLiteral(FT_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("opacity-keyframes.mp4"));
        settings.speedPreset = QStringLiteral("ultrafast");
        QVERIFY(job.start(p, settings));
        QVERIFY(finished.wait(60'000));
        QVERIFY2(job.state() == RenderJob::State::Succeeded,
                 qPrintable(job.errorMessage() + job.errorDetail()));
        QVERIFY(isCloseTo(averageColor(frameOf(settings.outputPath, 5)), Qt::blue, 30));
        QVERIFY(isCloseTo(averageColor(frameOf(settings.outputPath, 20)), QColor(128, 0, 128), 30));
        QVERIFY(isCloseTo(averageColor(frameOf(settings.outputPath, 35)), Qt::red, 30));
        QVERIFY(partFiles().isEmpty());
    }

    void helperProcessExportsPositionScaleAndRotationFromFormatFive()
    {
        ProjectSettings profile;
        profile.width = 160;
        profile.height = 90;
        profile.frameRate = *FrameRate::create(25, 1);
        Project p = Project::createNew(QStringLiteral("Transform export"), profile);
        const auto bottom = media::createAsset(makeStill(
            m_dir.path(), QStringLiteral("transform-blue.png"), Qt::blue, QSize(160, 90)));
        const auto top = media::createAsset(
            makeStill(m_dir.path(), QStringLiteral("transform-red.png"), Qt::red, QSize(160, 90)));
        QVERIFY(bottom && top);
        QVERIFY(p.addAsset(*bottom));
        QVERIFY(p.addAsset(*top));
        Clip lower;
        lower.id = createId();
        lower.assetId = bottom->id;
        lower.duration = 50;
        Clip upper = lower;
        upper.id = createId();
        upper.assetId = top->id;
        upper.sourceIn = 10;
        upper.properties.video.opacity = 0.5;
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[0].id, lower, p.sourceLookup()));
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[1].id, upper, p.sourceLookup()));
        QVERIFY(
            p.timeline().setTransformKeyframe(upper.id, 10, {-0.25, 0, 0.5, 0}, p.sourceLookup()));
        QVERIFY(
            p.timeline().setTransformKeyframe(upper.id, 50, {0.25, 0, 0.5, 90}, p.sourceLookup()));
        QVERIFY(p.timeline().setOpacityKeyframe(upper.id, 10, 0.5, p.sourceLookup()));
        QCOMPARE(project_format::toJson(p)[QStringLiteral("version")].toInt(), 5);
        RenderJob job(QStringLiteral(FT_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("transform-keyframes.mp4"));
        settings.speedPreset = QStringLiteral("ultrafast");
        QVERIFY(job.start(p, settings));
        QVERIFY(finished.wait(60'000));
        QVERIFY2(job.state() == RenderJob::State::Succeeded,
                 qPrintable(job.errorMessage() + job.errorDetail()));
        const QImage first = frameOf(settings.outputPath, 0);
        const QImage middle = frameOf(settings.outputPath, 20);
        const QImage last = frameOf(settings.outputPath, 40);
        const QColor blend(64, 0, 191);
        QVERIFY(isCloseTo(averageColor(first, QRect(35, 40, 10, 10)), blend, 25));
        QVERIFY(isCloseTo(averageColor(first, QRect(35, 10, 10, 5)), Qt::blue, 25));
        QVERIFY(isCloseTo(averageColor(middle, QRect(75, 40, 10, 10)), blend, 25));
        QVERIFY(isCloseTo(averageColor(last, QRect(115, 10, 10, 5)), blend, 25));
        QVERIFY(isCloseTo(averageColor(last, QRect(85, 40, 5, 10)), Qt::blue, 25));
        QVERIFY(partFiles().isEmpty());
    }

    void helperProcessCancel()
    {
        RenderJob job(QStringLiteral(FT_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("helper-cancel.mp4"));
        settings.speedPreset = QStringLiteral("veryslow");
        QVERIFY(job.start(project(), settings));
        QVERIFY(QTest::qWaitFor([&] { return job.state() == RenderJob::State::Running; }, 10'000));
        job.cancel();
        QCOMPARE(job.state(), RenderJob::State::Cancelling);
        QVERIFY(finished.wait(30'000));
        QCOMPARE(job.state(), RenderJob::State::Cancelled);
        QVERIFY(!QFile::exists(settings.outputPath));
        QVERIFY(QTest::qWaitFor([&] { return partFiles().isEmpty(); }, 5'000));
    }

    void helperProcessReportsErrors()
    {
        // The source disappears after the job validated it: the helper must fail cleanly.
        QTemporaryDir dir;
        const QString fragile = makeVideo(dir.path(), QStringLiteral("fragile.mp4"));
        ProjectSettings settings;
        settings.width = 160;
        settings.height = 90;
        settings.frameRate = *FrameRate::create(25, 1);
        Project p = Project::createNew(QStringLiteral("Fragile"), settings);
        Result<MediaAsset> asset = media::createAsset(fragile);
        QVERIFY(asset);
        (void)p.addAsset(*asset);
        Clip c;
        c.id = createId();
        c.assetId = asset->id;
        c.duration = 20;
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[0].id, c, p.sourceLookup()));

        RenderJob job(QStringLiteral(FT_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        ExportSettings exportSettings;
        exportSettings.outputPath = dir.filePath(QStringLiteral("never.mp4"));
        QVERIFY(job.start(p, exportSettings));
        QVERIFY(QFile::remove(fragile));
        QVERIFY(finished.wait(30'000));
        QCOMPARE(job.state(), RenderJob::State::Failed);
        QVERIFY(job.errorMessage().contains(QStringLiteral("offline")));
        QVERIFY(!QFile::exists(exportSettings.outputPath));
    }

    void selfTestPassesAndReportsEachStep()
    {
        QTemporaryDir dir;
        QStringList seen;
        const SelfTestReport report =
            runSelfTest(dir.filePath(QStringLiteral("self test")), // a space exercises path quoting
                        [&](const SelfTestStep& step) { seen << step.name; });
        for (const SelfTestStep& step : report.steps) {
            QVERIFY2(step.passed, qPrintable(step.name + QStringLiteral(": ") + step.detail));
        }
        QVERIFY(report.passed());
        QCOMPARE(seen, (QStringList{QStringLiteral("engine"), QStringLiteral("encoders"),
                                    QStringLiteral("media"), QStringLiteral("probe"),
                                    QStringLiteral("project"), QStringLiteral("export"),
                                    QStringLiteral("verify")}));
        QVERIFY(QFile::exists(report.projectPath));
        QVERIFY(QFile::exists(report.outputPath));
        // The generated project opens like any other project.
        QVERIFY(project_format::loadFromFile(report.projectPath));
    }

    void selfTestFailsWhenTheWorkFolderCannotBeCreated()
    {
        QTemporaryDir dir;
        const QString blocker = dir.filePath(QStringLiteral("file"));
        QFile file(blocker);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.close();
        const SelfTestReport report = runSelfTest(blocker + QStringLiteral("/inside"));
        QVERIFY(!report.passed());
        QCOMPARE(report.steps.back().name, QStringLiteral("media"));
        QVERIFY(!report.steps.back().passed);
    }

    void cleanupTestCase() { engine::MltRuntime::shutdown(); }
};

QTEST_GUILESS_MAIN(RenderTest)
#include "tst_render.moc"
