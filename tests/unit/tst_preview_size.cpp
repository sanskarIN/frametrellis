// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors

#include "engine/mlt_profile.h"
#include "project/project_settings.h"

#include <QTest>

#include <limits>

using ft::engine::fitPreviewSize;

class PreviewSizeTest : public QObject
{
    Q_OBJECT

private slots:
    void keepsAlreadyBoundedFrames()
    {
        QCOMPARE(fitPreviewSize(QSize(192, 108), QSize(320, 180)), QSize(192, 108));
        QCOMPARE(fitPreviewSize(QSize(15, 9), QSize(15, 9)), QSize(15, 9));
        QCOMPARE(fitPreviewSize(QSize(1, 1), QSize(1, 1)), QSize(1, 1));
    }

    void fitsLandscapeAndPortrait()
    {
        QCOMPARE(fitPreviewSize(QSize(1920, 1080), QSize(960, 540)), QSize(960, 540));
        QCOMPARE(fitPreviewSize(QSize(1080, 1920), QSize(960, 540)), QSize(304, 540));
        QCOMPARE(fitPreviewSize(QSize(1920, 1080), QSize(320, 320)), QSize(320, 180));
        QCOMPARE(fitPreviewSize(QSize(1080, 1920), QSize(320, 320)), QSize(180, 320));
    }

    void respectsOddAndTinyLimits()
    {
        QCOMPARE(fitPreviewSize(QSize(1920, 1080), QSize(81, 81)), QSize(80, 46));
        QCOMPARE(fitPreviewSize(QSize(1080, 1920), QSize(81, 81)), QSize(46, 80));
        QCOMPARE(fitPreviewSize(QSize(1920, 1080), QSize(1, 1)), QSize(1, 1));
        QCOMPARE(fitPreviewSize(QSize(1, 100), QSize(1, 20)), QSize(1, 20));
    }

    void capsInvalidAndHugeMetadata()
    {
        const int extreme = std::numeric_limits<int>::max();
        const int cap = ft::kMaxFrameDimension;
        QCOMPARE(fitPreviewSize(QSize(), QSize()), QSize(2, 2));
        QCOMPARE(fitPreviewSize(QSize(), QSize(1, 1)), QSize(1, 1));
        QCOMPARE(fitPreviewSize(QSize(extreme, extreme), QSize()), QSize(cap, cap));
        QCOMPARE(fitPreviewSize(QSize(extreme, 1), QSize(extreme, extreme)), QSize(cap, 1));
        QCOMPARE(fitPreviewSize(QSize(1, extreme), QSize(extreme, extreme)), QSize(1, cap));
    }

    void scaledFramesStayPositiveAndInsideEveryBound()
    {
        const int extreme = std::numeric_limits<int>::max();
        const QSize sources[] = {QSize(1920, 1080), QSize(100, 101), QSize(1, 5000),
                                 QSize(5000, 1), QSize(extreme, extreme - 1)};
        const QSize limits[] = {QSize(1, 1), QSize(2, 3), QSize(81, 79), QSize(1920, 1080),
                                QSize(extreme, extreme)};
        for (const QSize source : sources) {
            for (const QSize limit : limits) {
                const QSize result = fitPreviewSize(source, limit);
                QVERIFY(result.width() > 0);
                QVERIFY(result.height() > 0);
                QVERIFY(result.width() <= source.width());
                QVERIFY(result.height() <= source.height());
                QVERIFY(result.width() <= limit.width());
                QVERIFY(result.height() <= limit.height());
                QVERIFY(result.width() <= ft::kMaxFrameDimension);
                QVERIFY(result.height() <= ft::kMaxFrameDimension);
            }
        }
    }
};

QTEST_GUILESS_MAIN(PreviewSizeTest)
#include "tst_preview_size.moc"
