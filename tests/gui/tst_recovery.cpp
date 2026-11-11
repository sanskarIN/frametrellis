// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors

#include "project/project_serializer.h"
#include "ui/project_controller.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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
