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
    if (p.fadeIn < 0 || p.fadeOut < 0 || p.fadeIn > duration || p.fadeOut > duration - p.fadeIn) {
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

const Clip* clipIn(const Track& t, const Id& clipId)
{
    const auto it =
        std::find_if(t.clips.begin(), t.clips.end(), [&](const Clip& c) { return c.id == clipId; });
    return it == t.clips.end() ? nullptr : &*it;
}

void sortTransitions(Track& t)
{
    const auto cutOf = [&](const Transition& tr) {
        const Clip* from = clipIn(t, tr.fromClip);
        return from ? from->end() : FrameCount{0};
    };
    std::stable_sort(t.transitions.begin(), t.transitions.end(),
                     [&](const Transition& a, const Transition& b) { return cutOf(a) < cutOf(b); });
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
    // Validate before adding: callers can probe arbitrary ranges through QML and tests.
    if (!t || start < 0 || duration <= 0 || start > kMaxTimelineFrames ||
        duration > kMaxTimelineFrames - start) {
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

const Transition* Timeline::transition(const Id& transitionId) const
{
    for (const Track& t : m_tracks) {
        for (const Transition& tr : t.transitions) {
            if (tr.id == transitionId) {
                return &tr;
            }
        }
    }
    return nullptr;
}

Transition* Timeline::mutableTransition(const Id& transitionId, Track** owner)
{
    for (Track& t : m_tracks) {
        for (Transition& tr : t.transitions) {
            if (tr.id == transitionId) {
                *owner = &t;
                return &tr;
            }
        }
    }
    return nullptr;
}

const Transition* Timeline::transitionAfter(const Id& fromClipId) const
{
    const Track* t = trackOfClip(fromClipId);
    if (!t) {
        return nullptr;
    }
    const auto it = std::find_if(t->transitions.begin(), t->transitions.end(),
                                 [&](const Transition& tr) { return tr.fromClip == fromClipId; });
    return it == t->transitions.end() ? nullptr : &*it;
}

FrameCount Timeline::maxTransitionDuration(const Id& fromClipId, const SourceLookup& sources) const
{
    const auto location = findClip(fromClipId);
    if (!location) {
        return 0;
    }
    const Track& t = m_tracks[static_cast<size_t>(location->track)];
    const auto index = static_cast<size_t>(location->clip);
    if (index + 1 >= t.clips.size() || t.clips[index + 1].start != t.clips[index].end()) {
        return 0;
    }
    const Clip& from = t.clips[index];
    const Clip& to = t.clips[index + 1];
    // Room before the cut (taken from the outgoing clip and the incoming clip's handle) and after
    // it (taken from the incoming clip and the outgoing clip's handle).
    FrameCount roomBefore = from.duration;
    FrameCount roomAfter = to.duration;
    for (const Transition& other : t.transitions) {
        if (other.toClip == from.id) {
            roomBefore -= other.after();
        }
        if (other.fromClip == to.id) {
            roomAfter -= other.before();
        }
    }
    const std::optional<SourceLimits> a = sources(from.assetId);
    const std::optional<SourceLimits> b = sources(to.assetId);
    if (!a || !b) {
        return 0;
    }
    if (a->length) {
        roomAfter = std::min(roomAfter, *a->length - from.sourceOut());
    }
    if (b->length) {
        roomBefore = std::min(roomBefore, to.sourceIn);
    }
    // before() = d / 2 must fit roomBefore and after() = d - d / 2 must fit roomAfter.
    const FrameCount longest = std::min(2 * roomBefore + 1, 2 * roomAfter);
    return longest >= kMinTransitionFrames ? longest : 0;
}

Status Timeline::checkTransition(const Track& t, const Transition& tr, const SourceLookup* sources)
{
    if (tr.duration < kMinTransitionFrames || tr.duration > kMaxTimelineFrames) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("A transition must be at least two frames long."));
    }
    const Clip* from = clipIn(t, tr.fromClip);
    const Clip* to = clipIn(t, tr.toClip);
    if (!from || !to) {
        return notFound(QStringLiteral("A clip of the transition"));
    }
    if (from->end() != to->start) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("A transition needs two clips that touch."));
    }
    if (tr.before() > from->duration || tr.after() > to->duration) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The transition is longer than one of its clips."));
    }
    for (const Transition& other : t.transitions) {
        if (other.id == tr.id) {
            continue;
        }
        if (other.fromClip == tr.fromClip || other.toClip == tr.toClip) {
            return makeError(ErrorCode::Conflict,
                             QStringLiteral("There is already a transition at this cut."));
        }
        if ((other.toClip == tr.fromClip && other.after() + tr.before() > from->duration) ||
            (other.fromClip == tr.toClip && tr.after() + other.before() > to->duration)) {
            return makeError(
                ErrorCode::OutOfRange,
                QStringLiteral("The transitions at both ends of a clip would overlap."));
        }
    }
    if (sources) {
        const std::optional<SourceLimits> a = (*sources)(from->assetId);
        const std::optional<SourceLimits> b = (*sources)(to->assetId);
        if (!a || !b) {
            return notFound(QStringLiteral("The media of a transition clip"));
        }
        if (a->length && from->sourceOut() + tr.after() > *a->length) {
            return makeError(ErrorCode::OutOfRange,
                             QStringLiteral("The first clip has no more media after the cut. "
                                            "Trim its end to make room for the transition."));
        }
        if (b->length && to->sourceIn < tr.before()) {
            return makeError(ErrorCode::OutOfRange,
                             QStringLiteral("The second clip has no more media before the cut. "
                                            "Trim its start to make room for the transition."));
        }
    }
    return {};
}

void Timeline::pruneTransitions(Track& t, const SourceLookup* sources)
{
    std::vector<Transition> kept;
    kept.reserve(t.transitions.size());
    for (const Transition& tr : t.transitions) {
        if (checkTransition(t, tr, sources)) {
            kept.push_back(tr);
        }
    }
    t.transitions = std::move(kept);
    sortTransitions(t);
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
    // Never calculate c.end()/c.sourceOut() until both signed additions are proven safe.
    if (c.start > kMaxTimelineFrames || c.duration > kMaxTimelineFrames - c.start ||
        c.sourceIn > kMaxTimelineFrames || c.duration > kMaxTimelineFrames - c.sourceIn) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The clip exceeds the maximum timeline length."));
    }
    if (const Status keys = validateAudioKeyframes(c.audioKeyframes, kMaxTimelineFrames); !keys) {
        return keys;
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
    if (!c.audioKeyframes.empty() && !limits->hasAudio) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Volume keyframes require media with audio."));
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
    pruneTransitions(t, nullptr);
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
    pruneTransitions(t, nullptr);
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
    pruneTransitions(from, &sources);
    if (to != &from) {
        pruneTransitions(*to, &sources);
    }
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
    pruneTransitions(t, &sources);
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
    pruneTransitions(t, &sources);
    return {};
}

Status Timeline::slipClip(const Id& clipId, FrameCount newSourceIn, const SourceLookup& sources)
{
    const auto location = findClip(clipId);
    if (!location) {
        return notFound(QStringLiteral("The clip"));
    }
    const Track& track = m_tracks[static_cast<size_t>(location->track)];
    if (const Status editable = checkEditable(track); !editable) {
        return editable;
    }
    const Clip& original = track.clips[static_cast<size_t>(location->clip)];
    // A still image is not time-based media; slipping its source offset has no meaning.
    const std::optional<SourceLimits> limits = sources ? sources(original.assetId) : std::nullopt;
    if (!limits) {
        return makeError(ErrorCode::NotFound,
                         QStringLiteral("The clip's media is not part of the project."));
    }
    if (!limits->length) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Still images cannot be slipped."));
    }
    Clip changed = original;
    changed.sourceIn = newSourceIn;
    if (const Status shape = checkClipShape(changed); !shape) {
        return shape;
    }
    if (const Status source = checkSourceRange(changed, track, sources); !source) {
        return source;
    }
    if (newSourceIn == original.sourceIn) {
        return {};
    }

    // Keep every dissolve/crossfade intact: slipping media can consume a handle
    // that an existing transition still needs. Never silently prune transitions.
    Timeline candidate = *this;
    candidate.m_tracks[static_cast<size_t>(location->track)]
        .clips[static_cast<size_t>(location->clip)] = std::move(changed);
    if (const Status valid = candidate.validate(sources); !valid) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("The slip would invalidate a clip transition."),
                         valid.error().message);
    }
    *this = std::move(candidate);
    return {};
}

Status Timeline::rollCut(const Id& fromClipId, FrameCount delta, const SourceLookup& sources)
{
    if (delta == 0) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Choose a nonzero number of frames to roll the cut."));
    }
    const auto location = findClip(fromClipId);
    if (!location) {
        return notFound(QStringLiteral("The outgoing clip"));
    }
    const Track& track = m_tracks[static_cast<size_t>(location->track)];
    if (const Status editable = checkEditable(track); !editable) {
        return editable;
    }
    const size_t index = static_cast<size_t>(location->clip);
    if (index + 1 >= track.clips.size() ||
        track.clips[index].end() != track.clips[index + 1].start) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("Roll editing requires two clips touching at a cut."));
    }
    const Clip& left = track.clips[index];
    const Clip& right = track.clips[index + 1];
    // Both durations must stay positive. These comparisons also bound delta before additions.
    if (delta <= -left.duration || delta >= right.duration) {
        return makeError(
            ErrorCode::OutOfRange,
            QStringLiteral("Each side of a rolled cut must retain at least one frame."));
    }
    const auto leftLimits = sources ? sources(left.assetId) : std::nullopt;
    const auto rightLimits = sources ? sources(right.assetId) : std::nullopt;
    if (!leftLimits || !rightLimits) {
        return notFound(QStringLiteral("The media at the cut"));
    }
    // Still images have no source-time axis for the right clip's incoming trim.
    if (!leftLimits->length || !rightLimits->length) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Roll editing requires two time-based media clips."));
    }

    Timeline candidate = *this;
    Track& changed = candidate.m_tracks[static_cast<size_t>(location->track)];
    Clip& outgoing = changed.clips[index];
    Clip& incoming = changed.clips[index + 1];
    const FrameCount movedCut = outgoing.end() + delta;
    outgoing.duration += delta;
    incoming.start = movedCut;
    incoming.sourceIn += delta;
    incoming.duration -= delta;
    // Preserve clip properties and transition objects exactly. Do not silently truncate
    // a fade or discard a dissolve because a roll consumed a required source handle.
    if (const Status shape = checkClipShape(outgoing); !shape) {
        return shape;
    }
    if (const Status shape = checkClipShape(incoming); !shape) {
        return shape;
    }
    if (const Status source = checkSourceRange(outgoing, changed, sources); !source) {
        return source;
    }
    if (const Status source = checkSourceRange(incoming, changed, sources); !source) {
        return source;
    }
    if (const Status valid = candidate.validate(sources); !valid) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("The roll would invalidate the timeline or a transition."),
                         valid.error().message);
    }
    *this = std::move(candidate);
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

    // A transition out of the clip now belongs to its right part.
    const Id leftId = left.id;
    for (Transition& tr : t.transitions) {
        if (tr.fromClip == leftId) {
            tr.fromClip = rightId;
        }
    }
    t.clips.insert(t.clips.begin() + location->clip + 1, std::move(right));
    pruneTransitions(t, nullptr);
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

Status Timeline::setAudioKeyframe(const Id& clipId, FrameCount sourceFrame, double decibels,
                                  const SourceLookup& sources)
{
    const auto location = findClip(clipId);
    if (!location) {
        return notFound(QStringLiteral("The clip"));
    }
    Track& track = m_tracks[static_cast<size_t>(location->track)];
    if (const Status editable = checkEditable(track); !editable) {
        return editable;
    }
    Clip& clip = track.clips[static_cast<size_t>(location->clip)];
    const auto limits = sources ? sources(clip.assetId) : std::nullopt;
    if (!limits || !limits->hasAudio) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Select a clip with audio to add volume keyframes."));
    }
    if (sourceFrame < clip.sourceIn || sourceFrame >= clip.sourceOut()) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("Place the keyframe within the selected clip."));
    }
    std::vector<AudioKeyframe> points = clip.audioKeyframes;
    const auto it = std::lower_bound(
        points.begin(), points.end(), sourceFrame,
        [](const AudioKeyframe& point, FrameCount frame) { return point.sourceFrame < frame; });
    if (it != points.end() && it->sourceFrame == sourceFrame) {
        it->decibels = decibels;
    } else {
        points.insert(it, {sourceFrame, decibels});
    }
    if (const Status valid = validateAudioKeyframes(points, kMaxTimelineFrames); !valid) {
        return valid;
    }
    clip.audioKeyframes = std::move(points);
    return {};
}

Status Timeline::removeAudioKeyframe(const Id& clipId, FrameCount sourceFrame)
{
    const auto location = findClip(clipId);
    if (!location) {
        return notFound(QStringLiteral("The clip"));
    }
    Track& track = m_tracks[static_cast<size_t>(location->track)];
    if (const Status editable = checkEditable(track); !editable) {
        return editable;
    }
    auto& points = track.clips[static_cast<size_t>(location->clip)].audioKeyframes;
    const auto it = std::lower_bound(
        points.begin(), points.end(), sourceFrame,
        [](const AudioKeyframe& point, FrameCount frame) { return point.sourceFrame < frame; });
    if (it == points.end() || it->sourceFrame != sourceFrame) {
        return notFound(QStringLiteral("The volume keyframe"));
    }
    points.erase(it);
    return {};
}

Status Timeline::clearAudioKeyframes(const Id& clipId)
{
    const auto location = findClip(clipId);
    if (!location) {
        return notFound(QStringLiteral("The clip"));
    }
    Track& track = m_tracks[static_cast<size_t>(location->track)];
    if (const Status editable = checkEditable(track); !editable) {
        return editable;
    }
    track.clips[static_cast<size_t>(location->clip)].audioKeyframes.clear();
    return {};
}

Result<Id> Timeline::addTransition(const Id& fromClipId, FrameCount duration,
                                   const SourceLookup& sources, const Id& newTransitionId)
{
    const auto location = findClip(fromClipId);
    if (!location) {
        return notFound(QStringLiteral("The clip"));
    }
    Track& t = m_tracks[static_cast<size_t>(location->track)];
    if (const Status editable = checkEditable(t); !editable) {
        return editable.error();
    }
    const auto index = static_cast<size_t>(location->clip);
    if (index + 1 >= t.clips.size() || t.clips[index + 1].start != t.clips[index].end()) {
        return makeError(ErrorCode::Conflict,
                         QStringLiteral("A transition needs a clip that starts exactly where this "
                                        "one ends. Close the gap first."));
    }
    Transition tr;
    tr.id = newTransitionId.isEmpty() ? createId() : newTransitionId;
    tr.fromClip = t.clips[index].id;
    tr.toClip = t.clips[index + 1].id;
    tr.duration = duration;
    if (!isValidId(tr.id) || transition(tr.id) || findClip(tr.id)) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("The transition id is invalid."));
    }
    if (const Status valid = checkTransition(t, tr, &sources); !valid) {
        return valid.error();
    }
    t.transitions.push_back(tr);
    sortTransitions(t);
    return tr.id;
}

Status Timeline::setTransitionDuration(const Id& transitionId, FrameCount duration,
                                       const SourceLookup& sources)
{
    Track* t = nullptr;
    Transition* tr = mutableTransition(transitionId, &t);
    if (!tr) {
        return notFound(QStringLiteral("The transition"));
    }
    if (const Status editable = checkEditable(*t); !editable) {
        return editable;
    }
    Transition changed = *tr;
    changed.duration = duration;
    if (const Status valid = checkTransition(*t, changed, &sources); !valid) {
        return valid;
    }
    *tr = changed;
    return {};
}

Result<Transition> Timeline::removeTransition(const Id& transitionId)
{
    Track* t = nullptr;
    const Transition* tr = mutableTransition(transitionId, &t);
    if (!tr) {
        return notFound(QStringLiteral("The transition"));
    }
    if (const Status editable = checkEditable(*t); !editable) {
        return editable.error();
    }
    Transition removed = *tr;
    t->transitions.erase(std::find(t->transitions.begin(), t->transitions.end(), removed));
    return removed;
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
        for (const Transition& tr : t.transitions) {
            if (!isValidId(tr.id) || ids.contains(tr.id)) {
                return makeError(ErrorCode::InvalidDocument,
                                 QStringLiteral("A transition has a missing or duplicate id."));
            }
            ids.insert(tr.id);
            if (const Status valid = checkTransition(t, tr, &sources); !valid) {
                return makeError(ErrorCode::InvalidDocument, valid.error().message,
                                 QStringLiteral("transition %1").arg(tr.id));
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
