// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"
#include "project/media_asset.h"

#include <QString>

#include <vector>

namespace ft::media {

/// A read-only, bounded folder scan. It never changes project files or source media.
/// Only missing assets with a unique filename match and a successfully probed media
/// kind become candidates; later project validation checks clips and source handles.
struct FolderRelinkScan
{
    std::vector<MediaAsset> candidates;
    int missingSearched = 0;
    int unmatched = 0;
    int ambiguous = 0;
    int probeFailed = 0;
    int wrongKind = 0;
    bool truncated = false;
};

/// Find replacement media for missing assets, recursively, on a worker thread.
/// Does not follow directory or file symlinks; caps discovery at 500 supported
/// files and 10,000 visited files. Duplicate filenames in either direction
/// remain ambiguous; a new path is never guessed or chosen by directory order.
Result<FolderRelinkScan> discoverRelinkCandidates(
    const QString& directory, const std::vector<MediaAsset>& missingAssets);

} // namespace ft::media
