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
#include <limits>

namespace ft::media {

namespace {

// Audio is read in slices of a tenth of a second: few enough frames to be quick, small enough
// to keep memory use low.
constexpr int kReadRate = 10;
constexpr quint32 kMagic = 0x46545746; // "FTWF"
constexpr quint32 kFormatVersion = 2;
constexpr quint32 kMaxBins = 50'000'000; // more than 100 hours at the default resolution

// Only accept keys produced by keyFor(). Never interpret a caller-supplied
// string as a filesystem path, even if it has a valid cache suffix.
bool validCacheKey(const QString& key)
{
    if (key.size() != 40) {
        return false;
    }
    for (const QChar c : key) {
        if (!(c >= QLatin1Char('0') && c <= QLatin1Char('9')) &&
            !(c >= QLatin1Char('a') && c <= QLatin1Char('f'))) {
            return false;
        }
    }
    return true;
}

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
    // Compute exact ceiling lengths before any allocation or native decoder
    // calls. Floating-point rounding can create a spurious bin at boundaries.
    const auto bins = mulDiv(info.durationUs, binsPerSecond, 1'000'000, Rounding::Up);
    const auto decodedFrames = mulDiv(info.durationUs, kReadRate, 1'000'000, Rounding::Up);
    if (info.durationUs <= 0 || !bins || *bins < 1 || *bins > kMaxBins || !decodedFrames ||
        *decodedFrames < 1 || *decodedFrames >= std::numeric_limits<int>::max()) {
        return makeError(ErrorCode::Media,
                         QStringLiteral("The length of the sound cannot be determined."));
    }
    const auto expectedBins = static_cast<std::size_t>(*bins);
    const auto expectedFrames = static_cast<int>(*decodedFrames);
    // A corrupted probe may claim an absurd input rate (up to INT_MAX). At
    // one tenth of a second per decoder request, trusting it can allocate
    // hundreds of millions of PCM samples before a single waveform bin.
    // Unknown rates keep the existing 48 kHz fallback below.
    constexpr int kMaxWaveformSampleRate = 384'000;
    if (info.audioSampleRate > kMaxWaveformSampleRate) {
        return makeError(ErrorCode::Media,
                         QStringLiteral("The audio sample rate is out of range."));
    }
    if (cancel && cancel->load()) {
        return makeError(ErrorCode::Cancelled, QStringLiteral("Cancelled."));
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
    // At most one extra decoder frame can cover sample-clock rounding. A
    // corrupt native length must not cause an unbounded loop on empty blocks.
    const int frames = std::clamp(producer->get_length(), expectedFrames, expectedFrames + 1);

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
            const auto boundedTarget = std::min(target, expectedBins);
            while (bin < boundedTarget) {
                data.peaks.push_back(toLevel(binPeak));
                binPeak = 0;
                ++bin;
            }
            if (bin == expectedBins) {
                break; // do not decode padding beyond the final valid bin
            }
            for (int c = 0; c < block.channels; ++c) {
                const int value = block.samples[static_cast<std::size_t>(s) *
                                                    static_cast<std::size_t>(block.channels) +
                                                static_cast<std::size_t>(c)];
                binPeak = std::max(binPeak, std::abs(value));
            }
        }
    }
    if (cancel && cancel->load()) {
        return makeError(ErrorCode::Cancelled, QStringLiteral("Cancelled."));
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
    // Apply the same bounds as the QML provider before allocating an image.
    // Public C++ callers must not bypass the UI's memory limit.
    if (size.width() > 8192 || size.height() > 512) {
        return {};
    }
    QImage image(size.expandedTo(QSize(1, 1)), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) {
        return {};
    }
    image.fill(Qt::transparent);
    // Requests originate in QML and can carry invalid float values. Reject
    // them before flooring a bin index or converting one to size_t.
    if (data.isEmpty() || data.binsPerSecond > 1000 || data.peaks.size() > kMaxBins ||
        size.isEmpty() || !std::isfinite(startSeconds) ||
        !std::isfinite(seconds) || !std::isfinite(gain) || startSeconds < 0.0 || seconds <= 0.0 ||
        gain < 0.0) {
        return image;
    }
    // Media durations use signed 64-bit microseconds. A drawing window
    // beyond that representable range cannot correspond to valid media.
    constexpr double kMaxMediaSeconds =
        static_cast<double>(std::numeric_limits<std::int64_t>::max()) / 1'000'000.0;
    if (startSeconds > kMaxMediaSeconds || seconds > kMaxMediaSeconds - startSeconds) {
        return image;
    }
    const double binsPerPixel = seconds * data.binsPerSecond / size.width();
    const double firstBin = startSeconds * data.binsPerSecond;
    const auto count = static_cast<double>(data.peaks.size());
    if (!std::isfinite(binsPerPixel) || !std::isfinite(firstBin) || firstBin >= count) {
        return image;
    }
    QPainter painter(&image);
    const double half = size.height() / 2.0;
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
    const QString key = QStringLiteral("v%1|%2|%3|%4|%5|%6|%7|%8")
                            .arg(kFormatVersion)
                            .arg(canonicalMediaKey(filePath))
                            .arg(file.size())
                            .arg(file.lastModified().toMSecsSinceEpoch())
                            .arg(info.durationUs)
                            .arg(info.audioSampleRate)
                            .arg(info.audioChannels)
                            .arg(mediaKindToString(info.kind));
    return QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
}

std::optional<WaveformData> WaveformCache::load(const QString& key) const
{
    if (!validCacheKey(key)) {
        return std::nullopt;
    }
    const QFileInfo cached(QDir(m_directory).filePath(key + QStringLiteral(".ftwf")));
    if (!cached.isFile() || cached.isSymLink()) {
        return std::nullopt;
    }
    QFile file(cached.absoluteFilePath());
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
        binsPerSecond == 0 || binsPerSecond > 1000 || count == 0 || count > kMaxBins ||
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
    if (!validCacheKey(key) || data.isEmpty() || data.binsPerSecond > 1000 ||
        data.peaks.size() > kMaxBins) {
        return false;
    }
    const QFileInfo cached(QDir(m_directory).filePath(key + QStringLiteral(".ftwf")));
    if (cached.isSymLink() || (cached.exists() && !cached.isFile())) {
        return false;
    }
    QSaveFile file(cached.absoluteFilePath());
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    QDataStream out(&file);
    out << kMagic << kFormatVersion << static_cast<quint32>(data.binsPerSecond)
        << static_cast<quint32>(data.peaks.size());
    const auto count = static_cast<int>(data.peaks.size());
    if (out.writeRawData(reinterpret_cast<const char*>(data.peaks.data()), count) != count ||
        out.status() != QDataStream::Ok) {
        file.cancelWriting();
        return false;
    }
    return file.commit();
}

} // namespace ft::media
