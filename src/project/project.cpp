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

Status Project::setSettings(const ProjectSettings& settings)
{
    if (const Status valid = settings.validate(); !valid) {
        return valid;
    }
    if (settings.frameRate != m_settings.frameRate && m_timeline.clipCount() > 0) {
        return makeError(
            ErrorCode::Conflict,
            QStringLiteral("The frame rate can only be changed while the timeline is empty."));
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
    for (const MediaAsset& a : m_media) {
        if (!isValidId(a.id) || ids.contains(a.id) || a.filePath.isEmpty()) {
            return makeError(
                ErrorCode::InvalidDocument,
                QStringLiteral("A media entry has a missing or duplicate id or path."));
        }
        ids.insert(a.id);
    }
    return m_timeline.validate(sourceLookup());
}

} // namespace ft
