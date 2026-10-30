// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/media_audit.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace ft;

namespace {

void writeData(const QString& path, const QByteArray& data)
{
    QFile out(path);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(data);
    }
}

} // namespace

class MediaAuditTest : public QObject
{
    Q_OBJECT

private slots:
    void exportsRowsWithUsageAndSafeSpreadsheetCells()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString original = dir.filePath(QStringLiteral("original.mov"));
        writeData(original, "video contents");
        QVERIFY(QFileInfo::exists(original));

        Project project = Project::createNew(QStringLiteral("Audit"), ProjectSettings{});
        MediaAsset source;
        source.id = QStringLiteral("asset1");
        source.name = QStringLiteral("=SUM(2,3),\"name\"");
        source.filePath = original;
        source.info.kind = MediaKind::Video;
        source.info.hasVideo = true;
        source.info.durationUs = 20'000'000;
        source.info.fileSize = 4; // deliberately stale to test the changed status
        QVERIFY(project.addAsset(source));
        Clip clip;
        clip.id = QStringLiteral("clip1");
        clip.assetId = source.id;
        clip.start = 0;
        clip.duration = 30;
        QVERIFY(project.timeline().insertClip(project.timeline().tracks().front().id, clip,
                                              project.sourceLookup()));

        MediaAsset missing = source;
        missing.id = QStringLiteral("asset2");
        missing.name = QStringLiteral("@danger");
        missing.filePath = dir.filePath(QStringLiteral("missing.mov"));
        QVERIFY(project.addAsset(missing));
        const Project before = project;
        const QString csv = dir.filePath(QStringLiteral("report.csv"));
        QVERIFY(media_audit::saveCsvReport(project, csv));

        QFile output(csv);
        QVERIFY(output.open(QIODevice::ReadOnly));
        const QByteArray contents = output.readAll();
        QVERIFY(contents.startsWith(QByteArray::fromHex("efbbbf")));
        const QString text = QString::fromUtf8(contents);
        QVERIFY(text.contains(QStringLiteral("assetId,name,kind,status,clipUses")));
        QVERIFY(text.contains(QStringLiteral("\"'=SUM(2,3),\"\"name\"\"\"")));
        QVERIFY(text.contains(QStringLiteral("\"'@danger\"")));
        QVERIFY(text.contains(QStringLiteral("\"changed\",\"1\"")));
        QVERIFY(text.contains(QStringLiteral("\"missing\",\"0\"")));
        QVERIFY(text.contains(QStringLiteral("\r\n")));
        QCOMPARE(project, before);
        QCOMPARE(QFile(original).size(), qint64{14});
    }

    void rejectsUnsafeAndInvalidDestinationsWithoutChanges()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Project project = Project::createNew(QStringLiteral("Audit"), ProjectSettings{});
        MediaAsset asset;
        asset.id = QStringLiteral("source");
        asset.name = QStringLiteral("name");
        asset.filePath = dir.filePath(QStringLiteral("original.csv"));
        asset.info.kind = MediaKind::Video;
        asset.info.hasVideo = true;
        asset.info.durationUs = 10'000'000;
        writeData(asset.filePath, "precious data");
        QVERIFY(project.addAsset(asset));
        const Project before = project;
        QVERIFY(!media_audit::saveCsvReport(project, QStringLiteral("bad.json")));
        QVERIFY(!media_audit::saveCsvReport(project, QStringLiteral("")));
        QVERIFY(!media_audit::saveCsvReport(project, asset.filePath));
        QVERIFY(!media_audit::saveCsvReport(project, dir.filePath(QStringLiteral("project.csv")),
                                            dir.filePath(QStringLiteral("project.csv"))));
        QFile input(asset.filePath);
        QVERIFY(input.open(QIODevice::ReadOnly));
        QCOMPARE(input.readAll(), QByteArray("precious data"));
        QCOMPARE(project, before);
    }
};

QTEST_MAIN(MediaAuditTest)
#include "tst_media_audit.moc"
