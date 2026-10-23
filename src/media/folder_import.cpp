// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "media/folder_import.h"

#include "media/media_probe.h"
#include "project/media_asset.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace ft::media {

Result<FolderImportScan> discoverImportableFiles(const QString& directory, int maxFiles)
{
    const QFileInfo root(directory);
    if (directory.trimmed().isEmpty() || !root.isDir() || !root.isReadable()) {
        return makeError(ErrorCode::InvalidArgument,
                         QStringLiteral("Choose an existing, readable media folder."));
    }
    if (maxFiles <= 0 || maxFiles > 5000) {
        return makeError(ErrorCode::OutOfRange,
                         QStringLiteral("The folder import limit must be between 1 and 5000."));
    }

    QSet<QString> supported;
    for (const QString& suffix : importFileSuffixes()) {
        supported.insert(suffix.toCaseFolded());
    }

    FolderImportScan scan;
    QSet<QString> found;
    // Avoid lengthy synchronous scans of unexpectedly huge directory trees.
    constexpr int kMaxVisitedFiles = 10'000;
    int visited = 0;
    QDirIterator iterator(root.absoluteFilePath(),
                          QDir::AllDirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Readable,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QFileInfo info(iterator.next());
        if (!info.isFile() || info.isSymLink()) {
            continue;
        }
        if (++visited > kMaxVisitedFiles) {
            scan.truncated = true;
            break;
        }
        if (!supported.contains(info.suffix().toCaseFolded())) {
            ++scan.skippedUnsupported;
            continue;
        }
        const QString key = canonicalMediaKey(info.absoluteFilePath());
        if (found.contains(key)) {
            continue;
        }
        if (scan.files.size() >= maxFiles) {
            scan.truncated = true;
            break;
        }
        found.insert(key);
        scan.files.append(info.absoluteFilePath());
    }
    std::sort(scan.files.begin(), scan.files.end(), [](const QString& left, const QString& right) {
        const int folded = QString::compare(left, right, Qt::CaseInsensitive);
        return folded == 0 ? left < right : folded < 0;
    });
    return scan;
}

} // namespace ft::media
