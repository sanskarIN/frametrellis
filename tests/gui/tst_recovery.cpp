// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors

#include "project/project_serializer.h"
#include "ui/project_controller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace ft;

class RecoveryTest : public QObject
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
        QCoreApplication::setApplicationName(QStringLiteral("tst_recovery"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    }

    void renamedBackupSurvivesFailedCanonicalSave()
    {
        const Project lostEdit = Project::createNew(QStringLiteral("Renamed backup"), {});
        const QString autosaveDir = ui::ProjectController::autosaveDirectory();
        QVERIFY(QDir().mkpath(autosaveDir));
        const QDir dir(autosaveDir);
        const QString canonical = dir.filePath(lostEdit.id() + QStringLiteral(".ftproj"));
        const QString renamed = dir.filePath(lostEdit.id() + QStringLiteral("-renamed.ftproj"));
        QVERIFY(project_format::saveToFile(lostEdit, renamed));

        // A directory at the canonical destination exists, but a QSaveFile
        // cannot publish a replacement project over it.
        QVERIFY(QDir().mkpath(canonical));
        ui::ProjectController controller;
        QCOMPARE(controller.recovery().value(QStringLiteral("autosavePath")).toString(), renamed);
        QVERIFY(controller.recoverAutosave());
        QVERIFY(controller.isModified());
        QVERIFY(QFileInfo(renamed).isFile());
        const auto preserved = project_format::loadFromFile(renamed);
        QVERIFY(preserved);
        QCOMPARE(preserved->id(), lostEdit.id());

        QVERIFY(QDir().rmdir(canonical));
        const QString saved = m_dir.filePath(QStringLiteral("renamed-recovered.ftproj"));
        QVERIFY(controller.saveAs(saved));
        QVERIFY(QFile::remove(renamed));
    }

    void renamedBackupKeepsOriginalPathAfterSuccessfulRecovery()
    {
        const Project lostEdit = Project::createNew(QStringLiteral("Recover location"), {});
        const QString autosaveDir = ui::ProjectController::autosaveDirectory();
        QVERIFY(QDir().mkpath(autosaveDir));
        const QDir dir(autosaveDir);
        const QString canonical = dir.filePath(lostEdit.id() + QStringLiteral(".ftproj"));
        const QString renamed = dir.filePath(lostEdit.id() + QStringLiteral("-renamed.ftproj"));
        const QString original = m_dir.filePath(QStringLiteral("original.ftproj"));
        QVERIFY(project_format::saveToFile(lostEdit, renamed));
        QFile meta(renamed + QStringLiteral(".json"));
        QVERIFY(meta.open(QIODevice::WriteOnly));
        QVERIFY(meta.write(QJsonDocument(QJsonObject{{QStringLiteral("originalPath"), original}})
                               .toJson()) > 0);
        meta.close();

        ui::ProjectController controller;
        QCOMPARE(controller.recovery().value(QStringLiteral("originalPath")).toString(), original);
        QVERIFY(controller.recoverAutosave());
        QVERIFY(!QFileInfo::exists(renamed));
        QVERIFY(!QFileInfo::exists(renamed + QStringLiteral(".json")));
        QVERIFY(QFileInfo(canonical).isFile());

        controller.autosave(); // Simulate the next periodic save.
        QFile refreshed(canonical + QStringLiteral(".json"));
        QVERIFY(refreshed.open(QIODevice::ReadOnly));
        QCOMPARE(QJsonDocument::fromJson(refreshed.readAll())
                     .object()
                     .value(QStringLiteral("originalPath"))
                     .toString(),
                 original);
        refreshed.close();
        QVERIFY(controller.saveAs(original));
        QVERIFY(!QFileInfo::exists(canonical));
    }

    void recoveringDoesNotDeleteTheOnlyBackup()
    {
        const Project lostEdit = Project::createNew(QStringLiteral("Recovered edit"), {});
        const QString autosaveDir = ui::ProjectController::autosaveDirectory();
        QVERIFY(QDir().mkpath(autosaveDir));
        const QString backup =
            QDir(autosaveDir).filePath(lostEdit.id() + QStringLiteral(".ftproj"));
        const Status written = project_format::saveToFile(lostEdit, backup);
        QVERIFY2(written, qPrintable(written ? QString() : written.error().toString()));

        {
            ui::ProjectController firstLaunch;
            QCOMPARE(firstLaunch.recovery().value(QStringLiteral("autosavePath")).toString(),
                     backup);
            QVERIFY(firstLaunch.recoverAutosave());
            QCOMPARE(firstLaunch.project().id(), lostEdit.id());
            // A second unexpected shutdown can happen before the user chooses Save As.
            QVERIFY2(QFileInfo::exists(backup), "Recover deleted the only remaining autosave");
            const Result<Project> restored = project_format::loadFromFile(backup);
            QVERIFY2(restored, qPrintable(restored ? QString() : restored.error().toString()));
            QCOMPARE(restored->id(), lostEdit.id());
            QVERIFY(firstLaunch.project().id() == restored->id());
        }

        {
            ui::ProjectController secondLaunch;
            QCOMPARE(secondLaunch.recovery().value(QStringLiteral("autosavePath")).toString(),
                     backup);
            QVERIFY(secondLaunch.recoverAutosave());
            const QString saved = m_dir.filePath(QStringLiteral("saved-after-recovery.ftproj"));
            QVERIFY(secondLaunch.saveAs(saved));
            QVERIFY(QFileInfo::exists(saved));
            QVERIFY(!QFileInfo::exists(backup));
            QVERIFY(!QFileInfo::exists(backup + QStringLiteral(".json")));
        }
    }
};

QTEST_MAIN(RecoveryTest)
#include "tst_recovery.moc"
