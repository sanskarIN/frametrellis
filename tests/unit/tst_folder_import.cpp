// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "media/folder_import.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace ft;

namespace {

void writeFile(const QString& path)
{
    QFile output(path);
    if (output.open(QIODevice::WriteOnly)) {
        output.write("not actual media: discovery only");
    }
}

} // namespace

class FolderImportTest : public QObject
{
    Q_OBJECT

private slots:
    void findsSupportedFilesRecursivelyAndSorts()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QDir root(temp.path());
        QVERIFY(root.mkpath(QStringLiteral("sub")));
        const QString video = root.filePath(QStringLiteral("B.MP4"));
        const QString image = root.filePath(QStringLiteral("sub/फ़ोटो.png"));
        const QString text = root.filePath(QStringLiteral("notes.txt"));
        writeFile(video);
        writeFile(image);
        writeFile(text);
        QVERIFY(QFileInfo::exists(video));
        QVERIFY(QFileInfo::exists(image));

        const Result<media::FolderImportScan> found = media::discoverImportableFiles(temp.path());
        QVERIFY(found);
        QCOMPARE(found->files.size(), 2);
        QCOMPARE(found->files.first(), video);
        QCOMPARE(found->files.last(), image);
        QCOMPARE(found->skippedUnsupported, 1);
        QVERIFY(!found->truncated);
    }

    void respectsLimitWithoutProbingOrChangingFiles()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QDir root(temp.path());
        for (int index = 0; index < 4; ++index) {
            writeFile(root.filePath(QStringLiteral("part%1.wav").arg(index)));
        }
        const Result<media::FolderImportScan> found =
            media::discoverImportableFiles(temp.path(), 2);
        QVERIFY(found);
        QCOMPARE(found->files.size(), 2);
        QVERIFY(found->truncated);
        QCOMPARE(root.entryList(QStringList{QStringLiteral("*.wav")}, QDir::Files).size(), 4);
    }

    void directoryEntriesConsumeTheScanBudget()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QDir root(temp.path());
        for (int index = 0; index < 7; ++index) {
            QVERIFY(root.mkdir(QStringLiteral("empty%1").arg(index)));
        }
        const auto limited = media::discoverImportableFiles(temp.path(), 500, 3);
        QVERIFY(limited);
        QVERIFY(limited->truncated);
        QCOMPARE(limited->visitedEntries, 3);
        QVERIFY(limited->files.isEmpty());
        QCOMPARE(limited->skippedUnsupported, 0);

        const auto exact = media::discoverImportableFiles(temp.path(), 500, 7);
        QVERIFY(exact);
        QVERIFY(!exact->truncated);
        QCOMPARE(exact->visitedEntries, 7);
        QVERIFY(exact->files.isEmpty());
    }

    void mixedEntriesAreCountedBeforeFiltering()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QDir root(temp.path());
        QVERIFY(root.mkdir(QStringLiteral("nested")));
        writeFile(root.filePath(QStringLiteral("nested/clip.mp4")));
        writeFile(root.filePath(QStringLiteral("notes.txt")));
        const auto exact = media::discoverImportableFiles(temp.path(), 500, 3);
        QVERIFY(exact);
        QVERIFY(!exact->truncated);
        QCOMPARE(exact->visitedEntries, 3);
        QCOMPARE(exact->files, QStringList{root.filePath(QStringLiteral("nested/clip.mp4"))});
        QCOMPARE(exact->skippedUnsupported, 1);

        const auto limited = media::discoverImportableFiles(temp.path(), 500, 2);
        QVERIFY(limited);
        QVERIFY(limited->truncated);
        QCOMPARE(limited->visitedEntries, 2);
    }

    void rejectsInvalidLocationAndFileLimits()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QVERIFY(!media::discoverImportableFiles(QStringLiteral("   ")));
        QVERIFY(!media::discoverImportableFiles(temp.filePath(QStringLiteral("missing"))));
        QVERIFY(!media::discoverImportableFiles(temp.path(), 0));
        QVERIFY(!media::discoverImportableFiles(temp.path(), 5001));
        QVERIFY(!media::discoverImportableFiles(temp.path(), 500, 0));
        QVERIFY(!media::discoverImportableFiles(temp.path(), 500, -1));
        QVERIFY(
            !media::discoverImportableFiles(temp.path(), 500, media::kMaxFolderScanEntries + 1));
    }

    void doesNotFollowSymbolicFileLinks()
    {
#ifdef Q_OS_WIN
        QSKIP("Symbolic link creation can require administrator privileges on Windows.");
#else
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QDir root(temp.path());
        const QString original = root.filePath(QStringLiteral("source.mp4"));
        const QString link = root.filePath(QStringLiteral("alias.mp4"));
        writeFile(original);
        QVERIFY(QFile::link(original, link));
        const Result<media::FolderImportScan> found = media::discoverImportableFiles(temp.path());
        QVERIFY(found);
        QCOMPARE(found->files, QStringList{original});
#endif
    }
};

QTEST_GUILESS_MAIN(FolderImportTest)
#include "tst_folder_import.moc"
