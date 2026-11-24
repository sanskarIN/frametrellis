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

    void rationalHandlesInt64Limits()
    {
        const auto minimum = std::numeric_limits<std::int64_t>::min();
        const auto maximum = std::numeric_limits<std::int64_t>::max();

        // Normalization must never negate INT64_MIN or take its signed absolute value.
        QCOMPARE(Rational(minimum, 1).numerator(), minimum);
        QCOMPARE(Rational(minimum, 1).denominator(), std::int64_t{1});
        QCOMPARE(Rational(minimum, minimum), Rational(1, 1));
        QCOMPARE(Rational(minimum, -2), Rational(std::int64_t{1} << 62, 1));
        QCOMPARE(Rational(-2, minimum), Rational(1, std::int64_t{1} << 62));
        QCOMPARE(Rational(0, minimum), Rational(0, 1));
        QCOMPARE(Rational(minimum, maximum).numerator(), minimum);

        // Mathematically valid fractions are invalid as Rational if their normalized
        // numerator or positive denominator cannot fit in signed 64-bit storage.
        QVERIFY(!Rational(minimum, -1).isValid());
        QVERIFY(!Rational(1, minimum).isValid());
        QVERIFY(!Rational(-1, minimum).isValid());
    }

    void rationalComparisonRemainsExactAtInt64Limits()
    {
        const auto max = std::numeric_limits<std::int64_t>::max();
        const auto min = std::numeric_limits<std::int64_t>::min();

        // Both cross-products exceed int64 and differ by just one.
        // A floating-point fallback can incorrectly report equality on
        // Windows, where long double has the same precision as double.
        const Rational larger(max - 1, max);
        const Rational smaller(max - 2, max - 1);
        QVERIFY(larger > smaller);
        QVERIFY(smaller < larger);
        QVERIFY(!(larger == smaller));
        QVERIFY((larger <=> larger) == std::strong_ordering::equal);

        // Exact ordering must also reverse for negative magnitudes.
        const Rational negativeLarger(-(max - 1), max);
        const Rational negativeSmaller(-(max - 2), max - 1);
        QVERIFY(negativeLarger < negativeSmaller);
        QVERIFY(negativeSmaller > negativeLarger);
        QVERIFY(Rational(min, max) < Rational(-1, 1));
        QVERIFY(Rational(min, max) < Rational(0, 1));
        QVERIFY(Rational(0, 1) < larger);

        // Fractions near one must be ordered without losing the low bit of
        // a large cross-product on any supported compiler.
        QVERIFY(Rational(max, max - 1) > Rational(1, 1));
        QVERIFY(Rational(-max, max - 1) < Rational(-1, 1));

        // Invalid values have a normalised 0/0 representation and must not
        // compare equal to a valid zero. Keep ordering compatible with ==.
        const Rational invalid(1, 0);
        QVERIFY(!invalid.isValid());
        QVERIFY(invalid < Rational(0, 1));
        QVERIFY(invalid < larger);
        QVERIFY(invalid == Rational());
        QVERIFY((invalid <=> Rational()) == std::strong_ordering::equal);
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
        QTest::newRow("ntsc trailing zeros")
            << QStringLiteral("29.9700") << qint64(30000) << qint64(1001);
        QTest::newRow("neighbouring custom rate")
            << QStringLiteral("29.974") << qint64(14987) << qint64(500);
        QTest::newRow("four-place custom rate")
            << QStringLiteral("29.9701") << qint64(299701) << qint64(10000);
        QTest::newRow("six-place decimal")
            << QStringLiteral("0.000001") << qint64(1) << qint64(1000000);
        QTest::newRow("near film rate")
            << QStringLiteral("23.981") << qint64(23981) << qint64(1000);
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
        // Never round away a precision digit that the user explicitly entered.
        QVERIFY(!Rational::fromString(QStringLiteral("29.9700012")));
        // Fraction parsing must not report an unrepresentable Rational as a valid result.
        QVERIFY(!Rational::fromString(QStringLiteral("1/-9223372036854775808")));
        QVERIFY(!Rational::fromString(QStringLiteral("-9223372036854775808/-1")));
        const auto reduced =
            Rational::fromString(QStringLiteral("-9223372036854775808/-9223372036854775808"));
        QVERIFY(reduced.has_value());
        QCOMPARE(*reduced, Rational(1, 1));
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

        // A negative divisor must not negate INT64_MIN (undefined behavior).
        const auto minimum = std::numeric_limits<std::int64_t>::min();
        const auto maximum = std::numeric_limits<std::int64_t>::max();
        // MSVC uses the portable checked-multiply fallback. INT64_MIN is
        // representable and must not be mistaken for an overflow.
        QCOMPARE(*mulDiv(minimum, 1, 1), minimum);
        QCOMPARE(*mulDiv(1, minimum, 1), minimum);
        QCOMPARE(*mulDiv(minimum / 2, 2, 1), minimum);
        QCOMPARE(*mulDiv(minimum, 2, 2), minimum);
        // The immediately more-negative product does not fit.
        QVERIFY(!mulDiv(minimum, 2, 1).has_value());
        QCOMPARE(*mulDiv(minimum, 1, -2), std::int64_t{1} << 62);
        QCOMPARE(*mulDiv(-7, 3, -2, Rounding::Down), std::int64_t{10});
        QCOMPARE(*mulDiv(-7, 3, -2, Rounding::Up), std::int64_t{11});
        QVERIFY(!mulDiv(minimum, 1, -1).has_value());
        // Full-width divisors and numerators are now supported; the
        // mathematically rounded result, not an intermediate, sets the range.
        QCOMPARE(*mulDiv(1, 1, minimum, Rounding::Down), std::int64_t{-1});
        QCOMPARE(*mulDiv(1, 1, minimum, Rounding::Up), std::int64_t{0});
        QCOMPARE(*mulDiv(1, 1, minimum, Rounding::Nearest), std::int64_t{0});
        QCOMPARE(*mulDiv(minimum, 1, minimum), std::int64_t{1});
        QCOMPARE(*mulDiv(1, minimum, minimum), std::int64_t{1});
        QCOMPARE(*mulDiv(minimum, minimum, minimum), minimum);
        QVERIFY(!mulDiv(minimum, minimum, -1).has_value());

        // Formerly, r*b overflowed even when a*b/c was representable:
        // e.g. these coprime near-trillion ratios describe almost 1:1.
        constexpr std::int64_t hugeNumerator = 999'983LL * 999'979;
        constexpr std::int64_t hugeDenominator = 999'971LL * 999'959;
        QCOMPARE(*mulDiv(100'000'000, hugeNumerator, hugeDenominator, Rounding::Down),
                 std::int64_t{100'003'200});
        QCOMPARE(*mulDiv(100'000'000, hugeNumerator, hugeDenominator, Rounding::Up),
                 std::int64_t{100'003'201});
        QCOMPARE(*mulDiv(-100'000'000, hugeNumerator, hugeDenominator, Rounding::Down),
                 std::int64_t{-100'003'201});
        QCOMPARE(*mulDiv(-100'000'000, hugeNumerator, hugeDenominator, Rounding::Up),
                 std::int64_t{-100'003'200});
        QCOMPARE(*mulDiv(maximum, maximum, maximum), maximum);
        QVERIFY(!mulDiv(maximum, maximum, maximum - 1).has_value());

        // Nearest rounding must not double a remainder near INT64_MAX.
        QCOMPARE(*mulDiv(maximum - 1, 1, maximum, Rounding::Nearest), std::int64_t{1});
        QCOMPARE(*mulDiv(-(maximum - 1), 1, maximum, Rounding::Nearest), std::int64_t{-1});
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
        QCOMPARE(FrameRate::create(24000, 1001)->displayName(), QStringLiteral("23.976 fps"));
        QCOMPARE(FrameRate::create(60000, 1001)->displayName(), QStringLiteral("59.94 fps"));
        QCOMPARE(FrameRate::create(14987, 500)->displayName(), QStringLiteral("29.974 fps"));
        QCOMPARE(FrameRate::create(299701, 10000)->displayName(), QStringLiteral("29.9701 fps"));
        QCOMPARE(FrameRate::create(1, 1000000)->displayName(), QStringLiteral("0.000001 fps"));
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

    void dropFrameSeparatorMustPrecedeTheFrameField()
    {
        // An early semicolon used to be treated as a global drop-frame flag,
        // so malformed HH;MM:SS:FF was silently accepted as HH:MM:SS;FF.
        for (const auto& [num, den] : {std::pair{30000, 1001}, std::pair{60000, 1001}}) {
            const FrameRate rate = *FrameRate::create(num, den);
            QCOMPARE(*timecode::parse(QStringLiteral("00:10:00;00"), rate),
                     FrameCount{num == 30000 ? 17'982 : 35'964});
            for (const QString& invalid :
                 {QStringLiteral("00;10:00:00"), QStringLiteral("00:10;00:00"),
                  QStringLiteral("00:10;00.00"), QStringLiteral("00:10:00;00;00"),
                  QStringLiteral("00:10:00;00:00")}) {
                QVERIFY2(!timecode::parse(invalid, rate).has_value(), qPrintable(invalid));
            }
        }

        // The ordinary non-drop-frame notation must not gain a drop-frame flag.
        const FrameRate pal = FrameRate::integer(25);
        QVERIFY(!timecode::parse(QStringLiteral("00:10:00;00"), pal));
        QCOMPARE(*timecode::parse(QStringLiteral("00:10:00:00"), pal), FrameCount{15'000});
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

    void extremeFrameCountsAreSafe()
    {
        const FrameRate pal = *FrameRate::create(25, 1);
        const FrameRate ntsc = *FrameRate::create(30000, 1001);
        const FrameCount minimum = std::numeric_limits<FrameCount>::min();
        const FrameCount maximum = std::numeric_limits<FrameCount>::max();

        // Neither abs(INT64_MIN) nor drop-frame label insertion may overflow.
        const QString minPal = timecode::format(minimum, pal);
        QVERIFY(minPal.startsWith(QLatin1Char('-')));
        QVERIFY(minPal.contains(QLatin1Char(':')));
        const QString maxPal = timecode::format(maximum, pal);
        QVERIFY(!maxPal.startsWith(QLatin1Char('-')));
        const QString minNtsc = timecode::format(minimum, ntsc);
        QVERIFY(minNtsc.startsWith(QLatin1Char('-')));
        QVERIFY(minNtsc.contains(QLatin1Char(';')));
        const QString maxNtsc = timecode::format(maximum, ntsc);
        QVERIFY(maxNtsc.contains(QLatin1Char(';')));
        QVERIFY(!maxNtsc.startsWith(QLatin1Char('-')));

        // Clock units overflow for such extreme inputs; show an explicit unavailable marker.
        QCOMPARE(timecode::formatClock(minimum, pal), QStringLiteral("--"));
        QCOMPARE(timecode::formatClock(maximum, pal), QStringLiteral("--"));
        QCOMPARE(timecode::formatClock(-42, pal), QStringLiteral("-0:01.680"));
    }

    void clockFormattingNearInt64LimitDoesNotOverflow()
    {
        // At 999 fps, this valid frame count converts to INT64_MAX - 31
        // microseconds. Adding 500 before division used to overflow signed
        // int64_t, despite the rounded millisecond count being representable.
        const FrameRate rate = FrameRate::integer(999);
        constexpr FrameCount frames = 9'214'148'664'817'921;
        const auto micros = rate.framesToMicroseconds(frames);
        QVERIFY(micros.has_value());
        QCOMPARE(*micros, std::numeric_limits<std::int64_t>::max() - 31);
        QCOMPARE(timecode::formatClock(frames, rate), QStringLiteral("2562047788:00:54.776"));

        // Ordinary clock formatting and its negative sign are unaffected.
        QCOMPARE(timecode::formatClock(101, FrameRate::integer(25)), QStringLiteral("0:04.040"));
        QCOMPARE(timecode::formatClock(-101, FrameRate::integer(25)), QStringLiteral("-0:04.040"));
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
