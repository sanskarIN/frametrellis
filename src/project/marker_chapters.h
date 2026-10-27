// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"
#include "project/project.h"

namespace ft::chapters {

/// Converts ordered frame-accurate review cues into readable HH:MM:SS video chapters.
/// This is a read-only operation that never changes the timeline, media or document.
/// A cue at frame zero is required, and each later cue must fall in a distinct second.
[[nodiscard]] Result<QString> toText(const Project& project);

/// Generate an HTML5/WebVTT chapter track from the same named markers. A real
/// sequence duration is required: every cue starts at a marker and ends at the
/// following marker (or the exclusive sequence end). Unlike descriptions,
/// WebVTT preserves sub-second, frame-derived cue boundaries.
[[nodiscard]] Result<QString> toWebVtt(const Project& project);

} // namespace ft::chapters
