// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include <QString>

#include <cstdint>
#include <memory>

namespace ft::playback {

/// A push-model audio device. Playback writes interleaved 16-bit samples and uses the amount of
/// queued audio as its clock. Implementations must be usable from a single worker thread.
class AudioOutput
{
public:
    virtual ~AudioOutput() = default;
    AudioOutput() = default;
    AudioOutput(const AudioOutput&) = delete;
    AudioOutput& operator=(const AudioOutput&) = delete;

    virtual bool open(int frequency, int channels) = 0;
    virtual void close() = 0;
    [[nodiscard]] virtual bool isOpen() const = 0;
    /// Queues sampleFrames frames of interleaved samples.
    virtual void write(const std::int16_t* samples, int sampleFrames) = 0;
    /// Sample frames that have been queued but not yet played.
    [[nodiscard]] virtual std::int64_t queuedSampleFrames() const = 0;
    /// Drops queued audio (used on pause and seek).
    virtual void clear() = 0;
    [[nodiscard]] virtual QString deviceName() const = 0;
};

/// Creates the platform audio output (SDL2 when built with it), or nullptr when the build has no
/// audio backend.
std::unique_ptr<AudioOutput> createAudioOutput();

/// Name of the compiled-in backend, or an empty string.
QString audioBackendName();

} // namespace ft::playback
