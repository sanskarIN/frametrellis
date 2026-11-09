// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/marker_edits.h"
#include "editing/project_document.h"
#include "editing/subtitle_edits.h"
#include "editing/timeline_edits.h"
#include "project/project_serializer.h"

#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <limits>

using namespace ft;

namespace {

MediaAsset videoAsset(const QString& id, const QString& path = QStringLiteral("/media/a.mp4"))
{
    MediaAsset a;
    a.id = id;
    a.filePath = path;
    a.name = QStringLiteral("a.mp4");
    a.info.kind = MediaKind::Video;
    a.info.durationUs = 20'000'000; // 600 frames at 30 fps
    a.info.hasVideo = true;
    a.info.hasAudio = true;
    return a;
}

Clip clipOf(const Id& asset, FrameCount start, FrameCount duration, const Id& id = {})
{
    Clip c;
    c.id = id.isEmpty() ? createId() : id;
    c.assetId = asset;
    c.start = start;
    c.duration = duration;
    return c;
}

struct Fixture
{
    ProjectDocument doc{Project::createNew(QStringLiteral("Test"), ProjectSettings{})};
    Id v1;
    Id v2;
    Id a1;

    Fixture()
    {
        v1 = doc.project().timeline().tracks()[0].id;
        v2 = doc.project().timeline().tracks()[1].id;
        a1 = doc.project().timeline().tracks()[2].id;
        std::vector<MediaAsset> assets{videoAsset(QStringLiteral("m1"))};
        (void)doc.execute(std::make_unique<AddMediaCommand>(assets));
    }
};

} // namespace

class EditingTest : public QObject
{
    Q_OBJECT

private slots:
    void batchRelinkIsOneAtomicUndoableEditAndProtectsPartialFailures()
    {
        Fixture f;
        MediaAsset first =
            videoAsset(QStringLiteral("offline-a"), QStringLiteral("/offline/scene-a.mp4"));
        MediaAsset second =
            videoAsset(QStringLiteral("offline-b"), QStringLiteral("/offline/scene-b.mp4"));
        first.status = MediaStatus::Missing;
        second.status = MediaStatus::Missing;
        QVERIFY(f.doc.execute(
            std::make_unique<AddMediaCommand>(std::vector<MediaAsset>{first, second})));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(first.id, 0, 40, QStringLiteral("offline-clip-a")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v2, clipOf(second.id, 90, 50, QStringLiteral("offline-clip-b")))));

        MediaAsset foundFirst = first;
        foundFirst.filePath = QStringLiteral("/recovered/scene-a.mp4");
        foundFirst.status = MediaStatus::Online;
        MediaAsset foundSecond = second;
        foundSecond.filePath = QStringLiteral("/recovered/scene-b.mp4");
        foundSecond.status = MediaStatus::Online;
        const Project before = f.doc.project();
        QVERIFY(f.doc.execute(std::make_unique<RelinkMediaBatchCommand>(
            std::vector<MediaAsset>{foundFirst, foundSecond})));
        const Project after = f.doc.project();
        QCOMPARE(f.doc.undoText(), QStringLiteral("Relink Missing Media"));
        QCOMPARE(after.asset(first.id)->filePath, foundFirst.filePath);
        QCOMPARE(after.asset(second.id)->filePath, foundSecond.filePath);
        QCOMPARE(after.timeline(), before.timeline());
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), after);

        // An invalid member must not allow a partially updated project.
        const QString history = f.doc.undoText();
        MediaAsset conflicting = foundSecond;
        conflicting.filePath = foundFirst.filePath;
        QVERIFY(!f.doc.execute(std::make_unique<RelinkMediaBatchCommand>(
            std::vector<MediaAsset>{foundFirst, conflicting})));
        QCOMPARE(f.doc.project(), after);
        QCOMPARE(f.doc.undoText(), history);
        QVERIFY(!f.doc.execute(std::make_unique<RelinkMediaBatchCommand>(
            std::vector<MediaAsset>{foundFirst, foundFirst})));
        QVERIFY(
            !f.doc.execute(std::make_unique<RelinkMediaBatchCommand>(std::vector<MediaAsset>{})));
        QCOMPARE(f.doc.project(), after);

        // Already relinked media must not be silently replaced by a second
        // bulk operation; its scope is only offline assets.
        QVERIFY(!f.doc.execute(
            std::make_unique<RelinkMediaBatchCommand>(std::vector<MediaAsset>{foundFirst})));
        QCOMPARE(f.doc.project(), after);

        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        MediaAsset tooShort = foundSecond;
        tooShort.info.durationUs = 100'000;
        QVERIFY(!f.doc.execute(std::make_unique<RelinkMediaBatchCommand>(
            std::vector<MediaAsset>{foundFirst, tooShort})));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), QStringLiteral("Add Clip"));
    }

    void batchStereoBalanceIsAtomicUndoableAndRejectsLockedOrNonStereoClips()
    {
        Fixture f;
        MediaAsset stereo =
            videoAsset(QStringLiteral("stereo"), QStringLiteral("/media/stereo.mp4"));
        stereo.info.audioChannels = 2;
        QVERIFY(f.doc.execute(std::make_unique<AddMediaCommand>(std::vector<MediaAsset>{stereo})));

        Clip a = clipOf(stereo.id, 0, 50, QStringLiteral("balance-a"));
        Clip b = clipOf(stereo.id, 90, 40, QStringLiteral("balance-b"));
        a.properties.gain = 0.65;
        b.properties.fadeIn = 4;
        QVERIFY(f.doc.execute(edits::addClip(f.a1, a)));
        QVERIFY(f.doc.execute(edits::addClip(f.a1, b)));
        const Project before = f.doc.project();
        const QList<Id> ids{a.id, b.id};

        QVERIFY(f.doc.execute(edits::setClipBalances(ids, -0.5)));
        QCOMPARE(f.doc.undoText(), QStringLiteral("Set Selected Stereo Balance"));
        const Project changed = f.doc.project();
        QCOMPARE(changed.timeline().clip(a.id)->properties.balance, -0.5);
        QCOMPARE(changed.timeline().clip(b.id)->properties.balance, -0.5);
        QCOMPARE(changed.timeline().clip(a.id)->properties.gain, 0.65);
        QCOMPARE(changed.timeline().clip(b.id)->properties.fadeIn, FrameCount{4});
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), changed);

        const QString undoBefore = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::setClipBalances({a.id, a.id}, 1.0)));
        QVERIFY(!f.doc.execute(edits::setClipBalances(ids, 2.0)));
        QVERIFY(!f.doc.execute(edits::setClipBalances({a.id, QStringLiteral("missing")}, 0.0)));
        QVERIFY(!f.doc.execute(edits::setClipBalances(ids, -0.5)));
        QCOMPARE(f.doc.project(), changed);
        QCOMPARE(f.doc.undoText(), undoBefore);

        QVERIFY(f.doc.execute(edits::setTrackLocked(f.a1, true)));
        const Project locked = f.doc.project();
        QVERIFY(!f.doc.execute(edits::setClipBalances(ids, 1.0)));
        QCOMPARE(f.doc.project(), locked);
        QVERIFY(f.doc.undo()); // unlock
        QCOMPARE(f.doc.project(), changed);

        // The existing fixture's video asset has unknown audio channel count, not stereo.
        Clip unknown = clipOf(QStringLiteral("m1"), 170, 25, QStringLiteral("nonstereo"));
        QVERIFY(f.doc.execute(edits::addClip(f.a1, unknown)));
        const Project mixed = f.doc.project();
        QVERIFY(!f.doc.execute(edits::setClipBalances({a.id, unknown.id}, 0.5)));
        QCOMPARE(f.doc.project(), mixed);
    }

    void replaceClipSourceIsOneAtomicUndoableCommand()
    {
        Fixture f;
        QVERIFY(f.doc.execute(std::make_unique<AddMediaCommand>(std::vector<MediaAsset>{
            videoAsset(QStringLiteral("m2"), QStringLiteral("/media/b.mp4"))})));
        Clip clip = clipOf(QStringLiteral("m1"), 40, 55, QStringLiteral("original"));
        clip.sourceIn = 15;
        clip.properties.gain = 0.7;
        clip.properties.fadeIn = 4;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, clip)));
        const Project before = f.doc.project();

        QVERIFY(f.doc.execute(edits::replaceClipSource(clip.id, QStringLiteral("m2"), 23)));
        const Project after = f.doc.project();
        const Clip* changed = after.timeline().clip(clip.id);
        QVERIFY(changed);
        QCOMPARE(changed->assetId, QStringLiteral("m2"));
        QCOMPARE(changed->sourceIn, FrameCount{23});
        QCOMPARE(changed->start, clip.start);
        QCOMPARE(changed->duration, clip.duration);
        QCOMPARE(changed->properties, clip.properties);
        QCOMPARE(f.doc.undoText(), QStringLiteral("Replace Clip Source"));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), after);

        const QString undoBefore = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::replaceClipSource(clip.id, QStringLiteral("missing"), 0)));
        QVERIFY(!f.doc.execute(edits::replaceClipSource(clip.id, QStringLiteral("m2"), 560)));
        QCOMPARE(f.doc.project(), after);
        QCOMPARE(f.doc.undoText(), undoBefore);
    }

    void synchronizedSourceClipsShareTimeAndUndoAsOneEdit()
    {
        Fixture f;
        Clip picture = clipOf(QStringLiteral("m1"), 120, 45, QStringLiteral("sync-picture"));
        picture.sourceIn = 22;
        Clip sound = clipOf(QStringLiteral("m1"), 120, 45, QStringLiteral("sync-sound"));
        sound.sourceIn = 22;
        const Project before = f.doc.project();

        QVERIFY(f.doc.execute(edits::addSynchronizedSourceClips(f.v1, picture, f.a1, sound)));
        const Project placed = f.doc.project();
        QCOMPARE(placed.timeline().clipCount(), before.timeline().clipCount() + 2);
        const Clip* video = placed.timeline().clip(picture.id);
        const Clip* audio = placed.timeline().clip(sound.id);
        QVERIFY(video);
        QVERIFY(audio);
        QCOMPARE(*video, picture);
        QCOMPARE(*audio, sound);
        QCOMPARE(placed.timeline().trackOfClip(picture.id)->id, f.v1);
        QCOMPARE(placed.timeline().trackOfClip(sound.id)->id, f.a1);
        QCOMPARE(f.doc.undoText(), QStringLiteral("Place Synchronized Video and Audio"));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), placed);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
    }

    void synchronizedSourceClipsRejectFailureWithoutPartialPlacement()
    {
        Fixture f;
        Clip picture = clipOf(QStringLiteral("m1"), 140, 30, QStringLiteral("sync-picture"));
        picture.sourceIn = 20;
        Clip sound = clipOf(QStringLiteral("m1"), 140, 30, QStringLiteral("sync-sound"));
        sound.sourceIn = 20;

        // Second-lane collision must also undo the otherwise valid first insertion.
        QVERIFY(f.doc.execute(edits::addClip(
            f.a1, clipOf(QStringLiteral("m1"), 125, 60, QStringLiteral("occupied-audio")))));
        const Project occupied = f.doc.project();
        const QString occupiedUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::addSynchronizedSourceClips(f.v1, picture, f.a1, sound)));
        QCOMPARE(f.doc.project(), occupied);
        QCOMPARE(f.doc.undoText(), occupiedUndo);
        QVERIFY(!f.doc.project().timeline().clip(picture.id));

        // Both destinations exist, but locks always prevent the whole operation.
        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v1, true)));
        const Project locked = f.doc.project();
        const QString lockedUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::addSynchronizedSourceClips(f.v1, picture, f.a1, sound)));
        QCOMPARE(f.doc.project(), locked);
        QCOMPARE(f.doc.undoText(), lockedUndo);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), occupied);

        // Malformed/mismatched clips must never be accepted as a synchronized pair.
        Clip mismatched = sound;
        mismatched.sourceIn++;
        QVERIFY(!f.doc.execute(edits::addSynchronizedSourceClips(f.v1, picture, f.a1, mismatched)));
        mismatched = sound;
        mismatched.duration++;
        QVERIFY(!f.doc.execute(edits::addSynchronizedSourceClips(f.v1, picture, f.a1, mismatched)));
        mismatched = sound;
        mismatched.id = picture.id;
        QVERIFY(!f.doc.execute(edits::addSynchronizedSourceClips(f.v1, picture, f.a1, mismatched)));
        QVERIFY(!f.doc.execute(edits::addSynchronizedSourceClips(f.a1, picture, f.v1, sound)));
        QVERIFY(!f.doc.execute(edits::addSynchronizedSourceClips(f.v1, picture, f.v1, sound)));
        QVERIFY(!f.doc.execute(
            edits::addSynchronizedSourceClips(f.v1, picture, QStringLiteral("missing"), sound)));
        QCOMPARE(f.doc.project(), occupied);
        QCOMPARE(f.doc.undoText(), occupiedUndo);
    }

    void removeUnusedMediaPreservesTimelineFilesAndUndoOrder()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 80, QStringLiteral("used")))));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pathA = dir.filePath(QStringLiteral("unused-a.mp4"));
        const QString pathB = dir.filePath(QStringLiteral("unused-b.mp4"));
        for (const QString& path : {pathA, pathB}) {
            QFile output(path);
            QVERIFY(output.open(QIODevice::WriteOnly));
            QCOMPARE(output.write("unchanged", 9), qint64{9});
        }
        std::vector<MediaAsset> extra{videoAsset(QStringLiteral("unused-a"), pathA),
                                      videoAsset(QStringLiteral("unused-b"), pathB)};
        QVERIFY(f.doc.execute(std::make_unique<AddMediaCommand>(extra)));
        const Project before = f.doc.project();

        QVERIFY(f.doc.execute(std::make_unique<RemoveUnusedMediaCommand>()));
        QCOMPARE(f.doc.project().media().size(), size_t{1});
        QCOMPARE(f.doc.project().media().front().id, QStringLiteral("m1"));
        QCOMPARE(f.doc.project().timeline(), before.timeline());
        for (const QString& path : {pathA, pathB}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::ReadOnly));
            QCOMPARE(file.readAll(), QByteArrayLiteral("unchanged"));
        }
        const Project cleaned = f.doc.project();
        QCOMPARE(f.doc.undoText(), QStringLiteral("Remove Unused Media"));

        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), cleaned);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
    }

    void removeUnusedMediaRefusesNoOp()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("used")))));
        const Project before = f.doc.project();
        const QString priorUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(std::make_unique<RemoveUnusedMediaCommand>()));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), priorUndo);
    }

    void rippleInsertMovesMarkersAndSubtitlesWithClipsAndOneUndo()
    {
        Fixture f;
        const Clip first = clipOf(QStringLiteral("m1"), 0, 60, QStringLiteral("first"));
        const Clip later = clipOf(QStringLiteral("m1"), 60, 40, QStringLiteral("later"));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, first)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, later)));
        QVERIFY(f.doc.execute(
            edits::addMarker({QStringLiteral("before"), 59, QStringLiteral("Before")})));
        QVERIFY(f.doc.execute(edits::addMarker({QStringLiteral("at"), 60, QStringLiteral("At")})));
        QVERIFY(f.doc.execute(
            edits::addMarker({QStringLiteral("after"), 100, QStringLiteral("After")})));
        QVERIFY(f.doc.execute(edits::appendSubtitles(
            {{QStringLiteral("early"), 1000, 1500, QStringLiteral("Early")},
             {QStringLiteral("boundary"), 2000, 2600, QStringLiteral("Boundary")},
             {QStringLiteral("late"), 3000, 3400, QStringLiteral("Later")}})));
        const Project before = f.doc.project();
        QSignalSpy changed(&f.doc, &ProjectDocument::changed);
        const Clip inserted = clipOf(QStringLiteral("m1"), 60, 15, QStringLiteral("inserted"));

        QVERIFY(f.doc.execute(edits::rippleInsertSourceClip(f.v1, inserted)));
        const Project after = f.doc.project();
        QCOMPARE(after.timeline().clip(later.id)->start, FrameCount{75});
        QCOMPARE(after.timeline().clip(inserted.id)->start, FrameCount{60});
        QCOMPARE(after.markers()[0].frame, FrameCount{59});
        QCOMPARE(after.markers()[1].frame, FrameCount{75});
        QCOMPARE(after.markers()[2].frame, FrameCount{115});
        QCOMPARE(after.subtitles().cues[0].startMs, std::int64_t{1000});
        QCOMPARE(after.subtitles().cues[1].startMs, std::int64_t{2500});
        QCOMPARE(after.subtitles().cues[1].endMs, std::int64_t{3100});
        QCOMPARE(after.subtitles().cues[2].startMs, std::int64_t{3500});
        QVERIFY(changed.size() == 1);
        const ChangeSet flags = changed.last().at(0).value<ChangeSet>();
        QVERIFY(flags.markers);
        QVERIFY(flags.subtitles);
        QVERIFY(flags.tracks.contains(f.v1));
        QCOMPARE(f.doc.undoText(), QStringLiteral("Ripple Insert Source Range"));
        const auto serialized = project_format::fromBytes(project_format::toBytes(after));
        QVERIFY(serialized);
        QCOMPARE(serialized->markers(), after.markers());
        QCOMPARE(serialized->subtitles(), after.subtitles());
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), after);
    }

    void rippleInsertRejectsSpanningSubtitleAndOverflowWithoutPartialEdit()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 60, 50, QStringLiteral("later")))));
        QVERIFY(f.doc.execute(edits::addMarker({QStringLiteral("at"), 60, QStringLiteral("At")})));
        QVERIFY(f.doc.execute(edits::addSubtitle(
            {QStringLiteral("spanning"), 1500, 2200, QStringLiteral("Across edit")})));
        const Clip inserted = clipOf(QStringLiteral("m1"), 60, 15, QStringLiteral("inserted"));
        const Project before = f.doc.project();
        const QString undoBefore = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::rippleInsertSourceClip(f.v1, inserted)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undoBefore);

        QVERIFY(f.doc.execute(edits::removeSubtitle(QStringLiteral("spanning"))));
        QVERIFY(f.doc.execute(edits::addMarker(
            {QStringLiteral("limit"), kMaxTimelineFrames, QStringLiteral("End")})));
        const Project atLimit = f.doc.project();
        const QString undoAtLimit = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::rippleInsertSourceClip(f.v1, inserted)));
        QCOMPARE(f.doc.project(), atLimit);
        QCOMPARE(f.doc.undoText(), undoAtLimit);
    }

    void rippleInsertShiftsSubtitlesAtFractionalFrameRates()
    {
        const QList<FrameRate> rates = {FrameRate::integer(24), FrameRate::integer(25),
                                        *FrameRate::create(30000, 1001), FrameRate::integer(30),
                                        FrameRate::integer(60)};
        for (const FrameRate& rate : rates) {
            ProjectSettings settings;
            settings.frameRate = rate;
            Project project = Project::createNew(QStringLiteral("Rational ripple"), settings);
            const Id trackId = project.timeline().tracks().front().id;
            ProjectDocument doc(std::move(project));
            QVERIFY(doc.execute(std::make_unique<AddMediaCommand>(
                std::vector<MediaAsset>{videoAsset(QStringLiteral("m1"))})));
            constexpr FrameCount cut = 60;
            constexpr FrameCount duration = 17;
            const auto cutMs = subtitleMillisecondsAtFrame(cut, rate);
            QVERIFY(cutMs);
            const std::int64_t start = *cutMs + 100;
            const std::int64_t end = start + 250;
            QVERIFY(doc.execute(edits::addSubtitle(
                {QStringLiteral("cue"), start, end, QStringLiteral("Fractional rate")})));
            const Project before = doc.project();
            const auto cueStartFrame = subtitleFrameAtMilliseconds(start, rate);
            const auto cueEndFrame = subtitleFrameAtMilliseconds(end, rate);
            QVERIFY(cueStartFrame);
            QVERIFY(cueEndFrame);
            const auto startBefore = subtitleMillisecondsAtFrame(*cueStartFrame, rate);
            const auto startAfter = subtitleMillisecondsAtFrame(*cueStartFrame + duration, rate);
            const auto endBefore = subtitleMillisecondsAtFrame(*cueEndFrame, rate);
            const auto endAfter = subtitleMillisecondsAtFrame(*cueEndFrame + duration, rate);
            QVERIFY(startBefore && startAfter && endBefore && endAfter);
            Clip clip = clipOf(QStringLiteral("m1"), cut, duration);
            QVERIFY(doc.execute(edits::rippleInsertSourceClip(trackId, clip)));
            const SubtitleCue& moved = doc.project().subtitles().cues.front();
            QCOMPARE(moved.startMs, start + *startAfter - *startBefore);
            QCOMPARE(moved.endMs, end + *endAfter - *endBefore);
            QVERIFY(doc.undo());
            QCOMPARE(doc.project(), before);
            QVERIFY(doc.redo());
            QCOMPARE(doc.project().subtitles().cues.front().startMs,
                     start + *startAfter - *startBefore);
        }
    }

    void rippleInsertRangeShiftsMultipleTracksWithOneUndoAndKeepsTransitions()
    {
        Fixture f;
        Clip first = clipOf(QStringLiteral("m1"), 0, 30, QStringLiteral("before"));
        Clip second = clipOf(QStringLiteral("m1"), 80, 40, QStringLiteral("after"));
        Clip third = clipOf(QStringLiteral("m1"), 120, 40, QStringLiteral("later"));
        Clip sound = clipOf(QStringLiteral("m1"), 95, 30, QStringLiteral("sound"));
        second.sourceIn = 60;
        third.sourceIn = 120;
        third.properties.gain = 0.8;
        sound.sourceIn = 200;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, first)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, second)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, third)));
        QVERIFY(f.doc.execute(edits::addClip(f.a1, sound)));
        QVERIFY(f.doc.execute(edits::addTransition(second.id, 10, QStringLiteral("crossfade"))));
        const Project before = f.doc.project();

        Clip inserted = clipOf(QStringLiteral("m1"), 80, 20, QStringLiteral("ripple-source"));
        inserted.sourceIn = 12;
        QVERIFY(f.doc.execute(edits::rippleInsertSourceClip(f.v1, inserted)));
        const Project after = f.doc.project();
        const Timeline& timeline = after.timeline();
        QCOMPARE(timeline.clipCount(), before.timeline().clipCount() + 1);
        QCOMPARE(*timeline.clip(inserted.id), inserted);
        QCOMPARE(timeline.clip(first.id)->start, first.start);
        QCOMPARE(timeline.clip(second.id)->start, FrameCount{100});
        QCOMPARE(timeline.clip(third.id)->start, FrameCount{140});
        QCOMPARE(timeline.clip(sound.id)->start, FrameCount{115});
        QCOMPARE(timeline.clip(third.id)->properties, third.properties);
        QCOMPARE(timeline.clip(sound.id)->sourceIn, sound.sourceIn);
        QVERIFY(timeline.track(f.v1)->transitions == before.timeline().track(f.v1)->transitions);
        QVERIFY(timeline.validate(after.sourceLookup()));
        QCOMPARE(f.doc.undoText(), QStringLiteral("Ripple Insert Source Range"));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), after);
    }

    void rippleInsertRangeWorksAtEndWithNoFollowingClips()
    {
        Fixture f;
        Clip first = clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("first"));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, first)));
        const Project before = f.doc.project();
        Clip appended = clipOf(QStringLiteral("m1"), 40, 15, QStringLiteral("at-end"));
        appended.sourceIn = 25;
        QVERIFY(f.doc.execute(edits::rippleInsertSourceClip(f.v1, appended)));
        const Project after = f.doc.project();
        QCOMPARE(after.timeline().clipCount(), before.timeline().clipCount() + 1);
        QCOMPARE(*after.timeline().clip(appended.id), appended);
        QCOMPARE(*after.timeline().clip(first.id), first);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), after);

        // Empty projects also admit the initial excerpt, unlike Insert Timeline Gap.
        Fixture empty;
        Clip alone = clipOf(QStringLiteral("m1"), 0, 20, QStringLiteral("alone"));
        const Project emptyBefore = empty.doc.project();
        QVERIFY(empty.doc.execute(edits::rippleInsertSourceClip(empty.v1, alone)));
        QCOMPARE(*empty.doc.project().timeline().clip(alone.id), alone);
        QVERIFY(empty.doc.undo());
        QCOMPARE(empty.doc.project(), emptyBefore);
        QVERIFY(!empty.doc.execute(edits::insertGap(0, 20)));
    }

    void rippleInsertRangeFailuresLeaveAllTracksAndUndoHistoryIntact()
    {
        Fixture f;
        Clip left = clipOf(QStringLiteral("m1"), 0, 60, QStringLiteral("left"));
        Clip right = clipOf(QStringLiteral("m1"), 60, 40, QStringLiteral("right"));
        left.sourceIn = 40;
        right.sourceIn = 120;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, left)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, right)));
        Clip inserted = clipOf(QStringLiteral("m1"), 60, 15, QStringLiteral("inserted"));
        inserted.sourceIn = 10;
        const Project baseline = f.doc.project();
        const QString undo = f.doc.undoText();

        Clip invalid = inserted;
        invalid.assetId = QStringLiteral("missing");
        QVERIFY(!f.doc.execute(edits::rippleInsertSourceClip(f.v1, invalid)));
        QVERIFY(!f.doc.execute(edits::rippleInsertSourceClip(QStringLiteral("missing"), inserted)));
        invalid = inserted;
        invalid.sourceIn = 595;
        QVERIFY(!f.doc.execute(edits::rippleInsertSourceClip(f.v1, invalid)));
        QCOMPARE(f.doc.project(), baseline);
        QCOMPARE(f.doc.undoText(), undo);

        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v1, true)));
        const Project lockedTarget = f.doc.project();
        const QString lockedUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::rippleInsertSourceClip(f.v1, inserted)));
        QCOMPARE(f.doc.project(), lockedTarget);
        QCOMPARE(f.doc.undoText(), lockedUndo);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), baseline);

        // A locked, later audio clip blocks the whole cross-track ripple.
        Clip audio = clipOf(QStringLiteral("m1"), 75, 25, QStringLiteral("audio"));
        QVERIFY(f.doc.execute(edits::addClip(f.a1, audio)));
        QVERIFY(f.doc.execute(edits::setTrackLocked(f.a1, true)));
        const Project lockedAudio = f.doc.project();
        const QString audioUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::rippleInsertSourceClip(f.v1, inserted)));
        QCOMPARE(f.doc.project(), lockedAudio);
        QCOMPARE(f.doc.undoText(), audioUndo);
        QVERIFY(f.doc.undo());
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), baseline);

        // No implicit split of a clip which crosses the playhead on another lane.
        Clip spanning = clipOf(QStringLiteral("m1"), 45, 30, QStringLiteral("spanning"));
        QVERIFY(f.doc.execute(edits::addClip(f.v2, spanning)));
        const Project crossed = f.doc.project();
        const QString crossedUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::rippleInsertSourceClip(f.v1, inserted)));
        QCOMPARE(f.doc.project(), crossed);
        QCOMPARE(f.doc.undoText(), crossedUndo);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), baseline);

        // Existing transition attached to the original cut must not be lost.
        QVERIFY(f.doc.execute(edits::addTransition(left.id, 10, QStringLiteral("cut"))));
        const Project transitioned = f.doc.project();
        const QString transitionUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::rippleInsertSourceClip(f.v1, inserted)));
        QCOMPARE(f.doc.project(), transitioned);
        QCOMPARE(f.doc.undoText(), transitionUndo);
    }

    void sequenceGapEditsKeepClipsMarkersAndSubtitlesTogether()
    {
        Fixture f;
        const Clip early = clipOf(QStringLiteral("m1"), 0, 30, QStringLiteral("early"));
        const Clip picture = clipOf(QStringLiteral("m1"), 90, 40, QStringLiteral("picture"));
        const Clip sound = clipOf(QStringLiteral("m1"), 100, 20, QStringLiteral("sound"));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, early)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, picture)));
        QVERIFY(f.doc.execute(edits::addClip(f.a1, sound)));
        QVERIFY(f.doc.execute(edits::addMarker(
            {QStringLiteral("before"), 59, QStringLiteral("Before the gap")})));
        QVERIFY(f.doc.execute(edits::addMarker(
            {QStringLiteral("at"), 60, QStringLiteral("At the gap")})));
        QVERIFY(f.doc.execute(edits::addMarker(
            {QStringLiteral("after"), 110, QStringLiteral("After the gap")})));
        QVERIFY(f.doc.execute(edits::appendSubtitles(
            {{QStringLiteral("early-sub"), 1000, 1500, QStringLiteral("Earlier")},
             {QStringLiteral("at-sub"), 2000, 2500, QStringLiteral("At cut")},
             {QStringLiteral("late-sub"), 3000, 3300, QStringLiteral("Later")}})));
        const Project before = f.doc.project();
        QSignalSpy changes(&f.doc, &ProjectDocument::changed);

        QVERIFY(f.doc.execute(edits::insertGap(60, 15)));
        const Project gapped = f.doc.project();
        QCOMPARE(gapped.timeline().clip(early.id)->start, FrameCount{0});
        QCOMPARE(gapped.timeline().clip(picture.id)->start, FrameCount{105});
        QCOMPARE(gapped.timeline().clip(sound.id)->start, FrameCount{115});
        QCOMPARE(gapped.markers()[0].frame, FrameCount{59});
        QCOMPARE(gapped.markers()[1].frame, FrameCount{75});
        QCOMPARE(gapped.markers()[2].frame, FrameCount{125});
        QCOMPARE(gapped.subtitles().cues[0].startMs, std::int64_t{1000});
        QCOMPARE(gapped.subtitles().cues[1].startMs, std::int64_t{2500});
        QCOMPARE(gapped.subtitles().cues[1].endMs, std::int64_t{3000});
        QCOMPARE(gapped.subtitles().cues[2].startMs, std::int64_t{3500});
        QCOMPARE(changes.size(), 1);
        const ChangeSet flags = changes.last().at(0).value<ChangeSet>();
        QVERIFY(flags.markers);
        QVERIFY(flags.subtitles);
        QVERIFY(flags.tracks.contains(f.v1));
        QVERIFY(flags.tracks.contains(f.a1));
        const auto loaded = project_format::fromBytes(project_format::toBytes(gapped));
        QVERIFY(loaded);
        QCOMPARE(loaded->markers(), gapped.markers());
        QCOMPARE(loaded->subtitles(), gapped.subtitles());
        QCOMPARE(f.doc.undoText(), QStringLiteral("Insert Timeline Gap"));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), gapped);

        QVERIFY(f.doc.execute(edits::removeEmptyTime(60, 15)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), QStringLiteral("Remove Empty Timeline Time"));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), gapped);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), before);
    }

    void sequenceGapEditsRejectAmbiguousCuesAndMarkersAtomically()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 100, 20, QStringLiteral("later")))));
        QVERIFY(f.doc.execute(edits::addSubtitle(
            {QStringLiteral("cross"), 1900, 2100, QStringLiteral("Crosses insertion")})));
        const Project crossing = f.doc.project();
        const QString undoCrossing = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::insertGap(60, 15)));
        QCOMPARE(f.doc.project(), crossing);
        QCOMPARE(f.doc.undoText(), undoCrossing);

        QVERIFY(f.doc.execute(edits::removeSubtitle(QStringLiteral("cross"))));
        QVERIFY(f.doc.execute(edits::addMarker(
            {QStringLiteral("interior"), 67, QStringLiteral("Inside removed time")})));
        const Project markerInside = f.doc.project();
        const QString markerUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::removeEmptyTime(60, 15)));
        QCOMPARE(f.doc.project(), markerInside);
        QCOMPARE(f.doc.undoText(), markerUndo);

        QVERIFY(f.doc.execute(edits::removeMarker(QStringLiteral("interior"))));
        QVERIFY(f.doc.execute(edits::addSubtitle(
            {QStringLiteral("overlap"), 2400, 2700, QStringLiteral("Across removed time")})));
        const Project subtitleInside = f.doc.project();
        const QString subtitleUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::removeEmptyTime(60, 15)));
        QCOMPARE(f.doc.project(), subtitleInside);
        QCOMPARE(f.doc.undoText(), subtitleUndo);
        QVERIFY(!f.doc.execute(edits::removeEmptyTime(60, 0)));
        QCOMPARE(f.doc.project(), subtitleInside);
        QCOMPARE(f.doc.undoText(), subtitleUndo);
    }

    void insertingGapRejectsMarkerOverflowWithoutMovingAnyClip()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 90, 30, QStringLiteral("later")))));
        QVERIFY(f.doc.execute(edits::addMarker(
            {QStringLiteral("end"), kMaxTimelineFrames, QStringLiteral("Near limit")})));
        const Project before = f.doc.project();
        const QString undoBefore = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::insertGap(60, 15)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undoBefore);
    }

    void gapInsertionShiftsCaptionsExactlyAtFractionalRates()
    {
        const QList<FrameRate> rates = {FrameRate::integer(24), FrameRate::integer(25),
                                        *FrameRate::create(30000, 1001), FrameRate::integer(30),
                                        FrameRate::integer(60)};
        for (const FrameRate& rate : rates) {
            ProjectSettings settings;
            settings.frameRate = rate;
            ProjectDocument doc(Project::createNew(QStringLiteral("Gap captions"), settings));
            const Id trackId = doc.project().timeline().tracks().front().id;
            QVERIFY(doc.execute(std::make_unique<AddMediaCommand>(
                std::vector<MediaAsset>{videoAsset(QStringLiteral("m1"))})));
            QVERIFY(doc.execute(edits::addClip(
                trackId, clipOf(QStringLiteral("m1"), 95, 20, QStringLiteral("later")))));
            const auto baseMs = subtitleMillisecondsAtFrame(70, rate);
            QVERIFY(baseMs);
            const std::int64_t startMs = *baseMs + 10;
            const std::int64_t endMs = startMs + 170;
            QVERIFY(doc.execute(edits::addSubtitle(
                {QStringLiteral("cue"), startMs, endMs, QStringLiteral("Exact caption")})));
            const Project before = doc.project();
            const auto fromStart = subtitleFrameAtMilliseconds(startMs, rate);
            const auto fromEnd = subtitleFrameAtMilliseconds(endMs, rate);
            QVERIFY(fromStart && fromEnd);
            const auto startBefore = subtitleMillisecondsAtFrame(*fromStart, rate);
            const auto startAfter = subtitleMillisecondsAtFrame(*fromStart + 11, rate);
            const auto endBefore = subtitleMillisecondsAtFrame(*fromEnd, rate);
            const auto endAfter = subtitleMillisecondsAtFrame(*fromEnd + 11, rate);
            QVERIFY(startBefore && startAfter && endBefore && endAfter);
            QVERIFY(doc.execute(edits::insertGap(60, 11)));
            const SubtitleCue& cue = doc.project().subtitles().cues.front();
            QCOMPARE(cue.startMs, startMs + *startAfter - *startBefore);
            QCOMPARE(cue.endMs, endMs + *endAfter - *endBefore);
            QCOMPARE(doc.project().timeline().clip(QStringLiteral("later"))->start,
                     FrameCount{106});
            QVERIFY(doc.undo());
            QCOMPARE(doc.project(), before);
        }
    }

    void insertGapMovesMultipleTracksAndPreservesTransitions()
    {
        Fixture f;
        Clip first = clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("first"));
        Clip second = clipOf(QStringLiteral("m1"), 80, 40, QStringLiteral("second"));
        Clip third = clipOf(QStringLiteral("m1"), 120, 40, QStringLiteral("third"));
        Clip sound = clipOf(QStringLiteral("m1"), 100, 40, QStringLiteral("sound"));
        second.sourceIn = 60;
        second.properties.gain = 0.7;
        third.sourceIn = 120;
        sound.sourceIn = 200;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, first)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, second)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, third)));
        QVERIFY(f.doc.execute(edits::addClip(f.a1, sound)));
        QVERIFY(f.doc.execute(edits::addTransition(second.id, 10, QStringLiteral("crossfade"))));
        const Project before = f.doc.project();

        QVERIFY(f.doc.execute(edits::insertGap(60, 15)));
        const Timeline& changed = f.doc.project().timeline();
        QCOMPARE(changed.clip(first.id)->start, FrameCount{0});
        QCOMPARE(changed.clip(second.id)->start, FrameCount{95});
        QCOMPARE(changed.clip(third.id)->start, FrameCount{135});
        QCOMPARE(changed.clip(sound.id)->start, FrameCount{115});
        QCOMPARE(changed.clip(second.id)->sourceIn, second.sourceIn);
        QCOMPARE(changed.clip(second.id)->properties, second.properties);
        QCOMPARE(changed.clip(third.id)->sourceIn, third.sourceIn);
        QVERIFY(changed.track(f.v1)->transitions == before.timeline().track(f.v1)->transitions);
        QVERIFY(changed.validate(f.doc.project().sourceLookup()));
        QCOMPARE(f.doc.undoText(), QStringLiteral("Insert Timeline Gap"));
        const Project after = f.doc.project();
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), after);
    }

    void insertGapRefusesClipIntersectionsAndLockedAffectedTracks()
    {
        Fixture f;
        Clip existing = clipOf(QStringLiteral("m1"), 10, 70, QStringLiteral("existing"));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, existing)));
        const Project before = f.doc.project();
        const QString undo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::insertGap(50, 10)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undo);

        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v1, true)));
        const Project locked = f.doc.project();
        const QString lockedUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::insertGap(10, 10)));
        QCOMPARE(f.doc.project(), locked);
        QCOMPARE(f.doc.undoText(), lockedUndo);
        QVERIFY(!f.doc.execute(edits::insertGap(100, 10)));
        QCOMPARE(f.doc.project(), locked);
        QCOMPARE(f.doc.undoText(), lockedUndo);
    }

    void insertGapRejectsTransitionsAcrossTheInsertionCut()
    {
        Fixture f;
        Clip left = clipOf(QStringLiteral("m1"), 0, 60, QStringLiteral("left"));
        Clip right = clipOf(QStringLiteral("m1"), 60, 60, QStringLiteral("right"));
        left.sourceIn = 30;
        right.sourceIn = 100;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, left)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, right)));
        QVERIFY(f.doc.execute(edits::addTransition(left.id, 10, QStringLiteral("cut"))));
        const Project before = f.doc.project();
        const QString undo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::insertGap(60, 20)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undo);
    }

    void insertGapChecksBoundsAndLeavesFailedEditsOutOfHistory()
    {
        Fixture f;
        const FrameCount last = kMaxTimelineFrames;
        Clip late = clipOf(QStringLiteral("m1"), last - 25, 20, QStringLiteral("late"));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, late)));
        const Project before = f.doc.project();
        const QString undo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::insertGap(last - 30, 15)));
        QVERIFY(!f.doc.execute(edits::insertGap(-1, 1)));
        QVERIFY(!f.doc.execute(edits::insertGap(0, 0)));
        QVERIFY(!f.doc.execute(edits::insertGap(last, 1)));
        QVERIFY(!f.doc.execute(edits::insertGap(0, last)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undo);
    }

    void removeEmptyTimeShiftsTracksAndPreservesClipData()
    {
        Fixture f;
        Clip before = clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("before"));
        Clip second = clipOf(QStringLiteral("m1"), 80, 40, QStringLiteral("second"));
        Clip third = clipOf(QStringLiteral("m1"), 120, 40, QStringLiteral("third"));
        Clip sound = clipOf(QStringLiteral("m1"), 100, 40, QStringLiteral("sound"));
        second.sourceIn = 70;
        second.properties.video.opacity = 0.5;
        third.sourceIn = 120;
        sound.sourceIn = 200;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, before)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, second)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, third)));
        QVERIFY(f.doc.execute(edits::addClip(f.a1, sound)));
        QVERIFY(f.doc.execute(edits::addTransition(second.id, 10, QStringLiteral("blend"))));
        const Project original = f.doc.project();

        QVERIFY(f.doc.execute(edits::removeEmptyTime(60, 10)));
        const Timeline& moved = f.doc.project().timeline();
        QCOMPARE(moved.clip(before.id)->start, FrameCount{0});
        QCOMPARE(moved.clip(second.id)->start, FrameCount{70});
        QCOMPARE(moved.clip(third.id)->start, FrameCount{110});
        QCOMPARE(moved.clip(sound.id)->start, FrameCount{90});
        QCOMPARE(moved.clip(second.id)->sourceIn, second.sourceIn);
        QCOMPARE(moved.clip(second.id)->properties, second.properties);
        QVERIFY(moved.track(f.v1)->transitions == original.timeline().track(f.v1)->transitions);
        QVERIFY(moved.validate(f.doc.project().sourceLookup()));
        QCOMPARE(f.doc.undoText(), QStringLiteral("Remove Empty Timeline Time"));
        const Project after = f.doc.project();
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), original);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), after);
    }

    void removingEmptyTimeRejectsClipIntersectionsAndLockedTracks()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 20, 60, QStringLiteral("clip")))));
        const Project original = f.doc.project();
        const QString undo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::removeEmptyTime(50, 10)));
        QVERIFY(!f.doc.execute(edits::removeEmptyTime(10, 20)));
        QCOMPARE(f.doc.project(), original);
        QCOMPARE(f.doc.undoText(), undo);

        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v1, true)));
        const Project locked = f.doc.project();
        const QString lockedUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::removeEmptyTime(10, 5)));
        QCOMPARE(f.doc.project(), locked);
        QCOMPARE(f.doc.undoText(), lockedUndo);
    }

    void removingEmptyTimeValidatesRangeAndNoOp()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 100, 20, QStringLiteral("clip")))));
        const Project original = f.doc.project();
        const QString undo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::removeEmptyTime(-1, 10)));
        QVERIFY(!f.doc.execute(edits::removeEmptyTime(0, 0)));
        QVERIFY(!f.doc.execute(edits::removeEmptyTime(kMaxTimelineFrames, 1)));
        QVERIFY(!f.doc.execute(edits::removeEmptyTime(kMaxTimelineFrames - 1, 10)));
        QVERIFY(!f.doc.execute(edits::removeEmptyTime(120, 10)));
        QCOMPARE(f.doc.project(), original);
        QCOMPARE(f.doc.undoText(), undo);
    }

    void insertingThenRemovingSameEmptyGapRestoresWholeProject()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 50, 50, QStringLiteral("picture")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.a1, clipOf(QStringLiteral("m1"), 65, 40, QStringLiteral("sound")))));
        const Project before = f.doc.project();
        QVERIFY(f.doc.execute(edits::insertGap(0, 10)));
        const Project withGap = f.doc.project();
        QVERIFY(f.doc.execute(edits::removeEmptyTime(0, 10)));
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), withGap);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
    }

    void batchPictureAdjustmentsApplyAllThreePropertiesWithUndo()
    {
        Fixture f;
        Clip first = clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("picture-first"));
        first.properties.gain = 0.6;
        first.properties.fadeIn = 8;
        first.properties.video.opacity = 0.7;
        first.properties.video.brightness = 0.2;
        Clip second = clipOf(QStringLiteral("m1"), 50, 20, QStringLiteral("picture-second"));
        second.properties.video.saturation = 1.5;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, first)));
        QVERIFY(f.doc.execute(edits::addClip(f.v2, second)));
        const Project initial = f.doc.project();
        const QList<Id> selected{first.id, second.id};

        QVERIFY(f.doc.execute(
            edits::setClipVideoAdjustment(selected, QStringLiteral("brightness"), -0.2)));
        const Project bright = f.doc.project();
        Clip expectedFirst = first;
        Clip expectedSecond = second;
        expectedFirst.properties.video.brightness = -0.2;
        expectedSecond.properties.video.brightness = -0.2;
        QCOMPARE(*bright.timeline().clip(first.id), expectedFirst);
        QCOMPARE(*bright.timeline().clip(second.id), expectedSecond);
        QCOMPARE(f.doc.undoText(), QStringLiteral("Set Selected Video Adjustment"));

        QVERIFY(f.doc.execute(
            edits::setClipVideoAdjustment(selected, QStringLiteral("contrast"), 1.2)));
        const Project contrasted = f.doc.project();
        expectedFirst.properties.video.contrast = 1.2;
        expectedSecond.properties.video.contrast = 1.2;
        QCOMPARE(*contrasted.timeline().clip(first.id), expectedFirst);
        QCOMPARE(*contrasted.timeline().clip(second.id), expectedSecond);

        QVERIFY(f.doc.execute(
            edits::setClipVideoAdjustment(selected, QStringLiteral("saturation"), 0.0)));
        const Project desaturated = f.doc.project();
        expectedFirst.properties.video.saturation = 0.0;
        expectedSecond.properties.video.saturation = 0.0;
        QCOMPARE(*desaturated.timeline().clip(first.id), expectedFirst);
        QCOMPARE(*desaturated.timeline().clip(second.id), expectedSecond);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), contrasted);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), bright);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), initial);
        QVERIFY(f.doc.redo());
        QVERIFY(f.doc.redo());
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), desaturated);
    }

    void batchPictureAdjustmentsRejectInvalidAndLockedSelectionsAtomically()
    {
        Fixture f;
        const Clip video = clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("pic-valid"));
        const Clip audio = clipOf(QStringLiteral("m1"), 0, 30, QStringLiteral("pic-audio"));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, video)));
        QVERIFY(f.doc.execute(edits::addClip(f.a1, audio)));
        const Project before = f.doc.project();
        const QString undoBefore = f.doc.undoText();
        const QString brightness = QStringLiteral("brightness");
        const QString contrast = QStringLiteral("contrast");
        const QString saturation = QStringLiteral("saturation");

        QVERIFY(!f.doc.execute(edits::setClipVideoAdjustment({}, brightness, 0.2)));
        QVERIFY(
            !f.doc.execute(edits::setClipVideoAdjustment({video.id, video.id}, brightness, 0.2)));
        QVERIFY(
            !f.doc.execute(edits::setClipVideoAdjustment({video.id, audio.id}, brightness, 0.2)));
        QVERIFY(!f.doc.execute(
            edits::setClipVideoAdjustment({video.id, QStringLiteral("missing")}, brightness, 0.2)));
        QVERIFY(!f.doc.execute(
            edits::setClipVideoAdjustment({video.id}, QStringLiteral("opacity"), 0.5)));
        QVERIFY(!f.doc.execute(edits::setClipVideoAdjustment({video.id}, brightness, -1.1)));
        QVERIFY(!f.doc.execute(edits::setClipVideoAdjustment({video.id}, brightness, 1.1)));
        QVERIFY(!f.doc.execute(edits::setClipVideoAdjustment({video.id}, contrast, -0.1)));
        QVERIFY(!f.doc.execute(edits::setClipVideoAdjustment({video.id}, contrast, 2.1)));
        QVERIFY(!f.doc.execute(edits::setClipVideoAdjustment({video.id}, saturation, 3.1)));
        QVERIFY(!f.doc.execute(edits::setClipVideoAdjustment(
            {video.id}, saturation, std::numeric_limits<double>::quiet_NaN())));
        QVERIFY(!f.doc.execute(edits::setClipVideoAdjustment(
            {video.id}, saturation, std::numeric_limits<double>::infinity())));
        QVERIFY(!f.doc.execute(edits::setClipVideoAdjustment({video.id}, brightness,
                                                             video.properties.video.brightness)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undoBefore);

        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v1, true)));
        const Project locked = f.doc.project();
        const QString lockedUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::setClipVideoAdjustment({video.id}, brightness, 0.2)));
        QCOMPARE(f.doc.project(), locked);
        QCOMPARE(f.doc.undoText(), lockedUndo);
    }

    void batchVideoOpacityPreservesOtherPropertiesAndUndoRedo()
    {
        Fixture f;
        Clip first = clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("opacity-first"));
        first.properties.gain = 0.7;
        first.properties.fadeIn = 6;
        first.properties.video.brightness = 0.2;
        first.properties.video.opacity = 0.9;
        Clip second = clipOf(QStringLiteral("m1"), 50, 20, QStringLiteral("opacity-second"));
        second.properties.video.opacity = 0.6;
        second.properties.video.saturation = 1.5;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, first)));
        QVERIFY(f.doc.execute(edits::addClip(f.v2, second)));
        const Project before = f.doc.project();

        QVERIFY(f.doc.execute(edits::setClipOpacities({first.id, second.id}, 0.25)));
        const Project changed = f.doc.project();
        Clip expectedFirst = first;
        Clip expectedSecond = second;
        expectedFirst.properties.video.opacity = 0.25;
        expectedSecond.properties.video.opacity = 0.25;
        QCOMPARE(*changed.timeline().clip(first.id), expectedFirst);
        QCOMPARE(*changed.timeline().clip(second.id), expectedSecond);
        QCOMPARE(f.doc.undoText(), QStringLiteral("Set Selected Video Opacity"));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), changed);
    }

    void batchVideoOpacityRejectsAudioInvalidAndLockedSelectionsAtomically()
    {
        Fixture f;
        const Clip first = clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("opacity-first"));
        const Clip second = clipOf(QStringLiteral("m1"), 50, 20, QStringLiteral("opacity-second"));
        const Clip audio = clipOf(QStringLiteral("m1"), 20, 30, QStringLiteral("opacity-audio"));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, first)));
        QVERIFY(f.doc.execute(edits::addClip(f.v2, second)));
        QVERIFY(f.doc.execute(edits::addClip(f.a1, audio)));
        const Project before = f.doc.project();
        const QString undoBefore = f.doc.undoText();

        QVERIFY(!f.doc.execute(edits::setClipOpacities({}, 0.5)));
        QVERIFY(!f.doc.execute(edits::setClipOpacities({first.id, first.id}, 0.5)));
        QVERIFY(!f.doc.execute(edits::setClipOpacities({first.id, audio.id}, 0.5)));
        QVERIFY(!f.doc.execute(edits::setClipOpacities({first.id, second.id}, -0.1)));
        QVERIFY(!f.doc.execute(edits::setClipOpacities({first.id, second.id}, 1.1)));
        QVERIFY(!f.doc.execute(
            edits::setClipOpacities({first.id}, std::numeric_limits<double>::quiet_NaN())));
        QVERIFY(!f.doc.execute(
            edits::setClipOpacities({first.id}, std::numeric_limits<double>::infinity())));
        QVERIFY(
            !f.doc.execute(edits::setClipOpacities({first.id, QStringLiteral("missing")}, 0.5)));
        QVERIFY(!f.doc.execute(edits::setClipOpacities({first.id, second.id}, 1.0)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undoBefore);

        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v2, true)));
        const Project locked = f.doc.project();
        const QString lockedUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::setClipOpacities({first.id, second.id}, 0.5)));
        QCOMPARE(f.doc.project(), locked);
        QCOMPARE(f.doc.undoText(), lockedUndo);
    }

    void batchGainUpdatesOnlyGainAndSupportsUndoRedo()
    {
        Fixture f;
        Clip picture = clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("gain-picture"));
        picture.properties.gain = 0.5;
        picture.properties.fadeIn = 5;
        picture.properties.video.opacity = 0.75;
        Clip sound = clipOf(QStringLiteral("m1"), 20, 12, QStringLiteral("gain-sound"));
        sound.properties.gain = 0.8;
        sound.properties.fadeOut = 4;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, picture)));
        QVERIFY(f.doc.execute(edits::addClip(f.a1, sound)));
        const Project before = f.doc.project();

        QVERIFY(f.doc.execute(edits::setClipGains({picture.id, sound.id}, 1.5)));
        const Project changed = f.doc.project();
        ClipProperties expectedPicture = picture.properties;
        ClipProperties expectedSound = sound.properties;
        expectedPicture.gain = 1.5;
        expectedSound.gain = 1.5;
        QCOMPARE(changed.timeline().clip(picture.id)->properties, expectedPicture);
        QCOMPARE(changed.timeline().clip(sound.id)->properties, expectedSound);
        QCOMPARE(f.doc.undoText(), QStringLiteral("Set Selected Audio Gain"));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), changed);
    }

    void batchGainRejectsInvalidSelectionsAndLockedTracksAtomically()
    {
        Fixture f;
        const Clip picture = clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("gain-guard"));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, picture)));
        const Project before = f.doc.project();
        const QString undoBefore = f.doc.undoText();

        QVERIFY(!f.doc.execute(edits::setClipGains({}, 1.5)));
        QVERIFY(!f.doc.execute(edits::setClipGains({picture.id, picture.id}, 1.5)));
        QVERIFY(!f.doc.execute(edits::setClipGains({QStringLiteral("missing")}, 1.5)));
        QVERIFY(!f.doc.execute(edits::setClipGains({picture.id, QStringLiteral("missing")}, 1.5)));
        QVERIFY(!f.doc.execute(edits::setClipGains({picture.id}, -0.1)));
        QVERIFY(!f.doc.execute(edits::setClipGains({picture.id}, kMaxClipGain + 0.1)));
        QVERIFY(!f.doc.execute(
            edits::setClipGains({picture.id}, std::numeric_limits<double>::quiet_NaN())));
        QVERIFY(!f.doc.execute(
            edits::setClipGains({picture.id}, std::numeric_limits<double>::infinity())));
        QVERIFY(!f.doc.execute(edits::setClipGains({picture.id}, picture.properties.gain)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undoBefore);

        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v1, true)));
        const Project locked = f.doc.project();
        const QString lockedUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::setClipGains({picture.id}, 1.5)));
        QCOMPARE(f.doc.project(), locked);
        QCOMPARE(f.doc.undoText(), lockedUndo);
    }

    void batchFadeClampsToIndividualClipLengthsAndPreservesOtherProperties()
    {
        Fixture f;
        Clip picture = clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("picture"));
        picture.properties.fadeOut = 15;
        picture.properties.gain = 0.6;
        Clip audio = clipOf(QStringLiteral("m1"), 20, 12, QStringLiteral("audio"));
        audio.properties.video.saturation = 0.8;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, picture)));
        QVERIFY(f.doc.execute(edits::addClip(f.a1, audio)));
        const Project before = f.doc.project();

        QVERIFY(f.doc.execute(edits::setClipFades({picture.id, audio.id}, 30, false)));
        const Timeline& changed = f.doc.project().timeline();
        QCOMPARE(changed.clip(picture.id)->properties.fadeIn, FrameCount{25});
        QCOMPARE(changed.clip(picture.id)->properties.fadeOut, FrameCount{15});
        QCOMPARE(changed.clip(audio.id)->properties.fadeIn, FrameCount{12});
        QCOMPARE(changed.clip(audio.id)->properties.fadeOut, FrameCount{0});
        QCOMPARE(changed.clip(picture.id)->properties.gain, picture.properties.gain);
        QCOMPARE(changed.clip(audio.id)->properties.video, audio.properties.video);
        QCOMPARE(f.doc.undoText(), QStringLiteral("Set Selected Fade Ins"));
        const Project after = f.doc.project();
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), after);

        QVERIFY(f.doc.execute(edits::setClipFades({picture.id, audio.id}, 10, true)));
        const Timeline& withOut = f.doc.project().timeline();
        QCOMPARE(withOut.clip(picture.id)->properties.fadeOut, FrameCount{10});
        QCOMPARE(withOut.clip(audio.id)->properties.fadeOut, FrameCount{0});
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), after);
    }

    void batchFadeRejectsLockedDuplicateMissingAndNoOpAtomically()
    {
        Fixture f;
        Clip picture = clipOf(QStringLiteral("m1"), 0, 30, QStringLiteral("picture"));
        Clip audio = clipOf(QStringLiteral("m1"), 0, 20, QStringLiteral("audio"));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, picture)));
        QVERIFY(f.doc.execute(edits::addClip(f.a1, audio)));
        const Project before = f.doc.project();
        const QString undo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::setClipFades({}, 10, false)));
        QVERIFY(!f.doc.execute(edits::setClipFades({picture.id, picture.id}, 10, false)));
        QVERIFY(!f.doc.execute(
            edits::setClipFades({picture.id, QStringLiteral("missing")}, 10, false)));
        QVERIFY(!f.doc.execute(edits::setClipFades({picture.id}, -1, false)));
        QVERIFY(!f.doc.execute(edits::setClipFades({picture.id}, kMaxTimelineFrames + 1, false)));
        QVERIFY(!f.doc.execute(edits::setClipFades({picture.id, audio.id}, 0, false)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undo);

        QVERIFY(f.doc.execute(edits::setTrackLocked(f.a1, true)));
        const Project locked = f.doc.project();
        const QString lockedUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::setClipFades({picture.id, audio.id}, 10, false)));
        QCOMPARE(f.doc.project(), locked);
        QCOMPARE(f.doc.undoText(), lockedUndo);
    }

    void undoRedoRestoresExactStates()
    {
        Fixture f;
        const Project initial = f.doc.project();
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 100, QStringLiteral("c1")))));
        const Project afterAdd = f.doc.project();
        QVERIFY(f.doc.execute(edits::splitClips({QStringLiteral("c1")}, 40)));
        QVERIFY(f.doc.execute(edits::moveClip(QStringLiteral("c1"), f.v2, 300)));
        const Project afterMove = f.doc.project();
        QCOMPARE(f.doc.project().timeline().clipCount(), 2);

        QVERIFY(f.doc.undo());
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), afterAdd);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), initial);
        QVERIFY(f.doc.redo());
        QVERIFY(f.doc.redo());
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), afterMove);
        QVERIFY(!f.doc.redo());
    }

    void slipMultiTrackSelectionAndUndoRedo()
    {
        Fixture f;
        Clip video = clipOf(QStringLiteral("m1"), 30, 80, QStringLiteral("picture"));
        video.sourceIn = 20;
        Clip sound = clipOf(QStringLiteral("m1"), 50, 60, QStringLiteral("audio"));
        sound.sourceIn = 100;
        sound.properties.gain = 0.4;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, video)));
        QVERIFY(f.doc.execute(edits::addClip(f.a1, sound)));
        const Project original = f.doc.project();
        QVERIFY(f.doc.execute(
            edits::slipClips({QStringLiteral("picture"), QStringLiteral("audio")}, 15)));
        const Timeline& shifted = f.doc.project().timeline();
        QCOMPARE(shifted.clip(video.id)->sourceIn, FrameCount{35});
        QCOMPARE(shifted.clip(sound.id)->sourceIn, FrameCount{115});
        QCOMPARE(shifted.clip(video.id)->start, video.start);
        QCOMPARE(shifted.clip(video.id)->duration, video.duration);
        QCOMPARE(shifted.clip(sound.id)->start, sound.start);
        QCOMPARE(shifted.clip(sound.id)->properties.gain, sound.properties.gain);
        QCOMPARE(f.doc.undoText(), QStringLiteral("Slip 2 Clip(s)"));
        const Project slipped = f.doc.project();
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), original);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), slipped);
    }

    void slipBatchFailureIsAtomicAndDoesNotChangeUndoText()
    {
        Fixture f;
        Clip first = clipOf(QStringLiteral("m1"), 0, 50, QStringLiteral("first"));
        first.sourceIn = 70;
        Clip second = clipOf(QStringLiteral("m1"), 150, 50, QStringLiteral("second"));
        second.sourceIn = 550; // source ends at 600
        QVERIFY(f.doc.execute(edits::addClip(f.v1, first)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, second)));
        const Project before = f.doc.project();
        const QString undoText = f.doc.undoText();
        QVERIFY(!f.doc.execute(
            edits::slipClips({QStringLiteral("first"), QStringLiteral("second")}, 1)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undoText);
        QVERIFY(!f.doc.execute(edits::slipClips({}, 1)));
        QVERIFY(!f.doc.execute(edits::slipClips({QStringLiteral("first")}, 0)));
        QVERIFY(!f.doc.execute(
            edits::slipClips({QStringLiteral("first"), QStringLiteral("first")}, 1)));
        QVERIFY(!f.doc.execute(edits::slipClips({QStringLiteral("missing")}, 1)));
        QVERIFY(!f.doc.execute(edits::slipClips({QStringLiteral("first")}, -71)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undoText);
    }

    void rollCutCommandIsSingleUndoableOperation()
    {
        Fixture f;
        Clip left = clipOf(QStringLiteral("m1"), 10, 100, QStringLiteral("left"));
        left.sourceIn = 50;
        left.properties.gain = 0.6;
        Clip right = clipOf(QStringLiteral("m1"), 110, 80, QStringLiteral("right"));
        right.sourceIn = 150;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, left)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, right)));
        const Project before = f.doc.project();
        QVERIFY(f.doc.execute(edits::rollCut(left.id, 15)));
        QCOMPARE(f.doc.undoText(), QStringLiteral("Roll Cut Right"));
        const Timeline& t = f.doc.project().timeline();
        QCOMPARE(t.clip(left.id)->duration, FrameCount{115});
        QCOMPARE(t.clip(left.id)->properties.gain, 0.6);
        QCOMPARE(t.clip(right.id)->start, FrameCount{125});
        QCOMPARE(t.clip(right.id)->sourceIn, FrameCount{165});
        QCOMPARE(t.clip(right.id)->duration, FrameCount{65});
        const Project after = f.doc.project();
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), after);
        QVERIFY(f.doc.execute(edits::rollCut(left.id, -15)));
        QCOMPARE(f.doc.project(), before);
    }

    void rollCutFailuresDoNotModifyHistoryOrOtherClips()
    {
        Fixture f;
        Clip left = clipOf(QStringLiteral("m1"), 0, 50, QStringLiteral("left"));
        left.sourceIn = 550; // ends exactly at the source boundary
        Clip right = clipOf(QStringLiteral("m1"), 50, 50, QStringLiteral("right"));
        right.sourceIn = 10;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, left)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, right)));
        const Project original = f.doc.project();
        const QString undo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::rollCut(left.id, 1)));
        QVERIFY(!f.doc.execute(edits::rollCut(left.id, -11)));
        QVERIFY(!f.doc.execute(edits::rollCut(QStringLiteral("missing"), 1)));
        QVERIFY(!f.doc.execute(edits::rollCut(left.id, 0)));
        QCOMPARE(f.doc.project(), original);
        QCOMPARE(f.doc.undoText(), undo);
    }

    void duplicateSelectionAtPlayheadPreservesOffsetsAndAttributes()
    {
        Fixture f;
        Clip picture = clipOf(QStringLiteral("m1"), 10, 20, QStringLiteral("picture"));
        picture.sourceIn = 5;
        picture.properties.fadeIn = 3;
        picture.properties.video.opacity = 0.4;
        Clip overlay = clipOf(QStringLiteral("m1"), 35, 15, QStringLiteral("overlay"));
        overlay.sourceIn = 20;
        overlay.properties.video.contrast = 1.2;
        Clip sound = clipOf(QStringLiteral("m1"), 25, 10, QStringLiteral("sound"));
        sound.sourceIn = 50;
        sound.properties.gain = 0.7;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, picture)));
        QVERIFY(f.doc.execute(edits::addClip(f.v2, overlay)));
        QVERIFY(f.doc.execute(edits::addClip(f.a1, sound)));
        const Project before = f.doc.project();

        const QList<Id> originals{picture.id, overlay.id, sound.id};
        const QList<Id> copies{QStringLiteral("picture-copy"), QStringLiteral("overlay-copy"),
                               QStringLiteral("sound-copy")};
        QVERIFY(f.doc.execute(edits::duplicateClipsAt(originals, copies, 100)));
        const Project after = f.doc.project();
        QCOMPARE(after.timeline().clipCount(), before.timeline().clipCount() + 3);
        const std::vector<Clip> sourceClips{picture, overlay, sound};
        const std::vector<Id> tracks{f.v1, f.v2, f.a1};
        const std::vector<FrameCount> starts{100, 125, 115};
        for (size_t i = 0; i < sourceClips.size(); ++i) {
            Clip expected = sourceClips.at(i);
            expected.id = copies.at(static_cast<qsizetype>(i));
            expected.start = starts.at(i);
            const Clip* actual = after.timeline().clip(expected.id);
            QVERIFY(actual);
            QCOMPARE(*actual, expected);
            QCOMPARE(after.timeline().trackOfClip(expected.id)->id, tracks.at(i));
            QCOMPARE(*after.timeline().clip(sourceClips.at(i).id), sourceClips.at(i));
        }
        QCOMPARE(f.doc.undoText(), QStringLiteral("Duplicate Selected Clips at Playhead"));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project(), after);
    }

    void duplicateSelectionAtPlayheadRejectsCollisionsLocksAndInvalidIdsAtomically()
    {
        Fixture f;
        const Clip first = clipOf(QStringLiteral("m1"), 0, 20, QStringLiteral("first"));
        const Clip second = clipOf(QStringLiteral("m1"), 40, 20, QStringLiteral("second"));
        const Clip blocker = clipOf(QStringLiteral("m1"), 140, 20, QStringLiteral("blocker"));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, first)));
        QVERIFY(f.doc.execute(edits::addClip(f.v2, second)));
        QVERIFY(f.doc.execute(edits::addClip(f.v2, blocker)));
        const Project before = f.doc.project();
        const QString undoBefore = f.doc.undoText();
        const QList<Id> sources{first.id, second.id};
        const QList<Id> copies{QStringLiteral("first-copy"), QStringLiteral("second-copy")};

        QVERIFY(!f.doc.execute(edits::duplicateClipsAt({}, {}, 100)));
        QVERIFY(!f.doc.execute(edits::duplicateClipsAt(sources, {copies.front()}, 100)));
        QVERIFY(!f.doc.execute(edits::duplicateClipsAt({first.id, first.id}, copies, 100)));
        QVERIFY(!f.doc.execute(
            edits::duplicateClipsAt(sources, {copies.front(), copies.front()}, 100)));
        QVERIFY(!f.doc.execute(
            edits::duplicateClipsAt({first.id, QStringLiteral("missing")}, copies, 100)));
        QVERIFY(!f.doc.execute(edits::duplicateClipsAt(sources, {QString(), copies.back()}, 100)));
        QVERIFY(!f.doc.execute(edits::duplicateClipsAt(sources, {first.id, copies.back()}, 100)));
        QVERIFY(!f.doc.execute(edits::duplicateClipsAt(sources, copies, -1)));
        QVERIFY(!f.doc.execute(edits::duplicateClipsAt(sources, copies, kMaxTimelineFrames - 5)));
        // The first inserted copy would fit, but the later V2 copy hits a blocker.
        QVERIFY(!f.doc.execute(edits::duplicateClipsAt(sources, copies, 100)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undoBefore);

        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v2, true)));
        const Project locked = f.doc.project();
        const QString lockedUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::duplicateClipsAt(sources, copies, 250)));
        QCOMPARE(f.doc.project(), locked);
        QCOMPARE(f.doc.undoText(), lockedUndo);
    }

    void duplicatePreservesSourceEditsAndHistory()
    {
        Fixture f;
        Clip source = clipOf(QStringLiteral("m1"), 25, 120, QStringLiteral("original"));
        source.sourceIn = 30;
        source.properties.gain = 0.6;
        source.properties.fadeIn = 12;
        source.properties.video.contrast = 1.3;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, source)));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 300, 40, QStringLiteral("later")))));
        const Timeline before = f.doc.project().timeline();

        QVERIFY(f.doc.execute(edits::duplicateClip(source.id, QStringLiteral("copy"))));
        const Timeline& timeline = f.doc.project().timeline();
        const Clip* copy = timeline.clip(QStringLiteral("copy"));
        QVERIFY(copy);
        QCOMPARE(copy->start, FrameCount{340});
        QCOMPARE(copy->sourceIn, source.sourceIn);
        QCOMPARE(copy->duration, source.duration);
        QCOMPARE(copy->assetId, source.assetId);
        QCOMPARE(copy->properties, source.properties);
        QCOMPARE(timeline.trackOfClip(copy->id)->id, f.v1);
        QCOMPARE(timeline.clipCount(), 3);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project().timeline().clip(QStringLiteral("copy"))->start, FrameCount{340});

        // The new id is independent: changing the copy never alters its source.
        QVERIFY(f.doc.execute(edits::trimClipEnd(QStringLiteral("copy"), 370)));
        QCOMPARE(f.doc.project().timeline().clip(source.id)->duration, source.duration);
    }

    void duplicateRejectsLockedMissingAndConflictingClips()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 30, QStringLiteral("original")))));
        const Timeline before = f.doc.project().timeline();
        QVERIFY(!f.doc.execute(edits::duplicateClip(QStringLiteral("missing"))));
        QCOMPARE(f.doc.project().timeline(), before);
        QVERIFY(!f.doc.execute(
            edits::duplicateClip(QStringLiteral("original"), QStringLiteral("original"))));
        QCOMPARE(f.doc.project().timeline(), before);
        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v1, true)));
        const Timeline locked = f.doc.project().timeline();
        QVERIFY(!f.doc.execute(edits::duplicateClip(QStringLiteral("original"))));
        QCOMPARE(f.doc.project().timeline(), locked);
    }

    void duplicateMultiTrackSelectionKeepsTimingAndUndo()
    {
        Fixture f;
        Clip first = clipOf(QStringLiteral("m1"), 10, 80, QStringLiteral("first"));
        first.sourceIn = 12;
        first.properties.gain = 0.7;
        Clip second = clipOf(QStringLiteral("m1"), 55, 50, QStringLiteral("second"));
        second.properties.video.opacity = 0.4;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, first)));
        QVERIFY(f.doc.execute(edits::addClip(f.v2, second)));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 140, 20, QStringLiteral("late")))));
        const Timeline before = f.doc.project().timeline();
        QCOMPARE(before.duration(), FrameCount{160});
        QVERIFY(f.doc.execute(
            edits::duplicateClips({QStringLiteral("second"), QStringLiteral("first")},
                                  {QStringLiteral("copy-second"), QStringLiteral("copy-first")})));
        const Timeline after = f.doc.project().timeline();
        QCOMPARE(after.clip(QStringLiteral("copy-first"))->start, FrameCount{160});
        QCOMPARE(after.clip(QStringLiteral("copy-second"))->start, FrameCount{205});
        QCOMPARE(after.clip(QStringLiteral("copy-first"))->sourceIn, first.sourceIn);
        QCOMPARE(after.clip(QStringLiteral("copy-first"))->properties, first.properties);
        QCOMPARE(after.clip(QStringLiteral("copy-second"))->properties, second.properties);
        QCOMPARE(after.trackOfClip(QStringLiteral("copy-first"))->id, f.v1);
        QCOMPARE(after.trackOfClip(QStringLiteral("copy-second"))->id, f.v2);
        QCOMPARE(f.doc.undoText(), QStringLiteral("Duplicate 2 Clips"));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project().timeline(), after);
    }

    void duplicateBatchFailsAtomicallyOnLockedTrackAndDuplicateIds()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("one")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v2, clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("two")))));
        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v2, true)));
        const Timeline before = f.doc.project().timeline();
        const QString priorUndo = f.doc.undoText();
        QVERIFY(!f.doc.execute(
            edits::duplicateClips({QStringLiteral("one"), QStringLiteral("two")},
                                  {QStringLiteral("copy-one"), QStringLiteral("copy-two")})));
        QCOMPARE(f.doc.project().timeline(), before);
        QCOMPARE(f.doc.undoText(), priorUndo);
        QVERIFY(!f.doc.execute(
            edits::duplicateClips({QStringLiteral("one"), QStringLiteral("two")},
                                  {QStringLiteral("shared"), QStringLiteral("shared")})));
        QCOMPARE(f.doc.project().timeline(), before);
        QVERIFY(
            !f.doc.execute(edits::duplicateClips({QStringLiteral("one"), QStringLiteral("missing")},
                                                 {QStringLiteral("a"), QStringLiteral("b")})));
        QCOMPARE(f.doc.project().timeline(), before);
    }

    void batchDuplicationRejectsMalformedSelectionsWithoutEdits()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("one")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v2, clipOf(QStringLiteral("m1"), 60, 50, QStringLiteral("two")))));
        const Project before = f.doc.project();
        const QString undoText = f.doc.undoText();

        // Invalid requests must neither append a partial batch nor enter undo history.
        QVERIFY(!f.doc.execute(edits::duplicateClips({}, {})));
        QVERIFY(!f.doc.execute(edits::duplicateClips({QStringLiteral("one")}, {})));
        QVERIFY(!f.doc.execute(
            edits::duplicateClips({QStringLiteral("one"), QStringLiteral("one")},
                                  {QStringLiteral("first-copy"), QStringLiteral("second-copy")})));
        QVERIFY(!f.doc.execute(
            edits::duplicateClips({QStringLiteral("one"), QStringLiteral("two")},
                                  {QStringLiteral("one"), QStringLiteral("two-copy")})));
        QVERIFY(!f.doc.execute(edits::duplicateClips({QStringLiteral("one"), QStringLiteral("two")},
                                                     {QStringLiteral("first-copy"), QString()})));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undoText);
    }

    void nudgingAdjacentClipsAcrossTracksPreservesSyncAndUndo()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 20, 40, QStringLiteral("left")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 60, 30, QStringLiteral("right")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v2, clipOf(QStringLiteral("m1"), 20, 50, QStringLiteral("overlay")))));
        const Timeline before = f.doc.project().timeline();
        QVERIFY(f.doc.execute(edits::nudgeClips(
            {QStringLiteral("right"), QStringLiteral("overlay"), QStringLiteral("left")}, 5)));
        const Timeline moved = f.doc.project().timeline();
        QCOMPARE(moved.clip(QStringLiteral("left"))->start, FrameCount{25});
        QCOMPARE(moved.clip(QStringLiteral("right"))->start, FrameCount{65});
        QCOMPARE(moved.clip(QStringLiteral("overlay"))->start, FrameCount{25});
        QCOMPARE(f.doc.undoText(), QStringLiteral("Nudge Clips Right"));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project().timeline(), moved);
        QVERIFY(f.doc.execute(edits::nudgeClips(
            {QStringLiteral("left"), QStringLiteral("right"), QStringLiteral("overlay")}, -5)));
        QCOMPARE(f.doc.project().timeline(), before);
    }

    void nudgeRejectsBoundaryAndCollisionsWithoutChangingHistory()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 40, QStringLiteral("first")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 40, 40, QStringLiteral("second")))));
        const Project before = f.doc.project();
        const QString undoText = f.doc.undoText();
        QVERIFY(!f.doc.execute(edits::nudgeClips({QStringLiteral("first")}, -1)));
        QVERIFY(!f.doc.execute(edits::nudgeClips({QStringLiteral("first")}, 1)));
        QVERIFY(!f.doc.execute(edits::nudgeClips({}, 1)));
        QVERIFY(!f.doc.execute(edits::nudgeClips({QStringLiteral("first")}, 0)));
        QVERIFY(!f.doc.execute(
            edits::nudgeClips({QStringLiteral("first"), QStringLiteral("first")}, 1)));
        QVERIFY(!f.doc.execute(edits::nudgeClips({QStringLiteral("missing")}, 1)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undoText);
    }

    void nudgeCanCrossAnUnselectedClipWhenDestinationIsFree()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 10, QStringLiteral("moving")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 20, 10, QStringLiteral("fixed")))));
        const Timeline before = f.doc.project().timeline();
        QVERIFY(f.doc.execute(edits::nudgeClips({QStringLiteral("moving")}, 40)));
        QCOMPARE(f.doc.project().timeline().clip(QStringLiteral("moving"))->start, FrameCount{40});
        const Track* track = f.doc.project().timeline().track(f.v1);
        QVERIFY(track);
        QCOMPARE(track->clips.size(), size_t{2});
        QCOMPARE(track->clips.front().id, QStringLiteral("fixed"));
        QCOMPARE(track->clips.back().id, QStringLiteral("moving"));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project().timeline().track(f.v1)->clips.front().id, QStringLiteral("fixed"));
    }

    void nudgingAcrossAnotherTransitionKeepsCutOrder()
    {
        Fixture f;
        Clip a = clipOf(QStringLiteral("m1"), 10, 60, QStringLiteral("a"));
        Clip b = clipOf(QStringLiteral("m1"), 70, 60, QStringLiteral("b"));
        Clip c = clipOf(QStringLiteral("m1"), 150, 60, QStringLiteral("c"));
        Clip d = clipOf(QStringLiteral("m1"), 210, 60, QStringLiteral("d"));
        b.sourceIn = 120;
        d.sourceIn = 120;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, a)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, b)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, c)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, d)));
        QVERIFY(f.doc.execute(edits::addTransition(a.id, 10, QStringLiteral("t1"))));
        QVERIFY(f.doc.execute(edits::addTransition(c.id, 10, QStringLiteral("t2"))));
        const Timeline before = f.doc.project().timeline();
        const Track* track = before.track(f.v1);
        QVERIFY(track);
        QCOMPARE(track->transitions.front().id, QStringLiteral("t1"));

        QVERIFY(f.doc.execute(edits::nudgeClips({a.id, b.id}, 300)));
        track = f.doc.project().timeline().track(f.v1);
        QVERIFY(track);
        QCOMPARE(track->transitions.size(), size_t{2});
        QCOMPARE(track->transitions.front().id, QStringLiteral("t2"));
        QCOMPARE(track->transitions.back().id, QStringLiteral("t1"));
        QVERIFY(f.doc.project().timeline().validate(f.doc.project().sourceLookup()));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline(), before);
    }

    void nudgingLockedSelectionIsAtomic()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 10, 40, QStringLiteral("one")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v2, clipOf(QStringLiteral("m1"), 10, 40, QStringLiteral("two")))));
        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v2, true)));
        const Project before = f.doc.project();
        const QString undoText = f.doc.undoText();
        QVERIFY(
            !f.doc.execute(edits::nudgeClips({QStringLiteral("one"), QStringLiteral("two")}, 10)));
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undoText);
    }

    void nudgingBothEndsOfTransitionKeepsItValid()
    {
        Fixture f;
        Clip first = clipOf(QStringLiteral("m1"), 10, 60, QStringLiteral("first"));
        Clip second = clipOf(QStringLiteral("m1"), 70, 70, QStringLiteral("second"));
        second.sourceIn = 120;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, first)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, second)));
        QVERIFY(f.doc.execute(edits::addTransition(first.id, 10)));
        const Timeline before = f.doc.project().timeline();
        QVERIFY(f.doc.execute(edits::nudgeClips({first.id, second.id}, 1)));
        QCOMPARE(f.doc.project().timeline().clip(first.id)->start, FrameCount{11});
        QCOMPARE(f.doc.project().timeline().clip(second.id)->start, FrameCount{71});
        QVERIFY(f.doc.project().timeline().transitionAfter(first.id));
        const Timeline moved = f.doc.project().timeline();
        QVERIFY(!f.doc.execute(edits::nudgeClips({first.id}, 1)));
        QCOMPARE(f.doc.project().timeline(), moved);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline(), before);
    }

    void pasteAttributesToClipsClampsFadesAndUndoesTogether()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 100, QStringLiteral("long")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 150, 35, QStringLiteral("short")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v2, clipOf(QStringLiteral("m1"), 0, 60, QStringLiteral("other")))));
        const Timeline before = f.doc.project().timeline();
        ClipProperties copied;
        copied.gain = 0.3;
        copied.fadeIn = 30;
        copied.fadeOut = 20;
        copied.video.opacity = 0.6;
        QVERIFY(f.doc.execute(edits::pasteClipProperties(
            {QStringLiteral("short"), QStringLiteral("other")}, copied)));
        const Clip* shorter = f.doc.project().timeline().clip(QStringLiteral("short"));
        QVERIFY(shorter);
        QCOMPARE(shorter->properties.fadeIn, FrameCount{30});
        QCOMPARE(shorter->properties.fadeOut, FrameCount{5});
        QCOMPARE(shorter->properties.gain, 0.3);
        QCOMPARE(shorter->properties.video.opacity, 0.6);
        const Clip* other = f.doc.project().timeline().clip(QStringLiteral("other"));
        QCOMPARE(other->properties.fadeOut, FrameCount{20});
        QCOMPARE(other->properties.fadeIn, FrameCount{30});
        QCOMPARE(f.doc.undoText(), QStringLiteral("Paste Clip Attributes"));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline(), before);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project().timeline().clip(QStringLiteral("short"))->properties.fadeOut,
                 FrameCount{5});
    }

    void pasteAttributesRejectsLockedDestinationsWithoutPartialEdits()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 100, QStringLiteral("one")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v2, clipOf(QStringLiteral("m1"), 0, 100, QStringLiteral("two")))));
        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v2, true)));
        const Timeline before = f.doc.project().timeline();
        ClipProperties copied;
        copied.video.saturation = 2.0;
        QVERIFY(!f.doc.execute(
            edits::pasteClipProperties({QStringLiteral("one"), QStringLiteral("two")}, copied)));
        QCOMPARE(f.doc.project().timeline(), before);
    }

    void batchMuteAndPictureResetPreserveUnrelatedAttributes()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 100, QStringLiteral("one")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v2, clipOf(QStringLiteral("m1"), 0, 100, QStringLiteral("two")))));
        ClipProperties settings;
        settings.gain = 0.35;
        settings.fadeIn = 8;
        settings.video.contrast = 1.4;
        settings.video.saturation = 1.7;
        QVERIFY(f.doc.execute(
            edits::pasteClipProperties({QStringLiteral("one"), QStringLiteral("two")}, settings)));
        const Timeline original = f.doc.project().timeline();
        QVERIFY(
            f.doc.execute(edits::muteClips({QStringLiteral("one"), QStringLiteral("two")}, true)));
        for (const Id& id : {QStringLiteral("one"), QStringLiteral("two")}) {
            const ClipProperties& properties = f.doc.project().timeline().clip(id)->properties;
            QVERIFY(properties.muted);
            QCOMPARE(properties.gain, 0.35);
            QCOMPARE(properties.fadeIn, FrameCount{8});
            QCOMPARE(properties.video.contrast, 1.4);
        }
        QVERIFY(f.doc.execute(
            edits::resetClipPictures({QStringLiteral("one"), QStringLiteral("two")})));
        for (const Id& id : {QStringLiteral("one"), QStringLiteral("two")}) {
            const ClipProperties& properties = f.doc.project().timeline().clip(id)->properties;
            QVERIFY(properties.muted);
            QCOMPARE(properties.video, VideoAdjustments{});
            QCOMPARE(properties.gain, 0.35);
        }
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline().clip(QStringLiteral("one"))->properties.video.contrast,
                 1.4);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline(), original);
        QVERIFY(f.doc.redo());
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project().timeline().clip(QStringLiteral("two"))->properties.video,
                 VideoAdjustments{});
    }

    void batchChangesRejectLockedClipsWithoutPartialChanges()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 90, QStringLiteral("one")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v2, clipOf(QStringLiteral("m1"), 0, 90, QStringLiteral("two")))));
        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v2, true)));
        const Timeline before = f.doc.project().timeline();
        const QList<Id> ids{QStringLiteral("one"), QStringLiteral("two")};
        QVERIFY(!f.doc.execute(edits::muteClips(ids, true)));
        QCOMPARE(f.doc.project().timeline(), before);
        QVERIFY(!f.doc.execute(edits::resetClipPictures(ids)));
        QCOMPARE(f.doc.project().timeline(), before);
    }

    void failedEditChangesNothingAndIsNotRecorded()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 100, QStringLiteral("c1")))));
        const Project before = f.doc.project();
        const QString undoText = f.doc.undoText();
        const Status overlap =
            f.doc.execute(edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 50, 100)));
        QVERIFY(!overlap);
        QCOMPARE(f.doc.project(), before);
        QCOMPARE(f.doc.undoText(), undoText);
        // A multi-clip delete that fails half way must not leave partial changes.
        const Status partial =
            f.doc.execute(edits::removeClips({QStringLiteral("c1"), QStringLiteral("missing")}));
        QVERIFY(!partial);
        QCOMPARE(f.doc.project(), before);
    }

    void noOpEditsAreNotRecorded()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 100, QStringLiteral("c1")))));
        const QString undoText = f.doc.undoText();
        QVERIFY(f.doc.execute(edits::moveClip(QStringLiteral("c1"), f.v1, 0)));
        QCOMPARE(f.doc.undoText(), undoText);
    }

    void propertyEditsMerge()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 100, QStringLiteral("c1")))));
        f.doc.breakMerge();
        ClipProperties p;
        for (double gain : {0.9, 0.8, 0.7, 0.6}) {
            p.gain = gain;
            QVERIFY(f.doc.execute(edits::setClipProperties(QStringLiteral("c1"), p,
                                                           QStringLiteral("Change Volume"))));
        }
        QCOMPARE(f.doc.project().timeline().clip(QStringLiteral("c1"))->properties.gain, 0.6);
        QVERIFY(f.doc.undo()); // one undo reverts the whole drag
        QCOMPARE(f.doc.project().timeline().clip(QStringLiteral("c1"))->properties.gain, 1.0);
        QCOMPARE(f.doc.undoText(), QStringLiteral("Add Clip"));
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project().timeline().clip(QStringLiteral("c1"))->properties.gain, 0.6);

        // breakMerge starts a new step.
        f.doc.breakMerge();
        p.gain = 0.3;
        QVERIFY(f.doc.execute(
            edits::setClipProperties(QStringLiteral("c1"), p, QStringLiteral("Change Volume"))));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline().clip(QStringLiteral("c1"))->properties.gain, 0.6);
    }

    void transitionCommandsUndoAndMerge()
    {
        Fixture f;
        Clip left = clipOf(QStringLiteral("m1"), 0, 100, QStringLiteral("c1"));
        Clip right = clipOf(QStringLiteral("m1"), 100, 100, QStringLiteral("c2"));
        right.sourceIn = 200; // leaves media before the cut for the transition
        QVERIFY(f.doc.execute(edits::addClip(f.v1, left)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, right)));
        const Timeline before = f.doc.project().timeline();

        const Id id = createId();
        QVERIFY(f.doc.execute(edits::addTransition(QStringLiteral("c1"), 30, id)));
        QCOMPARE(f.doc.undoText(), QStringLiteral("Add Transition"));
        QCOMPARE(f.doc.project().timeline().transition(id)->duration, FrameCount{30});

        // Dragging the length is one undo step.
        f.doc.breakMerge();
        for (FrameCount length : {32, 40, 48}) {
            QVERIFY(f.doc.execute(edits::setTransitionDuration(id, length)));
        }
        QCOMPARE(f.doc.project().timeline().transition(id)->duration, FrameCount{48});
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline().transition(id)->duration, FrameCount{30});
        QVERIFY(f.doc.redo());

        // Invalid lengths are refused and not recorded.
        QVERIFY(!f.doc.execute(edits::setTransitionDuration(id, 1)));
        QCOMPARE(f.doc.project().timeline().transition(id)->duration, FrameCount{48});

        QVERIFY(f.doc.execute(edits::removeTransition(id)));
        QVERIFY(!f.doc.project().timeline().transition(id));
        QVERIFY(f.doc.undo());
        QVERIFY(f.doc.project().timeline().transition(id));

        // Moving a clip away drops the transition, and undo brings both back.
        QVERIFY(f.doc.execute(edits::moveClip(QStringLiteral("c2"), f.v1, 150)));
        QVERIFY(!f.doc.project().timeline().transition(id));
        QVERIFY(f.doc.undo());
        QVERIFY(f.doc.project().timeline().transition(id));

        while (f.doc.undoText() != QStringLiteral("Add Clip")) {
            QVERIFY(f.doc.undo());
        }
        QCOMPARE(f.doc.project().timeline(), before);
    }

    void modifiedStateFollowsSaveAndUndo()
    {
        QTemporaryDir dir;
        Fixture f;
        QSignalSpy modified(&f.doc, &ProjectDocument::modifiedChanged);
        QVERIFY(f.doc.isModified()); // media import happened
        QVERIFY(!f.doc.save());      // no file name yet
        QVERIFY(f.doc.saveAs(dir.filePath(QStringLiteral("p.ftproj"))));
        QVERIFY(!f.doc.isModified());
        QCOMPARE(f.doc.displayName(), QStringLiteral("p"));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 10))));
        QVERIFY(f.doc.isModified());
        QVERIFY(f.doc.undo());
        QVERIFY(!f.doc.isModified()); // back at the saved state
        QVERIFY(f.doc.undo());
        QVERIFY(f.doc.isModified());
        QVERIFY(modified.count() >= 3);
        // Making a new edit after undoing past the save point makes the saved state unreachable.
        QVERIFY(f.doc.execute(edits::addTrack(TrackKind::Video, QStringLiteral("V3"))));
        QVERIFY(f.doc.isModified());
        QVERIFY(f.doc.undo());
        QVERIFY(f.doc.isModified());
    }

    void changedSignalDescribesTouchedTracks()
    {
        Fixture f;
        QSignalSpy changed(&f.doc, &ProjectDocument::changed);
        QVERIFY(f.doc.execute(edits::addClip(f.v2, clipOf(QStringLiteral("m1"), 0, 10))));
        QCOMPARE(changed.count(), 1);
        const auto changes = changed.takeFirst().at(0).value<ChangeSet>();
        QVERIFY(!changes.trackStructure);
        QCOMPARE(changes.tracks, QSet<Id>{f.v2});
        QVERIFY(f.doc.execute(edits::addTrack(TrackKind::Audio, QStringLiteral("A3"))));
        QVERIFY(changed.takeFirst().at(0).value<ChangeSet>().trackStructure);
    }

    void bulkTrackLockingProtectsClipsAndPreservesFlagsOnUndo()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 80, QStringLiteral("clip")))));
        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v2, true)));
        QVERIFY(f.doc.execute(edits::setTrackMuted(f.a1, true)));
        QVERIFY(f.doc.execute(edits::setTrackHidden(f.v1, true)));
        const Timeline original = f.doc.project().timeline();

        QVERIFY(f.doc.execute(edits::setAllTracksLocked(true)));
        for (const Track& track : f.doc.project().timeline().tracks()) {
            QVERIFY(track.locked);
        }
        QVERIFY(!f.doc.execute(edits::removeClips({QStringLiteral("clip")})));
        const Timeline allLocked = f.doc.project().timeline();
        QCOMPARE(f.doc.project().timeline().track(f.a1)->muted, true);
        QCOMPARE(f.doc.project().timeline().track(f.v1)->hidden, true);

        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline(), original);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project().timeline(), allLocked);
        QVERIFY(f.doc.execute(edits::setAllTracksLocked(false)));
        for (const Track& track : f.doc.project().timeline().tracks()) {
            QVERIFY(!track.locked);
        }
        QVERIFY(f.doc.project().timeline().track(f.a1)->muted);
        QVERIFY(f.doc.project().timeline().track(f.v1)->hidden);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline(), allLocked);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline(), original);
    }

    void trackCommands()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addTrack(TrackKind::Video, QStringLiteral("V3"), 2, QStringLiteral("v3"))));
        QCOMPARE(f.doc.project().timeline().trackIndex(QStringLiteral("v3")), 2);
        QVERIFY(f.doc.execute(edits::renameTrack(QStringLiteral("v3"), QStringLiteral("Titles"))));
        QVERIFY(f.doc.execute(edits::setTrackLocked(QStringLiteral("v3"), true)));
        QVERIFY(!f.doc.execute(edits::removeTrack(QStringLiteral("v3")))); // locked
        QVERIFY(f.doc.execute(edits::setTrackLocked(QStringLiteral("v3"), false)));
        QVERIFY(f.doc.execute(edits::moveTrack(QStringLiteral("v3"), 0)));
        QVERIFY(f.doc.execute(edits::removeTrack(QStringLiteral("v3"))));
        QCOMPARE(f.doc.project().timeline().trackIndex(QStringLiteral("v3")), -1);
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline().trackIndex(QStringLiteral("v3")), 0);
        QCOMPARE(f.doc.project().timeline().track(QStringLiteral("v3"))->name,
                 QStringLiteral("Titles"));
        QVERIFY(f.doc.execute(edits::setTrackMuted(f.a1, true)));
        QVERIFY(f.doc.execute(edits::setTrackHidden(f.v1, true)));
        QVERIFY(f.doc.project().timeline().track(f.a1)->muted);
        QVERIFY(f.doc.project().timeline().track(f.v1)->hidden);
    }

    void duplicatingTrackPreservesClipsTransitionsAndUndo()
    {
        Fixture f;
        Clip first = clipOf(QStringLiteral("m1"), 0, 60, QStringLiteral("left"));
        first.sourceIn = 40;
        first.properties.gain = 0.65;
        Clip second = clipOf(QStringLiteral("m1"), 60, 60, QStringLiteral("right"));
        second.sourceIn = 80;
        QVERIFY(f.doc.execute(edits::addClip(f.v1, first)));
        QVERIFY(f.doc.execute(edits::addClip(f.v1, second)));
        QVERIFY(f.doc.execute(edits::addTransition(first.id, 10, QStringLiteral("dissolve"))));
        QVERIFY(f.doc.execute(edits::setTrackMuted(f.v1, true)));
        QVERIFY(f.doc.execute(edits::setTrackLocked(f.v1, true)));
        const Timeline original = f.doc.project().timeline();
        const Id copyId = QStringLiteral("duplicate-track");
        QVERIFY(f.doc.execute(edits::duplicateTrack(f.v1, QStringLiteral("V3"), copyId)));
        const Timeline& timeline = f.doc.project().timeline();
        QCOMPARE(timeline.trackIndex(copyId), timeline.trackIndex(f.v1) + 1);
        const Track* source = timeline.track(f.v1);
        const Track* copy = timeline.track(copyId);
        QVERIFY(source);
        QVERIFY(copy);
        QCOMPARE(copy->name, QStringLiteral("V3"));
        QCOMPARE(copy->locked, source->locked);
        QCOMPARE(copy->muted, source->muted);
        QCOMPARE(copy->clips.size(), size_t{2});
        QCOMPARE(copy->transitions.size(), size_t{1});
        for (size_t i = 0; i < copy->clips.size(); ++i) {
            QVERIFY(copy->clips[i].id != source->clips[i].id);
            QCOMPARE(copy->clips[i].assetId, source->clips[i].assetId);
            QCOMPARE(copy->clips[i].start, source->clips[i].start);
            QCOMPARE(copy->clips[i].properties, source->clips[i].properties);
        }
        QVERIFY(copy->transitions.front().id != source->transitions.front().id);
        QCOMPARE(copy->transitions.front().fromClip, copy->clips.front().id);
        QCOMPARE(copy->transitions.front().toClip, copy->clips.back().id);
        QVERIFY(timeline.validate(f.doc.project().sourceLookup()));
        const Timeline copied = timeline;
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().timeline(), original);
        QVERIFY(f.doc.redo());
        QCOMPARE(f.doc.project().timeline(), copied);
    }

    void duplicatingMissingTrackDoesNotCreateUndoStep()
    {
        Fixture f;
        const Timeline before = f.doc.project().timeline();
        const QString undoBefore = f.doc.undoText();
        QVERIFY(
            !f.doc.execute(edits::duplicateTrack(QStringLiteral("absent"), QStringLiteral("V3"))));
        QCOMPARE(f.doc.project().timeline(), before);
        QCOMPARE(f.doc.undoText(), undoBefore);
    }

    void splitRequiresPlayheadInsideClip()
    {
        Fixture f;
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 100, QStringLiteral("c1")))));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v2, clipOf(QStringLiteral("m1"), 50, 100, QStringLiteral("c2")))));
        QVERIFY(!f.doc.execute(edits::splitClips({QStringLiteral("c1")}, 100)));
        // Splitting several selected clips at once only touches those spanning the frame.
        QVERIFY(f.doc.execute(edits::splitClips({QStringLiteral("c1"), QStringLiteral("c2")}, 75)));
        QCOMPARE(f.doc.project().timeline().clipCount(), 4);
        // Both selected clips now end at frame 75, so neither spans frame 120.
        QVERIFY(
            !f.doc.execute(edits::splitClips({QStringLiteral("c1"), QStringLiteral("c2")}, 120)));
        QCOMPARE(f.doc.project().timeline().clipCount(), 4);
    }

    void mediaCommands()
    {
        Fixture f;
        QVERIFY(!f.doc.execute(std::make_unique<AddMediaCommand>(
            std::vector<MediaAsset>{videoAsset(QStringLiteral("m1"))})));
        QVERIFY(f.doc.execute(
            edits::addClip(f.v1, clipOf(QStringLiteral("m1"), 0, 300, QStringLiteral("c1")))));
        QVERIFY(
            !f.doc.execute(std::make_unique<RemoveMediaCommand>(QStringLiteral("m1")))); // in use
        // Relinking to a shorter file than the clips need is refused.
        MediaAsset shorter = videoAsset(QStringLiteral("m1"), QStringLiteral("/media/b.mp4"));
        shorter.info.durationUs = 5'000'000; // 150 frames
        const Status relinkShort =
            f.doc.execute(std::make_unique<UpdateMediaCommand>(shorter, QStringLiteral("Relink")));
        QVERIFY(!relinkShort);
        QCOMPARE(f.doc.project().asset(QStringLiteral("m1"))->filePath,
                 QStringLiteral("/media/a.mp4"));
        MediaAsset moved = videoAsset(QStringLiteral("m1"), QStringLiteral("/media/moved/a.mp4"));
        QVERIFY(
            f.doc.execute(std::make_unique<UpdateMediaCommand>(moved, QStringLiteral("Relink"))));
        QCOMPARE(f.doc.project().asset(QStringLiteral("m1"))->filePath,
                 QStringLiteral("/media/moved/a.mp4"));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().asset(QStringLiteral("m1"))->filePath,
                 QStringLiteral("/media/a.mp4"));
        QVERIFY(f.doc.execute(edits::removeClips({QStringLiteral("c1")})));
        QVERIFY(f.doc.execute(std::make_unique<RemoveMediaCommand>(QStringLiteral("m1"))));
        QVERIFY(f.doc.project().media().empty());
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().media().size(), size_t{1});
    }

    void settingsCommand()
    {
        Fixture f;
        ProjectSettings s = f.doc.project().settings();
        s.width = 1280;
        s.height = 720;
        QVERIFY(
            f.doc.execute(std::make_unique<ProjectSettingsCommand>(s, QStringLiteral("Renamed"))));
        QCOMPARE(f.doc.project().settings().width, 1280);
        QCOMPARE(f.doc.project().name(), QStringLiteral("Renamed"));
        QVERIFY(f.doc.undo());
        QCOMPARE(f.doc.project().settings().width, 1920);
        QCOMPARE(f.doc.project().name(), QStringLiteral("Test"));
        s.width = 7;
        QVERIFY(!f.doc.execute(std::make_unique<ProjectSettingsCommand>(s, QString())));
    }

    void historyLimitDropsOldestSteps()
    {
        Project project = Project::createNew(QStringLiteral("Limit"), ProjectSettings{});
        CommandHistory history(3);
        for (int i = 0; i < 5; ++i) {
            QVERIFY(history.execute(edits::addTrack(TrackKind::Audio, QStringLiteral("X%1").arg(i)),
                                    project));
        }
        QCOMPARE(history.count(), 3);
        QCOMPARE(project.timeline().trackCount(), 9);
        QVERIFY(!history.isClean()); // the initial state was dropped
        while (history.canUndo()) {
            QVERIFY(history.undo(project));
        }
        QCOMPARE(project.timeline().trackCount(), 6);
    }

    void mediaRescanNoticesChangedTimestampWithSameSize()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("source.mp4"));
        {
            QFile source(path);
            QVERIFY(source.open(QIODevice::WriteOnly));
            QCOMPARE(source.write("AAAA", 4), qint64{4});
        }

        const QFileInfo before(path);
        MediaAsset asset = videoAsset(QStringLiteral("observed"), path);
        asset.info.fileSize = before.size();
        asset.info.lastModified = before.lastModified();
        Project project = Project::createNew(QStringLiteral("Changed source"), ProjectSettings{});
        QVERIFY(project.addAsset(asset));
        ProjectDocument doc(std::move(project));

        QCOMPARE(doc.refreshMediaStatus().size(), 0);
        QCOMPARE(doc.project().asset(asset.id)->status, MediaStatus::Online);

        QFile edited(path);
        QVERIFY(edited.open(QIODevice::ReadWrite));
        QVERIFY(edited.setFileTime(before.lastModified().addSecs(120),
                                   QFileDevice::FileModificationTime));
        edited.close();
        QCOMPARE(QFileInfo(path).size(), before.size()); // no file size change

        QCOMPARE(doc.refreshMediaStatus().size(), 0); // not missing, just changed
        QCOMPARE(doc.project().asset(asset.id)->status, MediaStatus::Changed);
        QVERIFY(!doc.isModified()); // runtime status must not dirty a project
    }

    void loadedDocumentReportsMissingMedia()
    {
        QTemporaryDir dir;
        const QString existing = dir.filePath(QStringLiteral("present.mp4"));
        QFile file(existing);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("data");
        file.close();
        Project project = Project::createNew(QStringLiteral("Missing"), ProjectSettings{});
        (void)project.addAsset(videoAsset(QStringLiteral("present"), existing));
        (void)project.addAsset(
            videoAsset(QStringLiteral("gone"), dir.filePath(QStringLiteral("gone.mp4"))));
        const QString path = dir.filePath(QStringLiteral("m.ftproj"));
        QVERIFY(project_format::saveToFile(project, path));
        Result<std::unique_ptr<ProjectDocument>> loaded = ProjectDocument::load(path);
        QVERIFY(loaded);
        const ProjectDocument& doc = **loaded;
        QCOMPARE(doc.project().asset(QStringLiteral("gone"))->status, MediaStatus::Missing);
        QVERIFY(doc.project().asset(QStringLiteral("present"))->status != MediaStatus::Missing);
        QVERIFY(!doc.isModified());
        QCOMPARE(doc.filePath(), QFileInfo(path).absoluteFilePath());
    }
};

QTEST_GUILESS_MAIN(EditingTest)
#include "tst_editing.moc"
