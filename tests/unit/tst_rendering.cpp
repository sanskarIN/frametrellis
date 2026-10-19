// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "rendering/export_settings.h"
#include "rendering/render_job.h"
#include "rendering/render_protocol.h"

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <limits>

using namespace ft;
using namespace ft::rendering;

class RenderingTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    QString m_mediaPath;

    Project projectWithClip()
    {
        Project p = Project::createNew(QStringLiteral("Render"), ProjectSettings{});
        MediaAsset a;
        a.id = QStringLiteral("m1");
        a.filePath = m_mediaPath;
        a.name = QStringLiteral("source.mp4");
        a.info.kind = MediaKind::Video;
        a.info.durationUs = 10'000'000;
        a.info.hasVideo = true;
        a.info.hasAudio = true;
        (void)p.addAsset(a);
        Clip c;
        c.id = createId();
        c.assetId = a.id;
        c.duration = 60;
        (void)p.timeline().insertClip(p.timeline().tracks()[0].id, c, p.sourceLookup());
        return p;
    }

    ExportSettings settingsFor(const QString& name)
    {
        ExportSettings s;
        s.outputPath = m_dir.filePath(name);
        return s;
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        m_mediaPath = m_dir.filePath(QStringLiteral("source.mp4"));
        QFile f(m_mediaPath);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("placeholder");
    }

    void validSettingsPass()
    {
        const Status valid =
            validateExport(settingsFor(QStringLiteral("out.mp4")), projectWithClip());
        QVERIFY2(valid, qPrintable(valid ? QString() : valid.error().toString()));
        QVERIFY(validateExport(settingsFor(QStringLiteral("OUT.MP4")), projectWithClip()));
    }

    void rejectsUnsafeOrInvalidSettings_data()
    {
        QTest::addColumn<QString>("output");
        QTest::addColumn<int>("crf");
        QTest::addColumn<QString>("preset");
        QTest::addColumn<int>("audioKbps");
        QTest::newRow("empty path") << QString() << 20 << QStringLiteral("medium") << 192;
        QTest::newRow("relative path")
            << QStringLiteral("relative.mp4") << 20 << QStringLiteral("medium") << 192;
        QTest::newRow("wrong suffix")
            << QStringLiteral("@/out.mov") << 20 << QStringLiteral("medium") << 192;
        QTest::newRow("missing folder")
            << QStringLiteral("@/nope/out.mp4") << 20 << QStringLiteral("medium") << 192;
        QTest::newRow("overwrites source")
            << QStringLiteral("@/source.mp4") << 20 << QStringLiteral("medium") << 192;
        QTest::newRow("folder as file")
            << QStringLiteral("@/folder.mp4") << 20 << QStringLiteral("medium") << 192;
        QTest::newRow("crf too high")
            << QStringLiteral("@/out.mp4") << 52 << QStringLiteral("medium") << 192;
        QTest::newRow("crf negative")
            << QStringLiteral("@/out.mp4") << -1 << QStringLiteral("medium") << 192;
        QTest::newRow("unknown preset")
            << QStringLiteral("@/out.mp4") << 20 << QStringLiteral("ludicrous") << 192;
        QTest::newRow("audio bitrate")
            << QStringLiteral("@/out.mp4") << 20 << QStringLiteral("medium") << 8;
    }

    void rejectsUnsafeOrInvalidSettings()
    {
        QFETCH(QString, output);
        QFETCH(int, crf);
        QFETCH(QString, preset);
        QFETCH(int, audioKbps);
        QDir(m_dir.path()).mkpath(QStringLiteral("folder.mp4"));
        ExportSettings s;
        s.outputPath = output.replace(QStringLiteral("@"), m_dir.path());
        s.crf = crf;
        s.speedPreset = preset;
        s.audioBitrateKbps = audioKbps;
        const Status valid = validateExport(s, projectWithClip());
        QVERIFY(!valid);
        QVERIFY(!valid.error().message.isEmpty());
    }

    void rejectsProjectFileAsOutput()
    {
        const QString projectFile = m_dir.filePath(QStringLiteral("edit.mp4"));
        QVERIFY(!validateExport(settingsFor(QStringLiteral("edit.mp4")), projectWithClip(),
                                projectFile));
    }

    void rejectsEmptyTimelineAndOfflineMedia()
    {
        Project empty = Project::createNew(QStringLiteral("Empty"), ProjectSettings{});
        QVERIFY(!validateExport(settingsFor(QStringLiteral("out.mp4")), empty));
        Project offline = projectWithClip();
        MediaAsset moved = *offline.asset(QStringLiteral("m1"));
        moved.filePath = m_dir.filePath(QStringLiteral("gone.mp4"));
        QVERIFY(offline.replaceAsset(moved));
        const Status status = validateExport(settingsFor(QStringLiteral("out.mp4")), offline);
        QVERIFY(!status);
        QCOMPARE(status.error().code, ErrorCode::Media);
    }

    void outputSizes()
    {
        ProjectSettings hd;
        ExportSettings s;
        QCOMPARE(outputSize(s, hd), QSize(1920, 1080));
        s.shortSide = 720;
        QCOMPARE(outputSize(s, hd), QSize(1280, 720));
        ProjectSettings vertical;
        vertical.width = 1080;
        vertical.height = 1920;
        QCOMPARE(outputSize(s, vertical), QSize(720, 1280));
        ProjectSettings odd;
        odd.width = 1000;
        odd.height = 562;
        s.shortSide = 360;
        const QSize size = outputSize(s, odd);
        QVERIFY(size.width() % 2 == 0 && size.height() % 2 == 0);
        QCOMPARE(size.height(), 360);

        // Quick presets never enlarge footage just because an HD/4K size was selected.
        s.avoidUpscaling = true;
        s.shortSide = 2160;
        QCOMPARE(outputSize(s, hd), QSize(1920, 1080));
        QCOMPARE(outputSize(s, vertical), QSize(1080, 1920));
        s.shortSide = 720;
        QCOMPARE(outputSize(s, odd), QSize(1000, 562)); // never enlarge smaller projects
        s.shortSide = 360;
        QCOMPARE(outputSize(s, odd), QSize(640, 360));
        s.shortSide = 0;
        QCOMPARE(outputSize(s, hd), QSize(1920, 1080));
    }

    void rejectsBadRequestedExportDimensions()
    {
        const Project p = projectWithClip();
        ExportSettings s = settingsFor(QStringLiteral("invalid-short-side.mp4"));
        for (const int invalid : {-1, 8193, std::numeric_limits<int>::max()}) {
            s.shortSide = invalid;
            s.avoidUpscaling = true; // invalid input cannot hide behind this shortcut
            const Status status = validateExport(s, p);
            QVERIFY(!status);
            QCOMPARE(status.error().code, ErrorCode::OutOfRange);
        }

        // Zero retains the project canvas. An allowed high target with no-upscale
        // leaves a smaller project untouched without unsafe dimension calculations.
        s.shortSide = 0;
        QVERIFY(validateExport(s, p));
        s.shortSide = kMaxFrameDimension;
        QVERIFY(validateExport(s, p));
    }

    void presetsAndEncoders()
    {
        const QList<ExportPreset> presets = exportPresets();
        QVERIFY(presets.size() >= 4);
        QStringList identifiers;
        for (const ExportPreset& preset : presets) {
            QVERIFY2(!identifiers.contains(preset.id), qPrintable(preset.id));
            identifiers << preset.id;
            QVERIFY(speedPresets().contains(preset.speedPreset));
            const ExportSettings s = applyPreset(preset, m_dir.filePath(QStringLiteral("p.mp4")));
            QCOMPARE(s.avoidUpscaling, preset.avoidUpscaling);
            QVERIFY2(validateExport(s, projectWithClip()), qPrintable(preset.id));
        }
        QVERIFY(identifiers.contains(QStringLiteral("quick")));
        QVERIFY(identifiers.contains(QStringLiteral("quick1080p")));
        QVERIFY(identifiers.contains(QStringLiteral("turbo")));
        QVERIFY(identifiers.contains(QStringLiteral("draft"))); // existing preset ID retained
        const ExportPreset quick =
            *std::find_if(presets.cbegin(), presets.cend(),
                          [](const ExportPreset& p) { return p.id == QLatin1String("quick"); });
        QCOMPARE(quick.speedPreset, QStringLiteral("veryfast"));
        QVERIFY(quick.avoidUpscaling);
        QCOMPARE(chooseVideoEncoder({QStringLiteral("mpeg4"), QStringLiteral("libx264")}),
                 QStringLiteral("libx264"));
        QCOMPARE(chooseVideoEncoder({QStringLiteral("mpeg4"), QStringLiteral("h264_mf")}),
                 QStringLiteral("h264_mf"));
        QCOMPARE(chooseVideoEncoder({QStringLiteral("mpeg4")}), QStringLiteral("mpeg4"));
        QVERIFY(chooseVideoEncoder({QStringLiteral("rawvideo")}).isEmpty());
    }

    void protocolRoundTrip()
    {
        const QJsonObject event{{QStringLiteral("event"), QStringLiteral("progress")},
                                {QStringLiteral("frame"), 12}};
        const QByteArray line = protocol::encode(event);
        QVERIFY(line.startsWith("@ft {"));
        QVERIFY(line.endsWith('\n'));
        QCOMPARE(*protocol::decode(line), event);
        QVERIFY(!protocol::decode("video_codecs:"));
        QVERIFY(!protocol::decode("  - libx264"));
        QVERIFY(!protocol::decode(R"({"event":"progress"})")); // missing prefix
        QVERIFY(!protocol::decode("@ft [1,2,3]"));
        QVERIFY(!protocol::decode("@ft {\"frame\":1}")); // no event name
        QVERIFY(!protocol::decode("@ft {not json"));
    }

    void stateMachine()
    {
        using S = RenderJob::State;
        QVERIFY(RenderJob::canTransition(S::Idle, S::Starting));
        QVERIFY(RenderJob::canTransition(S::Starting, S::Running));
        QVERIFY(RenderJob::canTransition(S::Starting, S::Failed));
        QVERIFY(RenderJob::canTransition(S::Running, S::Succeeded));
        QVERIFY(RenderJob::canTransition(S::Running, S::Cancelling));
        QVERIFY(RenderJob::canTransition(S::Cancelling, S::Cancelled));
        QVERIFY(RenderJob::canTransition(S::Cancelling,
                                         S::Succeeded)); // finished before it saw the request
        QVERIFY(RenderJob::canTransition(S::Failed, S::Starting)); // retry
        QVERIFY(!RenderJob::canTransition(S::Idle, S::Running));
        QVERIFY(
            !RenderJob::canTransition(S::Running, S::Cancelled)); // must pass through Cancelling
        QVERIFY(!RenderJob::canTransition(S::Succeeded, S::Failed));
        QVERIFY(!RenderJob::canTransition(S::Cancelled, S::Running));
        QVERIFY(RenderJob::isFinished(S::Succeeded) && RenderJob::isFinished(S::Failed) &&
                RenderJob::isFinished(S::Cancelled));
        QVERIFY(!RenderJob::isFinished(S::Running));
    }

    void jobRefusesToStartWithoutHelperOrValidSettings()
    {
        RenderJob job(m_dir.filePath(QStringLiteral("no-such-helper")));
        QSignalSpy states(&job, &RenderJob::stateChanged);
        QCOMPARE(job.framesPerSecond(), 0.0); // idle job has no throughput estimate
        Status started = job.start(projectWithClip(), settingsFor(QStringLiteral("out.mp4")));
        QVERIFY(!started);
        QCOMPARE(started.error().code, ErrorCode::NotFound);
        ExportSettings bad = settingsFor(QStringLiteral("out.txt"));
        QVERIFY(!job.start(projectWithClip(), bad));
        QCOMPARE(job.state(), RenderJob::State::Idle);
        QCOMPARE(states.count(), 0);
    }
};

QTEST_GUILESS_MAIN(RenderingTest)
#include "tst_rendering.moc"
