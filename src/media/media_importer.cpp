// SPDX-License-Identifier: GPL-3.0-or-later
// SPDX-FileCopyrightText: 2026 Sanskar and FrameTrellis contributors
#include "media/media_importer.h"

#include "core/logging.h"
#include "media/media_probe.h"

#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrentRun>

namespace ft::media {

MediaImporter::MediaImporter(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<std::vector<ft::MediaAsset>>();
    qRegisterMetaType<QList<ft::media::MediaImporter::Failure>>();
}

MediaImporter::~MediaImporter()
{
    cancelAll();
}

void MediaImporter::cancelAll()
{
    ++m_generation;
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
    struct Batch
    {
        std::vector<MediaAsset> assets;
        QList<Failure> failures;
    };
    const int generation = m_generation;
    const int count = static_cast<int>(filePaths.size());
    m_pending += count;
    emit pendingCountChanged(m_pending);

    // The watcher is a child of the importer: if the importer is destroyed first, the result is
    // simply dropped. Shutdown waits for the global thread pool before closing MLT.
    auto* watcher = new QFutureWatcher<Batch>(this);
    connect(watcher, &QFutureWatcherBase::finished, this, [this, watcher, generation, count] {
        const Batch batch = watcher->result();
        watcher->deleteLater();
        if (generation != m_generation) {
            return; // the project changed while probing
        }
        m_pending = std::max(0, m_pending - count);
        emit pendingCountChanged(m_pending);
        emit finished(batch.assets, batch.failures);
    });
    watcher->setFuture(QtConcurrent::run([filePaths] {
        Batch batch;
        for (const QString& path : filePaths) {
            Result<MediaAsset> asset = createAsset(path);
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
