// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"
#include "project/project.h"

namespace ft::interchange {

/// Builds a cut-only CMX3600 picture EDL using exact project frames.
///
/// The interchange format cannot encode multiple active video layers, transitions,
/// speed changes or picture effects. Unsupported edits fail rather than being
/// silently discarded. Audio is intentionally outside this picture-only export.
[[nodiscard]] Result<QString> toCmx3600(const Project& project);

} // namespace ft::interchange
