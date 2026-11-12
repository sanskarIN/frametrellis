// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors

#include "media/waveform.h"

#include <QTest>

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
        for (const std::int64_t duration : {
                 std::int64_t{0}, std::int64_t{-1},
                 std::numeric_limits<std::int64_t>::min(),
                 std::numeric_limits<std::int64_t>::max()}) {
            info.durationUs = duration;
            const auto result = media::computeWaveform(
                QStringLiteral("nonexistent-media-file.wav"), info);
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
            const auto result = media::computeWaveform(
                QStringLiteral("nonexistent-media-file.wav"), info, resolution);
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
            const auto result = media::computeWaveform(
                QStringLiteral("nonexistent-media-file.wav"), info, resolution);
            QVERIFY(!result);
            QCOMPARE(result.error().code, ErrorCode::InvalidArgument);
        }
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
