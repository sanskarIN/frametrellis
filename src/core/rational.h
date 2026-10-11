// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include <QString>

#include <compare>
#include <cstdint>
#include <optional>

namespace ft {

/// Rounding policy for integer conversions between time bases.
enum class Rounding { Down, Nearest, Up };

/// Computes a * b / c without intermediate overflow for the value ranges used by the editor
/// (|b| and |c| below 2^31). Returns std::nullopt when the result does not fit in 64 bits or c is 0.
std::optional<std::int64_t> mulDiv(std::int64_t a, std::int64_t b, std::int64_t c,
                                   Rounding rounding = Rounding::Down);

/// An exact, normalised rational number (denominator always positive).
class Rational
{
public:
    constexpr Rational() = default;
    Rational(std::int64_t numerator, std::int64_t denominator);

    [[nodiscard]] constexpr std::int64_t numerator() const { return m_num; }
    [[nodiscard]] constexpr std::int64_t denominator() const { return m_den; }
    [[nodiscard]] constexpr bool isValid() const { return m_den > 0; }
    [[nodiscard]] constexpr bool isPositive() const { return m_den > 0 && m_num > 0; }
    [[nodiscard]] double toDouble() const;
    [[nodiscard]] QString toString() const;

    /// Parses "30000/1001", "25" or "29.97" style strings. Decimal input is matched against the
    /// common NTSC rates so that "29.97" yields exactly 30000/1001.
    static std::optional<Rational> fromString(const QString& text);

    friend bool operator==(const Rational& a, const Rational& b) = default;
    friend std::strong_ordering operator<=>(const Rational& a, const Rational& b);

private:
    std::int64_t m_num = 0;
    std::int64_t m_den = 0;
};

} // namespace ft
