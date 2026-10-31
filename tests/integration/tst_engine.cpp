// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/project_document.h"
#include "editing/timeline_edits.h"
#include "engine/frame_utils.h"
#include "engine/mlt_profile.h"
#include "engine/mlt_runtime.h"
#include "engine/timeline_graph.h"
#include "media/media_probe.h"
#include "media/waveform.h"
#include "media_fixtures.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <mlt++/Mlt.h>

#include <cmath>

using namespace ft;
using namespace ft::testing;

namespace {

struct Media
{
    QString video;       // 3 s: red, green, blue; 440 Hz tone
    QString videoMute;   // 2 s: white, black; no audio
    QString tone;        // 2 s stereo WAV
    QString still;       // yellow 16:9 PNG
    QString narrowStill; // yellow 1:1 PNG
    QString red;         // 3 s of red with a 440 Hz tone
    QString blue;        // 3 s of blue, silent
};

Clip clipOf(const Id& asset, FrameCount start, FrameCount in, FrameCount duration)
{
    Clip c;
    c.id = createId();
    c.assetId = asset;
    c.start = start;
    c.sourceIn = in;
    c.duration = duration;
    return c;
}

} // namespace

class EngineTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_dir;
    Media m_media;

    Project projectWith(const QStringList& paths, QList<Id>* ids)
    {
        ProjectSettings settings;
        settings.width = 160;
        settings.height = 90;
        settings.frameRate = *FrameRate::create(25, 1);
        Project p = Project::createNew(QStringLiteral("Engine"), settings);
        for (const QString& path : paths) {
            Result<MediaAsset> asset = media::createAsset(path);
            if (!asset) {
                qWarning() << asset.error().toString();
                continue;
            }
            ids->append(asset->id);
            (void)p.addAsset(*asset);
        }
        return p;
    }

    QImage renderAt(const Project& project, FrameCount frame, QStringList* warnings = nullptr)
    {
        auto profile = engine::createProfile(160, 90, project.settings().frameRate);
        engine::TimelineGraphBuilder builder(*profile);
        Result<engine::GraphBuildResult> graph = builder.build(project);
        if (!graph) {
            qWarning() << graph.error().toString();
            return {};
        }
        if (warnings) {
            *warnings = graph->warnings;
        }
        graph->tractor->seek(static_cast<int>(frame));
        std::unique_ptr<Mlt::Frame> f(graph->tractor->get_frame());
        return engine::frameToImage(*f, QSize(160, 90));
    }

    int audioPeakAt(const Project& project, FrameCount frame)
    {
        auto profile = engine::createProfile(160, 90, project.settings().frameRate);
        engine::TimelineGraphBuilder builder(*profile);
        Result<engine::GraphBuildResult> graph = builder.build(project);
        if (!graph) {
            return -1;
        }
        graph->tractor->seek(static_cast<int>(frame));
        std::unique_ptr<Mlt::Frame> f(graph->tractor->get_frame());
        return peakLevel(engine::frameToAudio(*f, 25.0, 48000, 2, frame).samples);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        const Status started = engine::MltRuntime::initialize();
        QVERIFY2(started, qPrintable(started ? QString() : started.error().toString()));
        const auto& caps = engine::MltRuntime::capabilities();
        QVERIFY(caps.missingEssentials().isEmpty());
        QVERIFY(!caps.mltVersion.isEmpty());
        m_media.video = makeVideo(m_dir.path(), QStringLiteral("rgb.mp4"));
        VideoSpec mute;
        mute.segmentColors = {Qt::white, Qt::black};
        mute.withAudio = false;
        m_media.videoMute = makeVideo(m_dir.path(), QStringLiteral("mute.mp4"), mute);
        m_media.tone = makeTone(m_dir.path(), QStringLiteral("tone.wav"), 2.0);
        m_media.still =
            makeStill(m_dir.path(), QStringLiteral("yellow.png"), Qt::yellow, QSize(64, 36));
        m_media.narrowStill =
            makeStill(m_dir.path(), QStringLiteral("narrow.png"), Qt::yellow, QSize(48, 48));
        QVERIFY(!m_media.video.isEmpty());
        QVERIFY(!m_media.videoMute.isEmpty());
        QVERIFY(!m_media.tone.isEmpty());
        QVERIFY(!m_media.still.isEmpty());
        VideoSpec red;
        red.segmentColors = {Qt::red, Qt::red, Qt::red};
        m_media.red = makeVideo(m_dir.path(), QStringLiteral("red.mp4"), red);
        VideoSpec blue;
        blue.segmentColors = {Qt::blue, Qt::blue, Qt::blue};
        blue.withAudio = false;
        m_media.blue = makeVideo(m_dir.path(), QStringLiteral("blue.mp4"), blue);
        QVERIFY(!m_media.red.isEmpty());
        QVERIFY(!m_media.blue.isEmpty());
    }

    void probeVideo()
    {
        Result<MediaInfo> info = media::probeMedia(m_media.video);
        QVERIFY2(info, qPrintable(info ? QString() : info.error().toString()));
        QCOMPARE(info->kind, MediaKind::Video);
        QVERIFY(info->hasVideo);
        QVERIFY(info->hasAudio);
        QCOMPARE(info->width, 160);
        QCOMPARE(info->height, 90);
        QCOMPARE(info->frameRate, Rational(25, 1));
        QVERIFY(qAbs(info->durationUs - 3'000'000) <= 60'000);
        QVERIFY(!info->videoCodec.isEmpty());
        QCOMPARE(info->audioCodec, QStringLiteral("aac"));
        QCOMPARE(info->audioChannels, 2);
        QCOMPARE(info->audioSampleRate, 48000);
        QVERIFY(info->fileSize > 0);
    }

    void probeAudioAndStill()
    {
        Result<MediaInfo> audio = media::probeMedia(m_media.tone);
        QVERIFY(audio);
        QCOMPARE(audio->kind, MediaKind::Audio);
        QVERIFY(!audio->hasVideo);
        QCOMPARE(audio->audioSampleRate, 48000);
        QVERIFY(qAbs(audio->durationUs - 2'000'000) <= 10'000);

        Result<MediaInfo> still = media::probeMedia(m_media.still);
        QVERIFY(still);
        QCOMPARE(still->kind, MediaKind::Image);
        QCOMPARE(still->width, 64);
        QCOMPARE(still->height, 36);
        QCOMPARE(still->durationUs, std::int64_t{0});
        QVERIFY(!still->hasAudio);
    }

    void probeRejectsBadFiles()
    {
        QCOMPARE(media::probeMedia(m_dir.filePath(QStringLiteral("missing.mp4"))).error().code,
                 ErrorCode::NotFound);
        QVERIFY(!media::probeMedia(m_dir.path()));
        QFile empty(m_dir.filePath(QStringLiteral("empty.mp4")));
        QVERIFY(empty.open(QIODevice::WriteOnly));
        empty.close();
        QCOMPARE(media::probeMedia(empty.fileName()).error().code, ErrorCode::Media);
        QFile garbage(m_dir.filePath(QStringLiteral("garbage.mp4")));
        QVERIFY(garbage.open(QIODevice::WriteOnly));
        QByteArray noise(64 * 1024, '\0');
        for (int i = 0; i < noise.size(); ++i) {
            noise[i] = static_cast<char>((i * 7919) % 251);
        }
        garbage.write(noise);
        garbage.close();
        Result<MediaInfo> bad = media::probeMedia(garbage.fileName());
        QVERIFY(!bad);
        QCOMPARE(bad.error().code, ErrorCode::Media);
        QVERIFY(bad.error().message.contains(QStringLiteral("garbage.mp4")));
    }

    void thumbnails()
    {
        Result<MediaInfo> info = media::probeMedia(m_media.video);
        QVERIFY(info);
        Result<QImage> thumb = media::renderThumbnail(m_media.video, *info, QSize(80, 80));
        QVERIFY(thumb);
        QCOMPARE(thumb->size(), QSize(80, 46)); // aspect kept, even dimensions
        QVERIFY2(isCloseTo(averageColor(*thumb), Qt::red), qPrintable(averageColor(*thumb).name()));
        Result<MediaInfo> stillInfo = media::probeMedia(m_media.still);
        QVERIFY(stillInfo);
        Result<QImage> stillThumb =
            media::renderThumbnail(m_media.still, *stillInfo, QSize(32, 32));
        QVERIFY(stillThumb);
        QVERIFY(isCloseTo(averageColor(*stillThumb), Qt::yellow));
        Result<MediaInfo> toneInfo = media::probeMedia(m_media.tone);
        QVERIFY(toneInfo);
        QVERIFY(!media::renderThumbnail(m_media.tone, *toneInfo, QSize(32, 32)));
    }

    void graphShowsTheRightSourceFrames()
    {
        QList<Id> ids;
        Project p = projectWith({m_media.video}, &ids);
        const Id v1 = p.timeline().tracks()[0].id;
        // Use the green second of the source (frames 25..49) at timeline 0, then the blue second.
        QVERIFY(p.timeline().insertClip(v1, clipOf(ids[0], 0, 25, 25), p.sourceLookup()));
        QVERIFY(p.timeline().insertClip(v1, clipOf(ids[0], 40, 50, 25), p.sourceLookup()));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 0)), Qt::green));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 24)), Qt::green));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 30)), Qt::black)); // gap shows the background
        QVERIFY(isCloseTo(averageColor(renderAt(p, 40)), Qt::blue));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 64)), Qt::blue));
    }

    void upperTracksCoverLowerTracks()
    {
        QList<Id> ids;
        Project p = projectWith({m_media.video, m_media.still}, &ids);
        const Id v1 = p.timeline().tracks()[0].id;
        const Id v2 = p.timeline().tracks()[1].id;
        QVERIFY(p.timeline().insertClip(v1, clipOf(ids[0], 0, 0, 50), p.sourceLookup()));
        QVERIFY(p.timeline().insertClip(v2, clipOf(ids[1], 10, 0, 10), p.sourceLookup()));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 5)), Qt::red));
        const QColor overlayColor = averageColor(renderAt(p, 15));
        QVERIFY2(isCloseTo(overlayColor, Qt::yellow), qPrintable(overlayColor.name()));
        // A still with another aspect ratio is pillarboxed; the transparent bars reveal V1.
        Project narrow = projectWith({m_media.video, m_media.narrowStill}, &ids);
        QVERIFY(narrow.timeline().insertClip(narrow.timeline().tracks()[0].id,
                                             clipOf(ids[2], 0, 0, 50), narrow.sourceLookup()));
        QVERIFY(narrow.timeline().insertClip(narrow.timeline().tracks()[1].id,
                                             clipOf(ids[3], 0, 0, 10), narrow.sourceLookup()));
        const QImage boxed = renderAt(narrow, 5);
        QVERIFY(isCloseTo(averageColor(boxed, QRect(70, 35, 20, 20)), Qt::yellow));
        QVERIFY(isCloseTo(averageColor(boxed, QRect(0, 0, 20, 90)), Qt::red));
        // Hiding V2 reveals V1 again.
        QVERIFY(p.timeline().setTrackHidden(v2, true));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 15)), Qt::red));
        // Half opacity blends yellow over red.
        QVERIFY(p.timeline().setTrackHidden(v2, false));
        const Id overlay = p.timeline().tracks()[1].clips[0].id;
        ClipProperties props;
        props.video.opacity = 0.5;
        QVERIFY(p.timeline().setClipProperties(overlay, props));
        const QColor blended = averageColor(renderAt(p, 15));
        QVERIFY2(blended.red() > 200 && blended.green() > 80 && blended.green() < 200,
                 qPrintable(blended.name()));
    }

    void transparentImageOverlayRevealsFootage()
    {
        // A generated title is a PNG with an alpha channel. Transparent pixels must leave the
        // footage below visible while opaque text pixels draw over it.
        const QString path = m_dir.filePath(QStringLiteral("transparent-overlay.png"));
        QImage layer(QSize(160, 90), QImage::Format_RGBA8888);
        layer.fill(Qt::transparent);
        for (int y = 30; y < 60; ++y) {
            for (int x = 40; x < 120; ++x) {
                layer.setPixelColor(x, y, Qt::yellow);
            }
        }
        QVERIFY(layer.save(path));

        QList<Id> ids;
        Project project = projectWith({m_media.red, path}, &ids);
        QCOMPARE(ids.size(), 2);
        QVERIFY(project.timeline().insertClip(project.timeline().tracks()[0].id,
                                              clipOf(ids[0], 0, 0, 25), project.sourceLookup()));
        QVERIFY(project.timeline().insertClip(project.timeline().tracks()[1].id,
                                              clipOf(ids[1], 0, 0, 25), project.sourceLookup()));
        const QImage picture = renderAt(project, 10);
        QVERIFY(!picture.isNull());
        QVERIFY(isCloseTo(averageColor(picture, QRect(0, 0, 20, 20)), Qt::red));
        QVERIFY(isCloseTo(averageColor(picture, QRect(60, 40, 20, 10)), Qt::yellow));
    }

    void opacityKeyframesUseSourceTimeAndCombineWithScalarOpacityAndFades()
    {
        QList<Id> ids;
        Project p = projectWith({m_media.blue, m_media.red}, &ids);
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[0].id, clipOf(ids[0], 0, 0, 50),
                                        p.sourceLookup()));
        const Clip top = clipOf(ids[1], 5, 5, 40);
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[1].id, top, p.sourceLookup()));
        QVERIFY(p.timeline().setOpacityKeyframe(top.id, 5, 0, p.sourceLookup()));
        QVERIFY(p.timeline().setOpacityKeyframe(top.id, 25, 1, p.sourceLookup()));
        QVERIFY(p.timeline().setOpacityKeyframe(top.id, 44, 0, p.sourceLookup()));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 5)), Qt::blue));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 15)), QColor(128, 0, 128), 25));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 25)), Qt::red));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 44)), Qt::blue));
        QVERIFY(audioPeakAt(p, 5) > 1000); // transparent picture does not mute source sound
        ClipProperties properties;
        properties.video.opacity = 0.5;
        QVERIFY(p.timeline().setClipProperties(top.id, properties));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 25)), QColor(128, 0, 128), 25));
        properties.fadeIn = 20;
        QVERIFY(p.timeline().setClipProperties(top.id, properties));
        const QColor faded = averageColor(renderAt(p, 15));
        QVERIFY2(faded.red() > 10 && faded.red() < 75 && faded.blue() > 170,
                 qPrintable(faded.name()));
        QVERIFY(p.timeline().setClipProperties(top.id, ClipProperties{}));
        const QColor before = averageColor(renderAt(p, 20));
        QVERIFY(p.timeline().trimClipStart(top.id, 12, p.sourceLookup()));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 20)), before, 5));
        QVERIFY(p.timeline().splitClip(top.id, 22, QStringLiteral("opacity-right")));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 25)), Qt::red));
    }

    void opacityKeyframesPreservePngAlphaAndOneFrameClips()
    {
        const QString path = m_dir.filePath(QStringLiteral("half-alpha-red.png"));
        QImage image(QSize(160, 90), QImage::Format_RGBA8888);
        image.fill(QColor(255, 0, 0, 128));
        for (int y = 0; y < 20; ++y) {
            for (int x = 0; x < 20; ++x) {
                image.setPixelColor(x, y, Qt::transparent);
            }
        }
        QVERIFY(image.save(path));
        QList<Id> ids;
        Project p = projectWith({m_media.blue, path}, &ids);
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[0].id, clipOf(ids[0], 0, 0, 30),
                                        p.sourceLookup()));
        Clip top = clipOf(ids[1], 0, 10, 25);
        top.properties.video.opacity = 0.5;
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[1].id, top, p.sourceLookup()));
        QVERIFY(p.timeline().setOpacityKeyframe(top.id, 10, 0.5, p.sourceLookup()));
        const QImage frame = renderAt(p, 10);
        QVERIFY(isCloseTo(averageColor(frame, QRect(0, 0, 5, 5)), Qt::blue));
        const QColor blended = averageColor(frame, QRect(60, 40, 20, 10));
        QVERIFY2(blended.red() > 10 && blended.red() < 65 && blended.blue() > 185,
                 qPrintable(blended.name())); // 1/2 PNG alpha * 1/2 scalar * 1/2 curve
        QVERIFY(p.timeline().trimClipEnd(top.id, 1, p.sourceLookup()));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 0), QRect(60, 40, 20, 10)), blended, 5));
    }

    void opacityKeyframesSurviveDissolveHandles()
    {
        QList<Id> ids;
        Project p = projectWith({m_media.blue, m_media.red}, &ids);
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[0].id, clipOf(ids[0], 0, 0, 60),
                                        p.sourceLookup()));
        const Id trackId = p.timeline().tracks()[1].id;
        const Clip left = clipOf(ids[1], 0, 10, 25);
        const Clip right = clipOf(ids[1], 25, 15, 25);
        QVERIFY(p.timeline().insertClip(trackId, left, p.sourceLookup()));
        QVERIFY(p.timeline().insertClip(trackId, right, p.sourceLookup()));
        QVERIFY(p.timeline().addTransition(left.id, 10, p.sourceLookup()));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 25)), Qt::red));
        QVERIFY(p.timeline().setOpacityKeyframe(left.id, 10, 0.5, p.sourceLookup()));
        QVERIFY(p.timeline().setOpacityKeyframe(right.id, 15, 0.5, p.sourceLookup()));
        const QColor blended = averageColor(renderAt(p, 25));
        QVERIFY2(blended.red() > 50 && blended.red() < 220 && blended.blue() > 40,
                 qPrintable(blended.name()));
    }

    void transformKeyframesUseSourceTimeAndPreserveAudio()
    {
        QVERIFY(engine::MltRuntime::capabilities().transformFilter);
        QList<Id> ids;
        Project p = projectWith({m_media.blue, m_media.red}, &ids);
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[0].id, clipOf(ids[0], 0, 0, 60),
                                        p.sourceLookup()));
        const Clip top = clipOf(ids[1], 5, 10, 41);
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[1].id, top, p.sourceLookup()));
        QVERIFY(
            p.timeline().setTransformKeyframe(top.id, 10, {-0.25, 0, 0.5, 0}, p.sourceLookup()));
        QVERIFY(p.timeline().setTransformKeyframe(top.id, 50, {0.25, 0, 0.5, 0}, p.sourceLookup()));
        const auto sample = [&](FrameCount frame, int x, int y) {
            return averageColor(renderAt(p, frame), QRect(x - 2, y - 2, 4, 4));
        };
        QVERIFY(isCloseTo(sample(5, 40, 45), Qt::red));
        QVERIFY(isCloseTo(sample(5, 120, 45), Qt::blue));
        QVERIFY(isCloseTo(sample(25, 80, 45), Qt::red));
        QVERIFY(isCloseTo(sample(25, 15, 45), Qt::blue));
        QVERIFY(isCloseTo(sample(45, 120, 45), Qt::red));
        QVERIFY(isCloseTo(sample(45, 40, 45), Qt::blue));
        QVERIFY(audioPeakAt(p, 25) > 1000);
        const QImage before = renderAt(p, 25);
        QVERIFY(p.timeline().trimClipStart(top.id, 12, p.sourceLookup()));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 25), QRect(75, 40, 10, 10)),
                          averageColor(before, QRect(75, 40, 10, 10)), 5));
        QVERIFY(p.timeline().splitClip(top.id, 22, QStringLiteral("transform-right")));
        QVERIFY(isCloseTo(sample(45, 120, 45), Qt::red));
        QVERIFY(p.timeline().slipClip(QStringLiteral("transform-right"), 30, p.sourceLookup()));
        QVERIFY(isCloseTo(sample(22, 80, 45), Qt::red));
    }

    void transformRotationAlphaAndPreviewResolution()
    {
        const QString path = m_dir.filePath(QStringLiteral("transform-quadrants.png"));
        QImage image(QSize(160, 90), QImage::Format_RGBA8888);
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                image.setPixelColor(x, y,
                                    y < 45 ? (x < 80 ? QColor(Qt::red) : QColor(Qt::green))
                                           : (x < 80 ? QColor(Qt::yellow) : QColor(Qt::cyan)));
            }
        }
        // A transparent hole must reveal the underlying track after rotation.
        for (int y = 50; y < 70; ++y) {
            for (int x = 20; x < 40; ++x) {
                image.setPixelColor(x, y, Qt::transparent);
            }
        }
        QVERIFY(image.save(path));
        QList<Id> ids;
        Project p = projectWith({m_media.blue, path}, &ids);
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[0].id, clipOf(ids[0], 0, 0, 60),
                                        p.sourceLookup()));
        const Clip top = clipOf(ids[1], 0, 10, 50);
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[1].id, top, p.sourceLookup()));
        QVERIFY(p.timeline().setTransformKeyframe(top.id, 10, {0, 0, 0.5, 90}, p.sourceLookup()));
        const QImage frame = renderAt(p, 20);
        QVERIFY2(!frame.isNull(), "Transformed frame unavailable");
        QVERIFY2(isCloseTo(averageColor(frame, QRect(85, 15, 5, 5)), Qt::red),
                 qPrintable(averageColor(frame, QRect(85, 15, 5, 5)).name()));
        QVERIFY(isCloseTo(averageColor(frame, QRect(65, 60, 5, 5)), Qt::cyan));
        QVERIFY(isCloseTo(averageColor(frame, QRect(5, 5, 5, 5)), Qt::blue));
        QVERIFY(isCloseTo(averageColor(frame, QRect(70, 18, 3, 3)), Qt::blue));
        auto profile = engine::createProfile(160, 90, p.settings().frameRate);
        engine::TimelineGraphBuilder builder(*profile);
        const auto graph = builder.build(p);
        QVERIFY(graph);
        graph->tractor->seek(20);
        std::unique_ptr<Mlt::Frame> smallFrame(graph->tractor->get_frame());
        const QImage small = engine::frameToImage(*smallFrame, QSize(80, 45));
        QVERIFY(isCloseTo(averageColor(small, QRect(43, 8, 2, 2)), Qt::red));
        ClipProperties props;
        props.video.opacity = 0.5;
        QVERIFY(p.timeline().setClipProperties(top.id, props));
        QVERIFY(p.timeline().setOpacityKeyframe(top.id, 10, 0.5, p.sourceLookup()));
        QVERIFY(
            isCloseTo(averageColor(renderAt(p, 20), QRect(85, 15, 5, 5)), QColor(64, 0, 191), 25));
        QVERIFY(p.timeline().trimClipEnd(top.id, 1, p.sourceLookup()));
        QVERIFY(
            isCloseTo(averageColor(renderAt(p, 0), QRect(85, 15, 5, 5)), QColor(64, 0, 191), 25));
    }

    void transformPreservesAspectFitAndTransitionHandles()
    {
        QList<Id> ids;
        Project p = projectWith({m_media.blue, m_media.narrowStill}, &ids);
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[0].id, clipOf(ids[0], 0, 0, 60),
                                        p.sourceLookup()));
        const Id trackId = p.timeline().tracks()[1].id;
        const Clip left = clipOf(ids[1], 0, 10, 25);
        const Clip right = clipOf(ids[1], 25, 15, 25);
        QVERIFY(p.timeline().insertClip(trackId, left, p.sourceLookup()));
        QVERIFY(p.timeline().insertClip(trackId, right, p.sourceLookup()));
        QVERIFY(p.timeline().setTransformKeyframe(left.id, 10, {0, 0, 0.5, 0}, p.sourceLookup()));
        QVERIFY(p.timeline().setTransformKeyframe(right.id, 15, {0, 0, 0.5, 0}, p.sourceLookup()));
        QVERIFY(p.timeline().addTransition(left.id, 10, p.sourceLookup()));
        for (FrameCount frame : {FrameCount{10}, FrameCount{25}, FrameCount{40}}) {
            const QImage image = renderAt(p, frame);
            QVERIFY(isCloseTo(averageColor(image, QRect(75, 40, 10, 10)), Qt::yellow));
            // Square source occupies a 45x45 rectangle, rather than stretching to 80x45.
            QVERIFY(isCloseTo(averageColor(image, QRect(45, 40, 5, 5)), Qt::blue));
        }
    }

    void subtitlesHaveExactBoundariesAndDoNotChangeSoundOrDuration()
    {
        QList<Id> ids;
        Project p = projectWith({m_media.red}, &ids);
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[0].id, clipOf(ids[0], 0, 0, 75),
                                        p.sourceLookup()));
        const QImage plain = renderAt(p, 25);
        const int baselineAudio = audioPeakAt(p, 25);
        SubtitleTrack captions;
        captions.fontPercent = 10;
        captions.cues = {
            {QStringLiteral("caption"), 1000, 2000, QStringLiteral("FrameTrellis\nSubtitles")}};
        QVERIFY(p.setSubtitles(captions));
        const auto whites = [](const QImage& image) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x) {
                    const QColor pixel = image.pixelColor(x, y);
                    if (pixel.red() > 120 && pixel.green() > 120 && pixel.blue() > 120)
                        ++count;
                }
            return count;
        };
        QCOMPARE(whites(renderAt(p, 24)), 0);
        QVERIFY(whites(renderAt(p, 25)) > 10);
        QVERIFY(whites(renderAt(p, 49)) > 10);
        QCOMPARE(whites(renderAt(p, 50)), 0);
        QVERIFY(isCloseTo(averageColor(renderAt(p, 25), QRect(0, 0, 160, 35)),
                          averageColor(plain, QRect(0, 0, 160, 35)), 2));
        QVERIFY(std::abs(audioPeakAt(p, 25) - baselineAudio) < 20);
        captions.burnIn = false;
        QVERIFY(p.setSubtitles(captions));
        QCOMPARE(whites(renderAt(p, 25)), 0);
        captions.burnIn = true;
        captions.cues[0].startMs = 0;
        captions.cues[0].endMs = 1;
        QVERIFY(p.setSubtitles(captions));
        QVERIFY(
            p.timeline().trimClipEnd(p.timeline().tracks()[0].clips[0].id, 1, p.sourceLookup()));
        QVERIFY(whites(renderAt(p, 0)) > 10);
        auto profile = engine::createProfile(160, 90, p.settings().frameRate);
        engine::TimelineGraphBuilder builder(*profile);
        const auto graph = builder.build(p);
        QVERIFY(graph);
        QCOMPARE(graph->duration, FrameCount{1});
    }

    void subtitleSamplingAtFractionalRatesAndPreviewLayout()
    {
        QList<Id> ids;
        Project p = projectWith({m_media.blue}, &ids);
        ProjectSettings settings = p.settings();
        settings.width = 1280;
        settings.height = 720;
        settings.frameRate = *FrameRate::create(30000, 1001);
        QVERIFY(p.setSettings(settings));
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[0].id, clipOf(ids[0], 0, 0, 80),
                                        p.sourceLookup()));
        SubtitleTrack captions;
        captions.fontPercent = 8;
        captions.cues = {{QStringLiteral("caption"), 1000, 2000,
                          QStringLiteral("A wrapping subtitle with precise timing")}};
        QVERIFY(p.setSubtitles(captions));
        const auto ink = [](const QImage& image) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y)
                for (int x = 0; x < image.width(); ++x)
                    if (image.pixelColor(x, y).red() > 100)
                        ++count;
            return count;
        };
        QCOMPARE(ink(renderAt(p, 29)), 0);
        const QImage full = renderAt(p, 30);
        QVERIFY(ink(full) > 20);
        QCOMPARE(ink(renderAt(p, 60)), 0);
        auto profile = engine::createProfile(80, 46, p.settings().frameRate);
        engine::TimelineGraphBuilder builder(*profile);
        const auto graph = builder.build(p);
        QVERIFY(graph);
        graph->tractor->seek(30);
        std::unique_ptr<Mlt::Frame> frame(graph->tractor->get_frame());
        const QImage small = engine::frameToImage(*frame, QSize(80, 46));
        QVERIFY(ink(small) > 5);
        const QImage expected =
            full.scaled(small.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        qint64 difference = 0;
        for (int y = 0; y < small.height(); ++y)
            for (int x = 0; x < small.width(); ++x) {
                const QColor a = small.pixelColor(x, y);
                const QColor b = expected.pixelColor(x, y);
                difference += std::abs(a.red() - b.red()) + std::abs(a.green() - b.green()) +
                              std::abs(a.blue() - b.blue());
            }
        // Different resamplers and the odd preview height can shift padding by one pixel.
        // Check both mean colour error and ink placement, rather than exact raster bytes.
        QVERIFY2(difference < small.width() * small.height() * 30,
                 qPrintable(QString::number(difference)));
        const auto bounds = [](const QImage& image) {
            QRect rectangle;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (image.pixelColor(x, y).red() > 100)
                        rectangle = rectangle.united(QRect(x, y, 1, 1));
                }
            }
            return rectangle;
        };
        const QRect actualInk = bounds(small);
        const QRect expectedInk = bounds(expected);
        QVERIFY(std::abs(actualInk.left() - expectedInk.left()) <= 2);
        QVERIFY(std::abs(actualInk.top() - expectedInk.top()) <= 2);
        QVERIFY(std::abs(actualInk.right() - expectedInk.right()) <= 2);
        QVERIFY(std::abs(actualInk.bottom() - expectedInk.bottom()) <= 2);
    }

    void videoFadeIn()
    {
        QList<Id> ids;
        Project p = projectWith({m_media.videoMute}, &ids);
        const Id v1 = p.timeline().tracks()[0].id;
        Clip c = clipOf(ids[0], 0, 0, 25); // white second
        c.properties.fadeIn = 20;
        QVERIFY(p.timeline().insertClip(v1, c, p.sourceLookup()));
        const int start = averageColor(renderAt(p, 0)).lightness();
        const int middle = averageColor(renderAt(p, 10)).lightness();
        const int end = averageColor(renderAt(p, 22)).lightness();
        QVERIFY2(start < 30 && middle > start + 40 && end > 220,
                 qPrintable(QStringLiteral("%1 %2 %3").arg(start).arg(middle).arg(end)));
    }

    void pictureAdjustments()
    {
        if (!engine::MltRuntime::capabilities().eqFilter) {
            QSKIP("avfilter.eq is not available in this MLT build");
        }
        QList<Id> ids;
        Project p = projectWith({m_media.video}, &ids);
        const Id v1 = p.timeline().tracks()[0].id;
        Clip c = clipOf(ids[0], 0, 0, 25);
        c.properties.video.saturation = 0.0; // red becomes grey
        QVERIFY(p.timeline().insertClip(v1, c, p.sourceLookup()));
        const QColor grey = averageColor(renderAt(p, 5));
        QVERIFY2(qAbs(grey.red() - grey.green()) < 25 && qAbs(grey.green() - grey.blue()) < 25,
                 qPrintable(grey.name()));
    }

    void batchPictureSaturationChangesTwoRenderedVideoClipsAndSurvivesUndo()
    {
        if (!engine::MltRuntime::capabilities().eqFilter) {
            QSKIP("avfilter.eq is not available in this MLT build");
        }
        QList<Id> ids;
        Project p = projectWith({m_media.red, m_media.blue}, &ids);
        QCOMPARE(ids.size(), 2);
        const Clip red = clipOf(ids[0], 0, 0, 25);
        const Clip blue = clipOf(ids[1], 30, 0, 25);
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[0].id, red, p.sourceLookup()));
        QVERIFY(p.timeline().insertClip(p.timeline().tracks()[1].id, blue, p.sourceLookup()));
        ProjectDocument doc{p};
        const Project before = doc.project();
        const QColor redBefore = averageColor(renderAt(doc.project(), 5));
        const QColor blueBefore = averageColor(renderAt(doc.project(), 35));
        QVERIFY2(isCloseTo(redBefore, Qt::red), qPrintable(redBefore.name()));
        QVERIFY2(isCloseTo(blueBefore, Qt::blue), qPrintable(blueBefore.name()));

        QVERIFY(doc.execute(edits::setClipVideoAdjustment(
            {red.id, blue.id}, QStringLiteral("saturation"), 0.0)));
        const QColor redAfter = averageColor(renderAt(doc.project(), 5));
        const QColor blueAfter = averageColor(renderAt(doc.project(), 35));
        const auto grey = [](const QColor& color) {
            return qAbs(color.red() - color.green()) < 25 &&
                   qAbs(color.green() - color.blue()) < 25;
        };
        QVERIFY2(grey(redAfter), qPrintable(redAfter.name()));
        QVERIFY2(grey(blueAfter), qPrintable(blueAfter.name()));
        QVERIFY(!isCloseTo(redAfter, redBefore, 25));
        QVERIFY(!isCloseTo(blueAfter, blueBefore, 25));
        const Project changed = doc.project();
        QCOMPARE(doc.undoText(), QStringLiteral("Set Selected Video Adjustment"));

        QVERIFY(doc.undo());
        QCOMPARE(doc.project(), before);
        QVERIFY(isCloseTo(averageColor(renderAt(doc.project(), 5)), redBefore, 5));
        QVERIFY(isCloseTo(averageColor(renderAt(doc.project(), 35)), blueBefore, 5));
        QVERIFY(doc.redo());
        QCOMPARE(doc.project(), changed);
        QVERIFY(isCloseTo(averageColor(renderAt(doc.project(), 5)), redAfter, 5));
        QVERIFY(isCloseTo(averageColor(renderAt(doc.project(), 35)), blueAfter, 5));
    }

    void audioMixMuteAndGain()
    {
        QList<Id> ids;
        Project p = projectWith({m_media.tone, m_media.videoMute}, &ids);
        const Id v1 = p.timeline().tracks()[0].id;
        const Id a1 = p.timeline().tracks()[2].id;
        QVERIFY(p.timeline().insertClip(a1, clipOf(ids[0], 0, 0, 50), p.sourceLookup()));
        QVERIFY(p.timeline().insertClip(v1, clipOf(ids[1], 0, 0, 50), p.sourceLookup()));
        const int full = audioPeakAt(p, 10);
        QVERIFY2(full > 8000, qPrintable(QString::number(full))); // -6 dBFS tone
        const Id toneClip = p.timeline().tracks()[2].clips[0].id;
        ClipProperties quiet;
        quiet.gain = 0.25;
        QVERIFY(p.timeline().setClipProperties(toneClip, quiet));
        const int reduced = audioPeakAt(p, 10);
        QVERIFY2(reduced > full / 6 && reduced < full / 2,
                 qPrintable(QStringLiteral("%1 vs %2").arg(reduced).arg(full)));
        ClipProperties muted;
        muted.muted = true;
        QVERIFY(p.timeline().setClipProperties(toneClip, muted));
        QVERIFY(audioPeakAt(p, 10) < 50);
        QVERIFY(p.timeline().setClipProperties(toneClip, ClipProperties{}));
        QVERIFY(p.timeline().setTrackMuted(a1, true));
        QVERIFY(audioPeakAt(p, 10) < 50);
        // Audio-only tracks never contribute picture.
        QVERIFY(isCloseTo(averageColor(renderAt(p, 10)), Qt::white));
    }

    void volumeKeyframesFollowSourceTimeAndCombineWithGainAndMute()
    {
        QList<Id> ids;
        Project p = projectWith({m_media.tone}, &ids);
        const Id trackId = p.timeline().tracks()[2].id;
        const Clip clip = clipOf(ids[0], 5, 5, 40);
        QVERIFY(p.timeline().insertClip(trackId, clip, p.sourceLookup()));
        const int full = audioPeakAt(p, 15); // source frame 15
        QVERIFY(full > 8000);
        QVERIFY(p.timeline().setAudioKeyframe(clip.id, 5, 0, p.sourceLookup()));
        QVERIFY(p.timeline().setAudioKeyframe(clip.id, 25, -20, p.sourceLookup()));
        QVERIFY(p.timeline().setAudioKeyframe(clip.id, 44, 0, p.sourceLookup()));
        const int midpoint = audioPeakAt(p, 15); // -10 dB
        QVERIFY2(midpoint > full * 0.28 && midpoint < full * 0.36,
                 qPrintable(QStringLiteral("interpolated %1 / %2").arg(midpoint).arg(full)));
        const int low = audioPeakAt(p, 25); // -20 dB
        QVERIFY(low > full * 0.08 && low < full * 0.13);
        ClipProperties properties;
        properties.gain = 0.5;
        QVERIFY(p.timeline().setClipProperties(clip.id, properties));
        const int combined = audioPeakAt(p, 15);
        QVERIFY(combined > midpoint * 0.45 && combined < midpoint * 0.55);
        properties.muted = true;
        QVERIFY(p.timeline().setClipProperties(clip.id, properties));
        QVERIFY(audioPeakAt(p, 15) < 50);
        properties.muted = false;
        properties.gain = 1.0;
        QVERIFY(p.timeline().setClipProperties(clip.id, properties));
        const int beforeTrim = audioPeakAt(p, 20);
        QVERIFY(p.timeline().trimClipStart(clip.id, 12, p.sourceLookup()));
        QCOMPARE(audioPeakAt(p, 20), beforeTrim);
        QVERIFY(p.timeline().splitClip(clip.id, 22, QStringLiteral("automation-right")));
        QVERIFY(qAbs(audioPeakAt(p, 25) - low) < full * 0.025);
    }

    void volumeKeyframesRemainActiveAcrossTransitionHandles()
    {
        QList<Id> ids;
        Project p = projectWith({m_media.red, m_media.blue}, &ids);
        const Id trackId = p.timeline().tracks()[0].id;
        const Clip outgoing = clipOf(ids[0], 0, 10, 35);
        const Clip incoming = clipOf(ids[1], 35, 15, 35);
        QVERIFY(p.timeline().insertClip(trackId, outgoing, p.sourceLookup()));
        QVERIFY(p.timeline().insertClip(trackId, incoming, p.sourceLookup()));
        QVERIFY(p.timeline().addTransition(outgoing.id, 10, p.sourceLookup()));
        const int baseline = audioPeakAt(p, 35);
        QVERIFY(baseline > 1000);
        QVERIFY(p.timeline().setAudioKeyframe(outgoing.id, 10, -12, p.sourceLookup()));
        const int automated = audioPeakAt(p, 35);
        QVERIFY2(automated > baseline * 0.20 && automated < baseline * 0.30,
                 qPrintable(QStringLiteral("transition %1 / %2").arg(automated).arg(baseline)));
    }

    void transitionsDissolveAndCrossfade()
    {
        QList<Id> ids;
        Project p = projectWith({m_media.red, m_media.blue}, &ids);
        const Id v1 = p.timeline().tracks()[0].id;
        // Red until frame 50, then blue; a 20-frame transition spans frames 40 to 59.
        QVERIFY(p.timeline().insertClip(v1, clipOf(ids[0], 0, 0, 50), p.sourceLookup()));
        QVERIFY(p.timeline().insertClip(v1, clipOf(ids[1], 50, 25, 50), p.sourceLookup()));
        const Id redClip = p.timeline().tracks()[0].clips[0].id;
        QVERIFY(p.timeline().addTransition(redClip, 20, p.sourceLookup()));
        QVERIFY(p.validate());

        auto profile = engine::createProfile(160, 90, p.settings().frameRate);
        engine::TimelineGraphBuilder builder(*profile);
        Result<engine::GraphBuildResult> graph = builder.build(p);
        QVERIFY(graph);
        QCOMPARE(graph->tractor->get_playtime(), 100); // the timeline keeps its length

        QVERIFY(isCloseTo(averageColor(renderAt(p, 30)), Qt::red));
        const QColor middle = averageColor(renderAt(p, 50));
        QVERIFY2(middle.red() > 70 && middle.red() < 190 && middle.blue() > 70 &&
                     middle.blue() < 190 && middle.green() < 50,
                 qPrintable(middle.name()));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 70)), Qt::blue));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 99)), Qt::blue));

        // The sound of the red clip fades out across the transition instead of stopping at the
        // cut, and is gone after it.
        const int before = audioPeakAt(p, 30);
        const int during = audioPeakAt(p, 50);
        const int after = audioPeakAt(p, 70);
        QVERIFY2(before > 8000, qPrintable(QString::number(before)));
        QVERIFY2(during > 1000 && during < before,
                 qPrintable(QStringLiteral("%1 of %2").arg(during).arg(before)));
        QVERIFY2(after < 50, qPrintable(QString::number(after)));
    }

    void transitionsKeepClipVolume()
    {
        QList<Id> ids;
        Project p = projectWith({m_media.red, m_media.blue}, &ids);
        const Id v1 = p.timeline().tracks()[0].id;
        QVERIFY(p.timeline().insertClip(v1, clipOf(ids[0], 0, 0, 50), p.sourceLookup()));
        QVERIFY(p.timeline().insertClip(v1, clipOf(ids[1], 50, 25, 50), p.sourceLookup()));
        const Id redClip = p.timeline().tracks()[0].clips[0].id;
        QVERIFY(p.timeline().addTransition(redClip, 20, p.sourceLookup()));
        ClipProperties muted;
        muted.muted = true;
        QVERIFY(p.timeline().setClipProperties(redClip, muted));
        // A muted clip stays muted inside the transition.
        QVERIFY(audioPeakAt(p, 45) < 50);
    }

    void missingMediaUsesPlaceholderOrFails()
    {
        QTemporaryDir dir;
        const QString path =
            makeStill(dir.path(), QStringLiteral("temp.png"), Qt::cyan, QSize(64, 36));
        QList<Id> ids;
        Project p = projectWith({path}, &ids);
        const Id v1 = p.timeline().tracks()[0].id;
        QVERIFY(p.timeline().insertClip(v1, clipOf(ids[0], 0, 0, 10), p.sourceLookup()));
        QVERIFY(isCloseTo(averageColor(renderAt(p, 2)), Qt::cyan));
        QVERIFY(QFile::remove(path));
        QStringList warnings;
        const QColor offline = averageColor(renderAt(p, 2, &warnings));
        QVERIFY2(offline.red() > offline.green() + 30,
                 qPrintable(offline.name())); // dark red placeholder
        QCOMPARE(warnings.size(), 1);
        auto profile = engine::createProfile(160, 90, p.settings().frameRate);
        engine::TimelineGraphBuilder builder(*profile);
        engine::GraphOptions strict;
        strict.placeholdersForMissingMedia = false;
        Result<engine::GraphBuildResult> result = builder.build(p, strict);
        QVERIFY(!result);
        QCOMPARE(result.error().code, ErrorCode::Media);
    }

    void producerCacheIsReused()
    {
        QList<Id> ids;
        Project p = projectWith({m_media.video}, &ids);
        const Id v1 = p.timeline().tracks()[0].id;
        const Id v2 = p.timeline().tracks()[1].id;
        QVERIFY(p.timeline().insertClip(v1, clipOf(ids[0], 0, 0, 10), p.sourceLookup()));
        QVERIFY(p.timeline().insertClip(v1, clipOf(ids[0], 20, 10, 10), p.sourceLookup()));
        QVERIFY(p.timeline().insertClip(v2, clipOf(ids[0], 0, 30, 10), p.sourceLookup()));
        auto profile = engine::createProfile(160, 90, p.settings().frameRate);
        engine::TimelineGraphBuilder builder(*profile);
        QVERIFY(builder.build(p));
        QCOMPARE(builder.cachedProducerCount(), 2); // one per (asset, track)
        QVERIFY(builder.build(p));
        QCOMPARE(builder.cachedProducerCount(), 2);
        (void)p.timeline().removeClip(p.timeline().tracks()[1].clips[0].id);
        QVERIFY(builder.build(p));
        QCOMPARE(builder.cachedProducerCount(), 1);
    }

    void emptyTimelineBuilds()
    {
        Project p = Project::createNew(QStringLiteral("Empty"), ProjectSettings{});
        auto profile = engine::createProfile(160, 90, p.settings().frameRate);
        engine::TimelineGraphBuilder builder(*profile);
        Result<engine::GraphBuildResult> graph = builder.build(p);
        QVERIFY(graph);
        QCOMPARE(graph->duration, FrameCount{0});
        QVERIFY(graph->tractor->get_length() >= 1);
    }

    void waveformOfAToneHasASteadyLevel()
    {
        Result<MediaInfo> info = media::probeMedia(m_media.tone);
        QVERIFY(info);
        Result<media::WaveformData> wave = media::computeWaveform(m_media.tone, *info, 50);
        QVERIFY2(wave, qPrintable(wave ? QString() : wave.error().toString()));
        QCOMPARE(wave->binsPerSecond, 50);
        // About 2 s; encoders may round the length to whole audio packets.
        const auto expectedBins =
            static_cast<std::size_t>(std::ceil(static_cast<double>(info->durationUs) * 50 / 1e6));
        QCOMPARE(wave->peaks.size(), expectedBins);
        QVERIFY(expectedBins >= 95 && expectedBins <= 101);
        // The tone is written at -6 dBFS, half of full scale.
        for (std::size_t i = 1; i + 1 < wave->peaks.size(); ++i) {
            QVERIFY2(std::abs(int(wave->peaks[i]) - 128) <= 12,
                     qPrintable(QStringLiteral("bin %1: %2").arg(i).arg(wave->peaks[i])));
        }
    }

    void waveformOfAVideoUsesItsSound()
    {
        Result<MediaInfo> info = media::probeMedia(m_media.video);
        QVERIFY(info);
        Result<media::WaveformData> wave = media::computeWaveform(m_media.video, *info);
        QVERIFY(wave);
        QVERIFY(std::abs(wave->durationSeconds() - 3.0) < 0.05);
        const auto loud = std::count_if(wave->peaks.begin(), wave->peaks.end(),
                                        [](std::uint8_t p) { return p > 90; });
        QVERIFY(loud > static_cast<long>(wave->peaks.size() * 9 / 10));
    }

    void waveformNeedsSound()
    {
        Result<MediaInfo> info = media::probeMedia(m_media.videoMute);
        QVERIFY(info);
        Result<media::WaveformData> wave = media::computeWaveform(m_media.videoMute, *info);
        QVERIFY(!wave);
        QCOMPARE(wave.error().code, ErrorCode::Media);
    }

    void waveformCanBeCancelled()
    {
        Result<MediaInfo> info = media::probeMedia(m_media.tone);
        QVERIFY(info);
        const std::atomic<bool> cancel{true};
        Result<media::WaveformData> wave =
            media::computeWaveform(m_media.tone, *info, 100, &cancel);
        QVERIFY(!wave);
        QCOMPARE(wave.error().code, ErrorCode::Cancelled);
    }

    void waveformCacheRoundTrip()
    {
        QTemporaryDir cacheDir;
        const media::WaveformCache cache(cacheDir.path());
        Result<MediaInfo> info = media::probeMedia(m_media.tone);
        QVERIFY(info);
        const QString key = cache.keyFor(m_media.tone, *info);
        QVERIFY(!cache.load(key));

        media::WaveformData data;
        data.binsPerSecond = 100;
        data.peaks = {0, 10, 200, 255, 128};
        QVERIFY(cache.store(key, data));
        QCOMPARE(cache.load(key), std::optional<media::WaveformData>(data));

        // A damaged cache file is ignored rather than trusted.
        QFile file(QDir(cacheDir.path()).filePath(key + QStringLiteral(".ftwf")));
        QVERIFY(file.open(QIODevice::Append));
        file.write("junk");
        file.close();
        QVERIFY(!cache.load(key));

        // Another file (or the same file changed) has another key.
        Result<MediaInfo> other = media::probeMedia(m_media.video);
        QVERIFY(other);
        QVERIFY(cache.keyFor(m_media.video, *other) != key);
    }

    void waveformRendering()
    {
        media::WaveformData data;
        data.binsPerSecond = 10;
        data.peaks = std::vector<std::uint8_t>(10, 128);
        data.peaks.resize(20, 255); // 1 s at half level, then 1 s at full level
        const QImage image =
            media::renderWaveform(data, 0.0, 2.0, QSize(100, 40), QColor(Qt::white));
        QCOMPARE(image.size(), QSize(100, 40));
        const auto columnHeight = [&](int x) {
            int painted = 0;
            for (int y = 0; y < image.height(); ++y) {
                painted += qAlpha(image.pixel(x, y)) > 0 ? 1 : 0;
            }
            return painted;
        };
        QVERIFY(std::abs(columnHeight(20) - 20) <= 2); // half of the height
        QCOMPARE(columnHeight(80), 40);                // full height
        QCOMPARE(qAlpha(image.pixel(20, 2)), 0);       // nothing drawn above a half level

        // Gain scales the outline; showing only the second half draws full columns throughout.
        const QImage louder =
            media::renderWaveform(data, 0.0, 2.0, QSize(100, 40), QColor(Qt::white), 2.0);
        QVERIFY(qAlpha(louder.pixel(20, 2)) > 0);
        const QImage tail = media::renderWaveform(data, 1.0, 1.0, QSize(50, 40), QColor(Qt::white));
        QVERIFY(qAlpha(tail.pixel(0, 0)) > 0 && qAlpha(tail.pixel(49, 39)) > 0);
        QVERIFY(media::renderWaveform({}, 0.0, 1.0, QSize(10, 10), Qt::white).pixel(5, 5) == 0);
    }

    void cleanupTestCase() { engine::MltRuntime::shutdown(); }
};

QTEST_MAIN(EngineTest)
#include "tst_engine.moc"
