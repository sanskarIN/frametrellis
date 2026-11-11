// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/framerate.h"
#include "core/result.h"

#include <vector>

namespace ft {

/// Audio automation is anchored to source frames at the project rate. Trimming and splitting
/// preserve the envelope; slipping reveals the envelope belonging to the new source frames.
struct AudioKeyframe
{
    FrameCount sourceFrame = 0;
    double decibels = 0.0;

    friend bool operator==(const AudioKeyframe&, const AudioKeyframe&) = default;
};

inline constexpr int kMaxAudioKeyframes = 1000;
inline constexpr double kMinAudioKeyframeDb = -60.0;
inline constexpr double kMaxAudioKeyframeDb = 12.0;

/// Requires strictly increasing, unique frames and finite, bounded decibel values.
Status validateAudioKeyframes(const std::vector<AudioKeyframe>& points, FrameCount maxFrame);
/// Linear interpolation in decibels, with constant values outside the first/last point.
/// Empty envelopes are neutral (0 dB). The input envelope must be validated.
double audioKeyframeDecibels(const std::vector<AudioKeyframe>& points, FrameCount sourceFrame);

} // namespace ft
