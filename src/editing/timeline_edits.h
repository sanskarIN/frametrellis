// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "editing/edit_command.h"

#include <memory>

/// Factories for the undoable timeline edits offered by the editor.
namespace ft::edits {

std::unique_ptr<EditCommand> addTrack(TrackKind kind, const QString& name, int index = -1,
                                      const Id& trackId = {});
std::unique_ptr<EditCommand> removeTrack(const Id& trackId);
std::unique_ptr<EditCommand> moveTrack(const Id& trackId, int newIndex);
std::unique_ptr<EditCommand> renameTrack(const Id& trackId, const QString& name);
std::unique_ptr<EditCommand> setTrackMuted(const Id& trackId, bool muted);
std::unique_ptr<EditCommand> setTrackHidden(const Id& trackId, bool hidden);
std::unique_ptr<EditCommand> setTrackLocked(const Id& trackId, bool locked);

/// Places a new clip; fails if the range is occupied.
std::unique_ptr<EditCommand> addClip(const Id& trackId, const Clip& clip);
/// Copies a clip to the end of its current track, retaining source trims and effects. The
/// source clip's transitions are not copied. The operation is a single undoable edit.
std::unique_ptr<EditCommand> duplicateClip(const Id& clipId, const Id& newClipId = {});
/// Duplicates a selection as a time-aligned block after the complete timeline.
/// Keeps track assignments, relative gaps, source trims and adjustments. Existing transitions
/// are not copied. Source ids and destination ids must have the same non-zero count.
/// A failure (including a locked track or overflow) leaves every clip untouched.
std::unique_ptr<EditCommand> duplicateClips(const QList<Id>& clipIds, const QList<Id>& newClipIds);

std::unique_ptr<EditCommand> removeClips(const QList<Id>& clipIds);
std::unique_ptr<EditCommand> rippleRemoveClip(const Id& clipId);
std::unique_ptr<EditCommand> moveClip(const Id& clipId, const Id& targetTrackId, FrameCount start);
/// Move a selection together by a signed number of frames. Keeps relative spacing, refuses
/// collisions and invalid transitions, and commits as one atomic undoable edit.
std::unique_ptr<EditCommand> nudgeClips(const QList<Id>& clipIds, FrameCount delta);
std::unique_ptr<EditCommand> trimClipStart(const Id& clipId, FrameCount newStart);
std::unique_ptr<EditCommand> trimClipEnd(const Id& clipId, FrameCount newEnd);
/// Slip selected time-based clips through their source without moving their timeline edges.
/// All clips must fit, keep required transition handles and be on unlocked tracks.
/// The command is atomic and undoable, even for multi-track selections.
std::unique_ptr<EditCommand> slipClips(const QList<Id>& clipIds, FrameCount delta);
/// Rolls the shared cut after the outgoing clip. Source trims, fades and existing
/// transitions are validated atomically; the edit is one undo/redo step.
std::unique_ptr<EditCommand> rollCut(const Id& fromClipId, FrameCount delta);
/// Splits every given clip that spans the frame. Fails when none does.
std::unique_ptr<EditCommand> splitClips(const QList<Id>& clipIds, FrameCount frame);
/// Updates clip properties; consecutive updates of the same clip merge into one undo step.
std::unique_ptr<EditCommand> setClipProperties(const Id& clipId, const ClipProperties& properties,
                                               const QString& text);
/// Applies copied volume, picture and fade settings to multiple clips as one undo step.
/// Fade lengths are shortened to fit each destination clip's duration.
std::unique_ptr<EditCommand> pasteClipProperties(const QList<Id>& clipIds,
                                                 const ClipProperties& properties);
/// Toggle audio muting for all selected clips without altering their other adjustments.
std::unique_ptr<EditCommand> muteClips(const QList<Id>& clipIds, bool muted);
/// Reset picture adjustments and opacity curves without changing sound or fades.
std::unique_ptr<EditCommand> resetClipPictures(const QList<Id>& clipIds);
std::unique_ptr<EditCommand> resetClipPicture(const Id& clipId);

/// Source-anchored audio automation. Changes at the same point merge until breakMerge().
std::unique_ptr<EditCommand> setAudioKeyframe(const Id& clipId, FrameCount sourceFrame,
                                              double decibels);
std::unique_ptr<EditCommand> removeAudioKeyframe(const Id& clipId, FrameCount sourceFrame);
std::unique_ptr<EditCommand> clearAudioKeyframes(const Id& clipId);
std::unique_ptr<EditCommand> setOpacityKeyframe(const Id& clipId, FrameCount sourceFrame,
                                                double opacity);
std::unique_ptr<EditCommand> removeOpacityKeyframe(const Id& clipId, FrameCount sourceFrame);
std::unique_ptr<EditCommand> clearOpacityKeyframes(const Id& clipId);

/// Adds a transition at the cut after the clip. The id lets callers select the new transition.
std::unique_ptr<EditCommand> addTransition(const Id& fromClipId, FrameCount duration,
                                           const Id& transitionId = {});
/// Changes a transition's length; consecutive changes of the same transition merge.
std::unique_ptr<EditCommand> setTransitionDuration(const Id& transitionId, FrameCount duration);
std::unique_ptr<EditCommand> removeTransition(const Id& transitionId);

} // namespace ft::edits
