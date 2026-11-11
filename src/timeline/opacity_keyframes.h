// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/framerate.h"
#include "core/result.h"

#include <vector>

namespace ft {

/// Source-anchored transparency multiplier, independent of scalar opacity and fades.
/// Images use the same project-frame clock as time-based media.
struct OpacityKeyframe
{
    FrameCount sourceFrame = 0;
    double opacity = 1.0;

    friend bool operator==(const OpacityKeyframe&, const OpacityKeyframe&) = default;
};

inline constexpr int kMaxOpacityKeyframes = 1000;

Status validateOpacityKeyframes(const std::vector<OpacityKeyframe>& points, FrameCount maxFrame);
/// Linear interpolation with held endpoints; an empty curve is fully opaque (factor 1).
/// Input points must be validated and sorted by strictly increasing source frame.
double opacityKeyframeValue(const std::vector<OpacityKeyframe>& points, FrameCount sourceFrame);

} // namespace ft
