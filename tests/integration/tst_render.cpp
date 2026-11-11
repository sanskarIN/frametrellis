// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/project_document.h"
#include "editing/timeline_edits.h"
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
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <mlt++/Mlt.h>

#include <algorithm>
#include <cmath>

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

    Project project(FrameCount videoFrames = 50, FrameRate rate = FrameRate::integer(25))
    {
        ProjectSettings settings;
        settings.width = 160;
        settings.height = 90;
        settings.frameRate = rate;
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

    QImage frameOf(const QString& path, int position, FrameRate rate = FrameRate::integer(25))
    {
        auto profile = engine::createProfile(160, 90, rate);
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

    void helperListsExportPresetsAsJson()
    {
        // Works without an input project, output path, video decoding or an active render.
        QProcess helper;
        helper.start(QStringLiteral(FT_RENDER_HELPER), {QStringLiteral("--list-presets")});
        QVERIFY(helper.waitForFinished(10000));
        QCOMPARE(helper.exitStatus(), QProcess::NormalExit);
        QCOMPARE(helper.exitCode(), 0);
        QJsonParseError error;
        const QJsonDocument parsed =
            QJsonDocument::fromJson(helper.readAllStandardOutput(), &error);
        QCOMPARE(error.error, QJsonParseError::NoError);
        QVERIFY(parsed.isObject());
        const QJsonArray profiles = parsed.object().value(QStringLiteral("presets")).toArray();
        QVERIFY(profiles.size() >= 4);
        QStringList identifiers;
        for (const QJsonValue& value : profiles) {
            QVERIFY(value.isObject());
            const QJsonObject item = value.toObject();
            const QString id = item.value(QStringLiteral("id")).toString();
            QVERIFY(!id.isEmpty());
            QVERIFY(!identifiers.contains(id));
            identifiers.append(id);
            QVERIFY(item.value(QStringLiteral("name")).isString());
            QVERIFY(item.value(QStringLiteral("shortSide")).isDouble());
            QVERIFY(item.value(QStringLiteral("crf")).isDouble());
            QVERIFY(item.value(QStringLiteral("speedPreset")).isString());
            QVERIFY(item.value(QStringLiteral("audioBitrateKbps")).isDouble());
            QVERIFY(item.value(QStringLiteral("avoidUpscaling")).isBool());
        }
        QVERIFY(identifiers.contains(QStringLiteral("quick")));
        QVERIFY(identifiers.contains(QStringLiteral("quick1080p")));
        QVERIFY(identifiers.contains(QStringLiteral("turbo")));
        QVERIFY(identifiers.contains(QStringLiteral("youtube1080p")));
        QVERIFY(identifiers.contains(QStringLiteral("youtube4k")));
    }

    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(engine::MltRuntime::initialize());
        m_video = makeVideo(m_dir.path(), QStringLiteral("source.mp4"));
        m_tone = makeTone(m_dir.path(), QStringLiteral("tone.wav"), 2.0, 880.0);
        QVERIFY(!m_video.isEmpty() && !m_tone.isEmpty());
        QVERIFY(!availableVideoEncoders().isEmpty());
    }

    void preCancelledRunPreservesPreviousExportWithoutStartingEncoder()
    {
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("pre-cancel-existing.mp4"));
        settings.speedPreset = QStringLiteral("ultrafast");
        const QByteArray previous = QByteArrayLiteral("previous successful output");
        QFile output(settings.outputPath);
        QVERIFY(output.open(QIODevice::WriteOnly));
        QCOMPARE(output.write(previous), static_cast<qint64>(previous.size()));
        output.close();

        const std::atomic<bool> cancel{true};
        bool started = false;
        const Status result =
            RenderRunner(project(25), settings).run(cancel, [&](const QString&, FrameCount) {
                started = true;
            });
        QVERIFY(!result);
        QCOMPARE(result.error().code, ErrorCode::Cancelled);
        QVERIFY(!started);
        QVERIFY(output.open(QIODevice::ReadOnly));
        QCOMPARE(output.readAll(), previous);
        output.close();
        QVERIFY(partFiles().isEmpty());
    }

    void cancellationFromStartedCallbackLeavesNoPublishedOrPartialFile()
    {
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("callback-cancel.mp4"));
        settings.speedPreset = QStringLiteral("ultrafast");
        std::atomic<bool> cancel{false};
        int startedCount = 0;
        const Status result =
            RenderRunner(project(25), settings).run(cancel, [&](const QString&, FrameCount) {
                ++startedCount;
                cancel.store(true);
            });
        QVERIFY(!result);
        QCOMPARE(result.error().code, ErrorCode::Cancelled);
        QCOMPARE(startedCount, 1);
        QVERIFY(!QFile::exists(settings.outputPath));
        QVERIFY(partFiles().isEmpty());
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

    void exportFpsConvertsFramesWithoutSpeedingUpAudioOrChangingProject()
    {
        const FrameRate sourceRate = FrameRate::integer(25);
        const Project original = project(50, sourceRate); // 2 seconds with visible green/blue cuts
        const Project copy = original;
        for (const QString& chosen :
             {QStringLiteral("24"), QStringLiteral("30"), QStringLiteral("30000/1001")}) {
            ExportSettings settings;
            settings.outputPath = m_dir.filePath(
                QStringLiteral("fps-%1.mp4")
                    .arg(QString(chosen).replace(QLatin1Char('/'), QLatin1Char('-'))));
            settings.frameRate = chosen;
            settings.speedPreset = QStringLiteral("ultrafast");
            const Result<FrameRate> target = outputFrameRate(settings, original.settings());
            QVERIFY(target);
            const Result<FrameCount> expected = outputFrameCount(50, sourceRate, *target);
            QVERIFY(expected);
            FrameCount reportedTotal = -1;
            std::atomic<bool> cancel{false};
            const Status completed =
                RenderRunner(original, settings).run(cancel, [&](const QString&, FrameCount total) {
                    reportedTotal = total;
                });
            QVERIFY2(completed, qPrintable(completed ? QString() : completed.error().toString()));
            QCOMPARE(reportedTotal, *expected);
            const Result<MediaInfo> info = media::probeMedia(settings.outputPath);
            QVERIFY(info);
            QCOMPARE(info->frameRate, Rational(target->numerator(), target->denominator()));
            QVERIFY(info->hasAudio);
            QVERIFY2(qAbs(info->durationUs - 2'000'000) <= 100'000,
                     qPrintable(QString::number(info->durationUs)));
            QVERIFY(isCloseTo(averageColor(frameOf(settings.outputPath, 4, *target)), Qt::green));
            QVERIFY(isCloseTo(averageColor(frameOf(settings.outputPath,
                                                   static_cast<int>(*expected) - 4, *target)),
                              Qt::blue));
            QVERIFY(partFiles().isEmpty());
            QCOMPARE(original, copy); // no export setting may change project timebase or clips
        }
    }

    void helperAbnormalExitPreservesExistingOutputAndAllowsRetry()
    {
        // Unlike a real native access violation, this fixture exits cleanly with a
        // nonzero code after emitting started + oversized stderr. It exercises the
        // editor's process isolation and error-reporting boundary deterministically.
        const Project original = project(25);
        ExportSettings settings;
        const QString existing = m_dir.filePath(QStringLiteral("failed-helper-existing.mp4"));
        {
            QFile output(existing);
            QVERIFY(output.open(QIODevice::WriteOnly));
            QCOMPARE(output.write("previous successful output"), qint64{26});
        }
        settings.outputPath = existing;
        settings.speedPreset = QStringLiteral("ultrafast");
        // A previous render's similarly named partial file must not be touched.
        const QString unrelated =
            m_dir.filePath(QStringLiteral(".failed-helper-existing.unrelated.part.mp4"));
        {
            QFile untouched(unrelated);
            QVERIFY(untouched.open(QIODevice::WriteOnly));
            QCOMPARE(untouched.write("unrelated partial"), qint64{17});
        }
        RenderJob job(QStringLiteral(FT_FAKE_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        for (int attempt = 0; attempt < 2; ++attempt) {
            QVERIFY(job.start(original, settings));
            QVERIFY(finished.count() > 0 || finished.wait(15'000));
            QCOMPARE(job.state(), RenderJob::State::Failed);
            QCOMPARE(job.encoder(), QStringLiteral("failure-fixture"));
            QVERIFY(job.errorMessage().contains(QStringLiteral("Rendering failed")));
            QVERIFY2(job.errorDetail().contains(QStringLiteral("0x0000002a")),
                     qPrintable(job.errorDetail()));
            QVERIFY(job.errorDetail().contains(QStringLiteral("FT_SIMULATED_RENDER_FAILURE")));
            QVERIFY(job.errorDetail().size() <= 17'000);
            QFile preserved(existing);
            QVERIFY(preserved.open(QIODevice::ReadOnly));
            QCOMPARE(preserved.readAll(), QByteArray("previous successful output"));
            QFile untouched(unrelated);
            QVERIFY(untouched.open(QIODevice::ReadOnly));
            QCOMPARE(untouched.readAll(), QByteArray("unrelated partial"));
            QCOMPARE(partFiles(),
                     QStringList{QStringLiteral(".failed-helper-existing.unrelated.part.mp4")});
            finished.clear();
        }
        QVERIFY(QFile::remove(unrelated));
        QVERIFY(partFiles().isEmpty());
    }

    void cancelBeforeProcessStartedStillReachesHelper()
    {
        const Project original = project(25);
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("early-cancel.mp4"));
        RenderJob job(QStringLiteral(FT_CANCEL_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        bool cancelAtStarting = true;
        QObject::connect(&job, &RenderJob::stateChanged, &job,
                         [&job, &cancelAtStarting](RenderJob::State state) {
                             if ((cancelAtStarting && state == RenderJob::State::Starting) ||
                                 (!cancelAtStarting && state == RenderJob::State::Running)) {
                                 job.cancel();
                             }
                         });
        // Exercise the synchronous Starting callback (before the OS process
        // starts) and a normal Running cancellation on the same reusable job.
        for (int attempt = 0; attempt < 2; ++attempt) {
            cancelAtStarting = attempt == 0;
            QVERIFY(job.start(original, settings));
            QVERIFY(finished.count() > 0 || finished.wait(15'000));
            QCOMPARE(job.state(), RenderJob::State::Cancelled);
            QVERIFY(!QFile::exists(settings.outputPath));
            QVERIFY(partFiles().isEmpty());
            finished.clear();
        }
    }

    void cancellationRequestMustNotHideRendererError()
    {
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("fail-after-cancel.mp4"));
        RenderJob job(QStringLiteral(FT_CANCEL_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        QObject::connect(&job, &RenderJob::stateChanged, &job, [&job](RenderJob::State state) {
            if (state == RenderJob::State::Running) {
                job.cancel();
            }
        });
        QVERIFY(job.start(project(25), settings));
        QVERIFY(finished.count() > 0 || finished.wait(15'000));
        QCOMPARE(job.state(), RenderJob::State::Failed);
        QVERIFY2(job.errorDetail().contains(QStringLiteral("0x0000002a")),
                 qPrintable(job.errorDetail()));
        QVERIFY(!QFile::exists(settings.outputPath));
        QVERIFY(partFiles().isEmpty());
    }

    void cancellationWatchdogKillIsReportedAsCancelled()
    {
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("hang-after-cancel.mp4"));
        RenderJob job(QStringLiteral(FT_CANCEL_RENDER_HELPER));
        job.setKillGracePeriod(350);
        QSignalSpy finished(&job, &RenderJob::finished);
        QObject::connect(&job, &RenderJob::stateChanged, &job, [&job](RenderJob::State state) {
            if (state == RenderJob::State::Running) {
                job.cancel();
            }
        });
        QVERIFY(job.start(project(25), settings));
        QVERIFY(finished.count() > 0 || finished.wait(15'000));
        QCOMPARE(job.state(), RenderJob::State::Cancelled);
        QVERIFY(!QFile::exists(settings.outputPath));
        QVERIFY(partFiles().isEmpty());
    }

    void helperDiscardsLargeUnframedStdoutAndRecoversProtocol()
    {
        const Project original = project(25);
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("flood-helper.mp4"));
        RenderJob job(QStringLiteral(FT_STDOUT_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        QVERIFY(job.start(original, settings));
        QVERIFY(finished.count() > 0 || finished.wait(20'000));
        QCOMPARE(job.state(), RenderJob::State::Failed);
        QCOMPARE(job.encoder(), QStringLiteral("flood-fixture"));
        QVERIFY2(job.errorDetail().contains(QStringLiteral("0x0000002b")),
                 qPrintable(job.errorDetail()));
        QVERIFY(!QFile::exists(settings.outputPath));
        QVERIFY(partFiles().isEmpty());
    }

    void renderHelperReceivesExportFpsAndRejectsInvalidFps()
    {
        Project p = project(50);
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("helper-30fps.mp4"));
        settings.frameRate = QStringLiteral("30");
        settings.speedPreset = QStringLiteral("ultrafast");
        RenderJob job(QStringLiteral(FT_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        QVERIFY(job.start(p, settings));
        QVERIFY(finished.wait(60'000));
        QVERIFY2(job.state() == RenderJob::State::Succeeded,
                 qPrintable(job.errorMessage() + job.errorDetail()));
        const Result<MediaInfo> info = media::probeMedia(settings.outputPath);
        QVERIFY(info);
        QCOMPARE(info->frameRate, Rational(30, 1));
        QVERIFY2(qAbs(info->durationUs - 2'000'000) <= 100'000,
                 qPrintable(QString::number(info->durationUs)));
        QVERIFY(partFiles().isEmpty());

        settings.outputPath = m_dir.filePath(QStringLiteral("invalid-0fps.mp4"));
        settings.frameRate = QStringLiteral("0");
        RenderJob invalid(QStringLiteral(FT_RENDER_HELPER));
        QVERIFY(!invalid.start(p, settings));
        QVERIFY(!QFile::exists(settings.outputPath));
    }

    void helperExportsBatchAdjustedPictureOnBothVideoTracks()
    {
        if (!engine::MltRuntime::capabilities().eqFilter) {
            QSKIP("avfilter.eq is not available in this MLT build");
        }

        Project p = project(25); // green video, frames [0, 25)
        const Clip first = p.timeline().tracks()[0].clips.front();
        Clip second = first;
        second.id = createId();
        second.start = 30;
        second.sourceIn = 50; // blue source segment
        second.duration = 25;
        ProjectDocument doc{p};
        QVERIFY(doc.execute(edits::addClip(doc.project().timeline().tracks()[1].id, second)));
        QVERIFY(doc.execute(edits::setClipVideoAdjustment({first.id, second.id},
                                                          QStringLiteral("saturation"), 0.0)));

        RenderJob job(QStringLiteral(FT_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("batch-graded.mp4"));
        settings.includeAudio = false;
        settings.speedPreset = QStringLiteral("ultrafast");
        QVERIFY(job.start(doc.project(), settings));
        QVERIFY(finished.wait(60'000));
        QVERIFY2(job.state() == RenderJob::State::Succeeded,
                 qPrintable(job.errorMessage() + job.errorDetail()));
        const QColor greenAfter = averageColor(frameOf(settings.outputPath, 5));
        const QColor blueAfter = averageColor(frameOf(settings.outputPath, 35));
        const auto grey = [](const QColor& color) {
            return qAbs(color.red() - color.green()) < 35 &&
                   qAbs(color.green() - color.blue()) < 35;
        };
        QVERIFY2(grey(greenAfter), qPrintable(greenAfter.name()));
        QVERIFY2(grey(blueAfter), qPrintable(blueAfter.name()));
        QVERIFY(!isCloseTo(greenAfter, Qt::green, 35));
        QVERIFY(!isCloseTo(blueAfter, Qt::blue, 35));
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

    void helperExportsCustomWidthAtSelectedFpsWithoutRetimingProject()
    {
        const Project source = project(50);
        const Project before = source;
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("custom-width-ntsc.mp4"));
        settings.shortSide = 720; // override preset size, not source time
        settings.customWidth = 128;
        settings.frameRate = QStringLiteral("30000/1001");
        settings.speedPreset = QStringLiteral("ultrafast");
        settings.includeAudio = true;

        RenderJob job(QStringLiteral(FT_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        QVERIFY(job.start(source, settings));
        QVERIFY(finished.count() > 0 || finished.wait(60'000));
        QVERIFY2(job.state() == RenderJob::State::Succeeded,
                 qPrintable(job.errorMessage() + job.errorDetail()));

        const Result<MediaInfo> encoded = media::probeMedia(settings.outputPath);
        QVERIFY(encoded);
        QCOMPARE(encoded->width, 128);
        QCOMPARE(encoded->height, 72);
        QCOMPARE(encoded->frameRate, Rational(30000, 1001));
        QVERIFY(encoded->hasAudio);
        QCOMPARE(encoded->audioCodec, QStringLiteral("aac"));
        QVERIFY2(qAbs(encoded->durationUs - 2'000'000) <= 100'000,
                 qPrintable(QString::number(encoded->durationUs)));
        QCOMPARE(source, before);
        QVERIFY(partFiles().isEmpty());

        // The renderer CLI must reject an invalid odd width before it
        // creates or replaces any destination.
        QProcess invalid;
        const QString denied = m_dir.filePath(QStringLiteral("invalid-odd-width.mp4"));
        const QString projectPath = m_dir.filePath(QStringLiteral("custom-width-cli.ftproj"));
        QVERIFY(project_format::saveToFile(source, projectPath));
        invalid.start(QStringLiteral(FT_RENDER_HELPER),
                      {QStringLiteral("--project"), projectPath, QStringLiteral("--output"),
                       denied, QStringLiteral("--width"), QStringLiteral("127")});
        QVERIFY(invalid.waitForFinished(15'000));
        QCOMPARE(invalid.exitStatus(), QProcess::NormalExit);
        QVERIFY(invalid.exitCode() != 0);
        QVERIFY(!QFileInfo::exists(denied));
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

    void exportTimelineRangeKeepsPictureSoundAndTimelineUnchanged()
    {
        const Project original = project(50); // 25 green frames, then blue
        for (const QString& fps : {QStringLiteral("match"), QStringLiteral("24000/1001")}) {
            ExportSettings settings;
            settings.outputPath =
                m_dir.filePath(QStringLiteral("range-%1.mp4")
                                   .arg(QString(fps).replace(QLatin1Char('/'), QLatin1Char('_'))));
            settings.frameRate = fps;
            settings.rangeStartFrame = 25; // first blue frame
            settings.rangeEndFrame = 45;   // exclusive
            settings.speedPreset = QStringLiteral("ultrafast");
            const FrameRate target = fps == QStringLiteral("match")
                                         ? FrameRate::integer(25)
                                         : *FrameRate::create(24000, 1001);
            const auto expected =
                resolveExportFrameBounds(settings, 50, original.settings().frameRate, target);
            QVERIFY(expected);
            QCOMPARE(expected->outputLength(), FrameCount{20});

            const std::atomic<bool> cancel{false};
            FrameCount startedTotal = 0;
            const Status rendered =
                RenderRunner(original, settings).run(cancel, [&](const QString&, FrameCount total) {
                    startedTotal = total;
                });
            QVERIFY2(rendered, qPrintable(rendered ? QString() : rendered.error().toString()));
            QCOMPARE(startedTotal, expected->outputLength());
            const auto info = media::probeMedia(settings.outputPath);
            QVERIFY(info);
            QVERIFY(info->hasAudio);
            QCOMPARE(info->frameRate, Rational(target.numerator(), target.denominator()));
            QVERIFY2(std::abs(info->durationUs - 800'000) < 120'000,
                     qPrintable(QStringLiteral("actual duration %1").arg(info->durationUs)));
            QVERIFY(isCloseTo(averageColor(frameOf(settings.outputPath, 0, target)), Qt::blue));
            QVERIFY(isCloseTo(averageColor(frameOf(settings.outputPath, 15, target)), Qt::blue));
            QCOMPARE(original.timeline().duration(), FrameCount{50});
            QVERIFY(partFiles().isEmpty());
        }
    }

    void helperExportsOnlyRequestedTimelineRange()
    {
        const Project original = project(50);
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("helper-range.mp4"));
        settings.rangeStartFrame = 5;
        settings.rangeEndFrame = 20;
        settings.frameRate = QStringLiteral("30");
        settings.speedPreset = QStringLiteral("ultrafast");
        RenderJob job(QStringLiteral(FT_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        QVERIFY(job.start(original, settings));
        QVERIFY(finished.count() > 0 || finished.wait(60'000));
        QVERIFY2(job.state() == RenderJob::State::Succeeded,
                 qPrintable(job.errorMessage() + job.errorDetail()));
        QCOMPARE(job.progress(), 1.0);
        const auto video = media::probeMedia(settings.outputPath);
        QVERIFY(video);
        QVERIFY(video->hasAudio);
        QCOMPARE(video->frameRate, Rational(30, 1));
        QVERIFY2(std::abs(video->durationUs - 600'000) < 120'000,
                 qPrintable(QStringLiteral("actual duration %1").arg(video->durationUs)));
        QVERIFY(isCloseTo(averageColor(frameOf(settings.outputPath, 2, FrameRate::integer(30))),
                          Qt::green));
        QCOMPARE(original.timeline().duration(), FrameCount{50});
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

    void repeatedCustomFpsExportCanReplaceSameDestination()
    {
        // In particular on Windows, the encoder must release its temporary output
        // file before probing it and atomically replacing an existing destination.
        const Project p = project(25);
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("repeated-fps.mp4"));
        settings.speedPreset = QStringLiteral("ultrafast");
        std::atomic<bool> cancel{false};
        for (const QString& fps :
             {QStringLiteral("30"), QStringLiteral("24"), QStringLiteral("30000/1001")}) {
            settings.frameRate = fps;
            const Status result = RenderRunner(p, settings).run(cancel);
            QVERIFY2(result, qPrintable(result ? QString() : result.error().toString()));
            const Result<MediaInfo> info = media::probeMedia(settings.outputPath);
            QVERIFY2(info, qPrintable(info ? QString() : info.error().toString()));
            const Result<FrameRate> rate = outputFrameRate(settings, p.settings());
            QVERIFY(rate);
            QCOMPARE(info->frameRate, Rational(rate->numerator(), rate->denominator()));
            QVERIFY(partFiles().isEmpty());
        }
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

    void helperRejectsUnsafeOrMalformedRenderToken()
    {
        const QString source = m_dir.filePath(QStringLiteral("token-source.ftproj"));
        const QString output = m_dir.filePath(QStringLiteral("token-test.mp4"));
        QVERIFY(project_format::saveToFile(project(25), source));
        for (const QString& token : {QStringLiteral("../outside"), QStringLiteral("1234"),
                                     QString(32, QLatin1Char('z'))}) {
            QProcess helper;
            helper.start(QStringLiteral(FT_RENDER_HELPER),
                         {QStringLiteral("--project"), source, QStringLiteral("--output"), output,
                          QStringLiteral("--render-token"), token});
            QVERIFY2(helper.waitForStarted(10'000), qPrintable(helper.errorString()));
            QVERIFY2(helper.waitForFinished(15'000), qPrintable(helper.errorString()));
            QCOMPARE(helper.exitStatus(), QProcess::NormalExit);
            QCOMPARE(helper.exitCode(), int(protocol::BadArguments));
            QVERIFY(helper.readAllStandardOutput().contains("Invalid internal render token"));
            QVERIFY(!QFile::exists(output));
            QVERIFY(partFiles().isEmpty());
        }
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
        QCOMPARE(report->value(QStringLiteral("sequenceWidth")).toInt(), 160);
        QCOMPARE(report->value(QStringLiteral("sequenceHeight")).toInt(), 90);
        QCOMPARE(report->value(QStringLiteral("frameRate")).toString(), QStringLiteral("25/1"));
        QCOMPARE(report->value(QStringLiteral("durationTimecode")).toString(),
                 QStringLiteral("00:00:01:15"));
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
        QCOMPARE(unavailable->value(QStringLiteral("frameRate")).toString(),
                 QStringLiteral("25/1"));
        QCOMPARE(unavailable->value(QStringLiteral("durationTimecode")).toString(),
                 QStringLiteral("00:00:01:15"));

        // The helper must preserve the exact 30000/1001 time base and use
        // drop-frame labels, rather than rounding 29.97 fps to an integer.
        const Project ntscProject = project(25, *FrameRate::create(30'000, 1'001));
        const QString ntscPath = m_dir.filePath(QStringLiteral("inspect-ntsc.ftproj"));
        QVERIFY(project_format::saveToFile(ntscProject, ntscPath));
        QProcess ntsc;
        ntsc.start(QStringLiteral(FT_RENDER_HELPER),
                   {QStringLiteral("--inspect-project"), QStringLiteral("--project"), ntscPath});
        QVERIFY2(ntsc.waitForStarted(10'000), qPrintable(ntsc.errorString()));
        QVERIFY2(ntsc.waitForFinished(15'000), qPrintable(ntsc.errorString()));
        QCOMPARE(ntsc.exitCode(), int(protocol::Success));
        const auto rateReport = protocol::decode(ntsc.readAllStandardOutput().trimmed());
        QVERIFY(rateReport);
        QCOMPARE(rateReport->value(QStringLiteral("frameRate")).toString(),
                 QStringLiteral("30000/1001"));
        QCOMPARE(rateReport->value(QStringLiteral("durationTimecode")).toString(),
                 QStringLiteral("00:00:01;10"));
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

    void helperProcessAppliesStereoBalanceToActualEncodedChannels()
    {
        if (!engine::MltRuntime::capabilities().pannerFilter) {
            QSKIP("This MLT installation does not provide the optional stereo panner.");
        }
        // Both fixture channels carry the same 880 Hz tone. A balanced clip
        // should be audible on both sides; hard left/right should attenuate
        // the opposite encoded MP4 channel, not merely update a UI property.
        for (const double balance : {-1.0, 0.0, 1.0}) {
            Project p = project();
            const Id video = p.timeline().tracks()[0].clips.front().id;
            const Id audio = p.timeline().tracks()[2].clips.front().id;
            ClipProperties videoProps = p.timeline().clip(video)->properties;
            videoProps.muted = true;
            QVERIFY(p.timeline().setClipProperties(video, videoProps));
            ClipProperties audioProps = p.timeline().clip(audio)->properties;
            audioProps.balance = balance;
            QVERIFY(p.timeline().setClipProperties(audio, audioProps));
            QVERIFY(p.validate());
            QCOMPARE(project_format::toJson(p).value(QStringLiteral("version")).toInt(),
                     balance == 0.0 ? 2 : 7);

            ExportSettings settings;
            settings.outputPath =
                m_dir.filePath(QStringLiteral("balance-%1.mp4").arg(balance, 0, 'f', 1));
            settings.speedPreset = QStringLiteral("ultrafast");
            RenderJob job(QStringLiteral(FT_RENDER_HELPER));
            QSignalSpy finished(&job, &RenderJob::finished);
            QVERIFY(job.start(p, settings));
            QVERIFY(finished.count() > 0 || finished.wait(60'000));
            QVERIFY2(job.state() == RenderJob::State::Succeeded,
                     qPrintable(job.errorMessage() + job.errorDetail()));

            auto profile = engine::createProfile(160, 90, p.settings().frameRate);
            const QByteArray resource =
                QByteArrayLiteral("avformat:") + settings.outputPath.toUtf8();
            Mlt::Producer output(*profile, resource.constData());
            QVERIFY(output.is_valid());
            output.seek(15);
            std::unique_ptr<Mlt::Frame> f(output.get_frame());
            QVERIFY(f);
            const engine::AudioBlock block = engine::frameToAudio(*f, 25.0, 48000, 2, 15);
            QCOMPARE(block.channels, 2);
            QVERIFY(block.sampleFrames() > 0);
            int leftPeak = 0;
            int rightPeak = 0;
            for (size_t i = 0; i + 1 < block.samples.size(); i += 2) {
                leftPeak = std::max(leftPeak, std::abs(static_cast<int>(block.samples[i])));
                rightPeak = std::max(rightPeak, std::abs(static_cast<int>(block.samples[i + 1])));
            }
            const QString levels = QStringLiteral("balance=%1, left=%2, right=%3")
                                       .arg(balance)
                                       .arg(leftPeak)
                                       .arg(rightPeak);
            if (balance < 0.0) {
                QVERIFY2(leftPeak > 1500 && rightPeak * 5 < leftPeak, qPrintable(levels));
            } else if (balance > 0.0) {
                QVERIFY2(rightPeak > 1500 && leftPeak * 5 < rightPeak, qPrintable(levels));
            } else {
                QVERIFY2(leftPeak > 1500 && rightPeak > 1500 && leftPeak * 2 > rightPeak &&
                             rightPeak * 2 > leftPeak,
                         qPrintable(levels));
            }
            QVERIFY(partFiles().isEmpty());
        }
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

    void helperBurnsSubtitlesAtExactBoundariesFromFormatSix()
    {
        Project p = project();
        SubtitleTrack captions;
        captions.fontPercent = 10;
        captions.cues = {
            {QStringLiteral("caption"), 200, 800, QStringLiteral("Caption test")},
            {QStringLiteral("future"), 100000, 102000, QStringLiteral("Retained outside video")}};
        QVERIFY(p.setSubtitles(captions));
        QCOMPARE(project_format::toJson(p)[QStringLiteral("version")].toInt(), 6);
        RenderJob job(QStringLiteral(FT_RENDER_HELPER));
        QSignalSpy finished(&job, &RenderJob::finished);
        ExportSettings settings;
        settings.outputPath = m_dir.filePath(QStringLiteral("subtitle-burn-in.mp4"));
        settings.speedPreset = QStringLiteral("ultrafast");
        QVERIFY(job.start(p, settings));
        QVERIFY(finished.wait(60000));
        QVERIFY2(job.state() == RenderJob::State::Succeeded,
                 qPrintable(job.errorMessage() + job.errorDetail()));
        const auto whites = [](const QImage& image) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const QColor pixel = image.pixelColor(x, y);
                    if (pixel.red() > 140 && pixel.green() > 140 && pixel.blue() > 140)
                        ++count;
                }
            return count;
        };
        QCOMPARE(whites(frameOf(settings.outputPath, 4)), 0);
        QVERIFY(whites(frameOf(settings.outputPath, 5)) > 10);
        QVERIFY(whites(frameOf(settings.outputPath, 19)) > 10);
        QCOMPARE(whites(frameOf(settings.outputPath, 20)), 0);
        const auto media = media::probeMedia(settings.outputPath);
        QVERIFY(media);
        QVERIFY(std::abs(media->durationUs - 2000000) < 100000);
        captions.burnIn = false;
        QVERIFY(p.setSubtitles(captions));
        settings.outputPath = m_dir.filePath(QStringLiteral("subtitle-sidecar-only.mp4"));
        finished.clear();
        QVERIFY(job.start(p, settings));
        QVERIFY(finished.wait(60000));
        QCOMPARE(job.state(), RenderJob::State::Succeeded);
        QCOMPARE(whites(frameOf(settings.outputPath, 10)), 0);
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
                                    QStringLiteral("subtitles"), QStringLiteral("verify")}));
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

QTEST_MAIN(RenderTest)
#include "tst_render.moc"
