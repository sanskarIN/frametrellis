// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "engine/frame_utils.h"

#include "project/project_settings.h"
#include "timeline/timeline.h"

#include <mlt++/Mlt.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>

namespace ft::engine {

namespace {

constexpr int kMaxAudioFrequency = 384'000;
constexpr int kMaxRequestedAudioChannels = 8;
constexpr int kMaxDecodedAudioChannels = 64;

bool supportedImageSize(QSize size)
{
    return !size.isEmpty() && size.width() <= kMaxFrameDimension &&
           size.height() <= kMaxFrameDimension;
}

bool supportedAudioRequest(double fps, int frequency, int channels, std::int64_t position)
{
    return std::isfinite(fps) && fps >= 1.0 && fps <= FrameRate::maxFramesPerSecond &&
           frequency > 0 && frequency <= kMaxAudioFrequency && channels > 0 &&
           channels <= kMaxRequestedAudioChannels && position >= 0 &&
           position <= kMaxTimelineFrames;
}

} // namespace

QImage frameToImage(Mlt::Frame& frame, QSize size)
{
    if (!supportedImageSize(size) || !frame.is_valid()) {
        return {};
    }
    frame.set("rescale.interp", "bilinear");
    frame.set("consumer_deinterlace", 1);
    frame.set("deinterlace_method", "linearblend");
    mlt_image_format format = mlt_image_rgba;
    int width = size.width();
    int height = size.height();
    const uint8_t* data = frame.get_image(format, width, height);
    // The decoder can change the dimensions supplied by the caller. Bound
    // those returned values too, before allocation or pointer arithmetic.
    if (!data || format != mlt_image_rgba || !supportedImageSize(QSize(width, height))) {
        return {};
    }
    QImage image(width, height, QImage::Format_RGBA8888);
    const qsizetype rowBytes = static_cast<qsizetype>(width) * 4;
    // Qt returns a null image when allocation fails. Never memcpy into its
    // null scanline, especially under memory pressure during long editing.
    if (image.isNull() || image.bytesPerLine() < rowBytes) {
        return {};
    }
    for (int y = 0; y < height; ++y) {
        std::memcpy(image.scanLine(y), data + y * rowBytes, static_cast<size_t>(rowBytes));
    }
    return image;
}

QImage grabFrame(Mlt::Producer& producer, int position, QSize size)
{
    if (position < 0 || !supportedImageSize(size)) {
        return {};
    }
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
    if (!supportedAudioRequest(fps, frequency, channels, position) || !frame.is_valid()) {
        return block;
    }
    block.frequency = frequency;
    block.channels = channels;

    const int requestedSamples =
        mlt_audio_calculate_frame_samples(static_cast<float>(fps), frequency, position);
    // With a supported rate >= 1 FPS, no single frame needs more than
    // one second of audio. Keep this bound even if the backend miscalculates.
    if (requestedSamples < 0 || requestedSamples > kMaxAudioFrequency) {
        return block;
    }
    int samples = requestedSamples;
    mlt_audio_format format = mlt_audio_s16;
    int actualFrequency = frequency;
    int actualChannels = channels;
    const auto* pcm = static_cast<const std::int16_t*>(
        frame.get_audio(format, actualFrequency, actualChannels, samples));
    const auto requestedChannels = static_cast<size_t>(channels);
    if (!pcm || format != mlt_audio_s16 || samples <= 0 || samples > kMaxAudioFrequency ||
        actualChannels <= 0 || actualChannels > kMaxDecodedAudioChannels || actualFrequency <= 0 ||
        actualFrequency > kMaxAudioFrequency) {
        // Preserve the playback clock with a bounded silent block. Never
        // allocate from an invalid sample count returned by the decoder.
        block.samples.assign(static_cast<size_t>(requestedSamples) * requestedChannels, 0);
        return block;
    }

    const auto sampleCount = static_cast<size_t>(samples);
    const auto decodedChannels = static_cast<size_t>(actualChannels);
    if (actualChannels == channels) {
        block.samples.assign(pcm, pcm + sampleCount * requestedChannels);
    } else {
        // Map to the requested channel count: duplicate mono or keep the first
        // channels. All indexing uses bounded size_t products, not signed int.
        block.samples.resize(sampleCount * requestedChannels);
        for (size_t s = 0; s < sampleCount; ++s) {
            for (size_t c = 0; c < requestedChannels; ++c) {
                const auto source = std::min(c, decodedChannels - 1);
                block.samples[s * requestedChannels + c] = pcm[s * decodedChannels + source];
            }
        }
    }
    return block;
}

} // namespace ft::engine
