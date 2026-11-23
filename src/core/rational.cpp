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
    return {p3 + (p1 >> 32) + (p2 >> 32) + (middle >> 32),
            (p0 & mask) | (middle << 32)};
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
        static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) +
        (negative ? 1u : 0u);
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
    // Recognise the NTSC family (x * 1000/1001) which cannot be written exactly in decimal.
    for (const std::int64_t base : {24, 30, 48, 60, 120, 240}) {
        const double ntsc = static_cast<double>(base) * 1000.0 / 1001.0;
        if (std::abs(value - ntsc) < 0.006) {
            return Rational(base * 1000, 1001);
        }
    }
    // Otherwise keep three decimal places of precision.
    return Rational(static_cast<std::int64_t>(std::llround(value * 1000.0)), 1000);
}

std::strong_ordering operator<=>(const Rational& a, const Rational& b)
{
    // Exact cross multiplication (denominators are positive). Fall back to extended precision only
    // if the products cannot be represented, which does not happen for realistic frame rates.
    std::int64_t lhs = 0;
    std::int64_t rhs = 0;
    if (!mulOverflows(a.m_num, b.m_den, &lhs) && !mulOverflows(b.m_num, a.m_den, &rhs)) {
        return lhs <=> rhs;
    }
    const auto lhsWide = static_cast<long double>(a.m_num) * static_cast<long double>(b.m_den);
    const auto rhsWide = static_cast<long double>(b.m_num) * static_cast<long double>(a.m_den);
    if (lhsWide < rhsWide) {
        return std::strong_ordering::less;
    }
    if (lhsWide > rhsWide) {
        return std::strong_ordering::greater;
    }
    return std::strong_ordering::equal;
}

} // namespace ft
