// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/marker_chapters.h"

#include <QStringList>

namespace ft::chapters {

Result<QString> toText(const Project& project)
{
    const auto& markers = project.markers();
    if (markers.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Add timeline markers before copying chapters."));
    }
    if (markers.front().frame != 0) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Add a chapter marker at the start (frame 0)."));
    }

    const FrameRate& rate = project.settings().frameRate;
    // The validated marker/frame-rate bounds make this multiplication safe in signed 64 bits:
    // at most 1e9 timeline frames times 1e6 denominator = 1e15.
    QStringList lines;
    FrameCount previousSecond = -1;
    for (const TimelineMarker& marker : markers) {
        const FrameCount totalSeconds = marker.frame * rate.denominator() / rate.numerator();
        if (totalSeconds <= previousSecond) {
            return makeError(
                ErrorCode::InvalidArgument,
                QStringLiteral("Two markers share the same second. Space chapter markers apart."));
        }
        previousSecond = totalSeconds;

        const FrameCount hours = totalSeconds / 3600;
        const FrameCount minutes = (totalSeconds / 60) % 60;
        const FrameCount seconds = totalSeconds % 60;
        const QString timestamp =
            QStringLiteral("%1:%2:%3")
                .arg(hours, 2, 10, QLatin1Char('0'))
                .arg(minutes, 2, 10, QLatin1Char('0'))
                .arg(seconds, 2, 10, QLatin1Char('0'));
        lines.push_back(timestamp + QLatin1Char(' ') + marker.label.simplified());
    }
    return lines.join(QLatin1Char('\n'));
}

} // namespace ft::chapters
