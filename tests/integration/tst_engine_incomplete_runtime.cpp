// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
//
// A module directory can exist and MLT Factory::init can succeed even when
// all of the media services required by the editor are missing. Neither the
// initial attempt nor a subsequent call may report a usable runtime.
#include "engine/mlt_runtime.h"
#include "playback/playback_engine.h"

#include <QSignalSpy>
#include <QTest>

using namespace ft;

class IncompleteRuntimeTest : public QObject
{
    Q_OBJECT

private slots:
    void incompleteInstallationNeverReportsSuccessfulStartup()
    {
        const Status first = engine::MltRuntime::initialize();
        QVERIFY2(!first, "MLT unexpectedly accepted the deliberately broken module directory");
        QVERIFY(first.error().code == ErrorCode::Engine);
        QVERIFY(!engine::MltRuntime::isInitialized());
        QVERIFY(!engine::MltRuntime::capabilities().missingEssentials().isEmpty());

        // This used to return success because g_initialized was set before
        // validating the essential producer, consumer and transition services.
        const Status second = engine::MltRuntime::initialize();
        QVERIFY2(!second, "Second startup silently accepted incomplete MLT services");
        QVERIFY(second.error().code == ErrorCode::Engine);
        QVERIFY(!engine::MltRuntime::isInitialized());

        engine::MltRuntime::shutdown();
        QVERIFY(!engine::MltRuntime::isInitialized());
    }

    void failedStartupNeverConstructsPreviewGraphs()
    {
        const Status startup = engine::MltRuntime::initialize();
        QVERIFY2(!startup, "The test requires an incomplete MLT module repository");
        QVERIFY(!engine::MltRuntime::isInitialized());

        playback::PlaybackEngine preview;
        QSignalSpy frames(&preview, &playback::PlaybackEngine::frameReady);
        const Project project =
            Project::createNew(QStringLiteral("Unavailable preview"), ProjectSettings{});
        preview.setProject(project);
        preview.play();
        preview.seek(0);
        QTest::qWait(250);

        QCOMPARE(preview.renderedFrameCount(), qint64{0});
        QCOMPARE(preview.deliveredFrameCount(), qint64{0});
        QCOMPARE(preview.duration(), FrameCount{0});
        QVERIFY(!preview.isPlaying());
        QVERIFY(!preview.hasAudioOutput());
        QCOMPARE(frames.count(), 0);
    }
};

QTEST_GUILESS_MAIN(IncompleteRuntimeTest)
#include "tst_engine_incomplete_runtime.moc"
