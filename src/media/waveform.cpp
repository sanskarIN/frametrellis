// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "media/waveform.h"

#include "core/framerate.h"
#include "core/logging.h"
#include "engine/frame_utils.h"
#include "engine/mlt_profile.h"
#include "engine/mlt_runtime.h"
#include "engine/timeline_graph.h"

#include <QCryptographicHash>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPainter>
#include <QSaveFile>
#include <QStandardPaths>
#include <mlt++/Mlt.h>

#include <algorithm>
#include <cmath>

namespace ft::media {

namespace {

// Audio is read in slices of a tenth of a second: few enough frames to be quick, small enough
// to keep memory use low.
constexpr int kReadRate = 10;
constexpr quint32 kMagic = 0x46545746; // "FTWF"
constexpr quint32 kFormatVersion = 1;
constexpr quint32 kMaxBins = 50'000'000; // more than 100 hours at the default resolution

std::uint8_t toLevel(int peak)
{
    return static_cast<std::uint8_t>(std::clamp((peak * 255 + 16384) / 32768, 0, 255));
}

} // namespace

Result<WaveformData> computeWaveform(const QString& filePath, const MediaInfo& info,
                                     int binsPerSecond, const std::atomic<bool>* cancel)
{
    if (!info.hasAudio) {
        return makeError(ErrorCode::Media, QStringLiteral("The file has no sound."));
    }
    if (binsPerSecond <= 0 || binsPerSecond > 1000) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("The waveform resolution is out of range."));
    }
    if (!engine::MltRuntime::isInitialized()) {
        return makeError(ErrorCode::Engine, QStringLiteral("The media engine is not initialised."));
    }
    auto profile = engine::createProfile(64, 36, FrameRate::integer(kReadRate));
    std::unique_ptr<Mlt::Producer> producer = engine::TimelineGraphBuilder::openProducer(
        *profile, filePath, info.kind, /*audioOnly=*/true);
    if (!producer) {
        return makeError(ErrorCode::Io, QStringLiteral("The file could not be opened."), filePath);
    }
    const int frequency = info.audioSampleRate > 0 ? info.audioSampleRate : 48000;
    const int channels = std::clamp(info.audioChannels, 1, 8);
    const double seconds = static_cast<double>(info.durationUs) / 1e6;
    const auto expectedBins = static_cast<std::size_t>(std::ceil(seconds * binsPerSecond));
    const int frames =
        std::max(producer->get_length(), static_cast<int>(std::ceil(seconds * kReadRate)));
    if (expectedBins == 0 || expectedBins > kMaxBins) {
        return makeError(ErrorCode::Media,
                         QStringLiteral("The length of the sound cannot be determined."));
    }

    WaveformData data;
    data.binsPerSecond = binsPerSecond;
    data.peaks.reserve(expectedBins);
    std::int64_t sampleIndex = 0; // sample frames read so far
    std::size_t bin = 0;
    int binPeak = 0;
    for (int f = 0; f < frames && data.peaks.size() < expectedBins; ++f) {
        if (cancel && cancel->load()) {
            return makeError(ErrorCode::Cancelled, QStringLiteral("Cancelled."));
        }
        producer->seek(f);
        std::unique_ptr<Mlt::Frame> frame(producer->get_frame());
        if (!frame || !frame->is_valid()) {
            break;
        }
        const engine::AudioBlock block =
            engine::frameToAudio(*frame, kReadRate, frequency, channels, f);
        const int count = block.sampleFrames();
        for (int s = 0; s < count; ++s, ++sampleIndex) {
            const auto target = static_cast<std::size_t>(sampleIndex * binsPerSecond / frequency);
            while (bin < target) {
                data.peaks.push_back(toLevel(binPeak));
                binPeak = 0;
                ++bin;
            }
            for (int c = 0; c < block.channels; ++c) {
                const int value = block.samples[static_cast<std::size_t>(s) *
                                                    static_cast<std::size_t>(block.channels) +
                                                static_cast<std::size_t>(c)];
                binPeak = std::max(binPeak, std::abs(value));
            }
        }
    }
    if (data.peaks.size() < expectedBins) {
        data.peaks.push_back(toLevel(binPeak));
    }
    data.peaks.resize(expectedBins, 0);
    return data;
}

QImage renderWaveform(const WaveformData& data, double startSeconds, double seconds, QSize size,
                      const QColor& color, double gain)
{
    QImage image(size.expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    if (data.isEmpty() || size.isEmpty() || seconds <= 0.0) {
        return image;
    }
    QPainter painter(&image);
    const double binsPerPixel = seconds * data.binsPerSecond / size.width();
    const double firstBin = startSeconds * data.binsPerSecond;
    const double half = size.height() / 2.0;
    const auto count = static_cast<double>(data.peaks.size());
    for (int x = 0; x < size.width(); ++x) {
        const double from = firstBin + x * binsPerPixel;
        const double to = std::max(from + binsPerPixel, from + 1.0);
        if (from >= count || to <= 0.0) {
            continue;
        }
        const auto begin = static_cast<std::size_t>(std::max(0.0, std::floor(from)));
        const auto end = static_cast<std::size_t>(std::min(count, std::ceil(to)));
        std::uint8_t peak = 0;
        for (std::size_t i = begin; i < end; ++i) {
            peak = std::max(peak, data.peaks[i]);
        }
        const double level = std::min(1.0, peak / 255.0 * std::max(0.0, gain));
        const double extent = std::max(0.5, level * half);
        painter.fillRect(QRectF(x, half - extent, 1.0, extent * 2.0), color);
    }
    return image;
}

WaveformCache::WaveformCache(QString directory)
    : m_directory(std::move(directory))
{
    QDir().mkpath(m_directory);
}

QString WaveformCache::defaultDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) +
           QStringLiteral("/waveforms");
}

QString WaveformCache::keyFor(const QString& filePath, const MediaInfo& info) const
{
    const QFileInfo file(filePath);
    const QString key = QStringLiteral("v%1|%2|%3|%4|%5")
                            .arg(kFormatVersion)
                            .arg(canonicalMediaKey(filePath))
                            .arg(file.size())
                            .arg(file.lastModified().toMSecsSinceEpoch())
                            .arg(info.durationUs);
    return QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
}

std::optional<WaveformData> WaveformCache::load(const QString& key) const
{
    QFile file(QDir(m_directory).filePath(key + QStringLiteral(".ftwf")));
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    QDataStream in(&file);
    quint32 magic = 0;
    quint32 version = 0;
    quint32 binsPerSecond = 0;
    quint32 count = 0;
    in >> magic >> version >> binsPerSecond >> count;
    if (in.status() != QDataStream::Ok || magic != kMagic || version != kFormatVersion ||
        binsPerSecond == 0 || binsPerSecond > 1000 || count > kMaxBins ||
        file.size() != static_cast<qint64>(count) + 16) {
        return std::nullopt; // damaged or from another version: compute again
    }
    WaveformData data;
    data.binsPerSecond = static_cast<int>(binsPerSecond);
    data.peaks.resize(count);
    if (in.readRawData(reinterpret_cast<char*>(data.peaks.data()), static_cast<int>(count)) !=
        static_cast<int>(count)) {
        return std::nullopt;
    }
    return data;
}

bool WaveformCache::store(const QString& key, const WaveformData& data) const
{
    if (data.isEmpty() || data.peaks.size() > kMaxBins) {
        return false;
    }
    QSaveFile file(QDir(m_directory).filePath(key + QStringLiteral(".ftwf")));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    QDataStream out(&file);
    out << kMagic << kFormatVersion << static_cast<quint32>(data.binsPerSecond)
        << static_cast<quint32>(data.peaks.size());
    out.writeRawData(reinterpret_cast<const char*>(data.peaks.data()),
                     static_cast<int>(data.peaks.size()));
    return out.status() == QDataStream::Ok && file.commit();
}

} // namespace ft::media
