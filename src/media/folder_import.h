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
    bool truncated = false;
};

/// Discover importable files in a folder and its subfolders. Does not open
/// or decode any media. Directory symlinks are not followed, and symlinked
/// files are ignored to prevent surprise imports from external directories.
/// The scan caps both candidate media and total visited files.
Result<FolderImportScan> discoverImportableFiles(const QString& directory,
                                                int maxFiles = 500);

} // namespace ft::media
