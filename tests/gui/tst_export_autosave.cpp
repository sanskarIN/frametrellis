// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors

#include "core/ids.h"
#include "editing/marker_edits.h"
#include "engine/mlt_runtime.h"
#include "media/media_probe.h"
#include "media_fixtures.h"
#include "project/project_serializer.h"
#include "ui/export_controller.h"
#include "ui/project_controller.h"

#include <QDir>
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

    void cleanupTestCase() { engine::MltRuntime::shutdown(); }
};

QTEST_MAIN(ExportAutosaveTest)
#include "tst_export_autosave.moc"
