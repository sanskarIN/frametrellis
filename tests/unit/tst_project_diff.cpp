// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/project_diff.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTest>

using namespace ft;
using namespace ft::project_diff;

namespace {

MediaAsset asset(const Id& id, const QString& path, MediaKind kind = MediaKind::Video)
{
    MediaAsset result;
    result.id = id;
    result.name = id;
    result.filePath = path;
    result.info.kind = kind;
    result.info.hasVideo = kind != MediaKind::Audio;
    result.info.hasAudio = kind != MediaKind::Image;
    result.info.audioChannels = result.info.hasAudio ? 2 : 0;
    result.info.audioSampleRate = result.info.hasAudio ? 48000 : 0;
    result.info.durationUs = kind == MediaKind::Image ? 0 : 120'000'000;
    return result;
}

Clip clip(const Id& id, const Id& source, FrameCount start, FrameCount duration,
          FrameCount sourceIn = 0)
{
    Clip result;
    result.id = id;
    result.assetId = source;
    result.start = start;
    result.duration = duration;
    result.sourceIn = sourceIn;
    return result;
}

const Counts& counts(const Report& report, Category category)
{
    return report.counts[static_cast<size_t>(category)];
}

const Detail* detail(const Report& report, Category category, const Id& id)
{
    for (const Detail& entry : report.details) {
        if (entry.category == category && entry.id == id) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

class ProjectDiffTest : public QObject
{
    Q_OBJECT

private slots:
    void identicalProjectsHaveNoChanges()
    {
        const Project project = Project::createNew(QStringLiteral("Identical"), {});
        const auto report = compare(project, project);
        QVERIFY(report);
        QCOMPARE(report->totalChanges, 0);
        QVERIFY(report->details.empty());
        QCOMPARE(counts(*report, Category::Project).unchanged, 1);
        QCOMPARE(counts(*report, Category::Settings).unchanged, 1);
        QCOMPARE(counts(*report, Category::Track).unchanged, 4);
        QCOMPARE(counts(*report, Category::SubtitleStyle).unchanged, 1);
        const auto json = toJson(*report);
        QCOMPARE(json.value(QStringLiteral("schemaVersion")).toInt(), 1);
        QCOMPARE(json.value(QStringLiteral("omittedDetails")).toInt(), 0);
        QCOMPARE(json.value(QStringLiteral("categories")).toArray().size(), qsizetype{9});
    }

    void detectsStableIdentityMovesAndPropertyChangesWithoutMutatingInputs()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project previous = Project::createNew(QStringLiteral("Original"), {});
        const auto source =
            asset(QStringLiteral("source"), dir.filePath(QStringLiteral("source.mp4")));
        QVERIFY(previous.addAsset(source));
        const Id v1 = previous.timeline().tracks()[0].id;
        const Id v2 = previous.timeline().tracks()[1].id;
        const Id clipId = QStringLiteral("clip");
        QVERIFY(previous.timeline().insertClip(v1, clip(clipId, source.id, 10, 30, 10),
                                               previous.sourceLookup()));
        QVERIFY(previous.setMarkers({{QStringLiteral("marker"), 10, QStringLiteral("Start")}}));
        SubtitleTrack subtitles;
        subtitles.cues = {{QStringLiteral("cue"), 100, 400, QStringLiteral("Old text")}};
        QVERIFY(previous.setSubtitles(subtitles));
        Project current = previous;
        current.setName(QStringLiteral("Revised"));
        QVERIFY(current.timeline().renameTrack(v2, QStringLiteral("B-roll")));
        QVERIFY(current.timeline().moveClip(clipId, v2, 50, current.sourceLookup()));
        ClipProperties properties = current.timeline().clip(clipId)->properties;
        properties.gain = 0.5;
        properties.video.saturation = 1.5;
        QVERIFY(current.timeline().setClipProperties(clipId, properties));
        QVERIFY(current.setMarkers({{QStringLiteral("marker"), 20, QStringLiteral("New start")}}));
        subtitles.cues.front().text = QStringLiteral("New text");
        QVERIFY(current.setSubtitles(subtitles));

        const Project before = previous;
        const Project after = current;
        const auto report = compare(previous, current);
        QVERIFY(report);
        QCOMPARE(previous, before);
        QCOMPARE(current, after);
        QCOMPARE(report->previousDuration, FrameCount{40});
        QCOMPARE(report->currentDuration, FrameCount{80});
        QCOMPARE(report->totalChanges, 5);
        QCOMPARE(counts(*report, Category::Project).changed, 1);
        QCOMPARE(counts(*report, Category::Track).changed, 1);
        QCOMPARE(counts(*report, Category::Clip).added, 0);
        QCOMPARE(counts(*report, Category::Clip).removed, 0);
        QCOMPARE(counts(*report, Category::Clip).changed, 1);
        QCOMPARE(counts(*report, Category::Marker).changed, 1);
        QCOMPARE(counts(*report, Category::Subtitle).changed, 1);
        const Detail* changed = detail(*report, Category::Clip, clipId);
        QVERIFY(changed);
        QVERIFY(changed->fields.contains(QStringLiteral("Destination track")));
        QVERIFY(changed->fields.contains(QStringLiteral("Timeline start")));
        QVERIFY(changed->fields.contains(QStringLiteral("Audio gain")));
        QVERIFY(changed->fields.contains(QStringLiteral("Picture adjustments")));
        QVERIFY(changed->before.contains(QStringLiteral("[10, 40)")));
        QVERIFY(changed->after.contains(QStringLiteral("[50, 80)")));
    }

    void separatesAddedRemovedAndChangedObjects()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project previous = Project::createNew(QStringLiteral("Replacements"), {});
        const auto oldMedia =
            asset(QStringLiteral("old"), dir.filePath(QStringLiteral("old.png")), MediaKind::Image);
        const auto kept =
            asset(QStringLiteral("kept"), dir.filePath(QStringLiteral("kept.png")), MediaKind::Image);
        const auto fresh =
            asset(QStringLiteral("fresh"), dir.filePath(QStringLiteral("fresh.png")), MediaKind::Image);
        QVERIFY(previous.addAsset(oldMedia));
        QVERIFY(previous.addAsset(kept));
        const Id track = previous.timeline().tracks()[0].id;
        QVERIFY(previous.timeline().insertClip(track, clip(QStringLiteral("removed"), oldMedia.id, 0, 10),
                                               previous.sourceLookup()));
        QVERIFY(previous.timeline().insertClip(track, clip(QStringLiteral("kept"), kept.id, 10, 10),
                                               previous.sourceLookup()));
        Project current = previous;
        QVERIFY(current.timeline().removeClip(QStringLiteral("removed")));
        QVERIFY(current.removeAsset(oldMedia.id));
        QVERIFY(current.addAsset(fresh));
        QVERIFY(current.timeline().insertClip(track, clip(QStringLiteral("added"), fresh.id, 0, 10),
                                              current.sourceLookup()));

        const auto report = compare(previous, current);
        QVERIFY(report);
        QCOMPARE(report->totalChanges, 4);
        const Counts& media = counts(*report, Category::Media);
        QCOMPARE(media.added, 1);
        QCOMPARE(media.removed, 1);
        QCOMPARE(media.changed, 0);
        QCOMPARE(media.unchanged, 1);
        const Counts& clips = counts(*report, Category::Clip);
        QCOMPARE(clips.added, 1);
        QCOMPARE(clips.removed, 1);
        QCOMPARE(clips.changed, 0);
        QCOMPARE(clips.unchanged, 1);
        const Detail* added = detail(*report, Category::Clip, QStringLiteral("added"));
        const Detail* removed = detail(*report, Category::Clip, QStringLiteral("removed"));
        QVERIFY(added);
        QVERIFY(removed);
        QCOMPARE(added->action, Action::Added);
        QCOMPARE(removed->action, Action::Removed);
        QVERIFY(added->before.isEmpty());
        QVERIFY(removed->after.isEmpty());
    }

    void ignoresRuntimeStatusAndCreationTimestamp()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project previous = Project::createNew(QStringLiteral("Runtime status"), {});
        const auto source =
            asset(QStringLiteral("source"), dir.filePath(QStringLiteral("source.mp4")));
        QVERIFY(previous.addAsset(source));
        Project current = previous;
        current.setAssetStatus(source.id, MediaStatus::Missing);
        current.setCreated(previous.created().addDays(1));
        const auto report = compare(previous, current);
        QVERIFY(report);
        QCOMPARE(report->totalChanges, 0);
        QCOMPARE(counts(*report, Category::Media).unchanged, 1);
    }

    void reportsTransitionDurationAndRemovalIndependentlyOfClips()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project previous = Project::createNew(QStringLiteral("Transitions"), {});
        const auto source =
            asset(QStringLiteral("source"), dir.filePath(QStringLiteral("source.mp4")));
        QVERIFY(previous.addAsset(source));
        const Id track = previous.timeline().tracks()[0].id;
        const Id a = QStringLiteral("a");
        const Id b = QStringLiteral("b");
        const Id transition = QStringLiteral("dissolve");
        QVERIFY(previous.timeline().insertClip(track, clip(a, source.id, 0, 30, 20),
                                               previous.sourceLookup()));
        QVERIFY(previous.timeline().insertClip(track, clip(b, source.id, 30, 30, 80),
                                               previous.sourceLookup()));
        QVERIFY(previous.timeline().addTransition(a, 10, previous.sourceLookup(), transition));

        Project current = previous;
        QVERIFY(current.timeline().setTransitionDuration(transition, 8, current.sourceLookup()));
        const auto changed = compare(previous, current);
        QVERIFY(changed);
        QCOMPARE(changed->totalChanges, 1);
        QCOMPARE(counts(*changed, Category::Transition).changed, 1);
        QCOMPARE(counts(*changed, Category::Clip).unchanged, 2);
        const auto* entry = detail(*changed, Category::Transition, transition);
        QVERIFY(entry);
        QCOMPARE(entry->fields, QStringList{QStringLiteral("Duration")});

        QVERIFY(current.timeline().removeTransition(transition));
        const auto removed = compare(previous, current);
        QVERIFY(removed);
        QCOMPARE(counts(*removed, Category::Transition).removed, 1);
        QCOMPARE(counts(*removed, Category::Clip).unchanged, 2);
    }

    void reportsSettingsAndSubtitleStyleWithoutLeakingCaptionTextOrPaths()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project previous = Project::createNew(QStringLiteral("Privacy"), {});
        const auto source =
            asset(QStringLiteral("source"), dir.filePath(QStringLiteral("private-source.mp4")));
        QVERIFY(previous.addAsset(source));
        SubtitleTrack subtitles;
        subtitles.cues = {{QStringLiteral("cue"), 100, 400, QStringLiteral("private old caption")}};
        QVERIFY(previous.setSubtitles(subtitles));
        Project current = previous;
        ProjectSettings settings = current.settings();
        settings.width = 1280;
        settings.height = 720;
        QVERIFY(current.setSettings(settings));
        MediaAsset relinked = source;
        relinked.filePath = dir.filePath(QStringLiteral("private-relinked.mp4"));
        QVERIFY(current.replaceAsset(relinked));
        subtitles.cues.front().text = QStringLiteral("private new caption");
        subtitles.burnIn = false;
        subtitles.fontPercent = 7;
        QVERIFY(current.setSubtitles(subtitles));
        const auto report = compare(previous, current);
        QVERIFY(report);
        QCOMPARE(report->totalChanges, 4);
        QCOMPARE(counts(*report, Category::Settings).changed, 1);
        QCOMPARE(counts(*report, Category::Media).changed, 1);
        QCOMPARE(counts(*report, Category::Subtitle).changed, 1);
        QCOMPARE(counts(*report, Category::SubtitleStyle).changed, 1);
        const QByteArray json = QJsonDocument(toJson(*report)).toJson();
        QVERIFY(json.contains("Source location"));
        QVERIFY(json.contains("Cue text"));
        QVERIFY(json.contains("Burn-in"));
        QVERIFY(!json.contains("private-source.mp4"));
        QVERIFY(!json.contains("private-relinked.mp4"));
        QVERIFY(!json.contains("private old caption"));
        QVERIFY(!json.contains("private new caption"));
    }

    void boundsDetailsWhileKeepingCompleteCategoryCounts()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project previous = Project::createNew(QStringLiteral("Large comparison"), {});
        const auto source =
            asset(QStringLiteral("still"), dir.filePath(QStringLiteral("still.png")), MediaKind::Image);
        QVERIFY(previous.addAsset(source));
        auto tracks = previous.timeline().tracks();
        constexpr int count = kMaxComparisonDetails + 200;
        for (int i = 0; i < count; ++i) {
            tracks[0].clips.push_back(clip(QStringLiteral("clip-%1").arg(i, 4, 10, QLatin1Char('0')),
                                           source.id, i, 1));
        }
        previous.timeline().restoreTracks(tracks);
        Project current = previous;
        for (Clip& changed : tracks[0].clips) {
            changed.start += 1;
        }
        current.timeline().restoreTracks(std::move(tracks));
        const auto report = compare(previous, current);
        QVERIFY(report);
        QCOMPARE(report->totalChanges, count);
        QCOMPARE(counts(*report, Category::Clip).changed, count);
        QCOMPARE(report->details.size(), size_t(kMaxComparisonDetails));
        QCOMPARE(report->details.front().id, QStringLiteral("clip-0000"));
        QCOMPARE(report->details.back().id, QStringLiteral("clip-0999"));
        QCOMPARE(toJson(*report).value(QStringLiteral("omittedDetails")).toInt(), 200);
    }

    void rejectsUnrelatedAndInvalidProjects()
    {
        const Project first = Project::createNew(QStringLiteral("First"), {});
        const Project other = Project::createNew(QStringLiteral("Other"), {});
        const auto unrelated = compare(first, other);
        QVERIFY(!unrelated);
        QCOMPARE(unrelated.error().code, ErrorCode::Conflict);
        Project invalid = first;
        invalid.setName(QString());
        const auto malformed = compare(first, invalid);
        QVERIFY(!malformed);
        QCOMPARE(malformed.error().code, ErrorCode::InvalidDocument);
    }
};

QTEST_GUILESS_MAIN(ProjectDiffTest)
#include "tst_project_diff.moc"
