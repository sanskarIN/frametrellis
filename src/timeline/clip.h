// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/framerate.h"
#include "core/ids.h"
#include "timeline/audio_keyframes.h"

namespace ft {

/// Basic per-clip picture adjustments. Neutral values leave the image untouched.
struct VideoAdjustments
{
    double brightness = 0.0; ///< -1.0 .. 1.0, 0 is neutral
    double contrast = 1.0;   ///< 0.0 .. 2.0, 1 is neutral
    double saturation = 1.0; ///< 0.0 .. 3.0, 1 is neutral
    double opacity = 1.0;    ///< 0.0 .. 1.0

    [[nodiscard]] bool isNeutral() const
    {
        return brightness == 0.0 && contrast == 1.0 && saturation == 1.0 && opacity == 1.0;
    }
    friend bool operator==(const VideoAdjustments&, const VideoAdjustments&) = default;
};

/// Editable clip properties that do not affect the clip's placement on the timeline.
struct ClipProperties
{
    double gain = 1.0; ///< linear audio gain, 0.0 .. 4.0 (about +12 dB)
    bool muted = false;
    FrameCount fadeIn = 0;  ///< frames of fade-in at the clip start
    FrameCount fadeOut = 0; ///< frames of fade-out at the clip end
    VideoAdjustments video;

    friend bool operator==(const ClipProperties&, const ClipProperties&) = default;
};

/// A placement of a media source on a track. All values are frames at the project rate.
struct Clip
{
    Id id;
    Id assetId;
    FrameCount start = 0;    ///< position of the first frame on the track
    FrameCount sourceIn = 0; ///< first source frame used
    FrameCount duration = 1; ///< number of frames, always >= 1
    ClipProperties properties;
    std::vector<AudioKeyframe> audioKeyframes; ///< source-anchored volume envelope, in dB

    [[nodiscard]] FrameCount end() const { return start + duration; }          ///< exclusive
    [[nodiscard]] FrameCount sourceOut() const { return sourceIn + duration; } ///< exclusive
    [[nodiscard]] bool contains(FrameCount frame) const { return frame >= start && frame < end(); }

    friend bool operator==(const Clip&, const Clip&) = default;
};

inline constexpr double kMaxClipGain = 4.0;

} // namespace ft
