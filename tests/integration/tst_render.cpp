// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "engine/frame_utils.h"
#include "engine/mlt_profile.h"
#include "engine/mlt_runtime.h"
#include "media/media_probe.h"
#include "media_fixtures.h"
#include "project/project_serializer.h"
#include "rendering/render_job.h"
#include "rendering/render_runner.h"

#include <QDir>
#include <QFile>
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
        return QDir(m_dir.path()).entryList({QStringLiteral(".*.part.mp4")}, QDir::Files | QDir::Hidden);
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
        QVERIFY2(qAbs(info->durationUs - 2'000'000) <= 100'000, qPrintable(QString::number(info->durationUs)));
        // The picture follows the edit: green first, then blue.
        QVERIFY(isCloseTo(averageColor(frameOf(settings.outputPath, 5)), Qt::green));
        QVERIFY(isCloseTo(averageColor(frameOf(settings.outputPath, 40)), Qt::blue));
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

    void cancellationLeavesNothingBehind()
    {
        const QString output = m_dir.filePath(QStringLiteral("cancelled.mp4"));
        ExportSettings settings;
        settings.outputPath = output;
        settings.speedPreset = QStringLiteral("veryslow");
        std::atomic<bool> cancel{false};
        RenderRunner runner(project(), settings);
        const Status result = runner.run(cancel, {}, [&](const RenderRunner::Progress&) { cancel = true; });
        QVERIFY(!result);
        QCOMPARE(result.error().code, ErrorCode::Cancelled);
        QVERIFY(!QFile::exists(output));
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

    void cleanupTestCase()
    {
        engine::MltRuntime::shutdown();
    }
};

QTEST_GUILESS_MAIN(RenderTest)
#include "tst_render.moc"
