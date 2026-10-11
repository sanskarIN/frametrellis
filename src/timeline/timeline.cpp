// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "timeline/timeline.h"

#include <QSet>

#include <algorithm>
#include <cmath>

namespace ft {

namespace {

Error notFound(const QString& what)
{
    return makeError(ErrorCode::NotFound, QStringLiteral("%1 was not found.").arg(what));
}

bool inRange(double value, double low, double high)
{
    return std::isfinite(value) && value >= low && value <= high;
}

Status checkProperties(const ClipProperties& p, FrameCount duration)
{
    if (!inRange(p.gain, 0.0, kMaxClipGain)) {
        return makeError(ErrorCode::OutOfRange, QStringLiteral("Clip volume is out of range."));
    }
    if (p.fadeIn < 0 || p.fadeOut < 0 || p.fadeIn + p.fadeOut > duration) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("Fades must fit within the clip duration."));
    }
    const VideoAdjustments& v = p.video;
    if (!inRange(v.brightness, -1.0, 1.0) || !inRange(v.contrast, 0.0, 2.0) ||
        !inRange(v.saturation, 0.0, 3.0) || !inRange(v.opacity, 0.0, 1.0)) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("A picture adjustment is out of range."));
    }
    return {};
}

void clampFades(ClipProperties& p, FrameCount duration)
{
    p.fadeIn = std::clamp<FrameCount>(p.fadeIn, 0, duration);
    p.fadeOut = std::clamp<FrameCount>(p.fadeOut, 0, duration - p.fadeIn);
}

} // namespace

int Timeline::trackIndex(const Id& trackId) const
{
    for (int i = 0; i < trackCount(); ++i) {
        if (m_tracks[static_cast<size_t>(i)].id == trackId) {
            return i;
        }
    }
    return -1;
}

const Track* Timeline::track(const Id& trackId) const
{
    const int index = trackIndex(trackId);
    return index < 0 ? nullptr : &m_tracks[static_cast<size_t>(index)];
}

Track* Timeline::mutableTrack(const Id& trackId)
{
    const int index = trackIndex(trackId);
    return index < 0 ? nullptr : &m_tracks[static_cast<size_t>(index)];
}

std::optional<ClipLocation> Timeline::findClip(const Id& clipId) const
{
    if (clipId.isEmpty()) {
        return std::nullopt;
    }
    for (size_t t = 0; t < m_tracks.size(); ++t) {
        const auto& clips = m_tracks[t].clips;
        for (size_t c = 0; c < clips.size(); ++c) {
            if (clips[c].id == clipId) {
                return ClipLocation{static_cast<int>(t), static_cast<int>(c)};
            }
        }
    }
    return std::nullopt;
}

const Clip* Timeline::clip(const Id& clipId) const
{
    const auto location = findClip(clipId);
    if (!location) {
        return nullptr;
    }
    return &m_tracks[static_cast<size_t>(location->track)]
                .clips[static_cast<size_t>(location->clip)];
}

const Track* Timeline::trackOfClip(const Id& clipId) const
{
    const auto location = findClip(clipId);
    return location ? &m_tracks[static_cast<size_t>(location->track)] : nullptr;
}

int Timeline::clipCount() const
{
    int count = 0;
    for (const Track& t : m_tracks) {
        count += static_cast<int>(t.clips.size());
    }
    return count;
}

FrameCount Timeline::duration() const
{
    FrameCount end = 0;
    for (const Track& t : m_tracks) {
        end = std::max(end, t.end());
    }
    return end;
}

bool Timeline::isRangeFree(const Id& trackId, FrameCount start, FrameCount duration,
                           const Id& ignoreClipId) const
{
    const Track* t = track(trackId);
    if (!t || duration <= 0) {
        return false;
    }
    const FrameCount end = start + duration;
    return std::none_of(t->clips.begin(), t->clips.end(), [&](const Clip& c) {
        return c.id != ignoreClipId && c.start < end && start < c.end();
    });
}

std::vector<FrameCount> Timeline::editPoints(const Id& excludeClipId) const
{
    std::vector<FrameCount> points{0};
    for (const Track& t : m_tracks) {
        for (const Clip& c : t.clips) {
            if (c.id != excludeClipId) {
                points.push_back(c.start);
                points.push_back(c.end());
            }
        }
    }
    std::sort(points.begin(), points.end());
    points.erase(std::unique(points.begin(), points.end()), points.end());
    return points;
}

bool Timeline::usesAsset(const Id& assetId) const
{
    return std::any_of(m_tracks.begin(), m_tracks.end(), [&](const Track& t) {
        return std::any_of(t.clips.begin(), t.clips.end(),
                           [&](const Clip& c) { return c.assetId == assetId; });
    });
}

std::vector<Id> Timeline::clipsUsingAsset(const Id& assetId) const
{
    std::vector<Id> ids;
    for (const Track& t : m_tracks) {
        for (const Clip& c : t.clips) {
            if (c.assetId == assetId) {
                ids.push_back(c.id);
            }
        }
    }
    return ids;
}

Status Timeline::checkEditable(const Track& t) const
{
    if (t.locked) {
        return makeError(
            ErrorCode::Conflict,
            QStringLiteral("Track \"%1\" is locked. Unlock it to edit its clips.").arg(t.name));
    }
    return {};
}

Status Timeline::checkClipShape(const Clip& c)
{
    if (!isValidId(c.id) || !isValidId(c.assetId)) {
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("The clip has an invalid id."));
    }
    if (c.duration < 1) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("A clip must be at least one frame long."));
    }
    if (c.start < 0 || c.sourceIn < 0) {
        return makeError(
            ErrorCode::OutOfRange,
            QStringLiteral("Clips cannot start before the beginning of the timeline."));
    }
    if (c.end() > kMaxTimelineFrames || c.sourceOut() > kMaxTimelineFrames) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The clip exceeds the maximum timeline length."));
    }
    return checkProperties(c.properties, c.duration);
}

Status Timeline::checkSourceRange(const Clip& c, const Track& t, const SourceLookup& sources)
{
    const std::optional<SourceLimits> limits = sources ? sources(c.assetId) : std::nullopt;
    if (!limits) {
        return makeError(ErrorCode::NotFound,
                         QStringLiteral("The clip's media is not part of the project."));
    }
    if (t.kind == TrackKind::Video && !limits->hasVideo) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Audio-only media can only be placed on audio tracks."));
    }
    if (t.kind == TrackKind::Audio && !limits->hasAudio) {
        return makeError(
            ErrorCode::InvalidArgument,
            QStringLiteral("This media has no audio, so it cannot go on an audio track."));
    }
    if (limits->length && c.sourceOut() > *limits->length) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The clip extends past the end of its source media."));
    }
    return {};
}

void Timeline::insertSorted(Track& t, Clip c)
{
    const auto pos =
        std::lower_bound(t.clips.begin(), t.clips.end(), c.start,
                         [](const Clip& a, FrameCount start) { return a.start < start; });
    t.clips.insert(pos, std::move(c));
}

Status Timeline::insertTrack(Track t, int index)
{
    if (!isValidId(t.id) || trackIndex(t.id) >= 0) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("The track id is invalid or already used."));
    }
    if (trackCount() >= kMaxTracks) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("A timeline can have at most %1 tracks.").arg(kMaxTracks));
    }
    FrameCount previousEnd = 0;
    for (const Clip& c : t.clips) {
        if (const Status shape = checkClipShape(c); !shape) {
            return shape;
        }
        if (c.start < previousEnd || findClip(c.id)) {
            return makeError(ErrorCode::InvalidArgument,
                             QStringLiteral("The track's clips are inconsistent."));
        }
        previousEnd = c.end();
    }
    if (index < 0 || index > trackCount()) {
        index = trackCount();
    }
    m_tracks.insert(m_tracks.begin() + index, std::move(t));
    return {};
}

Result<Track> Timeline::removeTrack(const Id& trackId)
{
    const int index = trackIndex(trackId);
    if (index < 0) {
        return notFound(QStringLiteral("The track"));
    }
    Track removed = std::move(m_tracks[static_cast<size_t>(index)]);
    m_tracks.erase(m_tracks.begin() + index);
    return removed;
}

Status Timeline::moveTrack(const Id& trackId, int newIndex)
{
    const int index = trackIndex(trackId);
    if (index < 0) {
        return notFound(QStringLiteral("The track"));
    }
    newIndex = std::clamp(newIndex, 0, trackCount() - 1);
    if (newIndex == index) {
        return {};
    }
    Track moving = std::move(m_tracks[static_cast<size_t>(index)]);
    m_tracks.erase(m_tracks.begin() + index);
    m_tracks.insert(m_tracks.begin() + newIndex, std::move(moving));
    return {};
}

Status Timeline::renameTrack(const Id& trackId, const QString& name)
{
    Track* t = mutableTrack(trackId);
    if (!t) {
        return notFound(QStringLiteral("The track"));
    }
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 64) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Track names must contain 1 to 64 characters."));
    }
    t->name = trimmed;
    return {};
}

Status Timeline::setTrackMuted(const Id& trackId, bool muted)
{
    Track* t = mutableTrack(trackId);
    if (!t) {
        return notFound(QStringLiteral("The track"));
    }
    t->muted = muted;
    return {};
}

Status Timeline::setTrackHidden(const Id& trackId, bool hidden)
{
    Track* t = mutableTrack(trackId);
    if (!t) {
        return notFound(QStringLiteral("The track"));
    }
    t->hidden = hidden;
    return {};
}

Status Timeline::setTrackLocked(const Id& trackId, bool locked)
{
    Track* t = mutableTrack(trackId);
    if (!t) {
        return notFound(QStringLiteral("The track"));
    }
    t->locked = locked;
    return {};
}

Status Timeline::insertClip(const Id& trackId, Clip c, const SourceLookup& sources)
{
    Track* t = mutableTrack(trackId);
    if (!t) {
        return notFound(QStringLiteral("The track"));
    }
    if (const Status editable = checkEditable(*t); !editable) {
        return editable;
    }
    if (c.id.isEmpty()) {
        c.id = createId();
    }
    if (findClip(c.id)) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("A clip with this id already exists."));
    }
    if (const Status shape = checkClipShape(c); !shape) {
        return shape;
    }
    if (const Status source = checkSourceRange(c, *t, sources); !source) {
        return source;
    }
    if (!isRangeFree(trackId, c.start, c.duration)) {
        return makeError(
            ErrorCode::Conflict,
            QStringLiteral("There is already a clip at that position on \"%1\".").arg(t->name));
    }
    insertSorted(*t, std::move(c));
    return {};
}

Result<Clip> Timeline::removeClip(const Id& clipId)
{
    const auto location = findClip(clipId);
    if (!location) {
        return notFound(QStringLiteral("The clip"));
    }
    Track& t = m_tracks[static_cast<size_t>(location->track)];
    if (const Status editable = checkEditable(t); !editable) {
        return editable.error();
    }
    Clip removed = t.clips[static_cast<size_t>(location->clip)];
    t.clips.erase(t.clips.begin() + location->clip);
    return removed;
}

Result<Clip> Timeline::rippleRemoveClip(const Id& clipId)
{
    const auto location = findClip(clipId);
    if (!location) {
        return notFound(QStringLiteral("The clip"));
    }
    Track& t = m_tracks[static_cast<size_t>(location->track)];
    if (const Status editable = checkEditable(t); !editable) {
        return editable.error();
    }
    Clip removed = t.clips[static_cast<size_t>(location->clip)];
    t.clips.erase(t.clips.begin() + location->clip);
    for (auto it = t.clips.begin() + location->clip; it != t.clips.end(); ++it) {
        it->start -= removed.duration;
    }
    return removed;
}

Status Timeline::moveClip(const Id& clipId, const Id& targetTrackId, FrameCount newStart,
                          const SourceLookup& sources)
{
    const auto location = findClip(clipId);
    if (!location) {
        return notFound(QStringLiteral("The clip"));
    }
    Track& from = m_tracks[static_cast<size_t>(location->track)];
    Track* to = mutableTrack(targetTrackId);
    if (!to) {
        return notFound(QStringLiteral("The destination track"));
    }
    if (const Status editable = checkEditable(from); !editable) {
        return editable;
    }
    if (const Status editable = checkEditable(*to); !editable) {
        return editable;
    }
    Clip moved = from.clips[static_cast<size_t>(location->clip)];
    moved.start = newStart;
    if (const Status shape = checkClipShape(moved); !shape) {
        return shape;
    }
    if (to != &from) {
        if (const Status source = checkSourceRange(moved, *to, sources); !source) {
            return source;
        }
    }
    if (!isRangeFree(targetTrackId, moved.start, moved.duration, clipId)) {
        return makeError(
            ErrorCode::Conflict,
            QStringLiteral("The clip would overlap another clip on \"%1\".").arg(to->name));
    }
    from.clips.erase(from.clips.begin() + location->clip);
    insertSorted(*to, std::move(moved));
    return {};
}

Status Timeline::trimClipStart(const Id& clipId, FrameCount newStart, const SourceLookup& sources)
{
    const auto location = findClip(clipId);
    if (!location) {
        return notFound(QStringLiteral("The clip"));
    }
    Track& t = m_tracks[static_cast<size_t>(location->track)];
    if (const Status editable = checkEditable(t); !editable) {
        return editable;
    }
    Clip trimmed = t.clips[static_cast<size_t>(location->clip)];
    const FrameCount delta = newStart - trimmed.start;
    trimmed.start = newStart;
    trimmed.sourceIn += delta;
    trimmed.duration -= delta;
    if (trimmed.sourceIn < 0) {
        return makeError(
            ErrorCode::OutOfRange,
            QStringLiteral("The clip cannot be extended before the start of its media."));
    }
    clampFades(trimmed.properties, std::max<FrameCount>(trimmed.duration, 0));
    if (const Status shape = checkClipShape(trimmed); !shape) {
        return shape;
    }
    if (const Status source = checkSourceRange(trimmed, t, sources); !source) {
        return source;
    }
    if (!isRangeFree(t.id, trimmed.start, trimmed.duration, clipId)) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("The clip would overlap its neighbour."));
    }
    t.clips[static_cast<size_t>(location->clip)] = std::move(trimmed);
    return {};
}

Status Timeline::trimClipEnd(const Id& clipId, FrameCount newEnd, const SourceLookup& sources)
{
    const auto location = findClip(clipId);
    if (!location) {
        return notFound(QStringLiteral("The clip"));
    }
    Track& t = m_tracks[static_cast<size_t>(location->track)];
    if (const Status editable = checkEditable(t); !editable) {
        return editable;
    }
    Clip trimmed = t.clips[static_cast<size_t>(location->clip)];
    trimmed.duration = newEnd - trimmed.start;
    clampFades(trimmed.properties, std::max<FrameCount>(trimmed.duration, 0));
    if (const Status shape = checkClipShape(trimmed); !shape) {
        return shape;
    }
    if (const Status source = checkSourceRange(trimmed, t, sources); !source) {
        return source;
    }
    if (!isRangeFree(t.id, trimmed.start, trimmed.duration, clipId)) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("The clip would overlap its neighbour."));
    }
    t.clips[static_cast<size_t>(location->clip)] = std::move(trimmed);
    return {};
}

Result<Id> Timeline::splitClip(const Id& clipId, FrameCount frame, const Id& newClipId)
{
    const auto location = findClip(clipId);
    if (!location) {
        return notFound(QStringLiteral("The clip"));
    }
    Track& t = m_tracks[static_cast<size_t>(location->track)];
    if (const Status editable = checkEditable(t); !editable) {
        return editable.error();
    }
    Clip& left = t.clips[static_cast<size_t>(location->clip)];
    if (frame <= left.start || frame >= left.end()) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("Place the playhead inside the clip to split it."));
    }
    const Id rightId = newClipId.isEmpty() ? createId() : newClipId;
    if (!isValidId(rightId) || findClip(rightId)) {
        return makeError(ErrorCode::InvalidArgument, QStringLiteral("The new clip id is invalid."));
    }
    Clip right = left;
    right.id = rightId;
    right.start = frame;
    right.sourceIn = left.sourceIn + (frame - left.start);
    right.duration = left.end() - frame;
    right.properties.fadeIn = 0;
    clampFades(right.properties, right.duration);

    left.duration = frame - left.start;
    left.properties.fadeOut = 0;
    clampFades(left.properties, left.duration);

    t.clips.insert(t.clips.begin() + location->clip + 1, std::move(right));
    return rightId;
}

Status Timeline::setClipProperties(const Id& clipId, const ClipProperties& properties)
{
    const auto location = findClip(clipId);
    if (!location) {
        return notFound(QStringLiteral("The clip"));
    }
    Track& t = m_tracks[static_cast<size_t>(location->track)];
    if (const Status editable = checkEditable(t); !editable) {
        return editable;
    }
    Clip& c = t.clips[static_cast<size_t>(location->clip)];
    if (const Status valid = checkProperties(properties, c.duration); !valid) {
        return valid;
    }
    c.properties = properties;
    return {};
}

void Timeline::restoreTrack(const Track& t)
{
    const int index = trackIndex(t.id);
    if (index >= 0) {
        m_tracks[static_cast<size_t>(index)] = t;
    }
}

void Timeline::restoreTracks(std::vector<Track> tracks)
{
    m_tracks = std::move(tracks);
}

Status Timeline::validate(const SourceLookup& sources) const
{
    if (trackCount() > kMaxTracks) {
        return makeError(ErrorCode::InvalidDocument,
                         QStringLiteral("The timeline has too many tracks."));
    }
    QSet<Id> ids;
    for (const Track& t : m_tracks) {
        if (!isValidId(t.id) || ids.contains(t.id)) {
            return makeError(ErrorCode::InvalidDocument,
                             QStringLiteral("A track has a missing or duplicate id."));
        }
        ids.insert(t.id);
        FrameCount previousEnd = 0;
        for (const Clip& c : t.clips) {
            if (ids.contains(c.id)) {
                return makeError(ErrorCode::InvalidDocument,
                                 QStringLiteral("A clip has a duplicate id."));
            }
            ids.insert(c.id);
            if (const Status shape = checkClipShape(c); !shape) {
                return makeError(ErrorCode::InvalidDocument, shape.error().message,
                                 QStringLiteral("clip %1").arg(c.id));
            }
            if (c.start < previousEnd) {
                return makeError(
                    ErrorCode::InvalidDocument,
                    QStringLiteral("Clips on track \"%1\" overlap or are out of order.")
                        .arg(t.name));
            }
            previousEnd = c.end();
            if (const Status source = checkSourceRange(c, t, sources); !source) {
                return makeError(ErrorCode::InvalidDocument, source.error().message,
                                 QStringLiteral("clip %1").arg(c.id));
            }
        }
    }
    return {};
}

std::optional<FrameCount> snapToNearest(FrameCount candidate, FrameCount tolerance,
                                        const std::vector<FrameCount>& targets)
{
    if (targets.empty() || tolerance < 0) {
        return std::nullopt;
    }
    const auto it = std::lower_bound(targets.begin(), targets.end(), candidate);
    std::optional<FrameCount> best;
    FrameCount bestDistance = tolerance + 1;
    auto consider = [&](FrameCount value) {
        const FrameCount distance = value > candidate ? value - candidate : candidate - value;
        if (distance < bestDistance) {
            bestDistance = distance;
            best = value;
        }
    };
    if (it != targets.end()) {
        consider(*it);
    }
    if (it != targets.begin()) {
        consider(*(it - 1));
    }
    return best;
}

} // namespace ft
