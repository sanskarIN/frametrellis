// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "engine/mlt_runtime.h"
#include "media/folder_relink.h"

#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

using namespace ft;

namespace {

bool writeStereoWav(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    constexpr quint32 samples = 4800;
    constexpr quint32 dataBytes = samples * 2 * 2;
    QDataStream stream(&file);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream.writeRawData("RIFF", 4);
    stream << quint32(36 + dataBytes);
    stream.writeRawData("WAVEfmt ", 8);
    stream << quint32(16) << quint16(1) << quint16(2) << quint32(48000) << quint32(48000 * 4)
           << quint16(4) << quint16(16);
    stream.writeRawData("data", 4);
    stream << dataBytes;
    const QByteArray silence(dataBytes, '\0');
    return stream.writeRawData(silence.constData(), silence.size()) == silence.size() &&
           stream.status() == QDataStream::Ok;
}

MediaAsset missing(const QString& id, const QString& name)
{
    MediaAsset asset;
    asset.id = id;
    asset.name = name;
    asset.filePath = QStringLiteral("/missing/") + name;
    asset.info.kind = MediaKind::Audio;
    asset.info.hasAudio = true;
    asset.info.audioChannels = 2;
    asset.status = MediaStatus::Missing;
    return asset;
}

} // namespace

class FolderRelinkTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase() { QVERIFY(engine::MltRuntime::initialize()); }

    void identifiesUniqueMatchingAudioWithoutChangingSourceOrAsset()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QDir root(temp.path());
        QVERIFY(root.mkpath(QStringLiteral("nested")));
        const QString candidate = root.filePath(QStringLiteral("nested/music.wav"));
        QVERIFY(writeStereoWav(candidate));
        MediaAsset offline = missing(QStringLiteral("music"), QStringLiteral("music.wav"));
        const MediaAsset before = offline;
        const auto result = media::discoverRelinkCandidates(temp.path(), {offline});
        QVERIFY2(result, qPrintable(result ? QString() : result.error().toString()));
        QCOMPARE(result->missingSearched, 1);
        QCOMPARE(result->unmatched, 0);
        QCOMPARE(result->ambiguous, 0);
        QCOMPARE(result->candidates.size(), size_t{1});
        QCOMPARE(result->candidates[0].filePath, candidate);
        QCOMPARE(result->candidates[0].status, MediaStatus::Online);
        QCOMPARE(result->candidates[0].info.audioChannels, 2);
        QCOMPARE(offline, before);
        QVERIFY(QFileInfo::exists(candidate));
    }

    void refusesAmbiguousMatchesInEitherDirection()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        QDir root(temp.path());
        QVERIFY(root.mkpath(QStringLiteral("a")));
        QVERIFY(root.mkpath(QStringLiteral("b")));
        for (const QString& folder : {QStringLiteral("a"), QStringLiteral("b")}) {
            QFile fake(root.filePath(folder + QStringLiteral("/same.wav")));
            QVERIFY(fake.open(QIODevice::WriteOnly));
            QCOMPARE(fake.write("discovery only"), qint64{14});
        }
        const MediaAsset original = missing(QStringLiteral("id"), QStringLiteral("same.wav"));
        const auto ambiguous = media::discoverRelinkCandidates(temp.path(), {original});
        QVERIFY(ambiguous);
        QCOMPARE(ambiguous->ambiguous, 1);
        QVERIFY(ambiguous->candidates.empty());

        QVERIFY(QFile::remove(root.filePath(QStringLiteral("b/same.wav"))));
        const MediaAsset second = missing(QStringLiteral("id2"), QStringLiteral("same.wav"));
        const auto duplicateNames =
            media::discoverRelinkCandidates(temp.path(), {original, second});
        QVERIFY(duplicateNames);
        QCOMPARE(duplicateNames->ambiguous, 2);
        QVERIFY(duplicateNames->candidates.empty());
    }

    void truncatedScanNeverRelinksAnUnverifiedFilename()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const QDir root(temp.path());
        const QString candidate = root.filePath(QStringLiteral("target.wav"));
        QVERIFY(writeStereoWav(candidate));
        // One more supported file than the bounded folder scanner accepts.
        // A duplicate filename could be beyond the limit, so even an apparent
        // single match must be rejected until the user narrows the folder.
        for (int i = 0; i < 501; ++i) {
            QFile file(root.filePath(QStringLiteral("filler%1.wav").arg(i, 4, 10,
                                                                        QLatin1Char('0'))));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("dummy"), qint64{5});
        }
        const MediaAsset offline = missing(QStringLiteral("t"), QStringLiteral("target.wav"));
        const auto result = media::discoverRelinkCandidates(temp.path(), {offline});
        QVERIFY(result);
        QVERIFY(result->truncated);
        QCOMPARE(result->missingSearched, 1);
        QCOMPARE(result->skippedTruncated, 1);
        QVERIFY(result->candidates.empty());
        QVERIFY(QFileInfo::exists(candidate));
    }

    void reportsMissingAndBadCandidateSafely()
    {
        QTemporaryDir temp;
        QVERIFY(temp.isValid());
        const MediaAsset original = missing(QStringLiteral("music"), QStringLiteral("music.wav"));
        const auto unmatched = media::discoverRelinkCandidates(temp.path(), {original});
        QVERIFY(unmatched);
        QCOMPARE(unmatched->unmatched, 1);
        QVERIFY(unmatched->candidates.empty());

        QFile corrupt(QDir(temp.path()).filePath(QStringLiteral("music.wav")));
        QVERIFY(corrupt.open(QIODevice::WriteOnly));
        QCOMPARE(corrupt.write("not real audio"), qint64{14});
        corrupt.close();
        const auto invalid = media::discoverRelinkCandidates(temp.path(), {original});
        QVERIFY(invalid);
        QCOMPARE(invalid->probeFailed, 1);
        QVERIFY(invalid->candidates.empty());
        QVERIFY(
            !media::discoverRelinkCandidates(temp.filePath(QStringLiteral("missing")), {original}));
    }
};

QTEST_GUILESS_MAIN(FolderRelinkTest)
#include "tst_folder_relink.moc"
