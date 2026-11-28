// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/project_serializer.h"
#include "ui/project_bundle_controller.h"
#include "ui/project_controller.h"

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace ft;

class ProjectBundleControllerTest : public QObject
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
        QCoreApplication::setApplicationName(QStringLiteral("tst_project_bundle_controller"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    }

    void backgroundCreationPreservesSnapshotAndCurrentDocument()
    {
        ui::ProjectController project;
        QVERIFY(project.newProject(0, QStringLiteral("Before copying")));
        QVERIFY(project.renameProject(QStringLiteral("Unsaved snapshot")));
        const Project snapshot = project.project();
        const QString undo = project.undoText();
        ui::ProjectBundleController bundles(&project);
        QVERIFY(bundles.createBundle(m_dir.path(), QStringLiteral("snapshot"), false));
        QVERIFY(bundles.busy());
        QVERIFY(!bundles.createBundle(m_dir.path(), QStringLiteral("another"), false));

        // Work must retain its original immutable snapshot when editing continues.
        QVERIFY(project.renameProject(QStringLiteral("Current edit")));
        QTRY_VERIFY_WITH_TIMEOUT(!bundles.busy(), 10000);
        const auto result = bundles.result();
        QVERIFY2(result.value(QStringLiteral("success")).toBool(),
                 qPrintable(result.value(QStringLiteral("message")).toString()));
        const auto loaded =
            project_format::loadFromFile(result.value(QStringLiteral("projectFile")).toString());
        QVERIFY(loaded);
        QCOMPARE(loaded->name(), snapshot.name());
        QVERIFY(loaded->id() != snapshot.id());
        QVERIFY(loaded->relativeMediaPaths());
        QCOMPARE(project.name(), QStringLiteral("Current edit"));
        QVERIFY(project.isModified());
        QCOMPARE(project.undoText(), undo);
        QVERIFY(!project.hasFilePath());
        QVERIFY(!QFileInfo::exists(m_dir.filePath(QStringLiteral("another"))));

        const QString folder = result.value(QStringLiteral("directory")).toString();
        QVERIFY(bundles.verifyBundle(folder));
        QTRY_VERIFY_WITH_TIMEOUT(!bundles.busy(), 10000);
        QVERIFY(bundles.result().value(QStringLiteral("success")).toBool());
        QVERIFY(bundles.result().value(QStringLiteral("matches")).toBool());
        QVERIFY(bundles.result().value(QStringLiteral("projectMatches")).toBool());
    }

    void invalidNamesAndExistingDirectoriesAreRefused()
    {
        ui::ProjectController project;
        ui::ProjectBundleController bundles(&project);
        for (const QString& name : {QString(), QStringLiteral("../escape"), QStringLiteral("a/b"),
                                    QStringLiteral("NUL"), QStringLiteral("name with spaces")}) {
            QVERIFY(!bundles.createBundle(m_dir.path(), name, false));
            QVERIFY(!bundles.busy());
            QVERIFY(!bundles.result().value(QStringLiteral("success")).toBool());
        }
        const QString folder = m_dir.filePath(QStringLiteral("existing"));
        QVERIFY(QDir().mkdir(folder));
        QVERIFY(!bundles.createBundle(m_dir.path(), QStringLiteral("existing"), false));
        QVERIFY(QFileInfo(folder).isDir());
        QVERIFY(!bundles.verifyBundle(m_dir.filePath(QStringLiteral("missing"))));
        QVERIFY(!bundles.busy());
    }
};

QTEST_MAIN(ProjectBundleControllerTest)
#include "tst_project_bundle_controller.moc"
