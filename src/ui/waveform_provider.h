// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#pragma once

#include "media/waveform.h"
#include "ui/thumbnail_provider.h"

#include <QHash>
#include <QQuickAsyncImageProvider>

#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace ft::ui {

/// Supplies waveform data for media assets: from memory, from the disk cache, or by decoding the
/// file once. Concurrent requests for the same file share one computation. Thread-safe.
class WaveformService
{
public:
    WaveformService(std::shared_ptr<ThumbnailRegistry> registry,
                    std::shared_ptr<media::WaveformCache> cache);

    /// Blocks until the waveform of @p assetId is available; returns nullptr if the asset is
    /// unknown, has no sound, cannot be decoded or the service is shutting down.
    std::shared_ptr<const media::WaveformData> waveform(const QString& assetId);

    /// Makes running and future computations stop early (used when the application quits).
    void shutdown();

private:
    struct Slot
    {
        bool done = false;
        std::shared_ptr<const media::WaveformData> data;
    };

    std::shared_ptr<ThumbnailRegistry> m_registry;
    std::shared_ptr<media::WaveformCache> m_cache;
    std::atomic<bool> m_stopping{false};
    std::mutex m_mutex;
    std::condition_variable m_ready;
    QHash<QString, std::shared_ptr<Slot>> m_slots; // by cache key
};

/// Serves "image://ft-waveform/<assetId>/<startUs>/<durationUs>/<rrggbb>/<gainPercent>".
class WaveformProvider : public QQuickAsyncImageProvider
{
public:
    explicit WaveformProvider(std::shared_ptr<WaveformService> service);
    QQuickImageResponse* requestImageResponse(const QString& id,
                                              const QSize& requestedSize) override;

private:
    std::shared_ptr<WaveformService> m_service;
};

} // namespace ft::ui
