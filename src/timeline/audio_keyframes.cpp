// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "timeline/audio_keyframes.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace ft {

Status validateAudioKeyframes(const std::vector<AudioKeyframe>& points, FrameCount maxFrame)
{
    if (points.size() > static_cast<size_t>(kMaxAudioKeyframes)) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("A clip can have at most 1000 volume keyframes."));
    }
    FrameCount previous = -1;
    for (const AudioKeyframe& point : points) {
        if (point.sourceFrame < 0 || point.sourceFrame > maxFrame ||
            point.sourceFrame <= previous || !std::isfinite(point.decibels) ||
            point.decibels < kMinAudioKeyframeDb || point.decibels > kMaxAudioKeyframeDb) {
            return makeError(ErrorCode::OutOfRange,
                             QStringLiteral("Volume keyframes need unique, increasing source "
                                            "frames and levels between -60 and +12 dB."));
        }
        previous = point.sourceFrame;
    }
    return {};
}

double audioKeyframeDecibels(const std::vector<AudioKeyframe>& points, FrameCount sourceFrame)
{
    if (points.empty()) {
        return 0.0;
    }
    const auto right = std::lower_bound(
        points.begin(), points.end(), sourceFrame,
        [](const AudioKeyframe& point, FrameCount frame) { return point.sourceFrame < frame; });
    if (right == points.begin()) {
        return right->decibels;
    }
    if (right == points.end()) {
        return points.back().decibels;
    }
    const AudioKeyframe& left = *std::prev(right);
    const double fraction = static_cast<double>(sourceFrame - left.sourceFrame) /
                            static_cast<double>(right->sourceFrame - left.sourceFrame);
    return left.decibels + fraction * (right->decibels - left.decibels);
}

} // namespace ft
