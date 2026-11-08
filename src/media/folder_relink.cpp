// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "media/folder_relink.h"

#include "media/folder_import.h"
#include "media/media_probe.h"

#include <QFileInfo>
#include <QHash>
#include <QStringList>

namespace ft::media {

Result<FolderRelinkScan> discoverRelinkCandidates(
    const QString& directory, const std::vector<MediaAsset>& missingAssets)
{
    const auto discovered = discoverImportableFiles(directory);
    if (!discovered) {
        return discovered.error();
    }

    QHash<QString, QStringList> pathsByName;
    for (const QString& path : discovered->files) {
        pathsByName[QFileInfo(path).fileName().toCaseFolded()].append(path);
    }
    QHash<QString, int> neededByName;
    for (const MediaAsset& asset : missingAssets) {
        if (asset.status == MediaStatus::Missing) {
            ++neededByName[QFileInfo(asset.filePath).fileName().toCaseFolded()];
        }
    }

    FolderRelinkScan result;
    result.truncated = discovered->truncated;
    for (const MediaAsset& asset : missingAssets) {
        if (asset.status != MediaStatus::Missing) {
            continue;
        }
        ++result.missingSearched;
        const QString filename = QFileInfo(asset.filePath).fileName().toCaseFolded();
        if (filename.isEmpty() || pathsByName.value(filename).isEmpty()) {
            ++result.unmatched;
            continue;
        }
        const QStringList matches = pathsByName.value(filename);
        if (matches.size() != 1 || neededByName.value(filename) != 1) {
            ++result.ambiguous;
            continue;
        }

        const QString path = matches.front();
        const auto info = probeMedia(path);
        if (!info) {
            ++result.probeFailed;
            continue;
        }
        if (info->kind != asset.info.kind) {
            ++result.wrongKind;
            continue;
        }

        MediaAsset updated = asset;
        updated.filePath = path;
        updated.info = *info;
        updated.status = MediaStatus::Online;
        result.candidates.push_back(std::move(updated));
    }
    return result;
}

} // namespace ft::media
