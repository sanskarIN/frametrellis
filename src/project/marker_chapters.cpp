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
        const QString timestamp = QStringLiteral("%1:%2:%3")
                                      .arg(hours, 2, 10, QLatin1Char('0'))
                                      .arg(minutes, 2, 10, QLatin1Char('0'))
                                      .arg(seconds, 2, 10, QLatin1Char('0'));
        lines.push_back(timestamp + QLatin1Char(' ') + marker.label.simplified());
    }
    return lines.join(QLatin1Char('\n'));
}

namespace {

QString webVttTimestamp(FrameCount frame, const FrameRate& rate)
{
    // All validated rates use numerator/denominator <= 1e6, with frames <= 1e9.
    // Multiplication therefore stays <= 1e18, safely inside signed 64-bit range.
    const FrameCount millis = frame * rate.denominator() * 1000 / rate.numerator();
    return QStringLiteral("%1:%2:%3.%4")
        .arg(millis / 3'600'000, 2, 10, QLatin1Char('0'))
        .arg((millis / 60'000) % 60, 2, 10, QLatin1Char('0'))
        .arg((millis / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(millis % 1000, 3, 10, QLatin1Char('0'));
}

} // namespace

Result<QString> toWebVtt(const Project& project)
{
    const auto& markers = project.markers();
    const FrameCount sequenceEnd = project.timeline().duration();
    if (sequenceEnd <= 0) {
        return makeError(
            ErrorCode::InvalidArgument,
            QStringLiteral("Add media to the sequence before exporting WebVTT chapters."));
    }
    if (markers.empty() || markers.front().frame != 0) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Place the first chapter marker at frame zero."));
    }
    const FrameRate& rate = project.settings().frameRate;
    QStringList output{QStringLiteral("WEBVTT"), QString()};
    for (size_t i = 0; i < markers.size(); ++i) {
        const FrameCount start = markers[i].frame;
        const FrameCount end = (i + 1 < markers.size()) ? markers[i + 1].frame : sequenceEnd;
        if (start >= sequenceEnd || end <= start || end > sequenceEnd) {
            return makeError(
                ErrorCode::InvalidArgument,
                QStringLiteral(
                    "Each WebVTT chapter must have a distinct frame inside the sequence."));
        }
        const FrameCount startMs = start * rate.denominator() * 1000 / rate.numerator();
        const FrameCount endMs = end * rate.denominator() * 1000 / rate.numerator();
        if (startMs >= endMs) {
            return makeError(
                ErrorCode::InvalidArgument,
                QStringLiteral("Chapter cue timing cannot be represented in milliseconds."));
        }
        output.push_back(webVttTimestamp(start, rate) + QStringLiteral(" --> ") +
                         webVttTimestamp(end, rate));
        // Prevent marker text from becoming executable WebVTT markup and normalize
        // whitespace so one named marker always occupies one cue payload line.
        QString label = markers[i].label.simplified();
        label.replace(QStringLiteral("-->"), QString::fromUtf8("→"));
        output.push_back(label.toHtmlEscaped());
        output.push_back(QString());
    }
    return output.join(QLatin1Char('\n'));
}

} // namespace ft::chapters
