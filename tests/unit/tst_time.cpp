// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "core/framerate.h"
#include "core/rational.h"
#include "core/timecode.h"

#include <QTest>

#include <limits>

using namespace ft;

class TimeTest : public QObject
{
    Q_OBJECT

private slots:
    void rationalNormalises()
    {
        const Rational r(60000, 2002);
        QCOMPARE(r.numerator(), 30000);
        QCOMPARE(r.denominator(), 1001);
        QCOMPARE(Rational(3, -6), Rational(-1, 2));
        QVERIFY(!Rational(1, 0).isValid());
        QVERIFY(Rational(24000, 1001) < Rational(24, 1));
        QVERIFY(Rational(30, 1) > Rational(30000, 1001));
    }

    void rationalParsing_data()
    {
        QTest::addColumn<QString>("text");
        QTest::addColumn<qint64>("num");
        QTest::addColumn<qint64>("den");
        QTest::newRow("fraction") << QStringLiteral("30000/1001") << qint64(30000) << qint64(1001);
        QTest::newRow("integer") << QStringLiteral("25") << qint64(25) << qint64(1);
        QTest::newRow("ntsc decimal") << QStringLiteral("29.97") << qint64(30000) << qint64(1001);
        QTest::newRow("film decimal") << QStringLiteral("23.976") << qint64(24000) << qint64(1001);
        QTest::newRow("59.94") << QStringLiteral("59.94") << qint64(60000) << qint64(1001);
        QTest::newRow("plain decimal") << QStringLiteral("12.5") << qint64(25) << qint64(2);
    }

    void rationalParsing()
    {
        QFETCH(QString, text);
        QFETCH(qint64, num);
        QFETCH(qint64, den);
        const auto parsed = Rational::fromString(text);
        QVERIFY(parsed.has_value());
        QCOMPARE(parsed->numerator(), num);
        QCOMPARE(parsed->denominator(), den);
    }

    void rationalRejectsGarbage()
    {
        QVERIFY(!Rational::fromString(QString()));
        QVERIFY(!Rational::fromString(QStringLiteral("abc")));
        QVERIFY(!Rational::fromString(QStringLiteral("1/0")));
        QVERIFY(!Rational::fromString(QStringLiteral("1/2/3")));
        QVERIFY(!Rational::fromString(QStringLiteral("-5.5")));
    }

    void mulDivRoundsAndDetectsOverflow()
    {
        QCOMPARE(*mulDiv(10, 3, 4, Rounding::Down), 7);
        QCOMPARE(*mulDiv(10, 3, 4, Rounding::Up), 8);
        QCOMPARE(*mulDiv(10, 3, 4, Rounding::Nearest), 8); // 7.5 rounds away from zero
        QCOMPARE(*mulDiv(-10, 3, 4, Rounding::Down), -8);
        QCOMPARE(*mulDiv(-10, 3, 4, Rounding::Up), -7);
        QVERIFY(!mulDiv(1, 1, 0).has_value());
        QVERIFY(!mulDiv(std::numeric_limits<std::int64_t>::max(), 3, 1).has_value());
        // A large value that would overflow a naive a*b but fits after division.
        const std::int64_t big = std::int64_t{1} << 40;
        QCOMPARE(*mulDiv(big, 1'000'000, 1'000'000), big);
    }

    void frameRateValidation()
    {
        QVERIFY(FrameRate::create(30000, 1001).has_value());
        QVERIFY(!FrameRate::create(0, 1).has_value());
        QVERIFY(!FrameRate::create(30, 0).has_value());
        QVERIFY(!FrameRate::create(-30, 1).has_value());
        QVERIFY(!FrameRate::create(5000, 1).has_value()); // above 1000 fps
        QCOMPARE(FrameRate::standardRates().size(), 9);
        QCOMPARE(FrameRate::create(24000, 1001)->nominalFramesPerSecond(), 24);
        QCOMPARE(FrameRate::create(25, 1)->nominalFramesPerSecond(), 25);
        QVERIFY(FrameRate::create(60000, 1001)->supportsDropFrame());
        QVERIFY(!FrameRate::create(25, 1)->supportsDropFrame());
        QCOMPARE(FrameRate::create(30000, 1001)->displayName(), QStringLiteral("29.97 fps"));
        QCOMPARE(FrameRate::create(25, 1)->displayName(), QStringLiteral("25 fps"));
    }

    void frameTimeConversions()
    {
        const FrameRate ntsc = *FrameRate::create(30000, 1001);
        // 30000 frames at 29.97 fps are exactly 1001 seconds.
        QCOMPARE(*ntsc.framesToMicroseconds(30000), std::int64_t{1'001'000'000});
        QCOMPARE(*ntsc.microsecondsToFrames(1'001'000'000), FrameCount{30000});
        const FrameRate pal = *FrameRate::create(25, 1);
        QCOMPARE(*pal.framesToMicroseconds(1), std::int64_t{40'000});
        QCOMPARE(*pal.microsecondsToFrames(39'999, Rounding::Down), FrameCount{0});
        QCOMPARE(*pal.microsecondsToFrames(39'999, Rounding::Nearest), FrameCount{1});
        QCOMPARE(pal.framesToSeconds(50), 2.0);
        // Round trip over a long range stays exact.
        for (FrameCount f : {FrameCount{0}, FrameCount{1}, FrameCount{1799}, FrameCount{107892},
                             FrameCount{2'589'408}}) {
            const auto us = ntsc.framesToMicroseconds(f);
            QVERIFY(us.has_value());
            QCOMPARE(*ntsc.microsecondsToFrames(*us, Rounding::Nearest), f);
        }
    }

    void nonDropFrameTimecode()
    {
        const FrameRate pal = *FrameRate::create(25, 1);
        QCOMPARE(timecode::format(0, pal), QStringLiteral("00:00:00:00"));
        QCOMPARE(timecode::format(24, pal), QStringLiteral("00:00:00:24"));
        QCOMPARE(timecode::format(25, pal), QStringLiteral("00:00:01:00"));
        QCOMPARE(timecode::format(90000, pal), QStringLiteral("01:00:00:00"));
        QCOMPARE(timecode::format(-25, pal), QStringLiteral("-00:00:01:00"));
        QCOMPARE(*timecode::parse(QStringLiteral("01:00:00:00"), pal), FrameCount{90000});
        QCOMPARE(*timecode::parse(QStringLiteral("2:10"), pal), FrameCount{60});
        QCOMPARE(*timecode::parse(QStringLiteral("1234"), pal), FrameCount{1234});
        QVERIFY(!timecode::parse(QStringLiteral("00:00:00:25"), pal)); // FF out of range
        QVERIFY(!timecode::parse(QStringLiteral("00:61:00:00"), pal));
        QVERIFY(!timecode::parse(QStringLiteral("00:00:00;10"), pal)); // drop-frame at 25 fps
        QVERIFY(!timecode::parse(QStringLiteral("a:b"), pal));
    }

    void dropFrameTimecode_data()
    {
        QTest::addColumn<qint64>("frame");
        QTest::addColumn<QString>("label");
        QTest::newRow("start") << qint64(0) << QStringLiteral("00:00:00;00");
        QTest::newRow("before first drop") << qint64(1799) << QStringLiteral("00:00:59;29");
        QTest::newRow("first drop") << qint64(1800) << QStringLiteral("00:01:00;02");
        QTest::newRow("ten minutes") << qint64(17982) << QStringLiteral("00:10:00;00");
        QTest::newRow("one hour") << qint64(107892) << QStringLiteral("01:00:00;00");
        QTest::newRow("end of minute ten") << qint64(19780) << QStringLiteral("00:10:59;28");
        QTest::newRow("eleven minutes") << qint64(19782) << QStringLiteral("00:11:00;02");
    }

    void dropFrameTimecode()
    {
        QFETCH(qint64, frame);
        QFETCH(QString, label);
        const FrameRate ntsc = *FrameRate::create(30000, 1001);
        QCOMPARE(timecode::format(frame, ntsc), label);
        QCOMPARE(*timecode::parse(label, ntsc), FrameCount{frame});
        // Explicit non-drop style counts every frame.
        QVERIFY(timecode::format(frame, ntsc, timecode::Style::NonDropFrame)
                    .contains(QLatin1Char(':')));
    }

    void dropFrameRejectsSkippedLabels()
    {
        const FrameRate ntsc = *FrameRate::create(30000, 1001);
        QVERIFY(!timecode::parse(QStringLiteral("00:01:00;00"), ntsc));
        QVERIFY(!timecode::parse(QStringLiteral("00:01:00;01"), ntsc));
        QVERIFY(timecode::parse(QStringLiteral("00:10:00;00"), ntsc).has_value());
    }

    void dropFrameRoundTripIsExact()
    {
        for (const auto& [num, den] : {std::pair{30000, 1001}, std::pair{60000, 1001}}) {
            const FrameRate rate = *FrameRate::create(num, den);
            for (FrameCount f = 0; f < 200'000; f += 7) {
                const QString label = timecode::format(f, rate);
                const auto parsed = timecode::parse(label, rate);
                QVERIFY2(parsed.has_value(), qPrintable(label));
                QCOMPARE(*parsed, f);
            }
        }
    }

    void clockFormat()
    {
        const FrameRate pal = *FrameRate::create(25, 1);
        QCOMPARE(timecode::formatClock(0, pal), QStringLiteral("0:00.000"));
        QCOMPARE(timecode::formatClock(101, pal), QStringLiteral("0:04.040"));
        QCOMPARE(timecode::formatClock(90000 + 25 * 62, pal), QStringLiteral("1:01:02.000"));
    }
};

QTEST_GUILESS_MAIN(TimeTest)
#include "tst_time.moc"
