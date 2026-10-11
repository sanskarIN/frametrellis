// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "core/timecode.h"

#include <QStringList>

#include <cmath>

namespace ft::timecode {

namespace {

struct DropFrameParams
{
    int nominal = 0;
    int dropPerMinute = 0;
    FrameCount framesPerMinute = 0;
    FrameCount framesPerTenMinutes = 0;
};

DropFrameParams dropFrameParams(const FrameRate& rate)
{
    DropFrameParams p;
    p.nominal = rate.nominalFramesPerSecond();
    p.dropPerMinute = p.nominal == 60 ? 4 : 2;
    p.framesPerMinute = static_cast<FrameCount>(p.nominal) * 60 - p.dropPerMinute;
    p.framesPerTenMinutes =
        static_cast<FrameCount>(p.nominal) * 600 - static_cast<FrameCount>(p.dropPerMinute) * 9;
    return p;
}

bool useDropFrame(const FrameRate& rate, Style style)
{
    switch (style) {
    case Style::NonDropFrame:
        return false;
    case Style::DropFrame:
    case Style::Automatic:
        return rate.supportsDropFrame();
    }
    return false;
}

QString pad(qint64 value, int width = 2)
{
    return QStringLiteral("%1").arg(value, width, 10, QLatin1Char('0'));
}

} // namespace

QString format(FrameCount frames, const FrameRate& rate, Style style)
{
    if (!rate.isValid()) {
        return QStringLiteral("--:--:--:--");
    }
    const bool negative = frames < 0;
    FrameCount value = negative ? -frames : frames;
    const bool drop = useDropFrame(rate, style);
    const int nominal = rate.nominalFramesPerSecond();

    if (drop) {
        // Convert the real frame count into the frame label by re-inserting dropped numbers.
        const DropFrameParams p = dropFrameParams(rate);
        const FrameCount tens = value / p.framesPerTenMinutes;
        const FrameCount rem = value % p.framesPerTenMinutes;
        value += static_cast<FrameCount>(p.dropPerMinute) * 9 * tens;
        if (rem > p.dropPerMinute) {
            value += p.dropPerMinute * ((rem - p.dropPerMinute) / p.framesPerMinute);
        }
    }

    const FrameCount ff = value % nominal;
    const FrameCount totalSeconds = value / nominal;
    const FrameCount ss = totalSeconds % 60;
    const FrameCount mm = (totalSeconds / 60) % 60;
    const FrameCount hh = totalSeconds / 3600;
    const QChar last = drop ? QLatin1Char(';') : QLatin1Char(':');
    return QStringLiteral("%1%2:%3:%4%5%6")
        .arg(negative ? QStringLiteral("-") : QString(), pad(hh), pad(mm), pad(ss))
        .arg(last)
        .arg(pad(ff, nominal > 99 ? 3 : 2));
}

std::optional<FrameCount> parse(const QString& text, const FrameRate& rate)
{
    if (!rate.isValid()) {
        return std::nullopt;
    }
    QString input = text.trimmed();
    bool negative = false;
    if (input.startsWith(QLatin1Char('-'))) {
        negative = true;
        input = input.mid(1);
    }
    if (input.isEmpty()) {
        return std::nullopt;
    }
    const bool dropLabel = input.contains(QLatin1Char(';'));
    if (dropLabel && !rate.supportsDropFrame()) {
        return std::nullopt;
    }
    QString normalised = input;
    normalised.replace(QLatin1Char(';'), QLatin1Char(':'));
    normalised.replace(QLatin1Char('.'), QLatin1Char(':'));
    const QStringList parts = normalised.split(QLatin1Char(':'));
    if (parts.size() > 4) {
        return std::nullopt;
    }
    FrameCount fields[4] = {0, 0, 0, 0}; // hh, mm, ss, ff
    const int offset = 4 - static_cast<int>(parts.size());
    for (int i = 0; i < parts.size(); ++i) {
        bool ok = false;
        const QString part = parts.at(i).trimmed();
        if (part.isEmpty()) {
            return std::nullopt;
        }
        const qlonglong value = part.toLongLong(&ok);
        if (!ok || value < 0) {
            return std::nullopt;
        }
        fields[offset + i] = value;
    }
    const int nominal = rate.nominalFramesPerSecond();
    const FrameCount hh = fields[0];
    const FrameCount mm = fields[1];
    const FrameCount ss = fields[2];
    const FrameCount ff = fields[3];
    // A bare frame number may be arbitrarily large; structured fields must be in range.
    if (parts.size() > 1 && (ff >= nominal || ss >= 60 || (parts.size() > 2 && mm >= 60))) {
        return std::nullopt;
    }
    if (hh > 99'999) {
        return std::nullopt;
    }
    FrameCount total = ((hh * 3600) + (mm * 60) + ss) * nominal + ff;
    if (dropLabel) {
        const DropFrameParams p = dropFrameParams(rate);
        if (ss == 0 && mm % 10 != 0 && ff < p.dropPerMinute) {
            return std::nullopt; // these labels are skipped in drop-frame counting
        }
        const FrameCount totalMinutes = hh * 60 + mm;
        total -= p.dropPerMinute * (totalMinutes - totalMinutes / 10);
    }
    return negative ? -total : total;
}

QString formatClock(FrameCount frames, const FrameRate& rate)
{
    if (!rate.isValid()) {
        return QStringLiteral("--");
    }
    const bool negative = frames < 0;
    const auto micros = rate.framesToMicroseconds(negative ? -frames : frames);
    if (!micros) {
        return QStringLiteral("--");
    }
    const qint64 totalMillis = (*micros + 500) / 1000;
    const qint64 millis = totalMillis % 1000;
    const qint64 totalSeconds = totalMillis / 1000;
    const qint64 seconds = totalSeconds % 60;
    const qint64 minutes = (totalSeconds / 60) % 60;
    const qint64 hours = totalSeconds / 3600;
    QString result = negative ? QStringLiteral("-") : QString();
    if (hours > 0) {
        result += QStringLiteral("%1:%2:%3.%4")
                      .arg(hours)
                      .arg(pad(minutes), pad(seconds), pad(millis, 3));
    } else {
        result += QStringLiteral("%1:%2.%3").arg(minutes).arg(pad(seconds), pad(millis, 3));
    }
    return result;
}

} // namespace ft::timecode
