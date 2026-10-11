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
        if ((a > 0 && b > 0 && a > maxValue / b) || (a < 0 && b < 0 && a < maxValue / b)
            || (a > 0 && b < 0 && b < -maxValue / a) || (a < 0 && b > 0 && a < -maxValue / b)) {
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

} // namespace

std::optional<std::int64_t> mulDiv(std::int64_t a, std::int64_t b, std::int64_t c,
                                   Rounding rounding)
{
    if (c == 0) {
        return std::nullopt;
    }
    if (c < 0) {
        a = -a;
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
            const std::int64_t twice = (fracRemainder < 0 ? -fracRemainder : fracRemainder) * 2;
            if (twice >= c) {
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
    if (denominator < 0) {
        numerator = -numerator;
        denominator = -denominator;
    }
    const std::int64_t divisor = std::gcd(numerator, denominator);
    m_num = divisor != 0 ? numerator / divisor : numerator;
    m_den = divisor != 0 ? denominator / divisor : denominator;
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
        return Rational(num, den);
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
