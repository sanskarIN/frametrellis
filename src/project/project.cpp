// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "project/project.h"

#include <QSet>

#include <algorithm>

namespace ft {

Project::Project()
    : m_id(createId())
    , m_name(QStringLiteral("Untitled"))
    , m_created(QDateTime::currentDateTimeUtc())
{}

Project Project::createNew(const QString& name, const ProjectSettings& settings)
{
    Project project;
    project.m_name = name.trimmed().isEmpty() ? QStringLiteral("Untitled") : name.trimmed();
    project.m_settings = settings;
    // Video tracks are composited bottom-up, so V1 comes first.
    for (const auto& [kind, label] : {std::pair{TrackKind::Video, QStringLiteral("V1")},
                                      std::pair{TrackKind::Video, QStringLiteral("V2")},
                                      std::pair{TrackKind::Audio, QStringLiteral("A1")},
                                      std::pair{TrackKind::Audio, QStringLiteral("A2")}}) {
        Track track;
        track.id = createId();
        track.kind = kind;
        track.name = label;
        (void)project.m_timeline.insertTrack(std::move(track));
    }
    return project;
}

namespace {
Status checkMarkers(const std::vector<TimelineMarker>& markers)
{
    if (markers.size() > static_cast<size_t>(kMaxTimelineMarkers)) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("Too many sequence markers (maximum 1000)."));
    }
    QSet<Id> seen;
    for (const TimelineMarker& marker : markers) {
        if (!isValidId(marker.id) || seen.contains(marker.id)) {
            return makeError(ErrorCode::InvalidArgument,
                             QStringLiteral("A sequence marker has an invalid or duplicate id."));
        }
        if (marker.frame < 0 || marker.frame > kMaxTimelineFrames) {
            return makeError(ErrorCode::OutOfRange,
                             QStringLiteral("A sequence marker is outside timeline limits."));
        }
        if (marker.label.trimmed().isEmpty() ||
            marker.label.size() > kMaxMarkerLabelLength) {
            return makeError(ErrorCode::InvalidArgument,
                             QStringLiteral("Marker names must contain 1 to 120 characters."));
        }
        seen.insert(marker.id);
    }
    return {};
}
} // namespace

Status Project::setMarkers(std::vector<TimelineMarker> markers)
{
    if (const Status checked = checkMarkers(markers); !checked) {
        return checked;
    }
    std::sort(markers.begin(), markers.end(), [](const TimelineMarker& a,
                                                 const TimelineMarker& b) {
        return a.frame == b.frame ? a.id < b.id : a.frame < b.frame;
    });
    m_markers = std::move(markers);
    return {};
}

Status Project::setSettings(const ProjectSettings& settings)
{
    if (const Status valid = settings.validate(); !valid) {
        return valid;
    }
    if (settings.frameRate != m_settings.frameRate &&
        (m_timeline.clipCount() > 0 || !m_markers.empty())) {
        return makeError(
            ErrorCode::Conflict,
            QStringLiteral("The frame rate can only be changed while clips and markers are absent."));
    }
    m_settings = settings;
    return {};
}

const MediaAsset* Project::asset(const Id& assetId) const
{
    const int index = assetIndex(assetId);
    return index < 0 ? nullptr : &m_media[static_cast<size_t>(index)];
}

int Project::assetIndex(const Id& assetId) const
{
    for (size_t i = 0; i < m_media.size(); ++i) {
        if (m_media[i].id == assetId) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

const MediaAsset* Project::assetByPath(const QString& filePath) const
{
    const QString key = canonicalMediaKey(filePath);
    const auto it = std::find_if(m_media.begin(), m_media.end(), [&](const MediaAsset& a) {
        return canonicalMediaKey(a.filePath) == key;
    });
    return it == m_media.end() ? nullptr : &*it;
}

Status Project::addAsset(MediaAsset newAsset, int index)
{
    if (!isValidId(newAsset.id) || asset(newAsset.id)) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("The media id is invalid or already used."));
    }
    if (newAsset.filePath.isEmpty()) {
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("The media has no file path."));
    }
    if (assetByPath(newAsset.filePath)) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("This file is already present in the media bin."));
    }
    if (static_cast<int>(m_media.size()) >= kMaxMediaAssets) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The project has too many media files."));
    }
    if (index < 0 || index > static_cast<int>(m_media.size())) {
        index = static_cast<int>(m_media.size());
    }
    m_media.insert(m_media.begin() + index, std::move(newAsset));
    return {};
}

Result<MediaAsset> Project::removeAsset(const Id& assetId)
{
    const int index = assetIndex(assetId);
    if (index < 0) {
        return makeError(ErrorCode::NotFound, QStringLiteral("The media was not found."));
    }
    if (m_timeline.usesAsset(assetId)) {
        return makeError(
            ErrorCode::Conflict,
            QStringLiteral("This media is used on the timeline. Remove its clips first."));
    }
    MediaAsset removed = m_media[static_cast<size_t>(index)];
    m_media.erase(m_media.begin() + index);
    return removed;
}

Status Project::replaceAsset(const MediaAsset& updated)
{
    const int index = assetIndex(updated.id);
    if (index < 0) {
        return makeError(ErrorCode::NotFound, QStringLiteral("The media was not found."));
    }
    if (updated.filePath.isEmpty()) {
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("The media has no file path."));
    }
    const QString key = canonicalMediaKey(updated.filePath);
    for (const MediaAsset& asset : m_media) {
        if (asset.id != updated.id && canonicalMediaKey(asset.filePath) == key) {
            return makeError(ErrorCode::Conflict,
                             QStringLiteral("Another media entry already uses that file."));
        }
    }
    m_media[static_cast<size_t>(index)] = updated;
    return {};
}

void Project::setAssetStatus(const Id& assetId, MediaStatus status)
{
    const int index = assetIndex(assetId);
    if (index >= 0) {
        m_media[static_cast<size_t>(index)].status = status;
    }
}

std::optional<SourceLimits> Project::sourceLimits(const Id& assetId) const
{
    const MediaAsset* a = asset(assetId);
    if (!a) {
        return std::nullopt;
    }
    SourceLimits limits;
    limits.length = a->lengthInFrames(m_settings.frameRate);
    limits.hasVideo = a->info.hasVideo;
    limits.hasAudio = a->info.hasAudio;
    return limits;
}

SourceLookup Project::sourceLookup() const
{
    return [this](const Id& assetId) { return sourceLimits(assetId); };
}

QString Project::nextTrackName(TrackKind kind) const
{
    const QString prefix = kind == TrackKind::Video ? QStringLiteral("V") : QStringLiteral("A");
    QSet<QString> used;
    for (const Track& t : m_timeline.tracks()) {
        used.insert(t.name);
    }
    for (int n = 1;; ++n) {
        const QString candidate = prefix + QString::number(n);
        if (!used.contains(candidate)) {
            return candidate;
        }
    }
}

Status Project::validate() const
{
    if (!isValidId(m_id)) {
        return makeError(ErrorCode::InvalidDocument, QStringLiteral("The project id is invalid."));
    }
    if (const Status settingsValid = m_settings.validate(); !settingsValid) {
        return makeError(ErrorCode::InvalidDocument, settingsValid.error().message);
    }
    QSet<Id> ids;
    QSet<QString> paths;
    for (const MediaAsset& a : m_media) {
        if (!isValidId(a.id) || ids.contains(a.id) || a.filePath.isEmpty()) {
            return makeError(
                ErrorCode::InvalidDocument,
                QStringLiteral("A media entry has a missing or duplicate id or path."));
        }
        const QString key = canonicalMediaKey(a.filePath);
        if (paths.contains(key)) {
            return makeError(ErrorCode::InvalidDocument,
                             QStringLiteral("Multiple media entries point to the same file."));
        }
        ids.insert(a.id);
        paths.insert(key);
    }
    if (const Status checked = checkMarkers(m_markers); !checked) {
        return makeError(ErrorCode::InvalidDocument, checked.error().message);
    }
    return m_timeline.validate(sourceLookup());
}

} // namespace ft
