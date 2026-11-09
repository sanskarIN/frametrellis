// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "editing/edit_command.h"

namespace ft::edits {

std::unique_ptr<EditCommand> addSubtitle(SubtitleCue cue);
std::unique_ptr<EditCommand> updateSubtitle(SubtitleCue cue);
std::unique_ptr<EditCommand> removeSubtitle(Id cueId);
/// Split one cue at an exclusive millisecond boundary and copy its text to the
/// second half. Retains the first ID, assigns a distinct new ID to the second.
/// Rejected boundaries and duplicate IDs leave the project/history unchanged.
std::unique_ptr<EditCommand> splitSubtitle(Id cueId, std::int64_t splitMilliseconds, Id secondId);
std::unique_ptr<EditCommand> appendSubtitles(std::vector<SubtitleCue> cues);
std::unique_ptr<EditCommand> replaceSubtitles(SubtitleTrack track);
std::unique_ptr<EditCommand> clearSubtitles();
/// Move every subtitle by the same exact number of milliseconds, atomically and undoably.
std::unique_ptr<EditCommand> shiftSubtitles(std::int64_t deltaMilliseconds);
std::unique_ptr<EditCommand> setSubtitleAppearance(bool burnIn, int fontPercent, bool background);

} // namespace ft::edits
