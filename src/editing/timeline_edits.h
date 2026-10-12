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
std::unique_ptr<EditCommand> removeClips(const QList<Id>& clipIds);
std::unique_ptr<EditCommand> rippleRemoveClip(const Id& clipId);
std::unique_ptr<EditCommand> moveClip(const Id& clipId, const Id& targetTrackId, FrameCount start);
std::unique_ptr<EditCommand> trimClipStart(const Id& clipId, FrameCount newStart);
std::unique_ptr<EditCommand> trimClipEnd(const Id& clipId, FrameCount newEnd);
/// Splits every given clip that spans the frame. Fails when none does.
std::unique_ptr<EditCommand> splitClips(const QList<Id>& clipIds, FrameCount frame);
/// Updates clip properties; consecutive updates of the same clip merge into one undo step.
std::unique_ptr<EditCommand> setClipProperties(const Id& clipId, const ClipProperties& properties,
                                               const QString& text);

/// Adds a transition at the cut after the clip. The id lets callers select the new transition.
std::unique_ptr<EditCommand> addTransition(const Id& fromClipId, FrameCount duration,
                                           const Id& transitionId = {});
/// Changes a transition's length; consecutive changes of the same transition merge.
std::unique_ptr<EditCommand> setTransitionDuration(const Id& transitionId, FrameCount duration);
std::unique_ptr<EditCommand> removeTransition(const Id& transitionId);

} // namespace ft::edits
