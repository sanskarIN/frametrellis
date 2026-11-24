// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "core/rational.h"

#include <QStringList>

#include <cmath>
#include <limits>
#include <numeric>

namespace ft {

namespace {

bool mulOverflows(std::int64_t a, std::int64_t b, std::int64_t* out)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_mul_overflow(a, b, out);
#else
    if (a != 0 && b != 0) {
        const auto maxValue = std::numeric_limits<std::int64_t>::max();
        const auto minValue = std::numeric_limits<std::int64_t>::min();
        // Signed int64 has one more negative value than positive values.
        // Using -maxValue as the lower bound incorrectly rejects valid products
        // such as INT64_MIN * 1 on MSVC (which lacks __builtin_mul_overflow).
        if ((a > 0 && b > 0 && a > maxValue / b) || (a < 0 && b < 0 && a < maxValue / b) ||
            (a > 0 && b < 0 && b < minValue / a) || (a < 0 && b > 0 && a < minValue / b)) {
            return true;
        }
    }
    *out = a * b;
    return false;
#endif
}

// std::abs(INT64_MIN) and std::gcd(INT64_MIN, ...) are not defined for signed int64_t.
// Unsigned subtraction computes the magnitude without overflowing a signed operand.
std::uint64_t unsignedMagnitude(std::int64_t value)
{
    const auto bits = static_cast<std::uint64_t>(value);
    return value < 0 ? std::uint64_t{0} - bits : bits;
}

/*
 * Compare nonnegative rational magnitudes without cross-products or floating
 * point. At each step the integer parts are compared, then reciprocal
 * fractional parts reverse the comparison. The denominators remain nonzero,
 * and Euclid's algorithm guarantees termination for finite rational inputs.
 */
std::strong_ordering compareUnsignedFractions(std::uint64_t aNum, std::uint64_t aDen,
                                              std::uint64_t bNum, std::uint64_t bDen)
{
    bool reversed = false;
    for (;;) {
        const auto aWhole = aNum / aDen;
        const auto bWhole = bNum / bDen;
        if (aWhole != bWhole) {
            const bool less = aWhole < bWhole;
            return less != reversed ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        const auto aRest = aNum % aDen;
        const auto bRest = bNum % bDen;
        if (aRest == 0 || bRest == 0) {
            if (aRest == bRest) {
                return std::strong_ordering::equal;
            }
            const bool less = aRest == 0;
            return less != reversed ? std::strong_ordering::less : std::strong_ordering::greater;
        }
        aNum = aDen;
        aDen = aRest;
        bNum = bDen;
        bDen = bRest;
        reversed = !reversed;
    }
}

/*
 * Compute a full unsigned 64-by-64 product as two 64-bit words. Splitting
 * operands into 32-bit halves avoids compiler-specific __int128 (MSVC), and
 * every intermediate stays within uint64_t.
 */
struct WideProduct
{
    std::uint64_t high = 0;
    std::uint64_t low = 0;
};

WideProduct multiplyUnsigned(std::uint64_t a, std::uint64_t b)
{
    constexpr std::uint64_t mask = 0xffff'ffff;
    const std::uint64_t a0 = a & mask;
    const std::uint64_t a1 = a >> 32;
    const std::uint64_t b0 = b & mask;
    const std::uint64_t b1 = b >> 32;
    const std::uint64_t p0 = a0 * b0;
    const std::uint64_t p1 = a1 * b0;
    const std::uint64_t p2 = a0 * b1;
    const std::uint64_t p3 = a1 * b1;
    const std::uint64_t middle = (p0 >> 32) + (p1 & mask) + (p2 & mask);
    return {p3 + (p1 >> 32) + (p2 >> 32) + (middle >> 32), (p0 & mask) | (middle << 32)};
}

} // namespace

std::optional<std::int64_t> mulDiv(std::int64_t a, std::int64_t b, std::int64_t c,
                                   Rounding rounding)
{
    if (c == 0) {
        return std::nullopt;
    }
    const bool negative = ((a < 0) != (b < 0)) != (c < 0);
    const std::uint64_t divisor = unsignedMagnitude(c);
    const WideProduct product = multiplyUnsigned(unsignedMagnitude(a), unsignedMagnitude(b));

    // If the high word already contains a full divisor, the quotient needs
    // more than 64 bits, so its signed result cannot be represented.
    if (product.high >= divisor) {
        return std::nullopt;
    }

    std::uint64_t quotient = 0;
    std::uint64_t remainder = product.high;
    if (product.high == 0) {
        // Fast path for the overwhelming majority of timeline conversions.
        quotient = product.low / divisor;
        remainder = product.low % divisor;
    } else {
        // Divide high:low by a 64-bit divisor, one low-word bit at a time.
        // The high word is initially < divisor. A shifted remainder can
        // temporarily carry a 65th bit; unsigned subtraction then wraps to
        // the mathematically exact remainder without signed overflow.
        for (int bit = 63; bit >= 0; --bit) {
            const bool carry = (remainder >> 63) != 0;
            remainder = (remainder << 1) | ((product.low >> bit) & 1u);
            if (carry || remainder >= divisor) {
                remainder -= divisor;
                quotient |= std::uint64_t{1} << bit;
            }
        }
    }

    // Down=floor, Up=ceil; Nearest rounds ties away from zero. Compare the
    // remainder with ceil(divisor/2) to avoid overflow from doubling it.
    bool roundAwayFromZero = false;
    if (remainder != 0) {
        switch (rounding) {
        case Rounding::Down:
            roundAwayFromZero = negative;
            break;
        case Rounding::Up:
            roundAwayFromZero = !negative;
            break;
        case Rounding::Nearest:
            roundAwayFromZero = remainder >= divisor / 2 + divisor % 2;
            break;
        }
    }

    const std::uint64_t maxMagnitude =
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) + (negative ? 1u : 0u);
    if (quotient > maxMagnitude - (roundAwayFromZero ? 1u : 0u)) {
        return std::nullopt;
    }
    quotient += roundAwayFromZero ? 1u : 0u;
    if (negative && quotient == maxMagnitude) {
        return std::numeric_limits<std::int64_t>::min();
    }
    const auto signedQuotient = static_cast<std::int64_t>(quotient);
    return negative ? -signedQuotient : signedQuotient;
}

Rational::Rational(std::int64_t numerator, std::int64_t denominator)
{
    if (denominator == 0) {
        return; // invalid rational: 0/0
    }

    // Reduce unsigned magnitudes first: negating INT64_MIN or using it with
    // std::gcd(int64_t, int64_t) would invoke undefined behavior.
    const auto divisor = std::gcd(unsignedMagnitude(numerator), unsignedMagnitude(denominator));
    const auto reducedNum = unsignedMagnitude(numerator) / divisor;
    const auto reducedDen = unsignedMagnitude(denominator) / divisor;
    const auto signedMax = static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max());
    const bool negative = (numerator < 0) != (denominator < 0);

    // A positive denominator and numerator must fit the stored int64_t fields.
    // Only a negative numerator may have magnitude INT64_MAX + 1.
    if (reducedDen > signedMax || reducedNum > signedMax + (negative ? 1u : 0u)) {
        return;
    }

    m_den = static_cast<std::int64_t>(reducedDen);
    if (negative && reducedNum == signedMax + 1u) {
        m_num = std::numeric_limits<std::int64_t>::min();
    } else {
        const auto value = static_cast<std::int64_t>(reducedNum);
        m_num = negative ? -value : value;
    }
}

double Rational::toDouble() const
{
    return isValid() ? static_cast<double>(m_num) / static_cast<double>(m_den)
                     : std::numeric_limits<double>::quiet_NaN();
}

QString Rational::toString() const
{
    if (!isValid()) {
        return QStringLiteral("invalid");
    }
    if (m_den == 1) {
        return QString::number(m_num);
    }
    return QStringLiteral("%1/%2").arg(m_num).arg(m_den);
}

std::optional<Rational> Rational::fromString(const QString& text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return std::nullopt;
    }
    if (trimmed.contains(QLatin1Char('/'))) {
        const QStringList parts = trimmed.split(QLatin1Char('/'));
        if (parts.size() != 2) {
            return std::nullopt;
        }
        bool okNum = false;
        bool okDen = false;
        const qlonglong num = parts.at(0).trimmed().toLongLong(&okNum);
        const qlonglong den = parts.at(1).trimmed().toLongLong(&okDen);
        if (!okNum || !okDen || den == 0) {
            return std::nullopt;
        }
        const Rational parsed(num, den);
        if (!parsed.isValid()) {
            return std::nullopt;
        }
        return parsed;
    }
    bool ok = false;
    const qlonglong integer = trimmed.toLongLong(&ok);
    if (ok) {
        return Rational(integer, 1);
    }
    const double value = trimmed.toDouble(&ok);
    if (!ok || !std::isfinite(value) || value <= 0.0 || value > 1.0e6) {
        return std::nullopt;
    }
    // The standard decimal display aliases (23.976, 29.97, 59.94, etc.)
    // designate exact NTSC rates. Do not snap neighbouring custom FPS values:
    // a tolerance here turns e.g. 29.974 into 30000/1001 without warning.
    for (const std::int64_t base : {24, 30, 48, 60, 120, 240}) {
        const double ntsc = static_cast<double>(base) * 1000.0 / 1001.0;
        const int digits = (base == 24 || base == 48) ? 3 : 2;
        if (value == QString::number(ntsc, 'f', digits).toDouble()) {
            return Rational(base * 1000, 1001);
        }
    }

    // Preserve up to six explicit decimal places exactly. Rounding all input
    // to three places silently changes valid custom export frame rates.
    const qsizetype dot = trimmed.indexOf(QLatin1Char('.'));
    if (dot >= 0 && dot == trimmed.lastIndexOf(QLatin1Char('.'))) {
        const QString wholeText = trimmed.left(dot);
        const QString fractionText = trimmed.mid(dot + 1);
        bool wholeOk = false;
        bool fractionOk = false;
        const qlonglong whole = wholeText.toLongLong(&wholeOk);
        const qlonglong fraction = fractionText.toLongLong(&fractionOk);
        if (wholeOk && fractionOk && whole >= 0 && fraction >= 0) {
            if (fractionText.size() > 6) {
                return std::nullopt; // reject input we cannot represent at stated precision
            }
            std::int64_t scale = 1;
            for (qsizetype i = 0; i < fractionText.size(); ++i) {
                scale *= 10;
            }
            return Rational(whole * scale + fraction, scale);
        }
    }

    // Keep historical scientific-notation support for existing callers.
    return Rational(static_cast<std::int64_t>(std::llround(value * 1000.0)), 1000);
}

std::strong_ordering operator<=>(const Rational& a, const Rational& b)
{
    // All invalid Rational values normalise to 0/0. Place them before
    // valid numbers so ordering stays consistent with operator==.
    if (!a.isValid() || !b.isValid()) {
        return a.isValid() <=> b.isValid();
    }
    // Valid Rational values have positive denominators. Keep the fast path
    // for ordinary timeline and frame-rate values, whose cross-products fit.
    std::int64_t lhs = 0;
    std::int64_t rhs = 0;
    if (!mulOverflows(a.m_num, b.m_den, &lhs) && !mulOverflows(b.m_num, a.m_den, &rhs)) {
        return lhs <=> rhs;
    }
    // long double is only double precision on MSVC, and neither long double
    // nor double can distinguish fractions differing by 1 in a ~2^126
    // cross-product. Compare signed magnitudes exactly instead.
    if ((a.m_num < 0) != (b.m_num < 0)) {
        return a.m_num < 0 ? std::strong_ordering::less : std::strong_ordering::greater;
    }
    const auto result =
        compareUnsignedFractions(unsignedMagnitude(a.m_num), static_cast<std::uint64_t>(a.m_den),
                                 unsignedMagnitude(b.m_num), static_cast<std::uint64_t>(b.m_den));
    if (a.m_num < 0) {
        if (result == std::strong_ordering::less) {
            return std::strong_ordering::greater;
        }
        if (result == std::strong_ordering::greater) {
            return std::strong_ordering::less;
        }
    }
    return result;
}

} // namespace ft
