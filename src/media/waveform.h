// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"
#include "project/media_asset.h"

#include <QColor>
#include <QImage>
#include <QString>

#include <atomic>
#include <cstdint>
#include <vector>

namespace ft::media {

/// The loudness outline of a file's sound: the peak level of each short slice of time.
struct WaveformData
{
    int binsPerSecond = 0;
    /// Peak absolute sample value of each bin across all channels, 0 (silence) .. 255 (full scale).
    std::vector<std::uint8_t> peaks;

    [[nodiscard]] bool isEmpty() const { return peaks.empty() || binsPerSecond <= 0; }
    [[nodiscard]] double durationSeconds() const
    {
        return binsPerSecond > 0 ? static_cast<double>(peaks.size()) / binsPerSecond : 0.0;
    }

    friend bool operator==(const WaveformData&, const WaveformData&) = default;
};

inline constexpr int kDefaultWaveformBinsPerSecond = 100;

/// Decodes the sound of @p filePath with the media engine and measures its peaks. Fails for files
/// without audio. Long files take a while; @p cancel stops the work early.
/// Requires ft::engine::MltRuntime::initialize().
Result<WaveformData> computeWaveform(const QString& filePath, const MediaInfo& info,
                                     int binsPerSecond = kDefaultWaveformBinsPerSecond,
                                     const std::atomic<bool>* cancel = nullptr);

/// Draws the part of a waveform between @p startSeconds and @p startSeconds + @p seconds as a
/// mirrored outline filling @p size. @p gain scales the levels (clip volume); levels above full
/// scale are clipped. Returns a transparent image when there is nothing to draw.
QImage renderWaveform(const WaveformData& data, double startSeconds, double seconds, QSize size,
                      const QColor& color, double gain = 1.0);

/// Stores computed waveforms on disk, keyed by file path, size and modification time, so each
/// file is decoded only once.
class WaveformCache
{
public:
    explicit WaveformCache(QString directory);

    [[nodiscard]] static QString defaultDirectory();
    [[nodiscard]] const QString& directory() const { return m_directory; }

    [[nodiscard]] QString keyFor(const QString& filePath, const MediaInfo& info) const;
    [[nodiscard]] std::optional<WaveformData> load(const QString& key) const;
    bool store(const QString& key, const WaveformData& data) const;

private:
    QString m_directory;
};

} // namespace ft::media
