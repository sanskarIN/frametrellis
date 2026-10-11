// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
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

QTEST_GUILESS_MAIN(EngineTest)
#include "tst_engine.moc"
