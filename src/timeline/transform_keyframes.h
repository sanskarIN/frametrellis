// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/framerate.h"
#include "core/result.h"

#include <vector>

namespace ft {

/// Transform the aspect-fitted picture about its centre. Offsets are canvas fractions;
/// positive x is right, positive y is down, positive rotation is clockwise in degrees.
struct VideoTransform
{
    double x = 0.0;        ///< -4 .. 4 canvas widths
    double y = 0.0;        ///< -4 .. 4 canvas heights
    double scale = 1.0;    ///< uniform scale, 0.01 .. 10
    double rotation = 0.0; ///< -3600 .. 3600 degrees; interpolation never wraps

    friend bool operator==(const VideoTransform&, const VideoTransform&) = default;
};

struct TransformKeyframe
{
    FrameCount sourceFrame = 0;
    VideoTransform value;

    friend bool operator==(const TransformKeyframe&, const TransformKeyframe&) = default;
};

inline constexpr int kMaxTransformKeyframes = 1000;
[[nodiscard]] Status validateVideoTransform(const VideoTransform& value);
[[nodiscard]] Status validateTransformKeyframes(const std::vector<TransformKeyframe>& points,
                                                FrameCount maxFrame);
/// Empty curves are identity. One point holds its value over the entire clip; two or more
/// linearly interpolate all four coordinates, holding the first/last value outside the curve.
[[nodiscard]] VideoTransform transformKeyframeValue(const std::vector<TransformKeyframe>& points,
                                                    FrameCount sourceFrame);

} // namespace ft
