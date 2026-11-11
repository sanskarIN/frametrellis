// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"

#include <QString>

namespace ft::rendering {

/// Publish a complete, verified temporary file to its final destination.
/// When replaceExisting is false, atomically refuse to replace an existing
/// output even when another process creates it during the render.
/// The part file and destination must be in the same output directory.
Status publishRenderedFile(const QString& partPath, const QString& destinationPath,
                           bool replaceExisting);

} // namespace ft::rendering
