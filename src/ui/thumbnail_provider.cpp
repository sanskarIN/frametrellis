// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/thumbnail_provider.h"

#include "media/media_probe.h"

#include <QQuickTextureFactory>
#include <QUrlQuery>
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
    ThumbnailResponse(std::optional<ThumbnailRegistry::Entry> entry,
                      std::shared_ptr<media::ThumbnailCache> cache, QSize size,
                      std::optional<FrameCount> sourceFrame,
                      std::optional<FrameRate> projectRate)
        : m_entry(std::move(entry))
        , m_cache(std::move(cache))
        , m_size(size)
        , m_sourceFrame(sourceFrame)
        , m_projectRate(projectRate)
    {
        setAutoDelete(false);
    }

    void run() override
    {
        if (m_entry && m_sourceFrame && m_projectRate) {
            const auto result = media::renderSourceFrame(
                m_entry->filePath, m_entry->info, m_size, *m_sourceFrame, *m_projectRate);
            if (result) {
                m_image = *result;
            }
        } else if (m_entry) {
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
    std::optional<FrameCount> m_sourceFrame;
    std::optional<FrameRate> m_projectRate;
    QImage m_image;
};

} // namespace

ThumbnailProvider::ThumbnailProvider(std::shared_ptr<ThumbnailRegistry> registry,
                                     std::shared_ptr<media::ThumbnailCache> cache)
    : m_registry(std::move(registry))
    , m_cache(std::move(cache))
{}

QQuickImageResponse* ThumbnailProvider::requestImageResponse(const QString& id,
                                                             const QSize& requestedSize)
{
    const QString assetId = id.section(QLatin1Char('?'), 0, 0);
    const QSize size =
        requestedSize.isValid() && !requestedSize.isEmpty() ? requestedSize : QSize(192, 108);

    // Ordinary media tiles remain cached thumbnails. The source excerpt editor
    // optionally asks this asynchronous provider for a specific project-rate
    // source frame. Never interpret the QML query as a file path.
    QUrlQuery query;
    query.setQuery(id.section(QLatin1Char('?'), 1));
    bool frameOk = false;
    bool numeratorOk = false;
    bool denominatorOk = false;
    const FrameCount frame =
        query.queryItemValue(QStringLiteral("frame")).toLongLong(&frameOk);
    const auto numerator =
        query.queryItemValue(QStringLiteral("fpsNum")).toLongLong(&numeratorOk);
    const auto denominator =
        query.queryItemValue(QStringLiteral("fpsDen")).toLongLong(&denominatorOk);
    std::optional<FrameCount> sourceFrame;
    std::optional<FrameRate> projectRate;
    if (frameOk && numeratorOk && denominatorOk && frame >= 0) {
        projectRate = FrameRate::create(numerator, denominator);
        if (projectRate) {
            sourceFrame = frame;
        }
    }
    auto* response = new ThumbnailResponse(m_registry->find(assetId), m_cache, size,
                                           sourceFrame, projectRate);
    QThreadPool::globalInstance()->start(response);
    return response;
}

} // namespace ft::ui
