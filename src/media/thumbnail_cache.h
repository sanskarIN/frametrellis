// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "project/media_asset.h"

#include <QImage>
#include <QSize>
#include <QString>

namespace ft::media {

/// Thumbnails stored as PNG files in the user cache folder, keyed by the file path, size,
/// modification time and requested size, so edited source files get fresh thumbnails.
/// Thread-safe: thumbnails are generated on worker threads.
class ThumbnailCache
{
public:
    explicit ThumbnailCache(QString directory);

    /// Returns a cached thumbnail or renders and caches a new one. Null for media without picture.
    QImage thumbnail(const QString& filePath, const MediaInfo& info, QSize maxSize) const;
    [[nodiscard]] const QString& directory() const { return m_directory; }
    /// Removes cached files that have not been used for the given number of days.
    void prune(int maxAgeDays) const;

    static QString defaultDirectory();

private:
    [[nodiscard]] QString cacheFile(const QString& filePath, QSize maxSize) const;
    QString m_directory;
};

} // namespace ft::media
