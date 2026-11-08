// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/marker_chapters.h"

#include "core/ids.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace ft::chapters {

Result<std::vector<TimelineMarker>> fromText(const Project& project, const QString& text)
{
    // Untrusted clipboard text must not allocate unlimited temporary markers.
    constexpr qsizetype kMaxChapterTextLength = qsizetype{128} * 1024;
    if (text.size() > kMaxChapterTextLength) {
        return makeError(ErrorCode::OutOfRange, QStringLiteral("Chapter list is too large."));
    }
    const FrameCount sequenceEnd = project.timeline().duration();
    if (sequenceEnd <= 0) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Add video to the timeline before importing chapters."));
    }
    const QStringList lines = text.split(QLatin1Char('\n'));
    if (lines.size() > kMaxTimelineMarkers + 1) {
        return makeError(ErrorCode::OutOfRange, QStringLiteral("Too many chapter lines."));
    }
    static const QRegularExpression chapterLine(
        QStringLiteral(R"(^(?:(\d{1,6}):)?([0-5]?\d):([0-5]\d)\s+(.+)$)"));
    const FrameRate& rate = project.settings().frameRate;
    QSet<FrameCount> occupied;
    for (const TimelineMarker& existing : project.markers()) {
        occupied.insert(existing.frame);
    }
    std::vector<TimelineMarker> parsed;
    FrameCount previous = -1;
    for (const QString& raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty()) {
            continue;
        }
        const auto match = chapterLine.match(line);
        if (!match.hasMatch()) {
            return makeError(
                ErrorCode::InvalidArgument,
                QStringLiteral("Expected MM:SS or HH:MM:SS followed by a chapter title."));
        }
        bool hoursOk = true;
        const qint64 hours =
            match.captured(1).isEmpty() ? 0 : match.captured(1).toLongLong(&hoursOk);
        bool minutesOk = false;
        const qint64 minutes = match.captured(2).toLongLong(&minutesOk);
        bool secondsOk = false;
        const qint64 seconds = match.captured(3).toLongLong(&secondsOk);
        const QString title = match.captured(4).simplified();
        if (!hoursOk || !minutesOk || !secondsOk || title.isEmpty() ||
            title.size() > kMaxMarkerLabelLength) {
            return makeError(
                ErrorCode::InvalidArgument,
                QStringLiteral("Chapter time or title is invalid (max 120 characters)."));
        }
        // Up to six digits of hours and 1e6 rate numerator: products stay
        // well below 2^63. Ceil gives the earliest frame at/after the stated time.
        const qint64 totalSeconds = hours * 3600 + minutes * 60 + seconds;
        const FrameCount numerator = totalSeconds * rate.numerator();
        const FrameCount frame = (numerator + rate.denominator() - 1) / rate.denominator();
        if (frame >= sequenceEnd || frame > kMaxTimelineFrames || frame <= previous ||
            occupied.contains(frame)) {
            return makeError(ErrorCode::OutOfRange,
                             QStringLiteral("Chapters must have distinct increasing times inside "
                                            "the sequence, without overlapping existing markers."));
        }
        if (parsed.empty() && frame != 0) {
            return makeError(ErrorCode::InvalidArgument,
                             QStringLiteral("The first imported chapter must begin at 00:00."));
        }
        parsed.push_back({createId(), frame, title});
        previous = frame;
        occupied.insert(frame);
        if (project.markers().size() + parsed.size() > static_cast<size_t>(kMaxTimelineMarkers)) {
            return makeError(ErrorCode::OutOfRange, QStringLiteral("Too many sequence markers."));
        }
    }
    if (parsed.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("The chapter list contains no timestamped titles."));
    }
    return parsed;
}

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
