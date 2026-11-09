// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "engine/mlt_runtime.h"
#include "media/media_probe.h"
#include "media_fixtures.h"
#include "project/project_serializer.h"
#include "ui/playback_controller.h"
#include "ui/project_controller.h"
#include "ui/subtitle_controller.h"

#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

#include <limits>

using namespace ft;

class SubtitleControllerTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;
    Project m_base;
    std::unique_ptr<ui::ProjectController> m_project;
    std::unique_ptr<ui::PlaybackController> m_playback;
    std::unique_ptr<ui::SubtitleController> m_subtitles;

    bool write(const QString& path, const QByteArray& data)
    {
        QFile file(path);
        return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
    }
    QByteArray read(const QString& path)
    {
        QFile file(path);
        return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
    }
    bool open(Project project, const QString& name = QStringLiteral("captions.ftproj"))
    {
        const QString path = m_dir.filePath(name);
        return project_format::saveToFile(project, path).ok() && m_project->openProject(path);
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("FrameTrellisTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_subtitle_controller"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
        QVERIFY(engine::MltRuntime::initialize());
        ProjectSettings settings;
        settings.width = 320;
        settings.height = 180;
        settings.frameRate = *FrameRate::create(25, 1);
        m_base = Project::createNew(QStringLiteral("Subtitles"), settings);
        const auto asset = media::createAsset(testing::makeStill(
            m_dir.path(), QStringLiteral("picture.png"), QColor(Qt::darkBlue), QSize(320, 180)));
        QVERIFY(asset);
        QVERIFY(m_base.addAsset(*asset));
        Clip clip;
        clip.id = createId();
        clip.assetId = asset->id;
        clip.duration = 200;
        QVERIFY(m_base.timeline().insertClip(m_base.timeline().tracks()[0].id, clip,
                                             m_base.sourceLookup()));
    }
    void init()
    {
        m_project = std::make_unique<ui::ProjectController>();
        m_playback = std::make_unique<ui::PlaybackController>(m_project.get());
        m_subtitles = std::make_unique<ui::SubtitleController>(m_project.get(), m_playback.get());
        QVERIFY(open(m_base));
    }
    void cleanup()
    {
        m_subtitles.reset();
        m_playback.reset();
        m_project->removeOwnAutosave();
        m_project.reset();
    }
    void editsPreserveTheTimelineAndUndoExactTimes()
    {
        QVERIFY(m_subtitles->canAdd());
        QVERIFY(m_subtitles->addAtPlayhead());
        QCOMPARE(m_subtitles->rowCount(), 1);
        QVERIFY(!m_subtitles->canAdd());
        QVERIFY(m_subtitles->updateSelected(QStringLiteral("00:00:00,123"),
                                            QStringLiteral("00:00:01,987"),
                                            QStringLiteral("Hello\r\n<b>literal</b>")));
        const SubtitleTrack edited = m_project->project().subtitles();
        QCOMPARE(edited.cues.front().startMs, 123);
        QCOMPARE(edited.cues.front().endMs, 1987);
        QCOMPARE(edited.cues.front().text, QStringLiteral("Hello\n<b>literal</b>"));
        QVERIFY(!m_subtitles->updateSelected(QStringLiteral("00:00:03,000"),
                                             QStringLiteral("00:00:02,000"),
                                             QStringLiteral("Invalid")));
        QCOMPARE(m_project->project().subtitles(), edited);
        QVERIFY(!m_subtitles->error().isEmpty());
        m_project->undo();
        QCOMPARE(m_project->project().subtitles().cues.front().startMs, 0);
        m_project->redo();
        QCOMPARE(m_project->project().subtitles(), edited);
        QVERIFY(m_subtitles->seekSelected());
        QCOMPARE(m_playback->positionFrames(), 4); // ceil(123 ms at 25 fps)
        QCOMPARE(m_project->project().timeline(), m_base.timeline());
        QVERIFY(m_subtitles->setAppearance(false, 7, false));
        QCOMPARE(m_subtitles->selectedCue().value(QStringLiteral("text")).toString(),
                 edited.cues.front().text);
        QVERIFY(!m_subtitles->burnIn());
        m_project->undo();
        QCOMPARE(m_project->project().subtitles(), edited);
        QVERIFY(m_subtitles->removeSelected());
        QCOMPARE(m_subtitles->selectedIndex(), -1);
        m_project->undo();
        QCOMPARE(m_project->project().subtitles(), edited);
    }
    void splitCueAtPlayheadPreservesBothHalvesAndRejectsBadPositions()
    {
        QVERIFY(m_subtitles->addAtPlayhead());
        const Project before = m_project->project();
        QCOMPARE(before.subtitles().cues.size(), size_t{1});
        const QString firstId = before.subtitles().cues.front().id;
        QVERIFY(!m_subtitles->canSplit());
        QVERIFY(!m_subtitles->splitSelectedAtPlayhead());
        QCOMPARE(m_project->project(), before);

        m_playback->seek(10); // 25 fps: 400 ms, strictly inside the default cue.
        QVERIFY(m_subtitles->canSplit());
        QVERIFY(m_subtitles->splitSelectedAtPlayhead());
        const Project after = m_project->project();
        QCOMPARE(after.subtitles().cues.size(), size_t{2});
        QCOMPARE(after.subtitles().cues[0].id, firstId);
        QCOMPARE(after.subtitles().cues[0].endMs, std::int64_t{400});
        QCOMPARE(after.subtitles().cues[1].startMs, std::int64_t{400});
        QCOMPARE(after.subtitles().cues[1].endMs, std::int64_t{2000});
        QCOMPARE(after.subtitles().cues[0].text, after.subtitles().cues[1].text);
        QCOMPARE(m_subtitles->selectedCue().value(QStringLiteral("id")).toString(),
                 after.subtitles().cues[1].id);
        QCOMPARE(m_project->undoText(), QStringLiteral("Split Subtitle"));
        QVERIFY(!m_subtitles->canSplit()); // playhead is at the new second cue's first frame
        QCOMPARE(after.timeline(), before.timeline());
        m_project->undo();
        QCOMPARE(m_project->project(), before);
        m_project->redo();
        QCOMPARE(m_project->project(), after);
        // Undo removes the second cue, so selection legitimately falls back to
        // the first ID. Reselect the restored second cue before editing it.
        QVERIFY(m_subtitles->selectCue(after.subtitles().cues[1].id, false));

        QVERIFY(m_subtitles->updateSelected(QStringLiteral("00:00:00,400"),
                                            QStringLiteral("00:00:02,000"),
                                            QStringLiteral("Edited second half")));
        QCOMPARE(m_project->project().subtitles().cues[0].text, before.subtitles().cues[0].text);
        QCOMPARE(m_project->project().subtitles().cues[1].text,
                 QStringLiteral("Edited second half"));
        const Project separatelyEdited = m_project->project();
        QVERIFY(m_subtitles->selectCue(firstId, false));
        QVERIFY(m_subtitles->canMergeWithNext());
        QVERIFY(m_subtitles->mergeSelectedWithNext());
        QCOMPARE(m_project->project().subtitles().cues.size(), size_t{1});
        QCOMPARE(m_project->project().subtitles().cues[0].text,
                 QStringLiteral("New subtitle\nEdited second half"));
        QCOMPARE(m_project->undoText(), QStringLiteral("Merge Subtitles"));
        m_project->undo();
        QCOMPARE(m_project->project(), separatelyEdited);
        m_project->redo();
        QCOMPARE(m_project->project().subtitles().cues.size(), size_t{1});
        QVERIFY(!m_subtitles->canMergeWithNext());
    }

    void findNextAndReplaceAllCaptionsPreserveExactTimelineAndUndo()
    {
        QVERIFY(m_subtitles->addAtPlayhead());
        QVERIFY(m_subtitles->updateSelected(QStringLiteral("00:00:00,000"),
                                            QStringLiteral("00:00:01,000"),
                                            QStringLiteral("Hello नमस्ते")));
        m_playback->seek(30); // 25 fps: 1200ms, outside first cue
        QVERIFY(m_subtitles->addAtPlayhead());
        QVERIFY(m_subtitles->updateSelected(QStringLiteral("00:00:01,200"),
                                            QStringLiteral("00:00:02,000"),
                                            QStringLiteral("hELLo World")));
        const Project original = m_project->project();
        QCOMPARE(m_subtitles->matchingCueCount(QStringLiteral("hello"), false), 2);
        QCOMPARE(m_subtitles->matchingCueCount(QStringLiteral("hello"), true), 0);
        QCOMPARE(m_subtitles->matchingCueCount(QStringLiteral("No match"), false), 0);
        QVERIFY(!m_subtitles->findNextMatchingCue(QStringLiteral("not found"), false));
        QCOMPARE(m_project->project(), original);

        QVERIFY(m_subtitles->findNextMatchingCue(QStringLiteral("HELLO"), false));
        QCOMPARE(m_subtitles->selectedCue().value(QStringLiteral("id")).toString(),
                 original.subtitles().cues[0].id); // wraps around
        QCOMPARE(m_playback->positionFrames(), FrameCount{0});
        QVERIFY(m_subtitles->findNextMatchingCue(QStringLiteral("hello"), false));
        QCOMPARE(m_subtitles->selectedCue().value(QStringLiteral("id")).toString(),
                 original.subtitles().cues[1].id);
        QCOMPARE(m_playback->positionFrames(), FrameCount{30});

        QVERIFY(m_subtitles->replaceAllMatchingText(QStringLiteral("hello"), QStringLiteral("Hi"),
                                                    false));
        const Project replaced = m_project->project();
        QCOMPARE(replaced.subtitles().cues[0].text, QStringLiteral("Hi नमस्ते"));
        QCOMPARE(replaced.subtitles().cues[1].text, QStringLiteral("Hi World"));
        QCOMPARE(replaced.timeline(), original.timeline());
        QCOMPARE(replaced.subtitles().cues[0].startMs, original.subtitles().cues[0].startMs);
        QCOMPARE(replaced.subtitles().cues[1].endMs, original.subtitles().cues[1].endMs);
        QCOMPARE(m_subtitles->matchingCueCount(QStringLiteral("hello"), false), 0);
        QCOMPARE(m_project->undoText(), QStringLiteral("Replace Subtitle Text"));

        QVERIFY(!m_subtitles->replaceAllMatchingText(
            QStringLiteral("Hi"), QString(kMaxSubtitleTextLength, QLatin1Char('x')), true));
        QCOMPARE(m_project->project(), replaced);
        m_project->undo();
        QCOMPARE(m_project->project(), original);
        QCOMPARE(m_subtitles->matchingCueCount(QStringLiteral("hello"), false), 2);
        m_project->redo();
        QCOMPARE(m_project->project(), replaced);
    }

    void importIsAtomicAndSidecarExportDoesNotEditTheProject()
    {
        const QString input = m_dir.filePath(QStringLiteral("Hindi captions %.SRT"));
        const QByteArray bytes =
            QStringLiteral("1\n00:00:01,001 --> 00:00:02,007\nनमस्ते 🌍\nSecond line\n\n"
                           "2\n00:01:40,000 --> 00:01:42,000\nAfter the video\n")
                .toUtf8();
        QVERIFY(write(input, bytes));
        QVERIFY(m_subtitles->importSrt(QUrl::fromLocalFile(input).toString()));
        QCOMPARE(m_subtitles->rowCount(), 2);
        const SubtitleTrack imported = m_project->project().subtitles();
        QCOMPARE(imported.cues.front().text, QStringLiteral("नमस्ते 🌍\nSecond line"));
        QCOMPARE(m_subtitles->selectedIndex(), 0);
        QVERIFY(m_project->save());
        QVERIFY(!m_subtitles->importSrt(input)); // overlap with itself rejects the entire append
        QCOMPARE(m_project->project().subtitles(), imported);
        QVERIFY(!m_project->isModified());
        const QString output = m_dir.filePath(QStringLiteral("roundtrip.srt"));
        QVERIFY(m_subtitles->exportSrt(output));
        QVERIFY(!m_project->isModified());
        const auto decoded = srt::fromBytes(read(output));
        QVERIFY(decoded);
        QCOMPARE(decoded->cues.size(), size_t{2});
        QCOMPARE(decoded->cues.front().startMs, 1001);
        QCOMPARE(decoded->cues.front().endMs, 2007);
        QCOMPARE(decoded->cues.front().text, imported.cues.front().text);
        m_subtitles->nextCue();
        QVERIFY(!m_subtitles->canSeek());
        QVERIFY(
            m_subtitles->data(m_subtitles->index(1), ui::SubtitleController::OutsideRole).toBool());
        QCOMPARE(m_subtitles->visibleCues(0, 200).size(), 1);
        QVERIFY(m_subtitles->clearAll());
        QCOMPARE(m_subtitles->rowCount(), 0);
        m_project->undo();
        QCOMPARE(m_project->project().subtitles(), imported);
        m_project->undo(); // importing every cue was one undo step
        QCOMPARE(m_subtitles->rowCount(), 0);
    }
    void transcriptExportIsProtectedAndDoesNotModifyProject()
    {
        QVERIFY(m_subtitles->addAtPlayhead());
        QVERIFY(m_subtitles->updateSelected(QStringLiteral("00:00:00,001"),
                                            QStringLiteral("00:00:01,002"),
                                            QStringLiteral("नमस्ते <b>literal</b>")));
        QVERIFY(m_project->save());
        const Project before = m_project->project();
        const bool undoBeforeExport = m_project->canUndo();
        const QString output = m_dir.filePath(QStringLiteral("captions.txt"));
        QVERIFY(m_subtitles->exportTranscriptTxt(QUrl::fromLocalFile(output).toString()));
        QCOMPARE(read(output), *transcript::toBytes(before.subtitles()));
        QCOMPARE(m_project->project(), before);
        QVERIFY(!m_project->isModified());
        QCOMPARE(m_project->canUndo(), undoBeforeExport);
        QVERIFY(!m_subtitles->exportTranscriptTxt(QStringLiteral("https://example.com/file.txt")));
        QVERIFY(!m_subtitles->exportTranscriptTxt(m_dir.filePath(QStringLiteral("wrong.srt"))));
        QVERIFY(
            !m_subtitles->exportTranscriptTxt(m_dir.filePath(QStringLiteral("absent/file.txt"))));
        QCOMPARE(m_project->project(), before);
        QVERIFY(open(before, QStringLiteral("project.txt")));
        const QByteArray saved = read(m_project->filePath());
        QVERIFY(!m_subtitles->exportTranscriptTxt(m_project->filePath()));
        QCOMPARE(read(m_project->filePath()), saved);
        QCOMPARE(m_project->project(), before);
        QVERIFY(!m_project->isModified());
    }

    void webVttImportAppendsCuesWithOneUndoAndRejectsCollisions()
    {
        const QString input = m_dir.filePath(QStringLiteral("captions-import.vtt"));
        const QByteArray bytes = QStringLiteral("WEBVTT\r\n\r\n"
                                                "NOTE Draft for client review\r\n\r\n"
                                                "Hindi caption\r\n00:01.001 --> 00:02.007\r\n"
                                                "नमस्ते &lt;b&gt;literal&lt;/b&gt; 🌍\r\n"
                                                "Second line\r\n\r\n"
                                                "00:04.050 --> 00:05.000\r\nAfter edit\r\n")
                                     .toUtf8();
        QVERIFY(write(input, bytes));
        QCOMPARE(m_subtitles->rowCount(), 0);
        QVERIFY(m_subtitles->importWebVtt(QUrl::fromLocalFile(input).toString()));
        QCOMPARE(m_subtitles->rowCount(), 2);
        const SubtitleTrack imported = m_project->project().subtitles();
        QCOMPARE(imported.cues[0].startMs, std::int64_t{1001});
        QCOMPARE(imported.cues[0].endMs, std::int64_t{2007});
        QCOMPARE(imported.cues[0].text, QStringLiteral("नमस्ते <b>literal</b> 🌍\nSecond line"));
        QCOMPARE(m_subtitles->selectedIndex(), 0);
        QVERIFY(m_project->save());
        QVERIFY(!m_subtitles->importWebVtt(input)); // collision; no partial append
        QCOMPARE(m_project->project().subtitles(), imported);
        QVERIFY(!m_project->isModified());
        const QString output = m_dir.filePath(QStringLiteral("reexported.vtt"));
        QVERIFY(m_subtitles->exportWebVtt(output));
        const auto again = webvtt::fromBytes(read(output));
        QVERIFY(again);
        QCOMPARE(again->cues.size(), imported.cues.size());
        QCOMPARE(again->cues[0].text, imported.cues[0].text);
        QCOMPARE(again->cues[1].endMs, imported.cues[1].endMs);

        QVERIFY(!m_subtitles->importWebVtt(QStringLiteral("https://example.com/captions.vtt")));
        QVERIFY(!m_subtitles->importWebVtt(m_dir.filePath(QStringLiteral("missing.vtt"))));
        QVERIFY(!m_subtitles->importWebVtt(m_dir.filePath(QStringLiteral("wrong.srt"))));
        QCOMPARE(m_project->project().subtitles(), imported);
        QVERIFY(m_subtitles->clearAll());
        m_project->undo();
        QCOMPARE(m_project->project().subtitles(), imported);
        m_project->undo();
        QCOMPARE(m_subtitles->rowCount(), 0);
        QCOMPARE(m_project->project().timeline(), m_base.timeline());
    }

    void webVttExportIsProtectedAndDoesNotModifyProject()
    {
        QVERIFY(m_subtitles->addAtPlayhead());
        QVERIFY(m_subtitles->updateSelected(QStringLiteral("00:00:00,001"),
                                            QStringLiteral("00:00:01,002"),
                                            QStringLiteral("नमस्ते <b>&</b>")));
        QVERIFY(m_project->save());
        const Project before = m_project->project();
        const bool undoBeforeExport = m_project->canUndo();
        const QString output = m_dir.filePath(QStringLiteral("captions.vtt"));
        QVERIFY(m_subtitles->exportWebVtt(QUrl::fromLocalFile(output).toString()));
        QCOMPARE(read(output), *webvtt::toBytes(before.subtitles()));
        QCOMPARE(m_project->project(), before);
        QVERIFY(!m_project->isModified());
        QCOMPARE(m_project->canUndo(), undoBeforeExport); // Sidecar export never edits history.
        QVERIFY(!m_subtitles->exportWebVtt(QStringLiteral("https://example.com/file.vtt")));
        QVERIFY(!m_subtitles->exportWebVtt(m_dir.filePath(QStringLiteral("wrong.srt"))));
        QVERIFY(!m_subtitles->exportWebVtt(m_dir.filePath(QStringLiteral("absent/file.vtt"))));
        QCOMPARE(m_project->project(), before);
        QVERIFY(open(before, QStringLiteral("project.vtt")));
        const QByteArray saved = read(m_project->filePath());
        QVERIFY(!m_subtitles->exportWebVtt(m_project->filePath()));
        QCOMPARE(read(m_project->filePath()), saved);
        QCOMPARE(m_project->project(), before);
        QVERIFY(!m_project->isModified());
    }

    void shiftControlsPreserveCuesAndRejectInvalidOffsets()
    {
        QVERIFY(m_subtitles->addAtPlayhead());
        const SubtitleTrack before = m_project->project().subtitles();
        QVERIFY(m_subtitles->shiftAll(100));
        const SubtitleTrack shifted = m_project->project().subtitles();
        QCOMPARE(shifted.cues.front().startMs, std::int64_t{100});
        QCOMPARE(shifted.cues.front().endMs - shifted.cues.front().startMs,
                 before.cues.front().endMs - before.cues.front().startMs);
        QCOMPARE(m_subtitles->selectedCue().value(QStringLiteral("start")).toString(),
                 QStringLiteral("00:00:00,100"));
        QVERIFY(!m_subtitles->shiftAll(-101));
        QCOMPARE(m_project->project().subtitles(), shifted);
        QVERIFY(m_project->canUndo());
        m_project->undo();
        QCOMPARE(m_project->project().subtitles(), before);
        QVERIFY(m_project->canRedo());
        m_project->redo();
        QCOMPARE(m_project->project().subtitles(), shifted);
        QVERIFY(m_subtitles->shiftAll(-100));
        QCOMPARE(m_project->project().subtitles(), before);
        QCOMPARE(m_project->project().timeline(), m_base.timeline());
    }

    void badFilesAndProtectedPathsLeaveExistingContentIntact()
    {
        const QString invalid = m_dir.filePath(QStringLiteral("invalid.srt"));
        QVERIFY(write(invalid, QByteArray("1\n00:00:00,000 --> 00:00:01,000\n\xc3")));
        QVERIFY(!m_subtitles->importSrt(invalid));
        QVERIFY(!m_subtitles->importSrt(QStringLiteral("https://example.com/captions.srt")));
        QVERIFY(!m_subtitles->importSrt(m_dir.filePath(QStringLiteral("missing.srt"))));
        QCOMPARE(m_subtitles->rowCount(), 0);
        QVERIFY(!m_project->canUndo());
        QVERIFY(m_subtitles->addAtPlayhead());
        QVERIFY(open(m_project->project(), QStringLiteral("project.srt")));
        const QByteArray saved = read(m_project->filePath());
        QVERIFY(!m_subtitles->exportSrt(m_project->filePath()));
        QCOMPARE(read(m_project->filePath()), saved);
        const QString previous = m_dir.filePath(QStringLiteral("existing.srt"));
        QVERIFY(write(previous, QByteArray("Existing contents")));
        QVERIFY(m_subtitles->exportSrt(previous));
        QVERIFY(srt::fromBytes(read(previous)));
        QVERIFY(!m_subtitles->exportSrt(m_dir.filePath(QStringLiteral("absent/export.srt"))));
        QVERIFY(!m_project->isModified());
    }
    void fractionalRateSamplingAndViewportFilteringKeepExactTimes()
    {
        ProjectSettings settings = m_base.settings();
        settings.frameRate = *FrameRate::create(30000, 1001);
        Project p = Project::createNew(QStringLiteral("Fractional rate"), settings);
        for (const MediaAsset& asset : m_base.media())
            QVERIFY(p.addAsset(asset));
        p.timeline() = m_base.timeline();
        SubtitleTrack track;
        track.cues = {{createId(), 1000, 2000, QStringLiteral("First")},
                      {createId(), 2001, 2002, QStringLiteral("Between sampled frames")},
                      {createId(), 4000, 5000, QStringLiteral("Last")}};
        QVERIFY(p.setSubtitles(track));
        QVERIFY(open(p));
        QVERIFY(m_subtitles->seekSelected());
        QCOMPARE(m_playback->positionFrames(), 30);
        QCOMPARE(m_subtitles->playheadTimestamp(), QStringLiteral("00:00:01,001"));
        QCOMPARE(m_subtitles->visibleCues(29.5, 30.5).size(), 1);
        QCOMPARE(m_subtitles->visibleCues(60, 61).size(), 0);
        QCOMPARE(m_subtitles->visibleCues(120, 150).size(), 1);
        QVERIFY(m_subtitles->visibleCues(-1, 2).isEmpty());
        QVERIFY(m_subtitles->visibleCues(0, std::numeric_limits<double>::infinity()).isEmpty());
        QVERIFY(m_subtitles->selectCue(track.cues[1].id));
        QVERIFY(!m_subtitles->canSeek());
        QSignalSpy draftUpdates(m_subtitles.get(), &ui::SubtitleController::selectedCueChanged);
        m_playback->seek(122);
        QCOMPARE(draftUpdates.size(), 0); // playback must not replace the user's text draft
        QCOMPARE(m_project->project().subtitles(), track);
        QVERIFY(open(m_base));
        QCOMPARE(m_subtitles->rowCount(), 0);
        QCOMPARE(m_subtitles->selectedIndex(), -1);
    }
    void cleanupTestCase() { engine::MltRuntime::shutdown(); }
};

QTEST_MAIN(SubtitleControllerTest)
#include "tst_subtitle_controller.moc"
