// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "timeline/track.h"

namespace ft {

QString trackKindToString(TrackKind kind)
{
    switch (kind) {
    case TrackKind::Video:
        return QStringLiteral("video");
    case TrackKind::Audio:
        return QStringLiteral("audio");
    }
    return {};
}

std::optional<TrackKind> trackKindFromString(const QString& text)
{
    if (text == QLatin1String("video")) {
        return TrackKind::Video;
    }
    if (text == QLatin1String("audio")) {
        return TrackKind::Audio;
    }
    return std::nullopt;
}

} // namespace ft
