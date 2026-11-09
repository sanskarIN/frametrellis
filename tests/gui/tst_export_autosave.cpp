// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors

#include "core/ids.h"
#include "editing/marker_edits.h"
#include "editing/timeline_edits.h"
#include "engine/mlt_runtime.h"
#include "media/media_probe.h"
#include "media_fixtures.h"
#include "project/project_serializer.h"
#include "ui/export_controller.h"
#include "ui/project_controller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace ft;

class ExportAutosaveTest : public QObject
{
    Q_OBJECT
private:
    QTemporaryDir m_dir;

private slots:
    void initTestCase()
    {
        QVERIFY(m_dir.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("FrameTrellisTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_export_autosave"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
        QVERIFY(engine::MltRuntime::initialize());
    }

    void capturesLastUnsavedEditBeforeHelperStarts()
    {
        ProjectSettings profile;
        profile.width = 320;
        profile.height = 180;
        profile.frameRate = FrameRate::integer(25);
        Project original = Project::createNew(QStringLiteral("Export recovery"), profile);
        const auto asset = media::createAsset(testing::makeStill(
            m_dir.path(), QStringLiteral("recovery-still.png"), Qt::green, QSize(320, 180)));
        QVERIFY(asset);
        QVERIFY(original.addAsset(*asset));
        Clip clip;
        clip.id = createId();
        clip.assetId = asset->id;
        clip.duration = 25;
        QVERIFY(original.timeline().insertClip(original.timeline().tracks()[0].id, clip,
                                               original.sourceLookup()));
        const QString projectPath = m_dir.filePath(QStringLiteral("before-export.ftproj"));
        QVERIFY(project_format::saveToFile(original, projectPath));

        ui::ProjectController project;
        QVERIFY(project.openProject(projectPath));
        // This new cue must survive even if the editor dies before the 60-second timer.
        QVERIFY(project.execute(
            edits::addMarker({createId(), 0, QStringLiteral("Last edit before render")})));
        QVERIFY(project.isModified());
        const QString autosave = QDir(ui::ProjectController::autosaveDirectory())
                                     .filePath(project.project().id() + QStringLiteral(".ftproj"));
        QVERIFY(!QFileInfo::exists(autosave));

        ui::ExportController exporter(&project, QStringLiteral(FT_RENDER_HELPER));
        const QVariantMap options{
            {QStringLiteral("presetId"), QStringLiteral("draft")},
            {QStringLiteral("outputPath"), m_dir.filePath(QStringLiteral("result.mp4"))},
            {QStringLiteral("speedPreset"), QStringLiteral("ultrafast")},
            {QStringLiteral("includeAudio"), false}};
        QSignalSpy completed(&exporter, &ui::ExportController::finished);
        QVERIFY(exporter.start(options));
        QVERIFY2(QFileInfo::exists(autosave),
                 "Starting an export failed to persist the most recent unsaved edit.");
        const Result<Project> backup = project_format::loadFromFile(autosave);
        QVERIFY(backup);
        QCOMPARE(backup->markers().size(), size_t{1});
        QCOMPARE(backup->markers().front().label, QStringLiteral("Last edit before render"));
        QVERIFY(completed.count() > 0 || completed.wait(60'000));
        QVERIFY2(completed.first().at(0).toBool(), qPrintable(completed.first().at(1).toString()));
        // The recovery snapshot must not silently mark the project as saved.
        QVERIFY(project.isModified());
        project.removeOwnAutosave();
    }

    void batchPreflightRejectsDuplicatesExistingFilesAndInvalidRanges()
    {
        ProjectSettings profile;
        profile.width = 320;
        profile.height = 180;
        profile.frameRate = FrameRate::integer(25);
        Project original = Project::createNew(QStringLiteral("Batch safety"), profile);
        const auto asset = media::createAsset(testing::makeStill(
            m_dir.path(), QStringLiteral("batch-safe-still.png"), Qt::blue, QSize(320, 180)));
        QVERIFY(asset);
        QVERIFY(original.addAsset(*asset));
        Clip clip;
        clip.id = createId();
        clip.assetId = asset->id;
        clip.duration = 25;
        QVERIFY(original.timeline().insertClip(original.timeline().tracks()[0].id, clip,
                                               original.sourceLookup()));
        const QString projectPath = m_dir.filePath(QStringLiteral("batch-preflight.ftproj"));
        QVERIFY(project_format::saveToFile(original, projectPath));
        ui::ProjectController project;
        QVERIFY(project.openProject(projectPath));
        ui::ExportController exporter(&project, QStringLiteral(FT_RENDER_HELPER));
        QVariantMap settings{{QStringLiteral("presetId"), QStringLiteral("match")},
                             {QStringLiteral("outputPath"), m_dir.filePath(QStringLiteral("batch-safe.mp4"))},
                             {QStringLiteral("frameRate"), QStringLiteral("30")},
                             {QStringLiteral("rangeStartFrame"), QStringLiteral("0")},
                             {QStringLiteral("rangeEndFrame"), QStringLiteral("25")},
                             {QStringLiteral("includeAudio"), false}};
        const QStringList variants{QStringLiteral("draft"), QStringLiteral("review480p")};
        QCOMPARE(exporter.validateBatch(settings, variants), QString());
        QVERIFY(!exporter.validateBatch(settings, {QStringLiteral("draft")}).isEmpty());
        QVERIFY(!exporter.validateBatch(settings, {QStringLiteral("draft"),
                                                  QStringLiteral("draft")}).isEmpty());
        QVERIFY(!exporter.validateBatch(settings, {QStringLiteral("draft"),
                                                  QStringLiteral("unknown")}).isEmpty());
        QVariantMap bad = settings;
        bad[QStringLiteral("frameRate")] = QStringLiteral("9999");
        QVERIFY(!exporter.validateBatch(bad, variants).isEmpty());
        bad = settings;
        bad[QStringLiteral("rangeStartFrame")] = QStringLiteral("25");
        QVERIFY(!exporter.validateBatch(bad, variants).isEmpty());

        const QString protectedOutput = m_dir.filePath(QStringLiteral("batch-safe-draft.mp4"));
        QFile existing(protectedOutput);
        QVERIFY(existing.open(QIODevice::WriteOnly));
        QCOMPARE(existing.write("keep-me"), qint64{7});
        existing.close();
        QVERIFY(!exporter.validateBatch(settings, variants).isEmpty());
        QSignalSpy notices(&exporter, &ui::ExportController::finished);
        QVERIFY(!exporter.startBatch(settings, variants));
        QCOMPARE(exporter.batchTotal(), 0);
        QCOMPARE(notices.size(), 1);
        QVERIFY(!notices.first().at(0).toBool());
        QVERIFY(existing.open(QIODevice::ReadOnly));
        QCOMPARE(existing.readAll(), QByteArray("keep-me"));
        existing.close();
        QVERIFY(existing.remove());
    }

    void batchRendersDistinctPresetsFromOneFrozenSnapshot()
    {
        ProjectSettings profile;
        profile.width = 320;
        profile.height = 180;
        profile.frameRate = FrameRate::integer(25);
        Project original = Project::createNew(QStringLiteral("Frozen batch"), profile);
        const auto asset = media::createAsset(testing::makeStill(
            m_dir.path(), QStringLiteral("batch-frozen-still.png"), Qt::yellow, QSize(320, 180)));
        QVERIFY(asset);
        QVERIFY(original.addAsset(*asset));
        Clip first;
        first.id = createId();
        first.assetId = asset->id;
        first.duration = 25;
        const Id trackId = original.timeline().tracks()[0].id;
        QVERIFY(original.timeline().insertClip(trackId, first, original.sourceLookup()));
        const QString projectPath = m_dir.filePath(QStringLiteral("batch-frozen.ftproj"));
        QVERIFY(project_format::saveToFile(original, projectPath));
        ui::ProjectController project;
        QVERIFY(project.openProject(projectPath));
        const QString base = m_dir.filePath(QStringLiteral("batch-two-variants.mp4"));
        const QVariantMap settings{{QStringLiteral("presetId"), QStringLiteral("match")},
                                   {QStringLiteral("outputPath"), base},
                                   {QStringLiteral("frameRate"), QStringLiteral("30")},
                                   {QStringLiteral("rangeStartFrame"), QStringLiteral("0")},
                                   {QStringLiteral("rangeEndFrame"), QStringLiteral("-1")},
                                   {QStringLiteral("includeAudio"), false}};
        const QStringList presets{QStringLiteral("draft"), QStringLiteral("review480p")};
        ui::ExportController exporter(&project, QStringLiteral(FT_RENDER_HELPER));
        QSignalSpy finished(&exporter, &ui::ExportController::finished);
        QVERIFY(exporter.startBatch(settings, presets));
        QCOMPARE(exporter.batchTotal(), 2);
        QVERIFY(exporter.batchActive());
        QVERIFY(exporter.busy());
        QVERIFY(!exporter.start(settings));
        QVERIFY(!exporter.startBatch(settings, presets));
        // Edits made during export must not be inserted into the second batch
        // variant; each helper must read the same frozen sequence at click time.
        Clip extra = first;
        extra.id = createId();
        extra.start = 25;
        QVERIFY(project.execute(edits::addClip(trackId, extra)));
        QCOMPARE(project.project().timeline().duration(), FrameCount{50});
        QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 120'000);
        QVERIFY2(finished.first().at(0).toBool(),
                 qPrintable(finished.first().at(1).toString()));
        QCOMPARE(exporter.batchState(), QStringLiteral("succeeded"));
        QCOMPARE(exporter.batchCompleted(), 2);
        QCOMPARE(exporter.batchTotal(), 2);
        QCOMPARE(exporter.batchProgress(), 1.0);
        QVERIFY(!exporter.busy());
        for (const QString& id : presets) {
            const QString path = m_dir.filePath(QStringLiteral("batch-two-variants-") + id +
                                               QStringLiteral(".mp4"));
            QVERIFY2(QFileInfo(path).size() > 1024, qPrintable(path));
            const Result<MediaInfo> info = media::probeMedia(path);
            QVERIFY2(info, qPrintable(path));
            QVERIFY(info->hasVideo);
            QVERIFY(info->durationUs >= 900'000 && info->durationUs <= 1'200'000);
        }
        QVERIFY(!QFileInfo::exists(base));
        QCOMPARE(project.project().timeline().duration(), FrameCount{50});
        exporter.reset();
        QCOMPARE(exporter.batchTotal(), 0);
        QCOMPARE(exporter.batchState(), QStringLiteral("idle"));
    }

    void cleanupTestCase() { engine::MltRuntime::shutdown(); }
};

QTEST_MAIN(ExportAutosaveTest)
#include "tst_export_autosave.moc"
