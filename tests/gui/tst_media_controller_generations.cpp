// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/media_controller.h"
#include "ui/project_controller.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

using namespace ft;

class MediaControllerGenerationTest : public QObject
{
    Q_OBJECT

private:
    QTemporaryDir m_settings;
    using Controller = ui::MediaController;

    struct Fixture
    {
        ui::ProjectController project;
        Controller media{&project, std::make_shared<ui::ThumbnailRegistry>()};
    };

    static void drainWorkers()
    {
        QVERIFY(QThreadPool::globalInstance()->waitForDone(10'000));
        QCoreApplication::processEvents();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("FrameTrellisTests"));
        QCoreApplication::setApplicationName(QStringLiteral("tst_media_controller_generations"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }

    void replacementDuringFileImportStatusCancelsSubmission()
    {
        Fixture f;
        const Id previous = f.project.project().id();
        QSignalSpy errors(&f.media, &Controller::importProblems);
        QSignalSpy imported(&f.media, &Controller::imported);
        bool replaced = false;
        connect(&f.media, &Controller::statusMessage, this, [&](const QString& text) {
            if (!replaced && text.startsWith(QStringLiteral("Importing "))) {
                replaced = f.project.newProject(0, QStringLiteral("Replacement"));
            }
        });

        f.media.importFiles({m_settings.filePath(QStringLiteral("old.wav"))});
        QVERIFY(replaced);
        QVERIFY(f.project.project().id() != previous);
        QCOMPARE(f.media.pendingImports(), 0);
        QVERIFY(!f.media.importing());
        drainWorkers();
        QCOMPARE(errors.count(), 0);
        QCOMPARE(imported.count(), 0);
        QVERIFY(f.project.project().media().empty());
    }

    void replacementDuringFolderSubmissionCancelsScan()
    {
        Fixture f;
        QSignalSpy errors(&f.media, &Controller::importProblems);
        bool replaced = false;
        connect(&f.media, &Controller::importingChanged, this, [&] {
            if (!replaced && f.media.importing()) {
                replaced = true;
                QVERIFY(f.project.newProject(0, QStringLiteral("New document")));
            }
        });

        QVERIFY(!f.media.importFolder(m_settings.filePath(QStringLiteral("missing"))));
        QVERIFY(replaced);
        QVERIFY(!f.media.importing());
        QCOMPARE(f.media.pendingImports(), 0);
        drainWorkers();
        QCOMPARE(errors.count(), 0);
        QVERIFY(f.project.project().media().empty());
    }

    void replacementDuringFolderCompletionSuppressesOldErrors()
    {
        Fixture f;
        QSignalSpy errors(&f.media, &Controller::importProblems);
        bool sawStart = false;
        bool replaced = false;
        connect(&f.media, &Controller::importingChanged, this, [&] {
            if (f.media.importing()) {
                sawStart = true;
            } else if (sawStart && !replaced) {
                replaced = true;
                QVERIFY(f.project.newProject(0, QStringLiteral("New document")));
            }
        });

        QVERIFY(f.media.importFolder(m_settings.filePath(QStringLiteral("missing-folder"))));
        QTRY_VERIFY_WITH_TIMEOUT(replaced, 10'000);
        QVERIFY(!f.media.importing());
        drainWorkers();
        QCOMPARE(errors.count(), 0);
        QVERIFY(f.project.project().media().empty());
    }

    void oldScanCannotSubmitFilesIntoReplacementProject()
    {
        Fixture f;
        QTemporaryDir folder;
        QVERIFY(folder.isValid());
        QFile candidate(folder.filePath(QStringLiteral("old.wav")));
        QVERIFY(candidate.open(QIODevice::WriteOnly));
        QVERIFY(candidate.write("test") > 0);
        candidate.close();

        QSignalSpy errors(&f.media, &Controller::importProblems);
        QSignalSpy imported(&f.media, &Controller::imported);
        bool replaced = false;
        connect(&f.media, &Controller::importingChanged, this, [&] {
            if (!f.media.importing() && !replaced) {
                replaced = true;
                QVERIFY(f.project.newProject(0, QStringLiteral("Fresh project")));
            }
        });

        QVERIFY(f.media.importFolder(folder.path()));
        QTRY_VERIFY_WITH_TIMEOUT(replaced, 10'000);
        drainWorkers();
        QCOMPARE(f.media.pendingImports(), 0);
        QVERIFY(!f.media.importing());
        QCOMPARE(errors.count(), 0);
        QCOMPARE(imported.count(), 0);
        QVERIFY(f.project.project().media().empty());
    }

    void replacementDuringRelinkCompletionSuppressesOldErrors()
    {
        Fixture f;
        MediaAsset missing;
        missing.id = createId();
        missing.filePath = m_settings.filePath(QStringLiteral("unavailable.png"));
        missing.name = QStringLiteral("Unavailable");
        missing.status = MediaStatus::Missing;
        missing.info.kind = MediaKind::Image;
        missing.info.hasVideo = true;
        missing.info.width = 32;
        missing.info.height = 18;
        QVERIFY(
            f.project.execute(std::make_unique<AddMediaCommand>(std::vector<MediaAsset>{missing})));
        QSignalSpy errors(&f.media, &Controller::importProblems);
        bool sawStart = false;
        bool replaced = false;
        connect(&f.media, &Controller::importingChanged, this, [&] {
            if (f.media.importing()) {
                sawStart = true;
            } else if (sawStart && !replaced) {
                replaced = true;
                QVERIFY(f.project.newProject(0, QStringLiteral("Unrelated document")));
            }
        });

        QVERIFY(f.media.relinkMissingFolder(m_settings.filePath(QStringLiteral("missing"))));
        QTRY_VERIFY_WITH_TIMEOUT(replaced, 10'000);
        drainWorkers();
        QCOMPARE(errors.count(), 0);
        QVERIFY(!f.media.importing());
        QVERIFY(f.project.project().media().empty());
        QVERIFY(!f.project.canUndo());
    }

    void freshScanAfterReplacementIsNotClearedByOldCancellation()
    {
        Fixture f;
        QSignalSpy errors(&f.media, &Controller::importProblems);
        bool newScanAccepted = false;
        bool replaced = false;
        connect(&f.media, &Controller::importingChanged, this, [&] {
            if (replaced && !newScanAccepted && !f.media.importing()) {
                newScanAccepted =
                    f.media.importFolder(m_settings.filePath(QStringLiteral("fresh-missing")));
            }
        });

        QVERIFY(f.media.importFolder(m_settings.filePath(QStringLiteral("old-missing"))));
        replaced = true;
        QVERIFY(f.project.newProject(0, QStringLiteral("Fresh project")));
        QVERIFY(newScanAccepted);
        QTRY_COMPARE_WITH_TIMEOUT(errors.count(), 1, 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(!f.media.importing(), 10'000);
        drainWorkers();
        QCOMPARE(errors.count(), 1);
        QCOMPARE(f.media.pendingImports(), 0);
        QVERIFY(f.project.project().media().empty());
    }
};

QTEST_MAIN(MediaControllerGenerationTest)
#include "tst_media_controller_generations.moc"
