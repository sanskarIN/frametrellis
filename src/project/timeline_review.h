// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "project/project.h"

#include <QByteArray>
#include <QJsonObject>

#include <vector>

namespace ft::timeline_review {

/// A half-open sequence interval in project frames, never in source frames.
struct Range
{
    FrameCount start = 0;
    FrameCount end = 0;

    [[nodiscard]] FrameCount length() const { return end - start; }
    friend bool operator==(const Range&, const Range&) = default;
};

/// A nominal media-coverage summary. It does not inspect decoded pixels or audio samples.
struct TrackSummary
{
    Id id;
    QString name;
    TrackKind kind = TrackKind::Video;
    bool hidden = false;
    bool muted = false;
    bool locked = false;
    int clips = 0;
    int transitions = 0;
    int unavailableClips = 0;
    FrameCount occupiedFrames = 0;
    FrameCount pictureFrames = 0;
    FrameCount audioFrames = 0;
};

enum class FindingKind {
    PictureGap,
    AudioGap,
    UnavailableMedia,
    MarkerBeyondEnd,
    SubtitleBeyondEnd,
    SubtitleWithoutSample,
};

QString findingCode(FindingKind kind);
QString findingTitle(FindingKind kind);

struct Finding
{
    FindingKind kind = FindingKind::PictureGap;
    Range range;
    QString subject;
    Id objectId;
    Id trackId;
    bool canSeek = false;
};

/// Count all findings, but retain only a bounded chronological sample for display/export.
inline constexpr int kMaxFindingDetails = 5000;

struct Report
{
    QString projectName;
    Id projectId;
    FrameRate rate;
    int width = 0;
    int height = 0;
    FrameCount duration = 0;
    int clips = 0;
    int transitions = 0;
    int media = 0;
    int usedMedia = 0;
    int unavailableMedia = 0;
    int unavailableClips = 0;
    int markers = 0;
    int subtitles = 0;
    FrameCount pictureFrames = 0;
    FrameCount audioFrames = 0;
    FrameCount pictureGapFrames = 0;
    FrameCount audioGapFrames = 0;
    int pictureGaps = 0;
    int audioGaps = 0;
    int markersBeyondEnd = 0;
    int subtitlesBeyondEnd = 0;
    int subtitlesWithoutSample = 0;
    int findingCount = 0;
    std::vector<TrackSummary> tracks;
    std::vector<Finding> findings;
};

/// Analyse current project state without modifying it, opening decoders or allocating per frame.
/// File availability is checked once per used asset. Coverage uses available media, track
/// visibility/mute and scalar clip mute/gain; it is not a black-frame or silence detector.
[[nodiscard]] Result<Report> build(const Project& project);

/// Stable schema with numeric frame counts and explicit exclusive ends. Source paths and
/// subtitle text are omitted so a report can be shared without exposing those fields.
[[nodiscard]] QJsonObject toJson(const Report& report, int detailLimit = kMaxFindingDetails);
[[nodiscard]] QByteArray toHtml(const Report& report);

/// Atomically save a JSON or standalone HTML report without overwriting project/source media.
[[nodiscard]] Status save(const Project& project, const QString& path,
                          const QString& projectFilePath = {});

} // namespace ft::timeline_review
