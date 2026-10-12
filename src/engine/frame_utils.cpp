// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "engine/frame_utils.h"

#include <mlt++/Mlt.h>

#include <cstring>
#include <memory>

namespace ft::engine {

QImage frameToImage(Mlt::Frame& frame, QSize size)
{
    if (size.isEmpty()) {
        return {};
    }
    frame.set("rescale.interp", "bilinear");
    frame.set("consumer_deinterlace", 1);
    frame.set("deinterlace_method", "linearblend");
    mlt_image_format format = mlt_image_rgba;
    int width = size.width();
    int height = size.height();
    const uint8_t* data = frame.get_image(format, width, height);
    if (!data || format != mlt_image_rgba || width <= 0 || height <= 0) {
        return {};
    }
    QImage image(width, height, QImage::Format_RGBA8888);
    const qsizetype rowBytes = static_cast<qsizetype>(width) * 4;
    for (int y = 0; y < height; ++y) {
        std::memcpy(image.scanLine(y), data + y * rowBytes, static_cast<size_t>(rowBytes));
    }
    return image;
}

QImage grabFrame(Mlt::Producer& producer, int position, QSize size)
{
    producer.seek(position);
    std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
    if (!frame || !frame->is_valid()) {
        return {};
    }
    return frameToImage(*frame, size);
}

AudioBlock frameToAudio(Mlt::Frame& frame, double fps, int frequency, int channels,
                        std::int64_t position)
{
    AudioBlock block;
    mlt_audio_format format = mlt_audio_s16;
    int samples = mlt_audio_calculate_frame_samples(static_cast<float>(fps), frequency, position);
    int actualFrequency = frequency;
    int actualChannels = channels;
    const auto* pcm = static_cast<const std::int16_t*>(
        frame.get_audio(format, actualFrequency, actualChannels, samples));
    block.frequency = frequency;
    block.channels = channels;
    if (!pcm || format != mlt_audio_s16 || samples <= 0 || actualChannels <= 0) {
        block.samples.assign(
            static_cast<size_t>(std::max(samples, 0)) * static_cast<size_t>(channels), 0);
        return block;
    }
    if (actualChannels == channels) {
        block.samples.assign(pcm,
                             pcm + static_cast<size_t>(samples) * static_cast<size_t>(channels));
    } else {
        // Map to the requested channel count: duplicate mono or keep the first channels.
        block.samples.resize(static_cast<size_t>(samples) * static_cast<size_t>(channels));
        for (int s = 0; s < samples; ++s) {
            for (int c = 0; c < channels; ++c) {
                const int source = std::min(c, actualChannels - 1);
                block.samples[static_cast<size_t>(s) * static_cast<size_t>(channels) +
                              static_cast<size_t>(c)] = pcm[s * actualChannels + source];
            }
        }
    }
    return block;
}

} // namespace ft::engine
