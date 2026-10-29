// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "timeline/opacity_keyframes.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace ft {

Status validateOpacityKeyframes(const std::vector<OpacityKeyframe>& points, FrameCount maxFrame)
{
    if (points.size() > static_cast<size_t>(kMaxOpacityKeyframes)) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("A clip can have at most 1000 opacity keyframes."));
    }
    FrameCount previous = -1;
    for (const OpacityKeyframe& point : points) {
        if (point.sourceFrame < 0 || point.sourceFrame > maxFrame ||
            point.sourceFrame <= previous || !std::isfinite(point.opacity) || point.opacity < 0.0 ||
            point.opacity > 1.0) {
            return makeError(ErrorCode::OutOfRange,
                             QStringLiteral("Opacity keyframes need unique, increasing source "
                                            "frames and values between 0 and 1."));
        }
        previous = point.sourceFrame;
    }
    return {};
}

double opacityKeyframeValue(const std::vector<OpacityKeyframe>& points, FrameCount sourceFrame)
{
    if (points.empty()) {
        return 1.0;
    }
    const auto right = std::lower_bound(
        points.begin(), points.end(), sourceFrame,
        [](const OpacityKeyframe& point, FrameCount frame) { return point.sourceFrame < frame; });
    if (right == points.begin()) {
        return right->opacity;
    }
    if (right == points.end()) {
        return points.back().opacity;
    }
    const OpacityKeyframe& left = *std::prev(right);
    const double fraction = static_cast<double>(sourceFrame - left.sourceFrame) /
                            static_cast<double>(right->sourceFrame - left.sourceFrame);
    return left.opacity + fraction * (right->opacity - left.opacity);
}

} // namespace ft
