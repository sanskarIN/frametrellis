// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "editing/timeline_edits.h"

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

} // namespace ft::edits
