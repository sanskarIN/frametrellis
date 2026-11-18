// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors

#include "media/waveform.h"

#include <QTest>

#include <cmath>
#include <cstdint>
#include <limits>

using namespace ft;

class WaveformDurationTest : public QObject
{
    Q_OBJECT

private slots:
    void rejectsBadDurationsBeforeInitialisingDecoder()
    {
        MediaInfo info;
        info.kind = MediaKind::Audio;
        info.hasAudio = true;
        info.audioChannels = 2;
        info.audioSampleRate = 48'000;

        // Invalid duration metadata must be rejected before opening MLT,
        // and must never be converted to an out-of-range size_t or int.
        for (const std::int64_t duration :
             {std::int64_t{0}, std::int64_t{-1}, std::numeric_limits<std::int64_t>::min(),
              std::numeric_limits<std::int64_t>::max()}) {
            info.durationUs = duration;
            const auto result =
                media::computeWaveform(QStringLiteral("nonexistent-media-file.wav"), info);
            QVERIFY(!result);
            QCOMPARE(result.error().code, ErrorCode::Media);
        }
    }

    void rejectsVeryLargeFiniteDurationWithoutStartingDecoder()
    {
        MediaInfo info;
        info.kind = MediaKind::Audio;
        info.hasAudio = true;
        info.audioSampleRate = 48'000;
        info.audioChannels = 2;
        // Greater than kMaxBins even at just one bin per second.
        info.durationUs = 60'000'000'000'000;
        for (const int resolution : {1, 10, 100, 1000}) {
            const auto result = media::computeWaveform(QStringLiteral("nonexistent-media-file.wav"),
                                                       info, resolution);
            QVERIFY(!result);
            QCOMPARE(result.error().code, ErrorCode::Media);
        }
    }

    void invalidResolutionIsReportedBeforeEngineAvailability()
    {
        MediaInfo info;
        info.kind = MediaKind::Audio;
        info.hasAudio = true;
        info.durationUs = 2'000'000;
        for (const int resolution : {-1, 0, 1001, std::numeric_limits<int>::max()}) {
            const auto result = media::computeWaveform(QStringLiteral("nonexistent-media-file.wav"),
                                                       info, resolution);
            QVERIFY(!result);
            QCOMPARE(result.error().code, ErrorCode::InvalidArgument);
        }
    }

    void invalidWaveformDrawParametersProduceTransparentImage()
    {
        media::WaveformData data;
        data.binsPerSecond = 100;
        data.peaks = {0, 64, 128, 255, 128, 64, 0};
        const QSize size(70, 20);
        const QColor color(Qt::white);
        const auto isTransparent = [](const QImage& image) {
            if (image.isNull()) {
                return false;
            }
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x) {
                    if (image.pixelColor(x, y).alpha() != 0) {
                        return false;
                    }
                }
            }
            return true;
        };

        const auto normal = media::renderWaveform(data, 0.0, 0.07, size, color);
        QVERIFY(!isTransparent(normal));
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double inf = std::numeric_limits<double>::infinity();
        for (double start : {nan, inf, -inf, -1.0, 1.0e300}) {
            QVERIFY(isTransparent(media::renderWaveform(data, start, 0.07, size, color)));
        }
        for (double seconds : {nan, inf, -inf, -1.0, 0.0, 1.0e300}) {
            QVERIFY(isTransparent(media::renderWaveform(data, 0.0, seconds, size, color)));
        }
        for (double gain : {nan, inf, -inf, -1.0}) {
            QVERIFY(isTransparent(media::renderWaveform(data, 0.0, 0.07, size, color, gain)));
        }
        QVERIFY(isTransparent(media::renderWaveform(data, inf, inf, size, color, nan)));
        // Rejected draw requests never mutate the cached waveform peaks.
        QCOMPARE(data.peaks, (std::vector<std::uint8_t>{0, 64, 128, 255, 128, 64, 0}));
    }

    void validDurationUsesNormalEngineAvailabilityCheck()
    {
        MediaInfo info;
        info.kind = MediaKind::Audio;
        info.hasAudio = true;
        info.durationUs = 2'000'000;
        const auto result =
            media::computeWaveform(QStringLiteral("nonexistent-media-file.wav"), info, 100);
        QVERIFY(!result);
        // No MLT initialisation is performed by this test.
        QCOMPARE(result.error().code, ErrorCode::Engine);
    }
};

QTEST_MAIN(WaveformDurationTest)
#include "tst_waveform_duration.moc"
