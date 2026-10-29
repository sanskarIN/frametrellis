// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/subtitles.h"

#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

#include <algorithm>
#include <iterator>

namespace ft {

Status validateSubtitles(const SubtitleTrack& track)
{
    if (track.cues.size() > static_cast<size_t>(kMaxSubtitleCues) || track.fontPercent < 2 ||
        track.fontPercent > 10) {
        return makeError(
            ErrorCode::OutOfRange,
            QStringLiteral("Subtitles allow at most 10000 cues and a font size of 2–10%."));
    }
    QSet<Id> ids;
    std::int64_t previousEnd = 0;
    qsizetype totalText = 0;
    for (const SubtitleCue& cue : track.cues) {
        if (!isValidId(cue.id) || ids.contains(cue.id)) {
            return makeError(ErrorCode::InvalidArgument,
                             QStringLiteral("A subtitle has an invalid or duplicate identifier."));
        }
        if (cue.startMs < 0 || cue.endMs <= cue.startMs || cue.endMs > kMaxSubtitleTimeMs) {
            return makeError(
                ErrorCode::OutOfRange,
                QStringLiteral("Subtitle times must increase within 00:00:00,000–999:59:59,999."));
        }
        if (cue.startMs < previousEnd) {
            return makeError(
                ErrorCode::Conflict,
                QStringLiteral(
                    "Subtitle cues overlap. This subtitle track supports one cue at a time."));
        }
        if (cue.text.isEmpty() || cue.text.size() > kMaxSubtitleTextLength) {
            return makeError(ErrorCode::OutOfRange,
                             QStringLiteral("Each subtitle needs 1–2000 characters."));
        }
        totalText += cue.text.size();
        if (totalText > kMaxSubtitleTotalTextLength) {
            return makeError(ErrorCode::OutOfRange,
                             QStringLiteral("The subtitle track contains too much text."));
        }
        for (qsizetype i = 0; i < cue.text.size(); ++i) {
            const QChar ch = cue.text.at(i);
            if ((ch.category() == QChar::Other_Control && ch != QLatin1Char('\n') &&
                 ch != QLatin1Char('\t')) ||
                ch.isLowSurrogate()) {
                return makeError(ErrorCode::InvalidArgument,
                                 QStringLiteral("Subtitle text contains an unsupported control or "
                                                "invalid Unicode character."));
            }
            if (ch.isHighSurrogate()) {
                if (i + 1 >= cue.text.size() || !cue.text.at(i + 1).isLowSurrogate()) {
                    return makeError(ErrorCode::InvalidArgument,
                                     QStringLiteral("Subtitle text contains invalid Unicode."));
                }
                ++i;
            }
        }
        const QStringList lines = cue.text.split(QLatin1Char('\n'));
        for (const QString& line : lines) {
            if (line.trimmed().isEmpty()) {
                return makeError(
                    ErrorCode::InvalidArgument,
                    QStringLiteral(
                        "Blank lines separate SRT cues; remove blank lines inside subtitle text."));
            }
        }
        ids.insert(cue.id);
        previousEnd = cue.endMs;
    }
    return {};
}

std::optional<std::int64_t> subtitleMillisecondsAtFrame(FrameCount frame, const FrameRate& rate)
{
    if (frame < 0 || !rate.isValid()) {
        return std::nullopt;
    }
    return mulDiv(frame, rate.denominator() * 1000, rate.numerator(), Rounding::Down);
}

std::optional<FrameCount> subtitleFrameAtMilliseconds(std::int64_t milliseconds,
                                                      const FrameRate& rate)
{
    if (milliseconds < 0 || milliseconds > kMaxSubtitleTimeMs || !rate.isValid()) {
        return std::nullopt;
    }
    return mulDiv(milliseconds, rate.numerator(), rate.denominator() * 1000, Rounding::Up);
}

const SubtitleCue* subtitleAtFrame(const SubtitleTrack& track, FrameCount frame,
                                   const FrameRate& rate)
{
    const auto ms = subtitleMillisecondsAtFrame(frame, rate);
    if (!ms) {
        return nullptr;
    }
    const auto after = std::upper_bound(
        track.cues.begin(), track.cues.end(), *ms,
        [](std::int64_t time, const SubtitleCue& cue) { return time < cue.startMs; });
    if (after == track.cues.begin()) {
        return nullptr;
    }
    const SubtitleCue& cue = *std::prev(after);
    return *ms < cue.endMs ? &cue : nullptr;
}

QString formatSubtitleTime(std::int64_t ms)
{
    if (ms < 0 || ms > kMaxSubtitleTimeMs) {
        return {};
    }
    return QStringLiteral("%1:%2:%3,%4")
        .arg(ms / 3'600'000, 2, 10, QLatin1Char('0'))
        .arg(ms / 60'000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms % 1000, 3, 10, QLatin1Char('0'));
}

Result<std::int64_t> parseSubtitleTime(const QString& text)
{
    static const QRegularExpression pattern(
        QStringLiteral("\\A([0-9]{2,3}):([0-5][0-9]):([0-5][0-9]),([0-9]{3})\\z"));
    const auto match = pattern.match(text);
    if (!match.hasMatch()) {
        return makeError(
            ErrorCode::InvalidArgument,
            QStringLiteral("Use subtitle time HH:MM:SS,mmm (hours may have three digits)."));
    }
    return match.captured(1).toLongLong() * 3'600'000 + match.captured(2).toLongLong() * 60'000 +
           match.captured(3).toLongLong() * 1000 + match.captured(4).toLongLong();
}

namespace srt {

Result<SubtitleTrack> fromBytes(const QByteArray& bytes)
{
    if (bytes.size() > kMaxSrtBytes) {
        return makeError(ErrorCode::OutOfRange, QStringLiteral("SRT files must be at most 8 MiB."));
    }
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    QString text = decoder(bytes);
    if (decoder.hasError()) {
        return makeError(ErrorCode::InvalidDocument,
                         QStringLiteral("Import an SRT file encoded as valid UTF-8."));
    }
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    const QStringList lines = text.split(QLatin1Char('\n'));
    SubtitleTrack track;
    static const QRegularExpression counter(QStringLiteral("^[0-9]{1,9}$"));
    static const QRegularExpression timing(
        QStringLiteral("^([^ \\t]+)[ \\t]*-->[ \\t]*([^ \\t]+)$"));
    qsizetype totalText = 0;
    qsizetype row = 0;
    const auto error = [&](const QString& detail) {
        return makeError(ErrorCode::InvalidDocument,
                         QStringLiteral("Invalid SRT near line %1: %2").arg(row + 1).arg(detail));
    };
    while (row < lines.size()) {
        if (lines.at(row).trimmed().isEmpty()) {
            ++row;
            continue;
        }
        if (!counter.match(lines.at(row).trimmed()).hasMatch() ||
            lines.at(row).trimmed().toLongLong() == 0) {
            return error(QStringLiteral("expected a positive numeric cue counter"));
        }
        ++row;
        if (row >= lines.size()) {
            return error(QStringLiteral("missing timing line"));
        }
        const auto match = timing.match(lines.at(row).trimmed());
        if (!match.hasMatch()) {
            return error(QStringLiteral("expected start --> end timestamps"));
        }
        const auto start = parseSubtitleTime(match.captured(1));
        const auto end = parseSubtitleTime(match.captured(2));
        if (!start || !end) {
            return error(QStringLiteral("use HH:MM:SS,mmm timestamps"));
        }
        QStringList caption;
        qsizetype cueLength = 0;
        ++row;
        while (row < lines.size() && !lines.at(row).trimmed().isEmpty()) {
            cueLength += lines.at(row).size() + (caption.isEmpty() ? 0 : 1);
            if (cueLength > kMaxSubtitleTextLength) {
                return error(QStringLiteral("a cue exceeds 2000 characters"));
            }
            caption.append(lines.at(row));
            ++row;
        }
        totalText += cueLength;
        if (track.cues.size() >= static_cast<size_t>(kMaxSubtitleCues) ||
            totalText > kMaxSubtitleTotalTextLength) {
            return error(QStringLiteral("subtitle count or total text limit exceeded"));
        }
        track.cues.push_back({createId(), *start, *end, caption.join(QLatin1Char('\n'))});
    }
    std::sort(track.cues.begin(), track.cues.end(),
              [](const SubtitleCue& a, const SubtitleCue& b) { return a.startMs < b.startMs; });
    if (const Status valid = validateSubtitles(track); !valid) {
        return makeError(ErrorCode::InvalidDocument, valid.error().message);
    }
    return track;
}

Result<QByteArray> toBytes(const SubtitleTrack& track)
{
    if (const Status valid = validateSubtitles(track); !valid) {
        return valid.error();
    }
    QByteArray bytes;
    int index = 1;
    for (const SubtitleCue& cue : track.cues) {
        bytes += QStringLiteral("%1\n%2 --> %3\n%4\n\n")
                     .arg(index++)
                     .arg(formatSubtitleTime(cue.startMs), formatSubtitleTime(cue.endMs), cue.text)
                     .toUtf8();
    }
    return bytes;
}

} // namespace srt
} // namespace ft
