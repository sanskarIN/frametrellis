// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/project_document.h"
#include "editing/timeline_edits.h"
#include "project/project_serializer.h"

#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

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
