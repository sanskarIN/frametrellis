// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/project_checkpoints.h"
#include "project/project_serializer.h"
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

class CheckpointControllerTest : public QObject
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
        QCoreApplication::setApplicationName(QStringLiteral("tst_checkpoint_controller"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_dir.path());
    }

    void restoresAnUnsavedVersionWithoutRewritingTheOriginalProject()
    {
        ui::ProjectController controller;
        QVERIFY(controller.newProject(0, QStringLiteral("Original")));
        const QString original = m_dir.filePath(QStringLiteral("original.ftproj"));
        QVERIFY(controller.saveAs(original));
        QVERIFY(!controller.isModified());
        QFile file(original);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray originalBytes = file.readAll();
        file.close();

        const Id projectId = controller.project().id();
        QVERIFY(controller.renameProject(QStringLiteral("First unsaved version")));
        const Project before = controller.project();
        const QString undoText = controller.undoText();
        QVERIFY(controller.createCheckpoint(QStringLiteral("First checkpoint")));
        QCOMPARE(controller.project(), before);
        QCOMPARE(controller.undoText(), undoText);
        QCOMPARE(controller.filePath(), original);
        QVERIFY(controller.isModified());

        const QVariantMap history = controller.checkpointHistory();
        QCOMPARE(history.value(QStringLiteral("projectId")).toString(), projectId);
        const QVariantList entries = history.value(QStringLiteral("entries")).toList();
        QCOMPARE(entries.size(), qsizetype{1});
        const QVariantMap first = entries.front().toMap();
        const Id checkpointId = first.value(QStringLiteral("id")).toString();
        QVERIFY(isValidId(checkpointId));
        QCOMPARE(first.value(QStringLiteral("label")).toString(),
                 QStringLiteral("First checkpoint"));
        QCOMPARE(first.value(QStringLiteral("originalPath")).toString(), original);

        QVERIFY(controller.renameProject(QStringLiteral("Second unsaved version")));
        QSignalSpy replaced(&controller, &ui::ProjectController::documentReplaced);
        QVERIFY(controller.restoreCheckpoint(projectId, checkpointId));
        QCOMPARE(replaced.size(), 1);
        QCOMPARE(controller.project().id(), projectId);
        QCOMPARE(controller.name(), QStringLiteral("First unsaved version"));
        QVERIFY(controller.isModified());
        QVERIFY(!controller.hasFilePath());
        QVERIFY(!controller.canUndo());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), originalBytes);
        file.close();

        const QString autosave = QDir(ui::ProjectController::autosaveDirectory())
                                     .filePath(projectId + QStringLiteral(".ftproj"));
        QVERIFY(QFileInfo(autosave).isFile());
        const auto recovered = project_format::loadFromFile(autosave);
        QVERIFY(recovered);
        QCOMPARE(recovered->name(), controller.name());

        const QString output = m_dir.filePath(QStringLiteral("restored.ftproj"));
        QVERIFY(controller.saveAs(output));
        QVERIFY(!controller.isModified());
        QVERIFY(!QFileInfo::exists(autosave));
        const auto restored = project_format::loadFromFile(output);
        QVERIFY(restored);
        QCOMPARE(restored->name(), QStringLiteral("First unsaved version"));
        QCOMPARE(controller.checkpointHistory().value(QStringLiteral("entries")).toList().size(),
                 qsizetype{1});

        QVERIFY(controller.removeCheckpoint(projectId, checkpointId));
        QVERIFY(controller.checkpointHistory().value(QStringLiteral("entries")).toList().isEmpty());
        QVERIFY(QFileInfo(original).isFile());
        QVERIFY(QFileInfo(output).isFile());
        QVERIFY(QDir(ui::ProjectController::checkpointDirectory()).rmdir(projectId));
    }

    void staleProjectSelectionsCannotModifyAnotherHistory()
    {
        ui::ProjectController controller;
        QVERIFY(controller.newProject(0, QStringLiteral("First")));
        QVERIFY(controller.createCheckpoint(QStringLiteral("Keep")));
        const Id firstId = controller.project().id();
        const QVariantList entries =
            controller.checkpointHistory().value(QStringLiteral("entries")).toList();
        QCOMPARE(entries.size(), qsizetype{1});
        const Id checkpoint = entries.front().toMap().value(QStringLiteral("id")).toString();
        QVERIFY(controller.newProject(0, QStringLiteral("Second")));
        const Project before = controller.project();
        QSignalSpy errors(&controller, &ui::ProjectController::errorOccurred);
        QVERIFY(!controller.restoreCheckpoint(firstId, checkpoint));
        QVERIFY(!controller.removeCheckpoint(firstId, checkpoint));
        QCOMPARE(errors.size(), 2);
        QCOMPARE(controller.project(), before);
        QVERIFY(controller.checkpointHistory().value(QStringLiteral("entries")).toList().isEmpty());

        checkpoints::Store store(ui::ProjectController::checkpointDirectory());
        QVERIFY(store.restore(firstId, checkpoint));
        QVERIFY(store.remove(firstId, checkpoint));
        QVERIFY(QDir(store.root()).rmdir(firstId));
    }
};

QTEST_MAIN(CheckpointControllerTest)
#include "tst_checkpoint_controller.moc"
