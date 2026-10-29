// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "timeline/transform_keyframes.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace ft {

Status validateVideoTransform(const VideoTransform& value)
{
    if (!std::isfinite(value.x) || !std::isfinite(value.y) || !std::isfinite(value.scale) ||
        !std::isfinite(value.rotation) || std::abs(value.x) > 4.0 || std::abs(value.y) > 4.0 ||
        value.scale < 0.01 || value.scale > 10.0 || std::abs(value.rotation) > 3600.0) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("Transform position must be within ±400% of the canvas, "
                                        "scale within 1–1000%, and rotation within ±3600°."));
    }
    return {};
}

Status validateTransformKeyframes(const std::vector<TransformKeyframe>& points, FrameCount maxFrame)
{
    if (points.size() > static_cast<size_t>(kMaxTransformKeyframes)) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("A clip can have at most 1000 transform keyframes."));
    }
    FrameCount previous = -1;
    for (const TransformKeyframe& point : points) {
        if (point.sourceFrame < 0 || point.sourceFrame > maxFrame ||
            point.sourceFrame <= previous) {
            return makeError(
                ErrorCode::OutOfRange,
                QStringLiteral("Transform keyframes need unique, increasing source frames."));
        }
        if (const Status valid = validateVideoTransform(point.value); !valid) {
            return valid;
        }
        previous = point.sourceFrame;
    }
    return {};
}

VideoTransform transformKeyframeValue(const std::vector<TransformKeyframe>& points,
                                      FrameCount sourceFrame)
{
    if (points.empty()) {
        return {};
    }
    const auto right = std::lower_bound(
        points.begin(), points.end(), sourceFrame,
        [](const TransformKeyframe& point, FrameCount frame) { return point.sourceFrame < frame; });
    if (right == points.begin()) {
        return right->value;
    }
    if (right == points.end()) {
        return points.back().value;
    }
    const TransformKeyframe& left = *std::prev(right);
    const double fraction = static_cast<double>(sourceFrame - left.sourceFrame) /
                            static_cast<double>(right->sourceFrame - left.sourceFrame);
    const auto lerp = [fraction](double a, double b) { return a + fraction * (b - a); };
    return {lerp(left.value.x, right->value.x), lerp(left.value.y, right->value.y),
            lerp(left.value.scale, right->value.scale),
            lerp(left.value.rotation, right->value.rotation)};
}

} // namespace ft
