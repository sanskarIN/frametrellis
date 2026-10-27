// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/edl_export.h"

#include "core/timecode.h"

#include <QHash>
#include <QStringList>

namespace ft::interchange {

namespace {

QString cleanComment(QString text)
{
    // EDL comments must not be able to introduce additional records.
    for (qsizetype i = 0; i < text.size(); ++i) {
        if (text.at(i).isNull() || !text.at(i).isPrint()) {
            text[i] = QLatin1Char(' ');
        }
    }
    return text.simplified();
}

bool isCmxRate(const FrameRate& rate)
{
    const auto n = rate.numerator();
    const auto d = rate.denominator();
    return (d == 1 && (n == 24 || n == 25 || n == 30)) ||
           (n == 24000 && d == 1001) || (n == 30000 && d == 1001);
}

QString tc(FrameCount frame, const FrameRate& rate)
{
    return timecode::format(frame, rate, timecode::Style::Automatic);
}

QString eventLine(int number, const QString& reel, FrameCount sourceIn,
                  FrameCount sourceOut, FrameCount recordIn, FrameCount recordOut,
                  const FrameRate& rate)
{
    return QStringLiteral("%1  %2 V     C        %3 %4 %5 %6")
        .arg(number, 3, 10, QLatin1Char('0'))
        .arg(reel, tc(sourceIn, rate), tc(sourceOut, rate), tc(recordIn, rate),
             tc(recordOut, rate));
}

} // namespace

Result<QString> toCmx3600(const Project& project)
{
    const FrameRate& rate = project.settings().frameRate;
    if (!isCmxRate(rate)) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("CMX3600 supports 24, 25, 30, 23.976 or 29.97 fps projects."));
    }

    const Track* picture = nullptr;
    for (const Track& track : project.timeline().tracks()) {
        if (track.kind != TrackKind::Video || track.hidden || track.clips.empty()) {
            continue;
        }
        if (picture) {
            return makeError(ErrorCode::InvalidArgument,
                             QStringLiteral("CMX3600 needs one visible video track; flatten layers first."));
        }
        picture = &track;
    }
    if (!picture) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Add picture clips to a visible video track before exporting an EDL."));
    }
    if (!picture->transitions.empty()) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("CMX3600 supports cuts only; remove video dissolves first."));
    }

    QStringList rows{QStringLiteral("TITLE: %1").arg(cleanComment(project.name())),
                     rate.supportsDropFrame() ? QStringLiteral("FCM: DROP FRAME")
                                              : QStringLiteral("FCM: NON-DROP FRAME"),
                     QString()};
    QHash<Id, QString> reels;
    int number = 0;
    FrameCount cursor = 0;
    const FrameCount nominalDay = static_cast<FrameCount>(rate.nominalFramesPerSecond()) * 3600 * 24;
    // Drop-frame numbering skips 2 frame labels per minute except every tenth minute.
    const FrameCount maxLabel = rate.supportsDropFrame() ? nominalDay - 2 * (1440 - 144)
                                                         : nominalDay;
    const auto addEvent = [&](const QString& reel, FrameCount sourceIn, FrameCount sourceOut,
                              FrameCount recordIn, FrameCount recordOut) -> bool {
        if (number >= 999 || sourceOut >= maxLabel || recordOut >= maxLabel) {
            return false;
        }
        rows.push_back(eventLine(++number, reel, sourceIn, sourceOut, recordIn, recordOut, rate));
        return true;
    };

    for (const Clip& clip : picture->clips) {
        const MediaAsset* media = project.asset(clip.assetId);
        if (!media || !media->info.hasVideo || media->info.kind == MediaKind::Image) {
            return makeError(ErrorCode::InvalidArgument,
                             QStringLiteral("CMX3600 requires video media for every picture clip."));
        }
        if (media->info.frameRate.isPositive() &&
            !(media->info.frameRate == rate.rational())) {
            return makeError(ErrorCode::InvalidArgument,
                             QStringLiteral("CMX3600 requires source and project frame rates to match."));
        }
        if (!clip.properties.video.isNeutral() || clip.properties.fadeIn > 0 ||
            clip.properties.fadeOut > 0) {
            return makeError(ErrorCode::InvalidArgument,
                             QStringLiteral("CMX3600 cannot describe picture adjustments or fades."));
        }

        if (cursor < clip.start) {
            const FrameCount gap = clip.start - cursor;
            if (!addEvent(QStringLiteral("BL"), 0, gap, cursor, clip.start)) {
                return makeError(ErrorCode::OutOfRange,
                                 QStringLiteral("CMX3600 is limited to 999 events and 24-hour timecodes."));
            }
            rows.push_back(QString());
        }
        if (!reels.contains(media->id)) {
            reels.insert(media->id, QStringLiteral("FT%1")
                                        .arg(reels.size() + 1, 6, 10, QLatin1Char('0')));
        }
        if (!addEvent(reels.value(media->id), clip.sourceIn, clip.sourceOut(),
                      clip.start, clip.end())) {
            return makeError(ErrorCode::OutOfRange,
                             QStringLiteral("CMX3600 is limited to 999 events and 24-hour timecodes."));
        }
        rows.push_back(QStringLiteral("* FROM CLIP NAME: %1").arg(cleanComment(media->name)));
        rows.push_back(QStringLiteral("* SOURCE FILE: %1").arg(cleanComment(media->filePath)));
        rows.push_back(QString());
        cursor = clip.end();
    }
    return rows.join(QLatin1Char('\n'));
}

} // namespace ft::interchange
