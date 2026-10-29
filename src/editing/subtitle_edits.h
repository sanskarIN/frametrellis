// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "editing/edit_command.h"

namespace ft::edits {

std::unique_ptr<EditCommand> addSubtitle(SubtitleCue cue);
std::unique_ptr<EditCommand> updateSubtitle(SubtitleCue cue);
std::unique_ptr<EditCommand> removeSubtitle(Id cueId);
std::unique_ptr<EditCommand> appendSubtitles(std::vector<SubtitleCue> cues);
std::unique_ptr<EditCommand> replaceSubtitles(SubtitleTrack track);
std::unique_ptr<EditCommand> clearSubtitles();
std::unique_ptr<EditCommand> setSubtitleAppearance(bool burnIn, int fontPercent, bool background);

} // namespace ft::edits
