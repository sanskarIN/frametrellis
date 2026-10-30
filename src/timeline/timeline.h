// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"
#include "timeline/track.h"

#include <functional>
#include <optional>
#include <vector>

namespace ft {

/// Facts about a media source that constrain how clips using it can be edited.
struct SourceLimits
{
    std::optional<FrameCount> length; ///< std::nullopt for unbounded sources such as stills
    bool hasVideo = false;
    bool hasAudio = false;
};

/// Resolves an asset id to its limits; returns std::nullopt for unknown assets.
using SourceLookup = std::function<std::optional<SourceLimits>(const Id& assetId)>;

struct ClipLocation
{
    int track = -1;
    int clip = -1;
};

/// Upper bound for any timeline position; keeps all arithmetic far from overflow.
inline constexpr FrameCount kMaxTimelineFrames = 1'000'000'000;
inline constexpr int kMaxTracks = 99;

/// The editable multi-track arrangement. Every mutation validates its input and keeps the
/// invariants: unique ids, clips sorted and non-overlapping per track, durations >= 1, clip
/// source ranges within the source length, and clip media compatible with the track kind.
class Timeline
{
public:
    [[nodiscard]] const std::vector<Track>& tracks() const { return m_tracks; }
    [[nodiscard]] int trackCount() const { return static_cast<int>(m_tracks.size()); }
    [[nodiscard]] int trackIndex(const Id& trackId) const;
    [[nodiscard]] const Track* track(const Id& trackId) const;
    [[nodiscard]] std::optional<ClipLocation> findClip(const Id& clipId) const;
    [[nodiscard]] const Clip* clip(const Id& clipId) const;
    [[nodiscard]] const Track* trackOfClip(const Id& clipId) const;
    [[nodiscard]] int clipCount() const;
    /// Exclusive end frame of the last clip on any track.
    [[nodiscard]] FrameCount duration() const;
    [[nodiscard]] bool isRangeFree(const Id& trackId, FrameCount start, FrameCount duration,
                                   const Id& ignoreClipId = {}) const;
    /// Clip boundaries usable as snap targets, sorted and de-duplicated.
    [[nodiscard]] std::vector<FrameCount> editPoints(const Id& excludeClipId = {}) const;
    [[nodiscard]] bool usesAsset(const Id& assetId) const;
    [[nodiscard]] const Transition* transition(const Id& transitionId) const;
    /// The transition at the cut after the given clip, if any.
    [[nodiscard]] const Transition* transitionAfter(const Id& fromClipId) const;
    /// The longest transition possible at the cut after the given clip (0 if none is possible:
    /// no adjacent clip, or no media beyond the cut).
    [[nodiscard]] FrameCount maxTransitionDuration(const Id& fromClipId,
                                                   const SourceLookup& sources) const;
    [[nodiscard]] std::vector<Id> clipsUsingAsset(const Id& assetId) const;

    // Track operations
    Status insertTrack(Track track, int index = -1);
    Result<Track> removeTrack(const Id& trackId);
    Status moveTrack(const Id& trackId, int newIndex);
    Status renameTrack(const Id& trackId, const QString& name);
    Status setTrackMuted(const Id& trackId, bool muted);
    Status setTrackHidden(const Id& trackId, bool hidden);
    Status setTrackLocked(const Id& trackId, bool locked);

    // Clip operations
    Status insertClip(const Id& trackId, Clip clip, const SourceLookup& sources);
    Result<Clip> removeClip(const Id& clipId);
    /// Removes a clip and closes the gap by shifting later clips on the same track.
    Result<Clip> rippleRemoveClip(const Id& clipId);
    Status moveClip(const Id& clipId, const Id& targetTrackId, FrameCount newStart,
                    const SourceLookup& sources);
    /// Moves the clip's left edge; the clip's content stays anchored to the timeline.
    Status trimClipStart(const Id& clipId, FrameCount newStart, const SourceLookup& sources);
    /// Moves the clip's right edge (exclusive end frame).
    Status trimClipEnd(const Id& clipId, FrameCount newEnd, const SourceLookup& sources);
    /// Changes source in/out without moving either timeline edge or changing duration.
    /// Rejects still images, unavailable source handles and invalidated transitions.
    Status slipClip(const Id& clipId, FrameCount newSourceIn, const SourceLookup& sources);
    /// Rolls the cut after fromClipId by signed frames. Two adjacent time-based clips
    /// exchange duration at their shared cut without moving the surrounding timeline edges.
    /// Rejects unsafe source handles, fades, transitions, locked tracks or invalid neighbours.
    Status rollCut(const Id& fromClipId, FrameCount delta, const SourceLookup& sources);
    /// Splits a clip at a timeline frame strictly inside it. Returns the id of the right part.
    Result<Id> splitClip(const Id& clipId, FrameCount frame, const Id& newClipId = {});
    Status setClipProperties(const Id& clipId, const ClipProperties& properties);
    /// Add/update a volume point at an audible source frame inside the clip's current trim.
    Status setAudioKeyframe(const Id& clipId, FrameCount sourceFrame, double decibels,
                            const SourceLookup& sources);
    Status removeAudioKeyframe(const Id& clipId, FrameCount sourceFrame);
    Status clearAudioKeyframes(const Id& clipId);
    /// Edit a source-anchored opacity point on an unlocked video track (including stills).
    Status setOpacityKeyframe(const Id& clipId, FrameCount sourceFrame, double opacity,
                              const SourceLookup& sources);
    Status removeOpacityKeyframe(const Id& clipId, FrameCount sourceFrame);
    Status clearOpacityKeyframes(const Id& clipId);
    Status setTransformKeyframe(const Id& clipId, FrameCount sourceFrame,
                                const VideoTransform& value, const SourceLookup& sources);
    Status removeTransformKeyframe(const Id& clipId, FrameCount sourceFrame);
    Status clearTransformKeyframes(const Id& clipId);
    /// Reset scalar picture adjustments, opacity and transforms, preserving audio and fades.
    Status resetClipPicture(const Id& clipId);

    // Transition operations
    /// Adds a transition at the cut between the clip and the clip that starts where it ends.
    Result<Id> addTransition(const Id& fromClipId, FrameCount duration, const SourceLookup& sources,
                             const Id& newTransitionId = {});
    Status setTransitionDuration(const Id& transitionId, FrameCount duration,
                                 const SourceLookup& sources);
    Result<Transition> removeTransition(const Id& transitionId);

    /// Replaces tracks wholesale. Used by the command history to restore previously validated
    /// states; regular editing goes through the validated operations above.
    void restoreTrack(const Track& track);
    void restoreTracks(std::vector<Track> tracks);

    /// Checks every invariant; used when loading documents and in tests.
    [[nodiscard]] Status validate(const SourceLookup& sources) const;

    friend bool operator==(const Timeline&, const Timeline&) = default;

private:
    Track* mutableTrack(const Id& trackId);
    Status checkEditable(const Track& track) const;
    static Status checkClipShape(const Clip& clip);
    static Status checkSourceRange(const Clip& clip, const Track& track,
                                   const SourceLookup& sources);
    static void insertSorted(Track& track, Clip clip);
    /// Why a transition cannot exist on a track as it is, or success. Without sources only the
    /// arrangement is checked, not the media handles.
    static Status checkTransition(const Track& track, const Transition& transition,
                                  const SourceLookup* sources);
    /// Removes transitions that an edit made impossible (clips moved apart, removed or trimmed
    /// at the cut) and keeps the rest in timeline order.
    static void pruneTransitions(Track& track, const SourceLookup* sources);
    Transition* mutableTransition(const Id& transitionId, Track** owner);

    std::vector<Track> m_tracks;
};

/// Finds the snap target closest to candidate within tolerance, if any.
std::optional<FrameCount> snapToNearest(FrameCount candidate, FrameCount tolerance,
                                        const std::vector<FrameCount>& targets);

} // namespace ft
