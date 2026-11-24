// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include <QImage>
#include <QSize>

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace Mlt {
class Frame;
class Producer;
} // namespace Mlt

namespace ft::engine {

/// Renders a frame as RGBA8888. Invalid dimensions, decoder output or allocation failure
/// return a null image without copying into an invalid buffer.
QImage frameToImage(Mlt::Frame& frame, QSize size);

/// Seeks the producer and renders one picture.
QImage grabFrame(Mlt::Producer& producer, int position, QSize size);

struct AudioBlock
{
    std::vector<std::int16_t> samples; ///< interleaved signed 16-bit
    int channels = 0;
    int frequency = 0;
    [[nodiscard]] int sampleFrames() const
    {
        if (channels <= 0) {
            return 0;
        }
        const auto frames = samples.size() / static_cast<std::size_t>(channels);
        return frames <= static_cast<std::size_t>(std::numeric_limits<int>::max())
                   ? static_cast<int>(frames)
                   : 0;
    }
};

/// Renders the audio belonging to one video frame at the given position.
/// Invalid requests return an empty block; invalid decoder output yields bounded silence
/// using the requested sample count to preserve the playback clock.
AudioBlock frameToAudio(Mlt::Frame& frame, double fps, int frequency, int channels,
                        std::int64_t position);

} // namespace ft::engine
