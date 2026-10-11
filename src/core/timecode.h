// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/framerate.h"

#include <QString>

#include <optional>

namespace ft::timecode {

enum class Style {
    NonDropFrame, ///< HH:MM:SS:FF counting every frame
    DropFrame,    ///< HH:MM:SS;FF SMPTE drop-frame (only for 29.97 and 59.94)
    Automatic,    ///< drop-frame where the rate supports it, otherwise non-drop
};

/// Formats a frame number as SMPTE timecode. Negative values are prefixed with '-'.
QString format(FrameCount frames, const FrameRate& rate, Style style = Style::Automatic);

/// Parses "HH:MM:SS:FF", "MM:SS:FF", "SS:FF" or "FF". A ';' separator selects drop-frame.
/// Returns std::nullopt for malformed input, out-of-range fields or non-existent drop-frame labels.
std::optional<FrameCount> parse(const QString& text, const FrameRate& rate);

/// Formats a frame count as a compact clock label such as "1:02:03.500" or "0:04.040".
QString formatClock(FrameCount frames, const FrameRate& rate);

} // namespace ft::timecode
