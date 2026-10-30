// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"
#include "project/media_asset.h"
#include "project/project_settings.h"
#include "project/subtitles.h"
#include "timeline/timeline.h"

#include <QDateTime>
#include <QString>

#include <vector>

namespace ft {

/// Named, frame-accurate review cue independent of media and track edits.
struct TimelineMarker
{
    Id id;
    FrameCount frame = 0;
    QString label;

    friend bool operator==(const TimelineMarker&, const TimelineMarker&) = default;
};

inline constexpr int kMaxTimelineMarkers = 1000;
inline constexpr int kMaxMarkerLabelLength = 120;

/// The complete editable state of a project: settings, media references and the timeline.
/// Project is a value type; ProjectDocument wraps it with undo history and change signals.
class Project
{
public:
    Project();

    /// A new project with two video and two audio tracks.
    static Project createNew(const QString& name, const ProjectSettings& settings);

    [[nodiscard]] const Id& id() const { return m_id; }
    void setId(const Id& id) { m_id = id; }
    [[nodiscard]] const QString& name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }
    [[nodiscard]] const QDateTime& created() const { return m_created; }
    void setCreated(const QDateTime& created) { m_created = created; }

    [[nodiscard]] const ProjectSettings& settings() const { return m_settings; }
    Status setSettings(const ProjectSettings& settings);

    [[nodiscard]] const std::vector<MediaAsset>& media() const { return m_media; }
    [[nodiscard]] const MediaAsset* asset(const Id& assetId) const;
    [[nodiscard]] const MediaAsset* assetByPath(const QString& filePath) const;
    [[nodiscard]] int assetIndex(const Id& assetId) const;
    Status addAsset(MediaAsset asset, int index = -1);
    /// Removes an unused asset; fails with ErrorCode::Conflict while clips reference it.
    Result<MediaAsset> removeAsset(const Id& assetId);
    /// Replaces an asset with the same id (used for relinking and renaming).
    Status replaceAsset(const MediaAsset& asset);
    void setAssetStatus(const Id& assetId, MediaStatus status);

    /// Sequence markers are ordered by frame then identifier. They survive source edits.
    [[nodiscard]] const std::vector<TimelineMarker>& markers() const { return m_markers; }
    /// Atomically replaces the marker collection after validating bounds, names and unique ids.
    Status setMarkers(std::vector<TimelineMarker> markers);
    [[nodiscard]] const SubtitleTrack& subtitles() const { return m_subtitles; }
    /// Canonicalises order and atomically validates the entire sequence subtitle track.
    Status setSubtitles(SubtitleTrack subtitles);

    [[nodiscard]] const Timeline& timeline() const { return m_timeline; }
    Timeline& timeline() { return m_timeline; }

    /// Source limits backed by this project's media. The returned function refers to this
    /// object and must not outlive it.
    [[nodiscard]] SourceLookup sourceLookup() const;
    [[nodiscard]] std::optional<SourceLimits> sourceLimits(const Id& assetId) const;

    /// Suggested name for a new track of the given kind, e.g. "V3".
    [[nodiscard]] QString nextTrackName(TrackKind kind) const;

    [[nodiscard]] Status validate() const;

    friend bool operator==(const Project&, const Project&) = default;

private:
    Id m_id;
    QString m_name;
    QDateTime m_created;
    ProjectSettings m_settings;
    std::vector<MediaAsset> m_media;
    std::vector<TimelineMarker> m_markers;
    SubtitleTrack m_subtitles;
    Timeline m_timeline;
};

inline constexpr int kMaxMediaAssets = 10'000;

} // namespace ft
