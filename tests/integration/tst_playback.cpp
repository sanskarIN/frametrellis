// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "engine/mlt_runtime.h"
#include "media/media_probe.h"
#include "media_fixtures.h"
#include "playback/playback_engine.h"

#include <QElapsedTimer>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace ft;
using namespace ft::testing;

class PlaybackTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_video;

    Project project()
    {
        ProjectSettings settings;
        settings.width = 160;
        settings.height = 90;
        settings.frameRate = *FrameRate::create(25, 1);
        Project p = Project::createNew(QStringLiteral("Playback"), settings);
        Result<MediaAsset> video = media::createAsset(m_video);
        if (!video) {
            return p;
        }
        (void)p.addAsset(*video);
        Clip c;
        c.id = createId();
        c.assetId = video->id;
        c.duration = 75; // red, green, blue seconds
        (void)p.timeline().insertClip(p.timeline().tracks()[0].id, c, p.sourceLookup());
        return p;
    }

    static QImage waitForFrameAt(QSignalSpy& spy, FrameCount position)
    {
        for (int attempt = 0; attempt < 50; ++attempt) {
            for (const QList<QVariant>& args : std::as_const(spy)) {
                if (args.at(1).value<FrameCount>() == position) {
                    return args.at(0).value<QImage>();
                }
            }
            spy.wait(200);
        }
        return {};
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QVERIFY(engine::MltRuntime::initialize());
        m_video = makeVideo(m_dir.path(), QStringLiteral("rgb.mp4"));
        QVERIFY(!m_video.isEmpty());
    }

    void seekRendersTheRequestedFrame()
    {
        playback::PlaybackEngine engine;
        QSignalSpy frames(&engine, &playback::PlaybackEngine::frameReady);
        QSignalSpy duration(&engine, &playback::PlaybackEngine::durationChanged);
        engine.setMaximumPreviewSize(QSize(160, 90));
        engine.setProject(project());
        QVERIFY(duration.wait(5000) || engine.duration() == 75);
        QCOMPARE(engine.duration(), FrameCount{75});
        engine.seek(30);
        const QImage green = waitForFrameAt(frames, 30);
        QVERIFY(!green.isNull());
        QCOMPARE(green.size(), QSize(160, 90));
        QVERIFY(isCloseTo(averageColor(green), Qt::green));
        engine.seek(60);
        QVERIFY(isCloseTo(averageColor(waitForFrameAt(frames, 60)), Qt::blue));
        // Out-of-range seeks clamp to the last frame.
        engine.seek(10'000);
        QVERIFY(!waitForFrameAt(frames, 74).isNull());
        QCOMPARE(engine.position(), FrameCount{74});
        engine.step(-4);
        QVERIFY(!waitForFrameAt(frames, 70).isNull());
    }

    void previewSizeIsBounded()
    {
        playback::PlaybackEngine engine;
        QSignalSpy frames(&engine, &playback::PlaybackEngine::frameReady);
        engine.setMaximumPreviewSize(QSize(80, 80));
        engine.setProject(project());
        engine.seek(0);
        const QImage frame = waitForFrameAt(frames, 0);
        QCOMPARE(frame.size(), QSize(80, 46));
    }

    void playsInRealTimeAndStopsAtTheEnd()
    {
        playback::PlaybackEngine engine;
        QSignalSpy frames(&engine, &playback::PlaybackEngine::frameReady);
        QSignalSpy playing(&engine, &playback::PlaybackEngine::playingChanged);
        engine.setMaximumPreviewSize(QSize(160, 90));
        engine.setProject(project());
        engine.seek(25);
        QVERIFY(!waitForFrameAt(frames, 25).isNull());
        QElapsedTimer timer;
        timer.start();
        engine.play();
        QVERIFY(playing.wait(3000));
        QVERIFY(playing.first().at(0).toBool());
        // 50 frames at 25 fps take about two seconds, then playback stops by itself.
        QVERIFY(QTest::qWaitFor([&] { return !engine.isPlaying(); }, 10'000));
        const qint64 elapsed = timer.elapsed();
        QVERIFY2(elapsed >= 1500 && elapsed < 6000, qPrintable(QString::number(elapsed)));
        QTest::qWait(100);
        QVERIFY(engine.position() >= 70);
        QVERIFY(engine.renderedFrameCount() >= 50);
        QVERIFY(engine.deliveredFrameCount() >= 10);
        QVERIFY(!playing.last().at(0).toBool());
    }

    void pauseStopsAdvancing()
    {
        playback::PlaybackEngine engine;
        engine.setMaximumPreviewSize(QSize(160, 90));
        engine.setProject(project());
        engine.seek(0);
        engine.play();
        QTest::qWait(400);
        engine.pause();
        QVERIFY(QTest::qWaitFor([&] { return !engine.isPlaying(); }, 2000));
        const FrameCount stoppedAt = engine.position();
        QTest::qWait(400);
        QCOMPARE(engine.position(), stoppedAt);
        QVERIFY(stoppedAt > 0 && stoppedAt < 75);
    }

    void projectUpdatesDuringPlayback()
    {
        playback::PlaybackEngine engine;
        QSignalSpy durations(&engine, &playback::PlaybackEngine::durationChanged);
        engine.setMaximumPreviewSize(QSize(160, 90));
        Project p = project();
        engine.setProject(p);
        engine.play();
        QTest::qWait(200);
        Clip extra = p.timeline().tracks()[0].clips[0];
        extra.id = createId();
        extra.start = 100;
        extra.duration = 20;
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[0].id, extra, p.sourceLookup()));
        engine.setProject(p);
        QVERIFY(QTest::qWaitFor([&] { return engine.duration() == 120; }, 5000));
        engine.pause();
    }

    void emptyAndClearedProjects()
    {
        playback::PlaybackEngine engine;
        engine.setProject(Project::createNew(QStringLiteral("Empty"), ProjectSettings{}));
        engine.play();
        QTest::qWait(200);
        QVERIFY(!engine.isPlaying());
        QCOMPARE(engine.duration(), FrameCount{0});
        engine.clearProject();
        engine.seek(5);
        QTest::qWait(100);
        QCOMPARE(engine.position(), FrameCount{0});
    }

    void cleanupTestCase() { engine::MltRuntime::shutdown(); }
};

QTEST_GUILESS_MAIN(PlaybackTest)
#include "tst_playback.moc"
