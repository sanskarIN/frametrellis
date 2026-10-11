// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include <QImage>
#include <QSize>

#include <cstdint>
#include <vector>

namespace Mlt {
class Frame;
class Producer;
} // namespace Mlt

namespace ft::engine {

/// Renders the frame's picture as RGBA8888 at the requested size. Returns a null image on failure.
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
        return channels > 0 ? static_cast<int>(samples.size()) / channels : 0;
    }
};

/// Renders the audio belonging to one video frame at the given position.
AudioBlock frameToAudio(Mlt::Frame& frame, double fps, int frequency, int channels,
                        std::int64_t position);

} // namespace ft::engine
