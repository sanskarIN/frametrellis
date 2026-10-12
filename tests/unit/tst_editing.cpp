// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/project_document.h"
#include "editing/timeline_edits.h"
#include "project/project_serializer.h"

#include <QFile>
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
