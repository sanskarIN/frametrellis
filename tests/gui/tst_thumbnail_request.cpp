// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors

#include "ui/thumbnail_provider.h"

#include <QTest>

using namespace ft::ui;

class ThumbnailRequestTest : public QObject
{
    Q_OBJECT

private slots:
    void keepsNormalRequestsAtOriginalResolution()
    {
        QCOMPARE(ThumbnailProvider::boundedRequestSize(QSize(192, 108)), QSize(192, 108));
        QCOMPARE(ThumbnailProvider::boundedRequestSize(QSize(960, 540)), QSize(960, 540));
        QCOMPARE(ThumbnailProvider::boundedRequestSize(QSize(1, 1)), QSize(1, 1));
        QCOMPARE(ThumbnailProvider::boundedRequestSize(QSize(1920, 1080)), QSize(1920, 1080));
    }

    void rejectsInvalidDimensionsWithSmallFallback()
    {
        QCOMPARE(ThumbnailProvider::boundedRequestSize(QSize()), QSize(192, 108));
        QCOMPARE(ThumbnailProvider::boundedRequestSize(QSize(-1, 1080)), QSize(192, 108));
        QCOMPARE(ThumbnailProvider::boundedRequestSize(QSize(200, 0)), QSize(192, 108));
    }

    void limitsExtremeRequestsWithoutStretching()
    {
        QCOMPARE(ThumbnailProvider::boundedRequestSize(QSize(3840, 2160)), QSize(1920, 1080));
        QCOMPARE(ThumbnailProvider::boundedRequestSize(QSize(4000, 2000)), QSize(1920, 960));
        const QSize square = ThumbnailProvider::boundedRequestSize(QSize(100'000, 100'000));
        QCOMPARE(square, QSize(1080, 1080));

        for (const QSize& size : {QSize(100'000, 100'000), QSize(100'000, 1000),
                                  QSize(1000, 100'000), QSize(1920, 100'000)}) {
            const QSize bounded = ThumbnailProvider::boundedRequestSize(size);
            QVERIFY2(bounded.width() >= 1 && bounded.width() <= 1920, "width out of range");
            QVERIFY2(bounded.height() >= 1 && bounded.height() <= 1080, "height out of range");
        }
    }
};

QTEST_GUILESS_MAIN(ThumbnailRequestTest)
#include "tst_thumbnail_request.moc"
