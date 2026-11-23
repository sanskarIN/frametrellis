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

bool addOverflows(std::int64_t a, std::int64_t b, std::int64_t* out)
{
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_add_overflow(a, b, out);
#else
    const auto maxValue = std::numeric_limits<std::int64_t>::max();
    const auto minValue = std::numeric_limits<std::int64_t>::min();
    if ((b > 0 && a > maxValue - b) || (b < 0 && a < minValue - b)) {
        return true;
    }
    *out = a + b;
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

} // namespace

std::optional<std::int64_t> mulDiv(std::int64_t a, std::int64_t b, std::int64_t c,
                                   Rounding rounding)
{
    if (c == 0) {
        return std::nullopt;
    }
    if (c < 0) {
        // Flip the multiplier rather than a: a may be INT64_MIN and cannot be negated.
        // INT64_MIN divisors and multipliers cannot be sign-normalised in int64_t.
        if (c == std::numeric_limits<std::int64_t>::min() ||
            b == std::numeric_limits<std::int64_t>::min()) {
            return std::nullopt;
        }
        b = -b;
        c = -c;
    }
    // Split a into quotient and remainder by c so that the partial products stay small:
    // a*b/c = q*b + r*b/c with |r| < c.
    const std::int64_t q = a / c;
    const std::int64_t r = a % c;
    std::int64_t whole = 0;
    std::int64_t partial = 0;
    if (mulOverflows(q, b, &whole) || mulOverflows(r, b, &partial)) {
        return std::nullopt;
    }
    std::int64_t fracQuotient = partial / c;
    const std::int64_t fracRemainder = partial % c;
    if (fracRemainder != 0) {
        switch (rounding) {
        case Rounding::Down:
            if (fracRemainder < 0) {
                --fracQuotient;
            }
            break;
        case Rounding::Up:
            if (fracRemainder > 0) {
                ++fracQuotient;
            }
            break;
        case Rounding::Nearest: {
            // Compare against ceil(c / 2) rather than doubling the remainder; doubling
            // can overflow for large positive divisors even when the result fits.
            const std::int64_t magnitude = fracRemainder < 0 ? -fracRemainder : fracRemainder;
            if (magnitude >= c / 2 + c % 2) {
                fracQuotient += fracRemainder < 0 ? -1 : 1;
            }
            break;
        }
        }
    }
    std::int64_t result = 0;
    if (addOverflows(whole, fracQuotient, &result)) {
        return std::nullopt;
    }
    return result;
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
