// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "media/media_importer.h"

#include "core/logging.h"
#include "media/media_probe.h"

#include <QFutureWatcher>
#include <QPointer>
#include <QtConcurrent/QtConcurrentRun>

#include <limits>

namespace ft::media {

MediaImporter::MediaImporter(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<std::vector<ft::MediaAsset>>();
    qRegisterMetaType<QList<ft::media::MediaImporter::Failure>>();
}

MediaImporter::~MediaImporter()
{
    // Do not emit pendingCountChanged from a partially destroyed object.
    m_cancelled->store(true);
}

void MediaImporter::cancelAll()
{
    m_cancelled->store(true);
    m_cancelled = std::make_shared<std::atomic<bool>>(false);
    if (m_pending != 0) {
        m_pending = 0;
        emit pendingCountChanged(0);
    }
}

void MediaImporter::import(const QStringList& filePaths)
{
    if (filePaths.isEmpty()) {
        return;
    }
    if (filePaths.size() > std::numeric_limits<int>::max() - m_pending) {
        qCWarning(lcMedia) << "Import request exceeds pending file-count range";
        return;
    }
    struct Batch
    {
        std::vector<MediaAsset> assets;
        QList<Failure> failures;
    };
    const auto cancelled = m_cancelled;
    const int count = static_cast<int>(filePaths.size());
    m_pending += count;
    const QPointer<MediaImporter> owner(this);
    emit pendingCountChanged(m_pending);

    // A synchronous observer may close the project during pendingCountChanged.
    // In that case, do not even submit work to the global pool.
    if (!owner || cancelled->load()) {
        return;
    }

    // The watcher is a child of the importer. Workers own only paths and the
    // cancellation token; neither outlives or accesses the GUI document.
    auto* watcher = new QFutureWatcher<Batch>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, cancelled, count] {
        watcher->deleteLater();
        if (cancelled->load()) {
            return;
        }
        const Batch batch = watcher->result();
        m_pending -= count;
        const QPointer<MediaImporter> live(this);
        emit pendingCountChanged(m_pending);
        // The signal above may itself replace the project or destroy the
        // importer. Check again before delivering a batch to the document.
        if (!live || cancelled->load()) {
            return;
        }
        emit finished(batch.assets, batch.failures);
    });
    watcher->setFuture(QtConcurrent::run([filePaths, cancelled] {
        Batch batch;
        for (const QString& path : filePaths) {
            if (cancelled->load()) {
                return Batch{};
            }
            Result<MediaAsset> asset = createAsset(path);
            // An in-flight native probe cannot be interrupted here, but
            // its result must not be retained after the generation ends.
            if (cancelled->load()) {
                return Batch{};
            }
            if (asset) {
                batch.assets.push_back(std::move(asset).value());
            } else {
                batch.failures.append(Failure{path, asset.error().message});
                qCInfo(lcMedia) << "Import failed:" << path << asset.error().toString();
            }
        }
        return batch;
    }));
}

} // namespace ft::media
