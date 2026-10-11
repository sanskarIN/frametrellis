// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "media/thumbnail_cache.h"
#include "project/media_asset.h"

#include <QHash>
#include <QQuickAsyncImageProvider>

#include <memory>
#include <mutex>

namespace ft::ui {

/// Thread-safe lookup from asset id to the file and metadata needed to render its thumbnail.
class ThumbnailRegistry
{
public:
    struct Entry
    {
        QString filePath;
        MediaInfo info;
    };
    void update(const std::vector<MediaAsset>& assets);
    [[nodiscard]] std::optional<Entry> find(const QString& assetId) const;

private:
    mutable std::mutex m_mutex;
    QHash<QString, Entry> m_entries;
};

/// Serves "image://ft-thumbnail/<assetId>" asynchronously from the thumbnail cache.
class ThumbnailProvider : public QQuickAsyncImageProvider
{
public:
    ThumbnailProvider(std::shared_ptr<ThumbnailRegistry> registry, std::shared_ptr<media::ThumbnailCache> cache);
    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;

private:
    std::shared_ptr<ThumbnailRegistry> m_registry;
    std::shared_ptr<media::ThumbnailCache> m_cache;
};

} // namespace ft::ui
