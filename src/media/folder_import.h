// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "core/result.h"

#include <QStringList>

namespace ft::media {

/// A bounded, deterministic list of candidate media to be probed asynchronously.
struct FolderImportScan
{
    QStringList files;
    int skippedUnsupported = 0;
    int visitedEntries = 0; ///< files, directories and links enumerated within the budget
    bool truncated = false;
};

/// Discover importable files in a folder and its subfolders. Does not open
/// or decode any media. Directory symlinks are not followed, and symlinked
/// files are ignored to prevent surprise imports from external directories.
/// The scan caps candidate media and all enumerated entries, including directories.
inline constexpr int kMaxFolderScanEntries = 10'000;
Result<FolderImportScan> discoverImportableFiles(const QString& directory, int maxFiles = 500,
                                                 int maxVisitedEntries = kMaxFolderScanEntries);

} // namespace ft::media
