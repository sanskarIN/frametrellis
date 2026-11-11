// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "rendering/export_file.h"
#include "rendering/export_settings.h"
#include "rendering/render_job.h"
#include "rendering/render_protocol.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <limits>
#include <utility>

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

    void customWidthKeepsAspectRatioAndHonoursNoUpscale()
    {
        const Project original = projectWithClip(); // 1920x1080, 30 fps
        ExportSettings settings = settingsFor(QStringLiteral("custom-width.mp4"));
        settings.shortSide = 720; // the custom width must take precedence
        settings.customWidth = 1280;
        QCOMPARE(outputSize(settings, original.settings()), QSize(1280, 720));
        QVERIFY(validateExport(settings, original));
        settings.customWidth = 854;
        QCOMPARE(outputSize(settings, original.settings()), QSize(854, 480));
        settings.customWidth = 3840;
        settings.avoidUpscaling = true;
        QCOMPARE(outputSize(settings, original.settings()), QSize(1920, 1080));
        settings.avoidUpscaling = false;
        QCOMPARE(outputSize(settings, original.settings()), QSize(3840, 2160));

        ProjectSettings portrait = original.settings();
        portrait.width = 1080;
        portrait.height = 1920;
        settings.customWidth = 720;
        QCOMPARE(outputSize(settings, portrait), QSize(720, 1280));
        ProjectSettings wide = original.settings();
        wide.width = 2000;
        wide.height = 1000;
        settings.customWidth = 1000;
        QCOMPARE(outputSize(settings, wide), QSize(1000, 500));

        settings.customWidth = 0;
        QCOMPARE(outputSize(settings, original.settings()), QSize(1280, 720));
        settings.shortSide = 0;
        QCOMPARE(outputSize(settings, original.settings()), QSize(1920, 1080));
        QVERIFY(validateExport(settings, original));
    }

    void customWidthRejectsInvalidAndOversizeOutput()
    {
        const Project original = projectWithClip();
        ExportSettings settings = settingsFor(QStringLiteral("invalid-width.mp4"));
        for (int invalid : {-1, 1, 15, 17, 8193, 8194, 10000}) {
            settings.customWidth = invalid;
            QVERIFY2(!validateExport(settings, original),
                     qPrintable(QStringLiteral("Unexpected valid width: %1").arg(invalid)));
        }
        settings.customWidth = 16;
        QVERIFY(validateExport(settings, original));
        settings.customWidth = 8192;
        QVERIFY(validateExport(settings, original));

        // Extreme tall project: requested width fits, but derived height does not.
        ProjectSettings tall;
        tall.width = 16;
        tall.height = 8192;
        settings.customWidth = 1024;
        QCOMPARE(outputSize(settings, tall).width(), 1024);
        QVERIFY(outputSize(settings, tall).height() > kMaxFrameDimension);
        settings.customWidth = 0;
        QCOMPARE(outputSize(settings, tall), QSize(16, 8192));
    }

    void verifiesAudioStreamsBeforePublishingEncodedVideo()
    {
        MediaInfo encoded;
        encoded.kind = MediaKind::Video;
        encoded.hasVideo = true;
        encoded.hasAudio = true;
        encoded.audioCodec = QStringLiteral("aac");
        QVERIFY(verifyExportStreams(encoded, true));
        QVERIFY(!verifyExportStreams(encoded, false));

        encoded.hasAudio = false;
        encoded.audioCodec.clear();
        QVERIFY(verifyExportStreams(encoded, false));
        QVERIFY(!verifyExportStreams(encoded, true));

        encoded.hasAudio = true;
        encoded.audioCodec = QStringLiteral("mp3");
        QVERIFY(!verifyExportStreams(encoded, true));

        encoded.audioCodec = QStringLiteral("aac");
        encoded.hasVideo = false;
        QVERIFY(!verifyExportStreams(encoded, true));
        QVERIFY(!verifyExportStreams(encoded, false));

        encoded.hasVideo = true;
        encoded.kind = MediaKind::Audio;
        QVERIFY(!verifyExportStreams(encoded, true));
    }

    void batchOutputPublicationDoesNotReplaceExistingFiles()
    {
        const QString from = m_dir.filePath(QStringLiteral("no-overwrite.part.mp4"));
        const QString to = m_dir.filePath(QStringLiteral("no-overwrite.mp4"));
        QFile partial(from);
        QVERIFY(partial.open(QIODevice::WriteOnly));
        QCOMPARE(partial.write("finished-new"), qint64{12});
        partial.close();
        QFile existing(to);
        QVERIFY(existing.open(QIODevice::WriteOnly));
        QCOMPARE(existing.write("original-good"), qint64{13});
        existing.close();

        QVERIFY(!publishRenderedFile(from, to, false));
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("original-good"));
        existing.close();
        QVERIFY(partial.open(QIODevice::ReadOnly));
        QCOMPARE(partial.readAll(), QByteArray("finished-new"));
        partial.close();

        QVERIFY(existing.remove());
        QVERIFY(publishRenderedFile(from, to, false));
        QVERIFY(!QFileInfo::exists(from));
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("finished-new"));
        existing.close();

        // Ordinary single-item exports retain their deliberately different
        // overwrite-after-success policy.
        QFile replacement(from);
        QVERIFY(replacement.open(QIODevice::WriteOnly));
        QCOMPARE(replacement.write("single"), qint64{6});
        replacement.close();
        QVERIFY(publishRenderedFile(from, to, true));
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("single"));
        existing.close();
        QVERIFY(!QFileInfo::exists(from));
    }

    void exportFpsSupportsExactRatesAndRejectsInvalidValues()
    {
        const Project project = projectWithClip();
        ExportSettings settings = settingsFor(QStringLiteral("fps.mp4"));
        const FrameRate& original = project.settings().frameRate;
        QCOMPARE(*outputFrameRate(settings, project.settings()), original);
        const auto thirty = FrameRate::integer(30);
        const auto ntsc = *FrameRate::create(30000, 1001);
        const auto twentyFour = FrameRate::integer(24);
        settings.frameRate = QStringLiteral("30");
        QCOMPARE(*outputFrameRate(settings, project.settings()), thirty);
        QCOMPARE(*outputFrameCount(50, FrameRate::integer(25), thirty), FrameCount{60});
        settings.frameRate = QStringLiteral("30000/1001");
        QCOMPARE(*outputFrameRate(settings, project.settings()), ntsc);
        settings.frameRate = QStringLiteral("29.97");
        QCOMPARE(*outputFrameRate(settings, project.settings()), ntsc);
        QCOMPARE(*outputFrameCount(50, FrameRate::integer(25), ntsc), FrameCount{60});
        QCOMPARE(*outputFrameCount(50, FrameRate::integer(25), twentyFour), FrameCount{48});
        QCOMPARE(*outputFrameCount(1, ntsc, ntsc), FrameCount{1});
        QCOMPARE(*outputFrameCount(1001, ntsc, ntsc), FrameCount{1001});
        QCOMPARE(*outputFrameCount(1, ntsc, *FrameRate::create(60000, 1001)), FrameCount{2});
        QVERIFY(
            !outputFrameCount(kMaxTimelineFrames, FrameRate::integer(1), FrameRate::integer(240)));
        for (const QString& bad : {QString(), QStringLiteral("0"), QStringLiteral("-1"),
                                   QStringLiteral("241"), QStringLiteral("30000/0"),
                                   QStringLiteral("nonsense"), QStringLiteral("not-a-number")}) {
            settings.frameRate = bad;
            QVERIFY2(!outputFrameRate(settings, project.settings()), qPrintable(bad));
            const Status invalid = validateExport(settings, project);
            QVERIFY2(!invalid, qPrintable(bad));
        }
        settings.frameRate = QStringLiteral("match");
        QVERIFY(validateExport(settings, project));
    }

    void rangeBoundsPreserveProjectFramesAndExactOutputFps()
    {
        const FrameRate projectRate = FrameRate::integer(25);
        const FrameRate outputRate = *FrameRate::create(24000, 1001);
        ExportSettings settings = settingsFor(QStringLiteral("range.mp4"));
        const auto full = resolveExportFrameBounds(settings, 50, projectRate, projectRate);
        QVERIFY(full);
        QCOMPARE(full->projectStart, FrameCount{0});
        QCOMPARE(full->projectEnd, FrameCount{50});
        QCOMPARE(full->outputStart, FrameCount{0});
        QCOMPARE(full->outputEnd, FrameCount{50});
        QCOMPARE(full->outputLength(), FrameCount{50});

        settings.rangeStartFrame = 25;
        settings.rangeEndFrame = 45;
        const auto native = resolveExportFrameBounds(settings, 50, projectRate, projectRate);
        QVERIFY(native);
        QCOMPARE(native->outputStart, FrameCount{25});
        QCOMPARE(native->outputEnd, FrameCount{45});
        QCOMPARE(native->outputLength(), FrameCount{20});
        QCOMPARE(native->projectLength(), FrameCount{20});

        const auto ntsc = resolveExportFrameBounds(settings, 50, projectRate, outputRate);
        QVERIFY(ntsc);
        QCOMPARE(ntsc->outputStart, FrameCount{24}); // ceil(25 * 24000 / 25025)
        QCOMPARE(ntsc->outputEnd, FrameCount{44});   // ceil(45 * 24000 / 25025)
        QCOMPARE(ntsc->outputLength(), FrameCount{20});

        settings.rangeEndFrame = -1;
        const auto toEnd = resolveExportFrameBounds(settings, 50, projectRate, projectRate);
        QVERIFY(toEnd);
        QCOMPARE(toEnd->projectEnd, FrameCount{50});
        QCOMPARE(toEnd->outputLength(), FrameCount{25});
    }

    void rejectsInvalidOrSubFrameExportRangesWithoutMutatingProject()
    {
        const Project project = projectWithClip();
        ExportSettings settings = settingsFor(QStringLiteral("range-invalid.mp4"));
        const Project original = project;
        for (const auto& [start, end] : {std::pair<FrameCount, FrameCount>{-1, -1},
                                         {-2, 20},
                                         {60, 60},
                                         {60, 61},
                                         {30, 29},
                                         {0, 0},
                                         {0, 61},
                                         {0, -2},
                                         {2, -4}}) {
            settings.rangeStartFrame = start;
            settings.rangeEndFrame = end;
            QVERIFY2(!validateExport(settings, project),
                     qPrintable(QStringLiteral("%1..%2").arg(start).arg(end)));
        }
        const FrameRate sourceRate = FrameRate::integer(240);
        const FrameRate slowRate = FrameRate::integer(1);
        settings.rangeStartFrame = 1;
        settings.rangeEndFrame = 2;
        QVERIFY(!resolveExportFrameBounds(settings, 240, sourceRate, slowRate));
        QCOMPARE(project, original);
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
        // Upload profiles must retain creator-friendly quality without enlarging sources.
        for (const auto& id : {QStringLiteral("youtube1080p"), QStringLiteral("youtube4k")}) {
            QVERIFY2(identifiers.contains(id), qPrintable(id));
            const auto it = std::find_if(presets.cbegin(), presets.cend(),
                                         [&id](const ExportPreset& p) { return p.id == id; });
            QVERIFY(it != presets.cend());
            QCOMPARE(it->crf, 18);
            QVERIFY(it->avoidUpscaling);
            QCOMPARE(it->speedPreset, QStringLiteral("medium"));
            const ExportSettings s =
                applyPreset(*it, m_dir.filePath(QStringLiteral("creator.mp4")));
            QCOMPARE(outputSize(s, ProjectSettings{}), QSize(1920, 1080));
            QVERIFY(validateExport(s, projectWithClip()));
        }
        const auto creatorHd =
            std::find_if(presets.cbegin(), presets.cend(), [](const ExportPreset& p) {
                return p.id == QLatin1String("youtube1080p");
            });
        const auto creator4k =
            std::find_if(presets.cbegin(), presets.cend(),
                         [](const ExportPreset& p) { return p.id == QLatin1String("youtube4k"); });
        QCOMPARE(creatorHd->shortSide, 1080);
        QCOMPARE(creatorHd->audioBitrateKbps, 192);
        QCOMPARE(creator4k->shortSide, 2160);
        QCOMPARE(creator4k->audioBitrateKbps, 256);
        ProjectSettings ultraHd;
        ultraHd.width = 3840;
        ultraHd.height = 2160;
        QCOMPARE(outputSize(applyPreset(*creatorHd, QString()), ultraHd), QSize(1920, 1080));
        QCOMPARE(outputSize(applyPreset(*creator4k, QString()), ultraHd), QSize(3840, 2160));
        const auto review480 =
            std::find_if(presets.cbegin(), presets.cend(),
                         [](const ExportPreset& p) { return p.id == QLatin1String("review480p"); });
        const auto creatorQhd =
            std::find_if(presets.cbegin(), presets.cend(), [](const ExportPreset& p) {
                return p.id == QLatin1String("youtube1440p");
            });
        QVERIFY(review480 != presets.cend());
        QVERIFY(creatorQhd != presets.cend());
        QCOMPARE(review480->shortSide, 480);
        QCOMPARE(review480->crf, 27);
        QCOMPARE(review480->speedPreset, QStringLiteral("veryfast"));
        QCOMPARE(review480->audioBitrateKbps, 96);
        QVERIFY(review480->avoidUpscaling);
        QCOMPARE(creatorQhd->shortSide, 1440);
        QCOMPARE(creatorQhd->crf, 18);
        QCOMPARE(creatorQhd->speedPreset, QStringLiteral("medium"));
        QCOMPARE(creatorQhd->audioBitrateKbps, 256);
        QVERIFY(creatorQhd->avoidUpscaling);
        const ExportSettings review =
            applyPreset(*review480, m_dir.filePath(QStringLiteral("review.mp4")));
        const ExportSettings qhd =
            applyPreset(*creatorQhd, m_dir.filePath(QStringLiteral("creator-qhd.mp4")));
        QCOMPARE(outputSize(review, ProjectSettings{}), QSize(854, 480));
        QCOMPARE(outputSize(qhd, ultraHd), QSize(2560, 1440));
        QCOMPARE(outputSize(qhd, ProjectSettings{}), QSize(1920, 1080));
        ProjectSettings portrait;
        portrait.width = 1080;
        portrait.height = 1920;
        QCOMPARE(outputSize(review, portrait), QSize(480, 854));
        ProjectSettings small;
        small.width = 640;
        small.height = 360;
        QCOMPARE(outputSize(review, small), QSize(640, 360));
        QVERIFY(validateExport(review, projectWithClip()));
        QVERIFY(validateExport(qhd, projectWithClip()));
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

    void protocolRejectsInvalidFrameCounts()
    {
        const auto zero = protocol::frameCount(QJsonValue(0));
        QVERIFY(zero);
        QCOMPARE(*zero, FrameCount{0});
        const auto max = protocol::frameCount(QJsonValue(1'000'000'000.0));
        QVERIFY(max);
        QCOMPARE(*max, kMaxTimelineFrames);

        // JSON numbers can be much larger than int64. Casting them directly to a
        // frame counter is undefined behavior, even when the JSON itself is valid.
        QVERIFY(!protocol::frameCount(QJsonValue(1e300)));
        QVERIFY(!protocol::frameCount(QJsonValue(1'000'000'001.0)));
        QVERIFY(!protocol::frameCount(QJsonValue(-1)));
        QVERIFY(!protocol::frameCount(QJsonValue(12.5)));
        QVERIFY(!protocol::frameCount(QJsonValue(QStringLiteral("12"))));
        QVERIFY(!protocol::frameCount(QJsonValue(true)));
        QVERIFY(!protocol::frameCount(QJsonValue()));

        const auto valid = protocol::decode(
            protocol::encode({{QStringLiteral("event"), QStringLiteral("progress")},
                              {QStringLiteral("frame"), 12},
                              {QStringLiteral("total"), 100}}));
        QVERIFY(valid);
        QCOMPARE(*protocol::frameCount(valid->value(QStringLiteral("frame"))), FrameCount{12});
        QCOMPARE(*protocol::frameCount(valid->value(QStringLiteral("total"))), FrameCount{100});
    }

    void protocolRejectsOversizedEvents()
    {
        const QByteArray normal = protocol::encode(
            {{QStringLiteral("event"), QStringLiteral("progress")}, {QStringLiteral("frame"), 12}});
        QVERIFY(normal.size() < protocol::kMaxEventBytes);
        QVERIFY(protocol::decode(normal));

        const QJsonObject oversized{
            {QStringLiteral("event"), QStringLiteral("error")},
            {QStringLiteral("detail"), QString(protocol::kMaxEventBytes, QLatin1Char('x'))}};
        const QByteArray longLine = protocol::encode(oversized);
        QVERIFY(longLine.size() > protocol::kMaxEventBytes);
        QVERIFY(!protocol::decode(longLine));
        QVERIFY(!protocol::decode(longLine.chopped(1)));
        QVERIFY(protocol::decode(normal)); // a rejected event does not poison later events
    }

    void cancelCommandsRequireCompleteLines()
    {
        protocol::CancelCommandReader commands;
        QVERIFY(!commands.ingest("uncancelled\ncancelled\n"));
        QVERIFY(!commands.ingest("please cancel\n"));
        QVERIFY(!commands.ingest("canc"));
        QVERIFY(commands.ingest("el\n"));

        protocol::CancelCommandReader windows;
        QVERIFY(!windows.ingest("status\r\ncan"));
        QVERIFY(windows.ingest("cel\r\n"));

        protocol::CancelCommandReader oversized;
        QVERIFY(!oversized.ingest(std::string(200'000, 'x')));
        QVERIFY(!oversized.ingest("cancel\n")); // still part of the discarded long line
        QVERIFY(!oversized.ingest("\n"));       // newline resets the discard state
        QVERIFY(oversized.ingest("cancel\n"));

        protocol::CancelCommandReader missingTerminator;
        QVERIFY(!missingTerminator.ingest("cancel")); // wait for LF or pipe close
        QVERIFY(!missingTerminator.ingest("led\n"));
        QVERIFY(missingTerminator.ingest("cancel\n"));
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
