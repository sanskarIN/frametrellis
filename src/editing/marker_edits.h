// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "editing/edit_command.h"

#include <memory>

namespace ft::edits {

/// All marker operations are single, reversible project edits.
std::unique_ptr<EditCommand> addMarker(TimelineMarker marker);
std::unique_ptr<EditCommand> removeMarker(Id markerId);
std::unique_ptr<EditCommand> renameMarker(Id markerId, QString newLabel);
std::unique_ptr<EditCommand> moveMarker(Id markerId, FrameCount newFrame);
/// Edits a marker's label and position together in exactly one undo step.
std::unique_ptr<EditCommand> updateMarker(Id markerId, FrameCount newFrame, QString newLabel);

} // namespace ft::edits
