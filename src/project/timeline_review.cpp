// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/timeline_review.h"

#include "core/timecode.h"

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <tuple>

namespace ft::timeline_review {

namespace {

struct Source
{
    const MediaAsset* asset = nullptr;
    bool available = false;
};

bool rangeLess(const Range& a, const Range& b)
{
    return std::tie(a.start, a.end) < std::tie(b.start, b.end);
}

bool findingLess(const Finding& a, const Finding& b)
{
    return std::tie(a.range.start, a.range.end, a.kind, a.trackId, a.objectId, a.subject) <
           std::tie(b.range.start, b.range.end, b.kind, b.trackId, b.objectId, b.subject);
}

// The heap retains the earliest findings by a deterministic total ordering. Even
// a very large project cannot allocate one QML/report row per issue without limit.
void addFinding(Report& report, Finding finding)
{
    ++report.findingCount;
    auto& details = report.findings;
    if (details.size() < static_cast<size_t>(kMaxFindingDetails)) {
        details.push_back(std::move(finding));
        std::push_heap(details.begin(), details.end(), findingLess);
    } else if (findingLess(finding, details.front())) {
        std::pop_heap(details.begin(), details.end(), findingLess);
        details.back() = std::move(finding);
        std::push_heap(details.begin(), details.end(), findingLess);
    }
}

struct Coverage
{
    FrameCount covered = 0;
    FrameCount uncovered = 0;
    int gaps = 0;
};

// Merge intervals across lanes, including touching clips. A long sequence does
// not require a bitmap, per-frame iteration, or a decoder to calculate coverage.
Coverage analyseCoverage(std::vector<Range>& intervals, Report& report, FindingKind kind)
{
    std::sort(intervals.begin(), intervals.end(), rangeLess);
    Coverage result;
    FrameCount cursor = 0;
    const auto gap = [&](FrameCount start, FrameCount end) {
        if (end <= start) {
            return;
        }
        result.uncovered += end - start;
        ++result.gaps;
        addFinding(report, Finding{kind, {start, end}, {}, {}, {}, true});
    };

    for (const Range& range : intervals) {
        if (range.end <= cursor) {
            continue;
        }
        if (range.start > cursor) {
            gap(cursor, range.start);
            cursor = range.start;
        }
        result.covered += range.end - cursor;
        cursor = range.end;
    }
    gap(cursor, report.duration);
    return result;
}

QJsonValue frames(FrameCount value)
{
    // The timeline limit is much smaller than the exact integer range of a
    // JSON number; avoid platform-dependent overloads for std::int64_t.
    return QJsonValue(static_cast<double>(value));
}

QJsonObject findingJson(const Finding& finding, const FrameRate& rate)
{
    return {
        {QStringLiteral("code"), findingCode(finding.kind)},
        {QStringLiteral("title"), findingTitle(finding.kind)},
        {QStringLiteral("subject"), finding.subject},
        {QStringLiteral("objectId"), finding.objectId},
        {QStringLiteral("trackId"), finding.trackId},
        {QStringLiteral("startFrame"), frames(finding.range.start)},
        {QStringLiteral("endFrameExclusive"), frames(finding.range.end)},
        {QStringLiteral("durationFrames"), frames(finding.range.length())},
        {QStringLiteral("startTimecode"), timecode::format(finding.range.start, rate)},
        {QStringLiteral("endTimecodeExclusive"), timecode::format(finding.range.end, rate)},
        {QStringLiteral("canSeek"), finding.canSeek},
    };
}

QJsonObject trackJson(const TrackSummary& track)
{
    return {
        {QStringLiteral("id"), track.id},
        {QStringLiteral("name"), track.name},
        {QStringLiteral("kind"), trackKindToString(track.kind)},
        {QStringLiteral("hidden"), track.hidden},
        {QStringLiteral("muted"), track.muted},
        {QStringLiteral("locked"), track.locked},
        {QStringLiteral("clips"), track.clips},
        {QStringLiteral("transitions"), track.transitions},
        {QStringLiteral("unavailableClips"), track.unavailableClips},
        {QStringLiteral("occupiedFrames"), frames(track.occupiedFrames)},
        {QStringLiteral("pictureFrames"), frames(track.pictureFrames)},
        {QStringLiteral("audioFrames"), frames(track.audioFrames)},
    };
}

QJsonObject summaryJson(const Report& report)
{
    return {
        {QStringLiteral("durationFrames"), frames(report.duration)},
        {QStringLiteral("durationTimecode"), timecode::format(report.duration, report.rate)},
        {QStringLiteral("tracks"), static_cast<int>(report.tracks.size())},
        {QStringLiteral("clips"), report.clips},
        {QStringLiteral("transitions"), report.transitions},
        {QStringLiteral("media"), report.media},
        {QStringLiteral("usedMedia"), report.usedMedia},
        {QStringLiteral("unavailableMedia"), report.unavailableMedia},
        {QStringLiteral("unavailableClips"), report.unavailableClips},
        {QStringLiteral("markers"), report.markers},
        {QStringLiteral("subtitles"), report.subtitles},
        {QStringLiteral("pictureFrames"), frames(report.pictureFrames)},
        {QStringLiteral("audioFrames"), frames(report.audioFrames)},
        {QStringLiteral("pictureGapFrames"), frames(report.pictureGapFrames)},
        {QStringLiteral("audioGapFrames"), frames(report.audioGapFrames)},
        {QStringLiteral("pictureGaps"), report.pictureGaps},
        {QStringLiteral("audioGaps"), report.audioGaps},
        {QStringLiteral("markersBeyondEnd"), report.markersBeyondEnd},
        {QStringLiteral("subtitlesBeyondEnd"), report.subtitlesBeyondEnd},
        {QStringLiteral("subtitlesWithoutSample"), report.subtitlesWithoutSample},
        {QStringLiteral("findingCount"), report.findingCount},
    };
}

void appendRow(QString& html, const QStringList& cells, bool heading = false)
{
    const auto start = heading ? QStringLiteral("<th scope=\"col\">") : QStringLiteral("<td>");
    const auto end = heading ? QStringLiteral("</th>") : QStringLiteral("</td>");
    html += QStringLiteral("<tr>");
    for (const QString& cell : cells) {
        html += start + cell.toHtmlEscaped() + end;
    }
    html += QStringLiteral("</tr>\n");
}

QString yesNo(bool value)
{
    return value ? QStringLiteral("Yes") : QStringLiteral("No");
}

QString frameText(FrameCount value)
{
    return QString::number(value);
}

} // namespace

QString findingCode(FindingKind kind)
{
    switch (kind) {
    case FindingKind::PictureGap:
        return QStringLiteral("picture-gap");
    case FindingKind::AudioGap:
        return QStringLiteral("audio-gap");
    case FindingKind::UnavailableMedia:
        return QStringLiteral("unavailable-media");
    case FindingKind::MarkerBeyondEnd:
        return QStringLiteral("marker-beyond-end");
    case FindingKind::SubtitleBeyondEnd:
        return QStringLiteral("subtitle-beyond-end");
    case FindingKind::SubtitleWithoutSample:
        return QStringLiteral("subtitle-without-sample");
    }
    return {};
}

QString findingTitle(FindingKind kind)
{
    switch (kind) {
    case FindingKind::PictureGap:
        return QStringLiteral("No available picture placement");
    case FindingKind::AudioGap:
        return QStringLiteral("No available unmuted audio placement");
    case FindingKind::UnavailableMedia:
        return QStringLiteral("Source file unavailable");
    case FindingKind::MarkerBeyondEnd:
        return QStringLiteral("Marker beyond sequence end");
    case FindingKind::SubtitleBeyondEnd:
        return QStringLiteral("Subtitle extends beyond sequence");
    case FindingKind::SubtitleWithoutSample:
        return QStringLiteral("Subtitle has no sampled sequence frame");
    }
    return {};
}

Result<Report> build(const Project& project)
{
    if (const Status valid = project.validate(); !valid) {
        return valid.error();
    }

    Report report;
    report.projectName = project.name();
    report.projectId = project.id();
    report.rate = project.settings().frameRate;
    report.width = project.settings().width;
    report.height = project.settings().height;
    report.duration = project.timeline().duration();
    report.media = static_cast<int>(project.media().size());
    report.markers = static_cast<int>(project.markers().size());
    report.subtitles = static_cast<int>(project.subtitles().cues.size());

    QHash<Id, Source> sources;
    sources.reserve(report.media);
    for (const MediaAsset& asset : project.media()) {
        sources.insert(asset.id, Source{&asset, false});
    }

    QSet<Id> used;
    std::vector<Range> picture;
    std::vector<Range> audio;
    for (const Track& track : project.timeline().tracks()) {
        TrackSummary row;
        row.id = track.id;
        row.name = track.name;
        row.kind = track.kind;
        row.hidden = track.hidden;
        row.muted = track.muted;
        row.locked = track.locked;
        row.clips = static_cast<int>(track.clips.size());
        row.transitions = static_cast<int>(track.transitions.size());
        report.clips += row.clips;
        report.transitions += row.transitions;

        for (const Clip& clip : track.clips) {
            const Range range{clip.start, clip.end()};
            row.occupiedFrames += range.length();
            auto source = sources.find(clip.assetId);
            if (source == sources.end() || !source->asset) {
                return makeError(ErrorCode::InvalidDocument,
                                 QStringLiteral("A timeline clip has no source media."));
            }
            const MediaAsset& asset = *source->asset;
            if (!used.contains(clip.assetId)) {
                used.insert(clip.assetId);
                const QFileInfo file(asset.filePath);
                source->available = file.isFile() && file.isReadable();
                if (!source->available) {
                    ++report.unavailableMedia;
                }
            }
            if (!source->available) {
                ++row.unavailableClips;
                ++report.unavailableClips;
                addFinding(report, Finding{FindingKind::UnavailableMedia, range, asset.name,
                                           clip.id, track.id, true});
                continue;
            }

            if (track.kind == TrackKind::Video && !track.hidden && asset.info.hasVideo) {
                row.pictureFrames += range.length();
                picture.push_back(range);
            }
            // Video clips may carry audio; hiding their picture must not silence
            // their soundtrack. Locking is an editing restriction, not a mute.
            if (!track.muted && !clip.properties.muted && clip.properties.gain > 0.0 &&
                asset.info.hasAudio) {
                row.audioFrames += range.length();
                audio.push_back(range);
            }
        }
        report.tracks.push_back(std::move(row));
    }
    report.usedMedia = static_cast<int>(used.size());

    const Coverage p = analyseCoverage(picture, report, FindingKind::PictureGap);
    report.pictureFrames = p.covered;
    report.pictureGapFrames = p.uncovered;
    report.pictureGaps = p.gaps;

    const Coverage a = analyseCoverage(audio, report, FindingKind::AudioGap);
    report.audioFrames = a.covered;
    report.audioGapFrames = a.uncovered;
    report.audioGaps = a.gaps;

    for (const TimelineMarker& marker : project.markers()) {
        // A marker at the exclusive end is a valid sequence boundary.
        if (marker.frame > report.duration) {
            ++report.markersBeyondEnd;
            addFinding(report, Finding{FindingKind::MarkerBeyondEnd, {marker.frame, marker.frame},
                                       marker.label, marker.id, {}, false});
        }
    }

    for (const SubtitleCue& cue : project.subtitles().cues) {
        const auto start = subtitleFrameAtMilliseconds(cue.startMs, report.rate);
        const auto end = subtitleFrameAtMilliseconds(cue.endMs, report.rate);
        if (!start || !end) {
            return makeError(ErrorCode::InvalidDocument,
                             QStringLiteral("A subtitle has an invalid frame boundary."));
        }
        const Range range{*start, *end};
        const bool seekable = *start < report.duration;
        const QString subject =
            QStringLiteral("%1 – %2").arg(formatSubtitleTime(cue.startMs),
                                          formatSubtitleTime(cue.endMs));
        if (*start >= report.duration || *end > report.duration) {
            ++report.subtitlesBeyondEnd;
            addFinding(report, Finding{FindingKind::SubtitleBeyondEnd, range, subject, cue.id,
                                       {}, seekable});
        }
        // At a low frame rate, two millisecond boundaries can round to the same
        // first sampled frame. Such a cue may be valid for sidecar export but
        // cannot appear on the sequence's sampled preview.
        if (*start >= std::min(*end, report.duration)) {
            ++report.subtitlesWithoutSample;
            addFinding(report, Finding{FindingKind::SubtitleWithoutSample, range, subject, cue.id,
                                       {}, seekable});
        }
    }

    std::sort(report.findings.begin(), report.findings.end(), findingLess);
    return report;
}

QJsonObject toJson(const Report& report, int detailLimit)
{
    const int limit = std::clamp(detailLimit, 0, kMaxFindingDetails);
    QJsonArray details;
    for (const Finding& finding : report.findings) {
        if (details.size() >= limit) {
            break;
        }
        details.append(findingJson(finding, report.rate));
    }
    QJsonArray tracks;
    for (const TrackSummary& track : report.tracks) {
        tracks.append(trackJson(track));
    }
    return {
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("projectName"), report.projectName},
        {QStringLiteral("projectId"), report.projectId},
        {QStringLiteral("frameRate"), report.rate.rational().toString()},
        {QStringLiteral("width"), report.width},
        {QStringLiteral("height"), report.height},
        {QStringLiteral("coverageMode"), QStringLiteral("nominal-available-placements")},
        {QStringLiteral("summary"), summaryJson(report)},
        {QStringLiteral("tracks"), tracks},
        {QStringLiteral("findings"), details},
        {QStringLiteral("omittedFindings"), report.findingCount - details.size()},
    };
}

QByteArray toHtml(const Report& report)
{
    QString html = QStringLiteral(
        "<!doctype html>\n<html lang=\"en\"><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
        "<meta name=\"referrer\" content=\"no-referrer\">"
        "<meta http-equiv=\"Content-Security-Policy\" content=\"default-src 'none'; "
        "style-src 'unsafe-inline'; base-uri 'none'; form-action 'none'\">"
        "<title>FrameTrellis timeline review</title><style>"
        "body{font:16px/1.5 system-ui,sans-serif;margin:2rem auto;padding:0 1rem;"
        "max-width:1100px;color:#162231;background:#fff}"
        "h1,h2{line-height:1.2}h2{margin-top:2rem}p{max-width:80ch}"
        "table{width:100%;border-collapse:collapse;margin-bottom:1rem}"
        "td,th{text-align:left;padding:.5rem .7rem;border-bottom:1px solid #cbd5e1;"
        "vertical-align:top;overflow-wrap:anywhere}"
        "th{background:#e8eef5}tbody tr:nth-child(even){background:#f6f8fa}"
        ".scroll{overflow-x:auto}caption{text-align:left;font-weight:600;margin-bottom:.5rem}"
        "@media print{body{margin:0;padding:0}thead{display:table-header-group}"
        "tr{break-inside:avoid}}"
        "</style></head><body><h1>Timeline review</h1><p>");
    html += report.projectName.toHtmlEscaped();
    html += QStringLiteral(" &middot; %1 &times; %2 &middot; %3 fps</p>\n")
                .arg(report.width)
                .arg(report.height)
                .arg(report.rate.rational().toString().toHtmlEscaped());
    html += QStringLiteral(
        "<p>This report describes available media placements, not decoded video or audio. "
        "Uncovered ranges may be intentional. Effects, fades, transitions and opacity can "
        "change the final output. Source paths and subtitle text are omitted.</p>"
        "<h2>Sequence summary</h2><table><thead>");
    appendRow(html, {QStringLiteral("Measure"), QStringLiteral("Value")}, true);
    html += QStringLiteral("</thead><tbody>");
    const auto metric = [&](const QString& label, FrameCount value) {
        appendRow(html, {label, frameText(value)});
    };
    appendRow(html, {QStringLiteral("Duration"), timecode::format(report.duration, report.rate)});
    metric(QStringLiteral("Duration (frames)"), report.duration);
    metric(QStringLiteral("Tracks"), static_cast<FrameCount>(report.tracks.size()));
    metric(QStringLiteral("Clips"), report.clips);
    metric(QStringLiteral("Transitions"), report.transitions);
    metric(QStringLiteral("Media assets"), report.media);
    metric(QStringLiteral("Used assets"), report.usedMedia);
    metric(QStringLiteral("Unavailable used assets"), report.unavailableMedia);
    metric(QStringLiteral("Clips with unavailable sources"), report.unavailableClips);
    metric(QStringLiteral("Picture placement frames"), report.pictureFrames);
    metric(QStringLiteral("Uncovered picture frames"), report.pictureGapFrames);
    metric(QStringLiteral("Uncovered picture ranges"), report.pictureGaps);
    metric(QStringLiteral("Audio placement frames"), report.audioFrames);
    metric(QStringLiteral("Uncovered audio frames"), report.audioGapFrames);
    metric(QStringLiteral("Uncovered audio ranges"), report.audioGaps);
    metric(QStringLiteral("Markers"), report.markers);
    metric(QStringLiteral("Markers beyond end"), report.markersBeyondEnd);
    metric(QStringLiteral("Subtitles"), report.subtitles);
    metric(QStringLiteral("Subtitles beyond end"), report.subtitlesBeyondEnd);
    metric(QStringLiteral("Subtitles without sampled frames"), report.subtitlesWithoutSample);
    metric(QStringLiteral("Total findings"), report.findingCount);
    html += QStringLiteral("</tbody></table>\n<h2>Track summary</h2><div class=\"scroll\">"
                           "<table><thead>");
    appendRow(html,
              {QStringLiteral("Track"), QStringLiteral("Type"), QStringLiteral("Hidden"),
               QStringLiteral("Muted"), QStringLiteral("Locked"), QStringLiteral("Clips"),
               QStringLiteral("Transitions"), QStringLiteral("Unavailable"),
               QStringLiteral("Occupied frames"), QStringLiteral("Picture frames"),
               QStringLiteral("Audio frames")},
              true);
    html += QStringLiteral("</thead><tbody>");
    for (const TrackSummary& track : report.tracks) {
        appendRow(html, {track.name, trackKindToString(track.kind), yesNo(track.hidden),
                         yesNo(track.muted), yesNo(track.locked), frameText(track.clips),
                         frameText(track.transitions), frameText(track.unavailableClips),
                         frameText(track.occupiedFrames), frameText(track.pictureFrames),
                         frameText(track.audioFrames)});
    }
    html += QStringLiteral("</tbody></table></div>\n<h2>Review findings</h2>");
    if (report.findings.empty()) {
        html += QStringLiteral("<p>No structural findings.</p>");
    } else {
        html += QStringLiteral("<p>Showing %1 of %2 findings, in sequence order. "
                               "End frame and end timecode are exclusive.</p>")
                    .arg(report.findings.size())
                    .arg(report.findingCount);
        html += QStringLiteral("<div class=\"scroll\"><table><thead>");
        appendRow(html,
                  {QStringLiteral("Finding"), QStringLiteral("Subject"), QStringLiteral("Start"),
                   QStringLiteral("End (exclusive)"), QStringLiteral("Start frame"),
                   QStringLiteral("End frame")},
                  true);
        html += QStringLiteral("</thead><tbody>");
        for (const Finding& finding : report.findings) {
            appendRow(html,
                      {findingTitle(finding.kind), finding.subject,
                       timecode::format(finding.range.start, report.rate),
                       timecode::format(finding.range.end, report.rate),
                       frameText(finding.range.start), frameText(finding.range.end)});
        }
        html += QStringLiteral("</tbody></table></div>");
    }
    html += QStringLiteral("<footer><p>Generated by FrameTrellis. Read-only review; no media "
                           "files were modified.</p></footer></body></html>\n");
    return html.toUtf8();
}

Status save(const Project& project, const QString& path, const QString& projectFilePath)
{
    const QFileInfo destination(path);
    const QString suffix = destination.suffix().toLower();
    if (path.trimmed().isEmpty() || !destination.isAbsolute() ||
        (suffix != QLatin1String("json") && suffix != QLatin1String("html"))) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Choose an absolute .json or .html report filename."));
    }
    const QString key = canonicalMediaKey(path);
    if (project.assetByPath(path) ||
        (!projectFilePath.isEmpty() && key == canonicalMediaKey(projectFilePath))) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("The review cannot overwrite project or source media."));
    }
    if (destination.isSymLink() || (destination.exists() && !destination.isFile())) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("The report destination is not a regular file."));
    }

    const auto report = build(project);
    if (!report) {
        return report.error();
    }
    const QByteArray bytes = suffix == QLatin1String("html")
                                 ? toHtml(*report)
                                 : QJsonDocument(toJson(*report)).toJson(QJsonDocument::Indented);
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly)) {
        return makeError(ErrorCode::Io, QStringLiteral("Could not open timeline review destination."),
                         output.errorString());
    }
    if (output.write(bytes) != bytes.size()) {
        output.cancelWriting();
        return makeError(ErrorCode::Io, QStringLiteral("Could not write timeline review."),
                         output.errorString());
    }
    if (!output.commit()) {
        return makeError(ErrorCode::Io, QStringLiteral("Could not finish timeline review."),
                         output.errorString());
    }
    return {};
}

} // namespace ft::timeline_review
