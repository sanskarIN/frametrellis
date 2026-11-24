// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "media/media_importer.h"

#include <QCoreApplication>
#include <QSignalSpy>
#include <QTest>
#include <QThreadPool>

using namespace ft;

class MediaImporterTest : public QObject
{
    Q_OBJECT

private:
    using Importer = media::MediaImporter;

    static QStringList failedPaths(const QList<Importer::Failure>& failures)
    {
        QStringList paths;
        for (const auto& failure : failures) {
            paths.append(failure.filePath);
        }
        return paths;
    }

    static void drainWorkers()
    {
        QVERIFY(QThreadPool::globalInstance()->waitForDone(10'000));
        QCoreApplication::processEvents();
    }

private slots:
    // No native MLT initialization is performed. Probe failures are enough
    // to verify the async batching contract without relying on codec timing.
    void emptyRequestDoesNotEmit()
    {
        Importer importer;
        QSignalSpy pending(&importer, &Importer::pendingCountChanged);
        QSignalSpy finished(&importer, &Importer::finished);
        importer.import({});
        importer.cancelAll();
        QCOMPARE(importer.pendingCount(), 0);
        QCOMPARE(pending.count(), 0);
        QCOMPARE(finished.count(), 0);
    }

    void oneBatchReportsEveryFailureInInputOrder()
    {
        Importer importer;
        QSignalSpy pending(&importer, &Importer::pendingCountChanged);
        QSignalSpy finished(&importer, &Importer::finished);
        QStringList received;
        const QStringList paths{QStringLiteral("first.mp4"), QStringLiteral("second.wav"),
                                QStringLiteral("third.png")};
        connect(&importer, &Importer::finished, this,
                [&](const std::vector<MediaAsset>& assets, const QList<Importer::Failure>& errors) {
                    QVERIFY(assets.empty());
                    received = failedPaths(errors);
                });

        importer.import(paths);
        QCOMPARE(importer.pendingCount(), 3);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10'000);
        QCOMPARE(importer.pendingCount(), 0);
        QCOMPARE(received, paths);
        QCOMPARE(pending.count(), 2);
        QCOMPARE(pending.at(0).at(0).toInt(), 3);
        QCOMPARE(pending.at(1).at(0).toInt(), 0);
    }

    void concurrentBatchesBalancePendingCounts()
    {
        Importer importer;
        QSignalSpy pending(&importer, &Importer::pendingCountChanged);
        QSignalSpy finished(&importer, &Importer::finished);
        QStringList received;
        connect(&importer, &Importer::finished, this,
                [&](const std::vector<MediaAsset>& assets, const QList<Importer::Failure>& errors) {
                    QVERIFY(assets.empty());
                    received.append(failedPaths(errors));
                });
        importer.import({QStringLiteral("one.wav"), QStringLiteral("two.wav")});
        importer.import({QStringLiteral("three.wav")});
        QCOMPARE(importer.pendingCount(), 3);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 2, 10'000);
        QCOMPARE(importer.pendingCount(), 0);
        received.sort();
        const QStringList expected{QStringLiteral("one.wav"), QStringLiteral("three.wav"),
                                   QStringLiteral("two.wav")};
        QCOMPARE(received, expected);
        QCOMPARE(pending.count(), 4);
        QCOMPARE(pending.at(0).at(0).toInt(), 2);
        QCOMPARE(pending.at(1).at(0).toInt(), 3);
        QCOMPARE(pending.last().at(0).toInt(), 0);
    }

    void cancellationDuringSubmissionDoesNotDeliverOldBatch()
    {
        Importer importer;
        QSignalSpy finished(&importer, &Importer::finished);
        QStringList received;
        bool replaced = false;
        connect(&importer, &Importer::pendingCountChanged, this, [&](int pending) {
            if (!replaced && pending == 2) {
                replaced = true;
                importer.cancelAll();
                importer.import({QStringLiteral("fresh.wav")});
            }
        });
        connect(&importer, &Importer::finished, this,
                [&](const std::vector<MediaAsset>&, const QList<Importer::Failure>& errors) {
                    received.append(failedPaths(errors));
                });

        importer.import({QStringLiteral("old1.wav"), QStringLiteral("old2.wav")});
        QVERIFY(replaced);
        QCOMPARE(importer.pendingCount(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10'000);
        drainWorkers();
        QCOMPARE(finished.count(), 1);
        QCOMPARE(received, QStringList{QStringLiteral("fresh.wav")});
        QCOMPARE(importer.pendingCount(), 0);
    }

    void cancellationDuringCompletionDoesNotDeliverOldBatch()
    {
        Importer importer;
        QSignalSpy finished(&importer, &Importer::finished);
        QStringList received;
        bool replaced = false;
        connect(&importer, &Importer::pendingCountChanged, this, [&](int pending) {
            if (!replaced && pending == 0) {
                replaced = true;
                importer.cancelAll();
                importer.import({QStringLiteral("new-project.wav")});
            }
        });
        connect(&importer, &Importer::finished, this,
                [&](const std::vector<MediaAsset>&, const QList<Importer::Failure>& errors) {
                    received.append(failedPaths(errors));
                });

        importer.import({QStringLiteral("old-project.wav")});
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10'000);
        drainWorkers();
        QVERIFY(replaced);
        QCOMPARE(finished.count(), 1);
        QCOMPARE(received, QStringList{QStringLiteral("new-project.wav")});
        QCOMPARE(importer.pendingCount(), 0);
    }

    void cancelledPendingBatchesCannotDecrementNewGeneration()
    {
        Importer importer;
        QSignalSpy finished(&importer, &Importer::finished);
        QStringList received;
        connect(&importer, &Importer::finished, this,
                [&](const std::vector<MediaAsset>&, const QList<Importer::Failure>& errors) {
                    received.append(failedPaths(errors));
                });

        importer.import({QStringLiteral("old.wav"), QStringLiteral("old2.wav")});
        importer.import({QStringLiteral("old3.wav")});
        QCOMPARE(importer.pendingCount(), 3);
        importer.cancelAll();
        importer.cancelAll();
        QCOMPARE(importer.pendingCount(), 0);
        importer.import({QStringLiteral("new.wav")});
        QCOMPARE(importer.pendingCount(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 10'000);
        drainWorkers();
        QCOMPARE(finished.count(), 1);
        QCOMPARE(received, QStringList{QStringLiteral("new.wav")});
        QCOMPARE(importer.pendingCount(), 0);
    }

    void destructionDoesNotNotifyObserversFromPartiallyDestroyedObject()
    {
        int notifications = 0;
        {
            Importer importer;
            connect(&importer, &Importer::pendingCountChanged, this,
                    [&](int) { ++notifications; });
            importer.import({QStringLiteral("abandoned.wav")});
            QCOMPARE(notifications, 1);
        }
        drainWorkers();
        QCOMPARE(notifications, 1);
    }
};

QTEST_GUILESS_MAIN(MediaImporterTest)
#include "tst_media_importer.moc"
