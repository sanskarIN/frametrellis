// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "ui/waveform_provider.h"

#include "core/ids.h"
#include "core/logging.h"

#include <QQuickTextureFactory>
#include <QRunnable>
#include <QThreadPool>

namespace ft::ui {

namespace {

// Waveforms of short clips are kept in memory; this bounds memory use for large projects.
constexpr int kMaxCachedWaveforms = 64;

struct Request
{
    QString assetId;
    double startSeconds = 0.0;
    double seconds = 0.0;
    QColor color;
    double gain = 1.0;
};

std::optional<Request> parseRequest(const QString& id)
{
    const QStringList parts = id.section(QLatin1Char('?'), 0, 0).split(QLatin1Char('/'));
    if (parts.size() != 5) {
        return std::nullopt;
    }
    bool okStart = false;
    bool okDuration = false;
    bool okGain = false;
    Request request;
    request.assetId = parts[0];
    request.startSeconds = static_cast<double>(parts[1].toLongLong(&okStart)) / 1e6;
    request.seconds = static_cast<double>(parts[2].toLongLong(&okDuration)) / 1e6;
    request.color = QColor(QLatin1Char('#') + parts[3]);
    request.gain = parts[4].toDouble(&okGain) / 100.0;
    if (!okStart || !okDuration || !okGain || !request.color.isValid() || request.seconds <= 0.0 ||
        request.startSeconds < 0.0 || !isValidId(request.assetId)) {
        return std::nullopt;
    }
    return request;
}

class WaveformResponse : public QQuickImageResponse, public QRunnable
{
public:
    WaveformResponse(std::shared_ptr<WaveformService> service, std::optional<Request> request,
                     QSize size)
        : m_service(std::move(service))
        , m_request(std::move(request))
        , m_size(size)
    {
        setAutoDelete(false);
    }

    void run() override
    {
        if (m_request) {
            if (const auto data = m_service->waveform(m_request->assetId)) {
                m_image = media::renderWaveform(*data, m_request->startSeconds, m_request->seconds,
                                                m_size, m_request->color, m_request->gain);
            }
        }
        emit finished();
    }

    QQuickTextureFactory* textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

    QString errorString() const override
    {
        return m_image.isNull() ? QStringLiteral("No waveform available") : QString();
    }

private:
    std::shared_ptr<WaveformService> m_service;
    std::optional<Request> m_request;
    QSize m_size;
    QImage m_image;
};

} // namespace

WaveformService::WaveformService(std::shared_ptr<ThumbnailRegistry> registry,
                                 std::shared_ptr<media::WaveformCache> cache)
    : m_registry(std::move(registry))
    , m_cache(std::move(cache))
{}

void WaveformService::shutdown()
{
    m_stopping = true;
    std::lock_guard lock(m_mutex);
    m_ready.notify_all();
}

std::shared_ptr<const media::WaveformData> WaveformService::waveform(const QString& assetId)
{
    const std::optional<ThumbnailRegistry::Entry> entry = m_registry->find(assetId);
    if (!entry || !entry->info.hasAudio || m_stopping) {
        return nullptr;
    }
    const QString key = m_cache->keyFor(entry->filePath, entry->info);

    std::shared_ptr<Slot> slot;
    bool compute = false;
    {
        std::unique_lock lock(m_mutex);
        auto it = m_slots.find(key);
        if (it == m_slots.end()) {
            if (m_slots.size() >= kMaxCachedWaveforms) {
                // Forget finished entries; they can be reloaded from the disk cache.
                for (auto i = m_slots.begin(); i != m_slots.end();) {
                    i = (*i)->done ? m_slots.erase(i) : std::next(i);
                }
            }
            slot = std::make_shared<Slot>();
            m_slots.insert(key, slot);
            compute = true;
        } else {
            slot = *it;
            m_ready.wait(lock, [&] { return slot->done || m_stopping.load(); });
            return slot->done ? slot->data : nullptr;
        }
    }

    std::shared_ptr<const media::WaveformData> data;
    if (std::optional<media::WaveformData> cached = m_cache->load(key)) {
        data = std::make_shared<const media::WaveformData>(std::move(*cached));
    } else if (compute) {
        Result<media::WaveformData> computed = media::computeWaveform(
            entry->filePath, entry->info, media::kDefaultWaveformBinsPerSecond, &m_stopping);
        if (computed) {
            (void)m_cache->store(key, *computed);
            data = std::make_shared<const media::WaveformData>(std::move(computed).value());
        } else {
            qCInfo(lcMedia) << "No waveform for" << entry->filePath << computed.error().toString();
        }
    }
    {
        std::lock_guard lock(m_mutex);
        slot->data = data;
        slot->done = true;
        if (!data) {
            m_slots.remove(key); // try again next time (the file may be back online)
        }
    }
    m_ready.notify_all();
    return data;
}

WaveformProvider::WaveformProvider(std::shared_ptr<WaveformService> service)
    : m_service(std::move(service))
{}

QQuickImageResponse* WaveformProvider::requestImageResponse(const QString& id,
                                                            const QSize& requestedSize)
{
    const QSize size = requestedSize.isValid() && !requestedSize.isEmpty()
                           ? requestedSize.boundedTo(QSize(8192, 512))
                           : QSize(512, 48);
    auto* response = new WaveformResponse(m_service, parseRequest(id), size);
    QThreadPool::globalInstance()->start(response);
    return response;
}

} // namespace ft::ui
