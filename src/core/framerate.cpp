// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "core/framerate.h"

#include <QRegularExpression>

namespace ft {

namespace {
constexpr std::int64_t kMicrosecondsPerSecond = 1'000'000;
}

std::optional<FrameRate> FrameRate::create(std::int64_t numerator, std::int64_t denominator)
{
    if (numerator <= 0 || denominator <= 0 || numerator > maxNumerator ||
        denominator > maxDenominator) {
        return std::nullopt;
    }
    const Rational rate(numerator, denominator);
    if (rate.toDouble() > maxFramesPerSecond) {
        return std::nullopt;
    }
    return FrameRate(rate);
}

std::optional<FrameRate> FrameRate::fromRational(const Rational& rational)
{
    return create(rational.numerator(), rational.denominator());
}

QList<FrameRate> FrameRate::standardRates()
{
    QList<FrameRate> rates;
    const std::pair<std::int64_t, std::int64_t> values[] = {{24000, 1001}, {24, 1},       {25, 1},
                                                            {30000, 1001}, {30, 1},       {48, 1},
                                                            {50, 1},       {60000, 1001}, {60, 1}};
    for (const auto& [num, den] : values) {
        rates.append(*create(num, den));
    }
    return rates;
}

int FrameRate::nominalFramesPerSecond() const
{
    const auto rounded = mulDiv(1, numerator(), denominator(), Rounding::Up);
    return rounded ? static_cast<int>(*rounded) : 0;
}

bool FrameRate::supportsDropFrame() const
{
    return m_rate == Rational(30000, 1001) || m_rate == Rational(60000, 1001);
}

QString FrameRate::displayName() const
{
    if (denominator() == 1) {
        return QStringLiteral("%1 fps").arg(numerator());
    }
    return QStringLiteral("%1 fps").arg(
        QString::number(toDouble(), 'f', 3).remove(QRegularExpression(QStringLiteral("0+$"))));
}

std::optional<std::int64_t> FrameRate::framesToMicroseconds(FrameCount frames) const
{
    // us = frames * den * 1e6 / num, computed as frames * (den * 1e6) / num.
    return mulDiv(frames, denominator() * kMicrosecondsPerSecond, numerator(), Rounding::Nearest);
}

std::optional<FrameCount> FrameRate::microsecondsToFrames(std::int64_t microseconds,
                                                          Rounding rounding) const
{
    // frames = us * num / (den * 1e6)
    return mulDiv(microseconds, numerator(), denominator() * kMicrosecondsPerSecond, rounding);
}

double FrameRate::framesToSeconds(FrameCount frames) const
{
    return static_cast<double>(frames) * static_cast<double>(denominator()) /
           static_cast<double>(numerator());
}

} // namespace ft
