// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/timeline_review.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>

using namespace ft;
using namespace ft::timeline_review;

namespace {

Project makeProject(const FrameRate& rate = FrameRate::integer(25))
{
    ProjectSettings settings;
    settings.frameRate = rate;
    return Project::createNew(QStringLiteral("Timeline review"), settings);
}

MediaAsset asset(const QString& id, const QString& path, MediaKind kind = MediaKind::Video,
                 bool audio = true)
{
    MediaAsset result;
    result.id = id;
    result.filePath = path;
    result.name = id;
    result.info.kind = kind;
    result.info.hasVideo = kind != MediaKind::Audio;
    result.info.hasAudio = audio && kind != MediaKind::Image;
    result.info.durationUs = kind == MediaKind::Image ? 0 : 120'000'000;
    result.info.width = kind == MediaKind::Audio ? 0 : 1920;
    result.info.height = kind == MediaKind::Audio ? 0 : 1080;
    return result;
}

Clip clip(const QString& id, const Id& assetId, FrameCount start, FrameCount duration)
{
    Clip result;
    result.id = id;
    result.assetId = assetId;
    result.start = start;
    result.duration = duration;
    return result;
}

bool putFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write("fixture") == 7;
}

std::vector<Range> ranges(const Report& report, FindingKind kind)
{
    std::vector<Range> result;
    for (const Finding& finding : report.findings) {
        if (finding.kind == kind) {
            result.push_back(finding.range);
        }
    }
    return result;
}

} // namespace

class TimelineReviewTest : public QObject
{
    Q_OBJECT

private slots:
    void emptySequenceHasNoCoverageFindings()
    {
        const Project project = makeProject();
        const auto report = build(project);
        QVERIFY(report);
        QCOMPARE(report->duration, FrameCount{0});
        QCOMPARE(report->clips, 0);
        QCOMPARE(report->pictureFrames, FrameCount{0});
        QCOMPARE(report->audioFrames, FrameCount{0});
        QCOMPARE(report->pictureGapFrames, FrameCount{0});
        QCOMPARE(report->audioGapFrames, FrameCount{0});
        QCOMPARE(report->findingCount, 0);
        QCOMPARE(report->tracks.size(), size_t{4});
        QVERIFY(report->findings.empty());
        QCOMPARE(toJson(*report).value(QStringLiteral("schemaVersion")).toInt(), 1);
        QVERIFY(toHtml(*report).contains("No structural findings."));
    }

    void unionsOverlappingAndAdjacentPlacementsAcrossTracks()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project project = makeProject();
        const auto source = asset(QStringLiteral("video"), dir.filePath(QStringLiteral("v.mp4")));
        const auto sound = asset(QStringLiteral("sound"), dir.filePath(QStringLiteral("a.wav")),
                                 MediaKind::Audio);
        QVERIFY(putFile(source.filePath));
        QVERIFY(putFile(sound.filePath));
        QVERIFY(project.addAsset(source));
        QVERIFY(project.addAsset(sound));
        const auto& tracks = project.timeline().tracks();
        const auto v1 = tracks[0].id;
        const auto v2 = tracks[1].id;
        const auto a1 = tracks[2].id;
        const auto lookup = project.sourceLookup();
        QVERIFY(project.timeline().insertClip(v1, clip("c1", source.id, 10, 20), lookup));
        QVERIFY(project.timeline().insertClip(v1, clip("c2", source.id, 50, 10), lookup));
        QVERIFY(project.timeline().insertClip(v2, clip("c3", source.id, 20, 30), lookup));
        QVERIFY(project.timeline().insertClip(a1, clip("c4", sound.id, 0, 80), lookup));

        const Project before = project;
        const auto report = build(project);
        QVERIFY(report);
        QCOMPARE(project, before);
        QCOMPARE(report->duration, FrameCount{80});
        QCOMPARE(report->clips, 4);
        QCOMPARE(report->media, 2);
        QCOMPARE(report->usedMedia, 2);
        QCOMPARE(report->pictureFrames, FrameCount{50});
        QCOMPARE(report->pictureGapFrames, FrameCount{30});
        QCOMPARE(report->pictureGaps, 2);
        QCOMPARE(report->audioFrames, FrameCount{80});
        QCOMPARE(report->audioGapFrames, FrameCount{0});
        QCOMPARE(report->audioGaps, 0);
        const std::vector<Range> expected{{0, 10}, {60, 80}};
        QCOMPARE(ranges(*report, FindingKind::PictureGap), expected);
        QCOMPARE(report->tracks[0].occupiedFrames, FrameCount{30});
        QCOMPARE(report->tracks[1].pictureFrames, FrameCount{30});
        QCOMPARE(report->tracks[2].audioFrames, FrameCount{80});
        QCOMPARE(report->findingCount, 2);
    }

    void hiddenPictureDoesNotMuteAudioAndLockDoesNotDisableCoverage()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project project = makeProject();
        const auto source = asset(QStringLiteral("av"), dir.filePath(QStringLiteral("av.mp4")));
        QVERIFY(putFile(source.filePath));
        QVERIFY(project.addAsset(source));
        const auto v1 = project.timeline().tracks()[0].id;
        const auto v2 = project.timeline().tracks()[1].id;
        auto hidden = clip("hidden", source.id, 0, 20);
        auto muted = clip("muted", source.id, 20, 20);
        muted.properties.muted = true;
        auto silent = clip("zero", source.id, 40, 20);
        silent.properties.gain = 0.0;
        const auto lookup = project.sourceLookup();
        QVERIFY(project.timeline().insertClip(v1, hidden, lookup));
        QVERIFY(project.timeline().insertClip(v2, muted, lookup));
        QVERIFY(project.timeline().insertClip(v2, silent, lookup));
        QVERIFY(project.timeline().setTrackHidden(v1, true));
        QVERIFY(project.timeline().setTrackLocked(v2, true));

        const auto report = build(project);
        QVERIFY(report);
        QCOMPARE(report->pictureFrames, FrameCount{40});
        QCOMPARE(report->audioFrames, FrameCount{20});
        QCOMPARE(report->tracks[0].pictureFrames, FrameCount{0});
        QCOMPARE(report->tracks[0].audioFrames, FrameCount{20});
        QVERIFY(report->tracks[0].hidden);
        QVERIFY(report->tracks[1].locked);
        QCOMPARE(report->tracks[1].pictureFrames, FrameCount{40});
        QCOMPARE(report->tracks[1].audioFrames, FrameCount{0});
        const std::vector<Range> expectedPicture{{0, 20}};
        const std::vector<Range> expectedAudio{{20, 60}};
        QCOMPARE(ranges(*report, FindingKind::PictureGap), expectedPicture);
        QCOMPARE(ranges(*report, FindingKind::AudioGap), expectedAudio);

        QVERIFY(project.timeline().setTrackMuted(v1, true));
        const auto mutedReport = build(project);
        QVERIFY(mutedReport);
        QCOMPARE(mutedReport->audioFrames, FrameCount{0});
        QCOMPARE(mutedReport->audioGapFrames, FrameCount{60});
    }

    void countsUnavailableSourcesOnceAndPlacementsIndividually()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project project = makeProject();
        const auto missing = asset("missing", dir.filePath(QStringLiteral("missing.mp4")));
        const auto folder = asset("folder", dir.filePath(QStringLiteral("folder.mp4")));
        const auto unused = asset("unused", dir.filePath(QStringLiteral("unused.mp4")));
        QVERIFY(QDir().mkpath(folder.filePath));
        QVERIFY(project.addAsset(missing));
        QVERIFY(project.addAsset(folder));
        QVERIFY(project.addAsset(unused));
        const auto v1 = project.timeline().tracks()[0].id;
        const auto v2 = project.timeline().tracks()[1].id;
        const auto lookup = project.sourceLookup();
        QVERIFY(project.timeline().insertClip(v1, clip("m1", missing.id, 0, 20), lookup));
        QVERIFY(project.timeline().insertClip(v1, clip("m2", missing.id, 20, 20), lookup));
        QVERIFY(project.timeline().insertClip(v2, clip("f1", folder.id, 0, 40), lookup));

        const auto report = build(project);
        QVERIFY(report);
        QCOMPARE(report->media, 3);
        QCOMPARE(report->usedMedia, 2);
        QCOMPARE(report->unavailableMedia, 2);
        QCOMPARE(report->unavailableClips, 3);
        QCOMPARE(report->tracks[0].unavailableClips, 2);
        QCOMPARE(report->tracks[1].unavailableClips, 1);
        QCOMPARE(report->pictureFrames, FrameCount{0});
        QCOMPARE(report->audioFrames, FrameCount{0});
        QCOMPARE(report->findingCount, 5);

        QVERIFY(putFile(missing.filePath));
        const auto refreshed = build(project);
        QVERIFY(refreshed);
        QCOMPARE(refreshed->unavailableMedia, 1);
        QCOMPARE(refreshed->unavailableClips, 1);
        QCOMPARE(refreshed->pictureFrames, FrameCount{40});
        QCOMPARE(refreshed->audioFrames, FrameCount{40});
        QCOMPARE(refreshed->findingCount, 1);
    }

    void subtitleSamplingAndMarkerEndBoundaries()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project project = makeProject(*FrameRate::create(30000, 1001));
        const auto still = asset("still", dir.filePath(QStringLiteral("still.png")),
                                 MediaKind::Image);
        QVERIFY(putFile(still.filePath));
        QVERIFY(project.addAsset(still));
        const auto track = project.timeline().tracks()[0].id;
        QVERIFY(project.timeline().insertClip(track, clip("picture", still.id, 0, 30),
                                              project.sourceLookup()));
        QVERIFY(project.setMarkers({{"at-end", 30, "End"}, {"outside", 31, "Outside"}}));
        SubtitleTrack subs;
        subs.cues = {{"tiny", 1, 2, "Not sampled"},
                     {"last", 980, 1001, "At end"},
                     {"outside", 1001, 1040, "Outside"}};
        QVERIFY(project.setSubtitles(subs));

        const auto report = build(project);
        QVERIFY(report);
        QCOMPARE(report->rate.rational(), Rational(30000, 1001));
        QCOMPARE(report->markersBeyondEnd, 1);
        QCOMPARE(report->subtitlesBeyondEnd, 1);
        QCOMPARE(report->subtitlesWithoutSample, 2);
        QCOMPARE(report->subtitles, 3);
        const std::vector<Range> outside{{30, 32}};
        QCOMPARE(ranges(*report, FindingKind::SubtitleBeyondEnd), outside);
        const std::vector<Range> tiny{{1, 1}, {30, 32}};
        QCOMPARE(ranges(*report, FindingKind::SubtitleWithoutSample), tiny);
        for (const Finding& finding : report->findings) {
            if (finding.kind == FindingKind::MarkerBeyondEnd ||
                finding.kind == FindingKind::SubtitleBeyondEnd) {
                QVERIFY(!finding.canSeek);
            }
        }
    }

    void usesIntervalsInsteadOfAllocatingOneEntryPerFrame()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project project = makeProject();
        const auto still = asset("still", dir.filePath(QStringLiteral("still.png")),
                                 MediaKind::Image);
        QVERIFY(putFile(still.filePath));
        QVERIFY(project.addAsset(still));
        const auto track = project.timeline().tracks()[0].id;
        QVERIFY(project.timeline().insertClip(
            track, clip("last", still.id, kMaxTimelineFrames - 1, 1), project.sourceLookup()));
        const auto report = build(project);
        QVERIFY(report);
        QCOMPARE(report->duration, kMaxTimelineFrames);
        QCOMPARE(report->pictureFrames, FrameCount{1});
        QCOMPARE(report->pictureGapFrames, kMaxTimelineFrames - 1);
        QCOMPARE(report->audioGapFrames, kMaxTimelineFrames);
        QCOMPARE(report->findingCount, 2);
    }

    void capsDetailRowsButRetainsCompleteCountsAndEarliestFindings()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project project = makeProject();
        const auto source = asset("missing", dir.filePath(QStringLiteral("missing.png")),
                                  MediaKind::Image);
        QVERIFY(project.addAsset(source));
        auto tracks = project.timeline().tracks();
        constexpr int count = kMaxFindingDetails + 100;
        tracks[0].clips.reserve(count);
        for (int i = 0; i < count; ++i) {
            tracks[0].clips.push_back(clip(QStringLiteral("clip-%1").arg(i), source.id, i, 1));
        }
        project.timeline().restoreTracks(std::move(tracks));
        const auto report = build(project);
        QVERIFY(report);
        QCOMPARE(report->unavailableClips, count);
        QCOMPARE(report->findingCount, count + 2);
        QCOMPARE(report->findings.size(), size_t(kMaxFindingDetails));
        QCOMPARE(report->findings.front().range.start, FrameCount{0});
        QVERIFY(report->findings.back().range.start < count - 1);
        const QJsonObject json = toJson(*report, 200);
        QCOMPARE(json.value(QStringLiteral("findings")).toArray().size(), qsizetype{200});
        QCOMPARE(json.value(QStringLiteral("omittedFindings")).toInt(), count + 2 - 200);
        QCOMPARE(json.value(QStringLiteral("summary"))
                     .toObject()
                     .value(QStringLiteral("findingCount"))
                     .toInt(),
                 count + 2);
        QCOMPARE(toJson(*report, -1).value(QStringLiteral("findings")).toArray().size(),
                 qsizetype{0});
    }

    void rejectsInvalidProjectAndPreservesExistingDestination()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project project = makeProject();
        auto tracks = project.timeline().tracks();
        tracks.front().clips.push_back(clip("invalid", "unknown", 0, 1));
        project.timeline().restoreTracks(std::move(tracks));
        QVERIFY(!build(project));
        const QString target = dir.filePath(QStringLiteral("existing.html"));
        QVERIFY(putFile(target));
        QVERIFY(!save(project, target));
        QFile existing(target);
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("fixture"));
    }

    void exportsEscapedHtmlAndJsonWithoutSourcePathsOrCaptionText()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project project = makeProject();
        project.setName(QStringLiteral("<script>project</script>"));
        auto source = asset("private", dir.filePath(QStringLiteral("private-source.json")));
        source.name = QStringLiteral("<img src=x onerror=alert(1)>");
        QVERIFY(project.addAsset(source));
        const auto track = project.timeline().tracks()[0].id;
        QVERIFY(project.timeline().insertClip(track, clip("missing", source.id, 0, 25),
                                              project.sourceLookup()));
        SubtitleTrack subtitles;
        subtitles.cues = {{"private-caption", 0, 100, "confidential subtitle text"}};
        QVERIFY(project.setSubtitles(subtitles));
        const Project before = project;

        const QString htmlPath = dir.filePath(QStringLiteral("review.html"));
        const QString jsonPath = dir.filePath(QStringLiteral("review.json"));
        QVERIFY(save(project, htmlPath));
        QVERIFY(save(project, jsonPath));
        QFile htmlFile(htmlPath);
        QVERIFY(htmlFile.open(QIODevice::ReadOnly));
        const auto html = htmlFile.readAll();
        QVERIFY(html.contains("&lt;script&gt;project&lt;/script&gt;"));
        QVERIFY(html.contains("&lt;img src=x onerror=alert(1)&gt;"));
        QVERIFY(!html.contains("<script>"));
        QVERIFY(!html.contains("<img"));
        QVERIFY(!html.contains("private-source.json"));
        QVERIFY(!html.contains("confidential subtitle text"));
        QVERIFY(html.contains("Content-Security-Policy"));

        QFile jsonFile(jsonPath);
        QVERIFY(jsonFile.open(QIODevice::ReadOnly));
        const QByteArray json = jsonFile.readAll();
        const auto parsed = QJsonDocument::fromJson(json);
        QVERIFY(parsed.isObject());
        QCOMPARE(parsed.object().value(QStringLiteral("projectName")).toString(), project.name());
        QVERIFY(!json.contains("private-source.json"));
        QVERIFY(!json.contains("confidential subtitle text"));
        QCOMPARE(project, before);
    }

    void refusesProjectMediaAndDirectoryDestinations()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project project = makeProject();
        auto source = asset("source", dir.filePath(QStringLiteral("source.json")));
        QVERIFY(putFile(source.filePath));
        QVERIFY(project.addAsset(source));
        const QString projectPath = dir.filePath(QStringLiteral("project.html"));
        QVERIFY(putFile(projectPath));
        const QString folderPath = dir.filePath(QStringLiteral("folder.html"));
        QVERIFY(QDir().mkpath(folderPath));

        QVERIFY(!save(project, QString()));
        QVERIFY(!save(project, QStringLiteral("relative.json")));
        QVERIFY(!save(project, dir.filePath(QStringLiteral("wrong.csv"))));
        QVERIFY(!save(project, folderPath));
        const Status media = save(project, source.filePath);
        QVERIFY(!media);
        QCOMPARE(media.error().code, ErrorCode::Conflict);
        const Status document = save(project, projectPath, projectPath);
        QVERIFY(!document);
        QCOMPARE(document.error().code, ErrorCode::Conflict);
        for (const QString& path : {source.filePath, projectPath}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArray("fixture"));
        }
    }
};

QTEST_GUILESS_MAIN(TimelineReviewTest)
#include "tst_timeline_review.moc"
