// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/rational.h"

#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

namespace ft {

/// Timeline positions and durations are integer frame counts at the project frame rate.
/// Using integers keeps clip boundaries exact; conversion to wall-clock time is done with
/// rational arithmetic only at the edges (display, media probing, encoding).
using FrameCount = std::int64_t;

/// A validated, exact frame rate such as 25/1 or 30000/1001.
class FrameRate
{
public:
    static constexpr std::int64_t maxNumerator = 1'000'000;
    static constexpr std::int64_t maxDenominator = 1'000'000;
    static constexpr double maxFramesPerSecond = 1000.0;

    FrameRate() = default;

    /// Returns a frame rate when num/den describes a positive rate within supported limits.
    static std::optional<FrameRate> create(std::int64_t numerator, std::int64_t denominator);
    static std::optional<FrameRate> fromRational(const Rational& rational);

    /// Commonly used production frame rates, in ascending order.
    static QList<FrameRate> standardRates();

    [[nodiscard]] const Rational& rational() const { return m_rate; }
    [[nodiscard]] std::int64_t numerator() const { return m_rate.numerator(); }
    [[nodiscard]] std::int64_t denominator() const { return m_rate.denominator(); }
    [[nodiscard]] bool isValid() const { return m_rate.isPositive(); }
    [[nodiscard]] double toDouble() const { return m_rate.toDouble(); }

    /// Integer frames-per-second used for timecode labelling (30 for 29.97, 24 for 23.976).
    [[nodiscard]] int nominalFramesPerSecond() const;
    /// True for 30000/1001 and 60000/1001, the rates where SMPTE drop-frame timecode is defined.
    [[nodiscard]] bool supportsDropFrame() const;
    /// Human readable label, e.g. "29.97 fps".
    [[nodiscard]] QString displayName() const;

    [[nodiscard]] std::optional<std::int64_t> framesToMicroseconds(FrameCount frames) const;
    [[nodiscard]] std::optional<FrameCount>
    microsecondsToFrames(std::int64_t microseconds, Rounding rounding = Rounding::Down) const;
    [[nodiscard]] double framesToSeconds(FrameCount frames) const;

    friend bool operator==(const FrameRate& a, const FrameRate& b) = default;

private:
    explicit FrameRate(Rational rate)
        : m_rate(rate)
    {}
    Rational m_rate{25, 1};
};

} // namespace ft
