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
