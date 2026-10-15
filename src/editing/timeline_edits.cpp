// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/timeline_edits.h"

#include <QSet>

#include <algorithm>

namespace ft::edits {

namespace {

std::unique_ptr<EditCommand> command(const QString& text, TimelineCommand::Operation op,
                                     const QString& mergeKey = {})
{
    return std::make_unique<TimelineCommand>(text, std::move(op), mergeKey);
}

} // namespace

std::unique_ptr<EditCommand> addTrack(TrackKind kind, const QString& name, int index,
                                      const Id& trackId)
{
    Track track;
    track.id = trackId.isEmpty() ? createId() : trackId;
    track.kind = kind;
    track.name = name;
    return command(
        kind == TrackKind::Video ? QStringLiteral("Add Video Track")
                                 : QStringLiteral("Add Audio Track"),
        [track, index](Timeline& t, const SourceLookup&) { return t.insertTrack(track, index); });
}

std::unique_ptr<EditCommand> removeTrack(const Id& trackId)
{
    return command(QStringLiteral("Remove Track"),
                   [trackId](Timeline& t, const SourceLookup&) -> Status {
                       const Track* track = t.track(trackId);
                       if (track && track->locked) {
                           return makeError(ErrorCode::Conflict,
                                            QStringLiteral("Unlock the track before removing it."));
                       }
                       Result<Track> removed = t.removeTrack(trackId);
                       return removed ? Status{} : Status{removed.error()};
                   });
}

std::unique_ptr<EditCommand> moveTrack(const Id& trackId, int newIndex)
{
    return command(QStringLiteral("Move Track"),
                   [trackId, newIndex](Timeline& t, const SourceLookup&) {
                       return t.moveTrack(trackId, newIndex);
                   });
}

std::unique_ptr<EditCommand> renameTrack(const Id& trackId, const QString& name)
{
    return command(
        QStringLiteral("Rename Track"),
        [trackId, name](Timeline& t, const SourceLookup&) { return t.renameTrack(trackId, name); });
}

std::unique_ptr<EditCommand> setTrackMuted(const Id& trackId, bool muted)
{
    return command(muted ? QStringLiteral("Mute Track") : QStringLiteral("Unmute Track"),
                   [trackId, muted](Timeline& t, const SourceLookup&) {
                       return t.setTrackMuted(trackId, muted);
                   });
}

std::unique_ptr<EditCommand> setTrackHidden(const Id& trackId, bool hidden)
{
    return command(hidden ? QStringLiteral("Hide Track") : QStringLiteral("Show Track"),
                   [trackId, hidden](Timeline& t, const SourceLookup&) {
                       return t.setTrackHidden(trackId, hidden);
                   });
}

std::unique_ptr<EditCommand> setTrackLocked(const Id& trackId, bool locked)
{
    return command(locked ? QStringLiteral("Lock Track") : QStringLiteral("Unlock Track"),
                   [trackId, locked](Timeline& t, const SourceLookup&) {
                       return t.setTrackLocked(trackId, locked);
                   });
}

std::unique_ptr<EditCommand> addClip(const Id& trackId, const Clip& clip)
{
    return command(QStringLiteral("Add Clip"),
                   [trackId, clip](Timeline& t, const SourceLookup& sources) {
                       return t.insertClip(trackId, clip, sources);
                   });
}

std::unique_ptr<EditCommand> duplicateClip(const Id& clipId, const Id& newClipId)
{
    const Id id = newClipId.isEmpty() ? createId() : newClipId;
    return command(QStringLiteral("Duplicate Clip"),
                   [clipId, id](Timeline& timeline, const SourceLookup& sources) -> Status {
                       const Clip* original = timeline.clip(clipId);
                       const Track* track = timeline.trackOfClip(clipId);
                       if (!original || !track) {
                           return makeError(ErrorCode::NotFound,
                                            QStringLiteral("The selected clip was not found."));
                       }
                       // Copy only the clip: transitions connect cuts and must be created
                       // explicitly. Append to avoid disturbing neighbouring clips.
                       Clip copy = *original;
                       copy.id = id;
                       copy.start = track->end();
                       return timeline.insertClip(track->id, copy, sources);
                   });
}

std::unique_ptr<EditCommand> duplicateClips(const QList<Id>& clipIds,
                                             const QList<Id>& newClipIds)
{
    return command(QStringLiteral("Duplicate %1 Clips").arg(clipIds.size()),
                   [clipIds, newClipIds](Timeline& timeline, const SourceLookup& sources) -> Status {
                       if (clipIds.isEmpty() || clipIds.size() != newClipIds.size()) {
                           return makeError(ErrorCode::InvalidArgument,
                                            QStringLiteral("Select clips to duplicate."));
                       }
                       QSet<Id> sourceIds;
                       QSet<Id> destinationIds;
                       struct Original
                       {
                           Id trackId;
                           Clip clip;
                       };
                       std::vector<Original> originals;
                       originals.reserve(static_cast<size_t>(clipIds.size()));
                       FrameCount earliest = kMaxTimelineFrames;
                       for (qsizetype i = 0; i < clipIds.size(); ++i) {
                           const Id& id = clipIds.at(i);
                           const Id& newId = newClipIds.at(i);
                           const Clip* clip = timeline.clip(id);
                           const Track* track = timeline.trackOfClip(id);
                           if (!clip || !track) {
                               return makeError(ErrorCode::NotFound,
                                                QStringLiteral("A selected clip was not found."));
                           }
                           if (sourceIds.contains(id) || newId.isEmpty() ||
                               destinationIds.contains(newId) || timeline.clip(newId)) {
                               return makeError(ErrorCode::Conflict,
                                                QStringLiteral("Duplicate clip ids are not allowed."));
                           }
                           sourceIds.insert(id);
                           destinationIds.insert(newId);
                           earliest = std::min(earliest, clip->start);
                           originals.push_back({track->id, *clip});
                       }
                       const FrameCount appendAt = timeline.duration();
                       for (qsizetype i = 0; i < clipIds.size(); ++i) {
                           Clip copy = originals.at(static_cast<size_t>(i)).clip;
                           const FrameCount relative = copy.start - earliest;
                           if (relative > kMaxTimelineFrames - appendAt ||
                               copy.duration > kMaxTimelineFrames - appendAt - relative) {
                               return makeError(ErrorCode::OutOfRange,
                                                QStringLiteral("The duplicates exceed the timeline limit."));
                           }
                           copy.start = appendAt + relative;
                           copy.id = newClipIds.at(i);
                           if (const Status added = timeline.insertClip(
                                   originals.at(static_cast<size_t>(i)).trackId, copy, sources);
                               !added) {
                               return added;
                           }
                       }
                       return {};
                   });
}

std::unique_ptr<EditCommand> removeClips(const QList<Id>& clipIds)
{
    const QString text = clipIds.size() == 1
                             ? QStringLiteral("Delete Clip")
                             : QStringLiteral("Delete %1 Clips").arg(clipIds.size());
    return command(text, [clipIds](Timeline& t, const SourceLookup&) -> Status {
        for (const Id& id : clipIds) {
            Result<Clip> removed = t.removeClip(id);
            if (!removed) {
                return removed.error();
            }
        }
        return {};
    });
}

std::unique_ptr<EditCommand> rippleRemoveClip(const Id& clipId)
{
    return command(QStringLiteral("Ripple Delete"),
                   [clipId](Timeline& t, const SourceLookup&) -> Status {
                       Result<Clip> removed = t.rippleRemoveClip(clipId);
                       return removed ? Status{} : Status{removed.error()};
                   });
}

std::unique_ptr<EditCommand> moveClip(const Id& clipId, const Id& targetTrackId, FrameCount start)
{
    return command(QStringLiteral("Move Clip"),
                   [clipId, targetTrackId, start](Timeline& t, const SourceLookup& sources) {
                       return t.moveClip(clipId, targetTrackId, start, sources);
                   });
}

std::unique_ptr<EditCommand> trimClipStart(const Id& clipId, FrameCount newStart)
{
    return command(QStringLiteral("Trim Clip Start"),
                   [clipId, newStart](Timeline& t, const SourceLookup& sources) {
                       return t.trimClipStart(clipId, newStart, sources);
                   });
}

std::unique_ptr<EditCommand> trimClipEnd(const Id& clipId, FrameCount newEnd)
{
    return command(QStringLiteral("Trim Clip End"),
                   [clipId, newEnd](Timeline& t, const SourceLookup& sources) {
                       return t.trimClipEnd(clipId, newEnd, sources);
                   });
}

std::unique_ptr<EditCommand> splitClips(const QList<Id>& clipIds, FrameCount frame)
{
    return command(
        QStringLiteral("Split Clip"), [clipIds, frame](Timeline& t, const SourceLookup&) -> Status {
            int splitCount = 0;
            for (const Id& id : clipIds) {
                const Clip* c = t.clip(id);
                if (c && frame > c->start && frame < c->end()) {
                    Result<Id> right = t.splitClip(id, frame);
                    if (!right) {
                        return right.error();
                    }
                    ++splitCount;
                }
            }
            if (splitCount == 0) {
                return makeError(
                    ErrorCode::OutOfRange,
                    QStringLiteral("Move the playhead inside a selected clip to split it."));
            }
            return {};
        });
}

std::unique_ptr<EditCommand> setClipProperties(const Id& clipId, const ClipProperties& properties,
                                               const QString& text)
{
    return command(
        text,
        [clipId, properties](Timeline& t, const SourceLookup&) {
            return t.setClipProperties(clipId, properties);
        },
        QStringLiteral("clip-properties:") + clipId + QLatin1Char(':') + text);
}

std::unique_ptr<EditCommand> pasteClipProperties(const QList<Id>& clipIds,
                                                  const ClipProperties& properties)
{
    return command(QStringLiteral("Paste Clip Attributes"),
                   [clipIds, properties](Timeline& timeline, const SourceLookup&) -> Status {
                       if (clipIds.isEmpty()) {
                           return makeError(ErrorCode::InvalidArgument,
                                            QStringLiteral("Select clips to update."));
                       }
                       QSet<Id> unique;
                       for (const Id& id : clipIds) {
                           if (unique.contains(id)) {
                               return makeError(ErrorCode::InvalidArgument,
                                                QStringLiteral("A clip was selected twice."));
                           }
                           unique.insert(id);
                           const Clip* clip = timeline.clip(id);
                           if (!clip) {
                               return makeError(ErrorCode::NotFound,
                                                QStringLiteral("A selected clip was not found."));
                           }
                           ClipProperties copy = properties;
                           copy.fadeIn = std::min(copy.fadeIn, clip->duration);
                           copy.fadeOut = std::min(copy.fadeOut, clip->duration - copy.fadeIn);
                           if (const Status updated = timeline.setClipProperties(id, copy);
                               !updated) {
                               return updated;
                           }
                       }
                       return {};
                   });
}

std::unique_ptr<EditCommand> addTransition(const Id& fromClipId, FrameCount duration,
                                           const Id& transitionId)
{
    const Id id = transitionId.isEmpty() ? createId() : transitionId;
    return command(QStringLiteral("Add Transition"),
                   [fromClipId, duration, id](Timeline& t, const SourceLookup& sources) {
                       Result<Id> added = t.addTransition(fromClipId, duration, sources, id);
                       return added ? Status{} : Status{added.error()};
                   });
}

std::unique_ptr<EditCommand> setTransitionDuration(const Id& transitionId, FrameCount duration)
{
    return command(
        QStringLiteral("Change Transition Length"),
        [transitionId, duration](Timeline& t, const SourceLookup& sources) {
            return t.setTransitionDuration(transitionId, duration, sources);
        },
        QStringLiteral("transition-duration:") + transitionId);
}

std::unique_ptr<EditCommand> removeTransition(const Id& transitionId)
{
    return command(QStringLiteral("Remove Transition"),
                   [transitionId](Timeline& t, const SourceLookup&) {
                       Result<Transition> removed = t.removeTransition(transitionId);
                       return removed ? Status{} : Status{removed.error()};
                   });
}

} // namespace ft::edits
