// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/project_settings.h"

#include <algorithm>
#include <iterator>

namespace ft {

Status ProjectSettings::validate() const
{
    if (width < kMinFrameDimension || height < kMinFrameDimension || width > kMaxFrameDimension ||
        height > kMaxFrameDimension) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The frame size must be between %1 and %2 pixels.")
                             .arg(kMinFrameDimension)
                             .arg(kMaxFrameDimension));
    }
    if (width % 2 != 0 || height % 2 != 0) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("The frame width and height must be even numbers."));
    }
    if (!frameRate.isValid()) {
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("The frame rate is invalid."));
    }
    if (frameRate.toDouble() < 1.0 || frameRate.toDouble() > 240.0) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The frame rate must be between 1 and 240 fps."));
    }
    static constexpr int kRates[] = {22050, 32000, 44100, 48000, 88200, 96000};
    if (std::find(std::begin(kRates), std::end(kRates), sampleRate) == std::end(kRates)) {
        return makeError(
            ErrorCode::InvalidArgument,
            QStringLiteral("The audio sample rate %1 Hz is not supported.").arg(sampleRate));
    }
    if (audioChannels != 1 && audioChannels != 2) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Only mono and stereo audio are supported."));
    }
    return {};
}

QString ProjectSettings::summary() const
{
    return QStringLiteral("%1×%2, %3, %4 kHz %5")
        .arg(width)
        .arg(height)
        .arg(frameRate.displayName())
        .arg(QString::number(sampleRate / 1000.0, 'g', 4))
        .arg(audioChannels == 1 ? QStringLiteral("mono") : QStringLiteral("stereo"));
}

QList<ProjectPreset> projectPresets()
{
    auto make = [](const QString& name, int w, int h, std::int64_t num, std::int64_t den) {
        ProjectSettings s;
        s.width = w;
        s.height = h;
        s.frameRate = *FrameRate::create(num, den);
        return ProjectPreset{name, s};
    };
    return {
        make(QStringLiteral("HD 1080p 30 fps"), 1920, 1080, 30, 1),
        make(QStringLiteral("HD 1080p 29.97 fps"), 1920, 1080, 30000, 1001),
        make(QStringLiteral("HD 1080p 25 fps"), 1920, 1080, 25, 1),
        make(QStringLiteral("HD 1080p 24 fps"), 1920, 1080, 24, 1),
        make(QStringLiteral("HD 1080p 23.976 fps"), 1920, 1080, 24000, 1001),
        make(QStringLiteral("HD 1080p 60 fps"), 1920, 1080, 60, 1),
        make(QStringLiteral("HD 720p 30 fps"), 1280, 720, 30, 1),
        make(QStringLiteral("UHD 2160p 30 fps"), 3840, 2160, 30, 1),
        make(QStringLiteral("UHD 2160p 25 fps"), 3840, 2160, 25, 1),
        make(QStringLiteral("Vertical 1080×1920 30 fps"), 1080, 1920, 30, 1),
        make(QStringLiteral("Square 1080×1080 30 fps"), 1080, 1080, 30, 1),
    };
}

} // namespace ft
