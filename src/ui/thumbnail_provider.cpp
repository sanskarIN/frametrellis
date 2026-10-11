// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/thumbnail_provider.h"

#include <QQuickTextureFactory>
#include <QRunnable>
#include <QThreadPool>

namespace ft::ui {

void ThumbnailRegistry::update(const std::vector<MediaAsset>& assets)
{
    QHash<QString, Entry> entries;
    for (const MediaAsset& asset : assets) {
        entries.insert(asset.id, Entry{asset.filePath, asset.info});
    }
    std::lock_guard lock(m_mutex);
    m_entries = std::move(entries);
}

std::optional<ThumbnailRegistry::Entry> ThumbnailRegistry::find(const QString& assetId) const
{
    std::lock_guard lock(m_mutex);
    const auto it = m_entries.constFind(assetId);
    if (it == m_entries.constEnd()) {
        return std::nullopt;
    }
    return *it;
}

namespace {

class ThumbnailResponse : public QQuickImageResponse, public QRunnable
{
public:
    ThumbnailResponse(std::optional<ThumbnailRegistry::Entry> entry, std::shared_ptr<media::ThumbnailCache> cache,
                      QSize size)
        : m_entry(std::move(entry))
        , m_cache(std::move(cache))
        , m_size(size)
    {
        setAutoDelete(false);
    }

    void run() override
    {
        if (m_entry) {
            m_image = m_cache->thumbnail(m_entry->filePath, m_entry->info, m_size);
        }
        emit finished();
    }

    QQuickTextureFactory* textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return m_image.isNull() ? QStringLiteral("No thumbnail available") : QString();
    }

private:
    std::optional<ThumbnailRegistry::Entry> m_entry;
    std::shared_ptr<media::ThumbnailCache> m_cache;
    QSize m_size;
    QImage m_image;
};

} // namespace

ThumbnailProvider::ThumbnailProvider(std::shared_ptr<ThumbnailRegistry> registry,
                                     std::shared_ptr<media::ThumbnailCache> cache)
    : m_registry(std::move(registry))
    , m_cache(std::move(cache))
{
}

QQuickImageResponse* ThumbnailProvider::requestImageResponse(const QString& id, const QSize& requestedSize)
{
    const QString assetId = id.section(QLatin1Char('?'), 0, 0);
    const QSize size = requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(192, 108);
    auto* response = new ThumbnailResponse(m_registry->find(assetId), m_cache, size);
    QThreadPool::globalInstance()->start(response);
    return response;
}

} // namespace ft::ui
